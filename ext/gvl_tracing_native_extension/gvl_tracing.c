// gvl-tracing: Ruby gem for getting a timelinew view of GVL usage
// Copyright (c) 2022 Ivo Anjo <ivo@ivoanjo.me>
//
// This file is part of gvl-tracing.
//
// MIT License
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <ruby/ruby.h>
#include <ruby/debug.h>
#include <ruby/thread.h>
#include <ruby/atomic.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <sys/types.h>

#include "extconf.h"

#ifdef HAVE_PTHREAD_H
  #include <pthread.h>
#endif

#ifdef HAVE_GETTID
  #include <unistd.h>
#endif

#ifndef HAVE_RB_INTERNAL_THREAD_SPECIFIC_GET
#define HAVE_RB_INTERNAL_THREAD_SPECIFIC_GET 0
#endif

#define RUBY_3_3_PLUS HAVE_RB_INTERNAL_THREAD_SPECIFIC_GET

// Used to mark function arguments that are deliberately left unused
#ifdef __GNUC__
  #define UNUSED_ARG  __attribute__((unused))
#else
  #define UNUSED_ARG
#endif

typedef struct {
  VALUE thread;
  bool current_thread_seen;
  unsigned int current_thread_serial;
  uint64_t thread_object_id;
  rb_event_flag_t previous_state; // Used to coalesce similar events
  rb_event_flag_t current_state; // Used to coalesce similar events
  bool sleeping; // Used to track when a thread is sleeping
} thread_meta_t;

static VALUE tracing_start(VALUE _self, VALUE output_path);
static VALUE tracing_stop(VALUE _self);
static VALUE tracing_clean(VALUE _self, VALUE thread);
static VALUE use_object_id(VALUE _self);
static double timestamp_microseconds(void);
static void render_event(const char *event_name, thread_meta_t *thread);
static void on_thread_event(rb_event_flag_t event, const rb_internal_thread_event_data_t *_unused1, void *_unused2);
static void on_gc_event(VALUE tpval, void *_unused1);
static VALUE mark_sleeping(VALUE _self);
static const char* get_event_name(rb_event_flag_t event_id);

// Global mutable state
static FILE *output_file = NULL;
static rb_internal_thread_event_hook_t *current_hook = NULL;
static double started_tracing_at_microseconds = 0;
static int64_t process_id = 0;
static VALUE gc_tracepoint = Qnil;

#if RUBY_3_3_PLUS
static int gvl_tracing_key = 0;
#else
static _Thread_local thread_meta_t thread_local_meta = {};
static rb_atomic_t thread_serial = 0;
static void set_native_thread_id(void);
#endif

void Init_gvl_tracing_native_extension(void) {
  rb_global_variable(&gc_tracepoint);

#if RUBY_3_3_PLUS
  gvl_tracing_key = rb_internal_thread_specific_key_create();
#endif
  VALUE gvl_tracing_module = rb_define_module("GvlTracing");

  rb_define_singleton_method(gvl_tracing_module, "_start", tracing_start, 1);
  rb_define_singleton_method(gvl_tracing_module, "_stop", tracing_stop, 0);
  rb_define_singleton_method(gvl_tracing_module, "_clean", tracing_clean, 1);
  rb_define_singleton_method(gvl_tracing_module, "_use_object_id?", use_object_id, 0);
  rb_define_singleton_method(gvl_tracing_module, "mark_sleeping", mark_sleeping, 0);
}

#if RUBY_3_3_PLUS

static thread_meta_t* current_thread_meta(VALUE thread) {
  void *data = rb_internal_thread_specific_get(thread, gvl_tracing_key);
  thread_meta_t* thread_meta = NULL;

  if (data != NULL) {
    thread_meta = (thread_meta_t *)data;
  } else {
    thread_meta = (thread_meta_t*)malloc(sizeof(thread_meta_t));
    memset(thread_meta, 0, sizeof(thread_meta_t));
    thread_meta->thread = thread;
    thread_meta->thread_object_id = RB_NUM2INT(rb_obj_id(thread));
    rb_internal_thread_specific_set(thread, gvl_tracing_key, (void *)thread_meta);
  }

  return thread_meta;
}

static inline void initialize_thread_id(thread_meta_t *thread_meta) {
  thread_meta->current_thread_seen = true;
  thread_meta->thread_object_id = RB_NUM2INT(rb_obj_id(thread_meta->thread));
}

static void free_thread_meta(VALUE thread) {
  void *data = rb_internal_thread_specific_get(thread, gvl_tracing_key);
  if (data != NULL) {
    free(data);
    rb_internal_thread_specific_set(thread, gvl_tracing_key, (void *)NULL);
  }
}

#else

static thread_meta_t* current_thread_meta(VALUE thread) {
  return &thread_local_meta;
}

static void set_native_thread_id(void) {
  uint64_t native_thread_id = 0;

  #ifdef HAVE_PTHREAD_THREADID_NP
    pthread_threadid_np(pthread_self(), &native_thread_id);
  #elif HAVE_GETTID
    native_thread_id = gettid();
  #else
    native_thread_id = thread_local_meta->current_thread_serial; // TODO: Better fallback for Windows?
  #endif

  thread_local_meta.thread_object_id = native_thread_id;
}

static inline void initialize_thread_id(thread_meta_t *thread_meta) {
  thread_meta->current_thread_seen = true;
  thread_meta->current_thread_serial = RUBY_ATOMIC_FETCH_ADD(thread_serial, 1);
  set_native_thread_id();
}

#endif

static inline void render_thread_metadata(thread_meta_t *thread_meta) {
  char native_thread_name_buffer[64] = "(unnamed)";

  #ifdef HAVE_PTHREAD_GETNAME_NP
    pthread_getname_np(pthread_self(), native_thread_name_buffer, sizeof(native_thread_name_buffer));
  #endif

  fprintf(output_file,
    "  {\"ph\": \"M\", \"pid\": %"PRId64", \"tid\": %"PRIu64", \"name\": \"thread_name\", \"args\": {\"name\": \"%s\"}},\n",
    process_id, thread_meta->thread_object_id, native_thread_name_buffer);
}

static VALUE tracing_clean(UNUSED_ARG VALUE _self, VALUE thread) {
#if RUBY_3_3_PLUS
  free_thread_meta(thread);
#endif
  return Qnil;
}

static VALUE use_object_id(UNUSED_ARG VALUE _self) {
  return RUBY_3_3_PLUS ? Qtrue : Qfalse;
}

static VALUE tracing_start(UNUSED_ARG VALUE _self, VALUE output_path) {
  Check_Type(output_path, T_STRING);

  if (output_file != NULL) rb_raise(rb_eRuntimeError, "Already started");
  output_file = fopen(StringValuePtr(output_path), "w");
  if (output_file == NULL) rb_syserr_fail(errno, "Failed to open GvlTracing output file");

  started_tracing_at_microseconds = timestamp_microseconds();
  process_id = getpid();

  fprintf(output_file, "[\n");
  render_event("started_tracing", current_thread_meta(rb_thread_current()));

  current_hook = rb_internal_thread_add_event_hook(
    on_thread_event,
    (
      RUBY_INTERNAL_THREAD_EVENT_READY |
      RUBY_INTERNAL_THREAD_EVENT_RESUMED |
      RUBY_INTERNAL_THREAD_EVENT_SUSPENDED |
      RUBY_INTERNAL_THREAD_EVENT_STARTED |
      RUBY_INTERNAL_THREAD_EVENT_EXITED
    ),
    NULL
  );

  gc_tracepoint = rb_tracepoint_new(
    0,
    (
      RUBY_INTERNAL_EVENT_GC_ENTER |
      RUBY_INTERNAL_EVENT_GC_EXIT
    ),
    on_gc_event,
    (void *) NULL
  );
  rb_tracepoint_enable(gc_tracepoint);

  return Qtrue;
}

static VALUE tracing_stop(UNUSED_ARG VALUE _self) {
  if (output_file == NULL) rb_raise(rb_eRuntimeError, "Tracing not running");

  rb_internal_thread_remove_event_hook(current_hook);
  rb_tracepoint_disable(gc_tracepoint);
  gc_tracepoint = Qnil;

  render_event("stopped_tracing", current_thread_meta(rb_thread_current()));
  // closing the json syntax in the output file is handled in GvlTracing.stop code

  if (fclose(output_file) != 0) rb_syserr_fail(errno, "Failed to close GvlTracing output file");

  output_file = NULL;

  return Qtrue;
}

static double timestamp_microseconds(void) {
  struct timespec current_monotonic;
  if (clock_gettime(CLOCK_MONOTONIC, &current_monotonic) != 0) rb_syserr_fail(errno, "Failed to read CLOCK_MONOTONIC");
  return (current_monotonic.tv_nsec / 1000.0) + (current_monotonic.tv_sec * 1000.0 * 1000.0);
}

// Render output using trace event format for perfetto:
// https://chromium.googlesource.com/catapult/+/refs/heads/main/docs/trace-event-format.md
static void render_event(const char *event_name, thread_meta_t *thread_meta) {
  // Event data
  double now_microseconds = timestamp_microseconds() - started_tracing_at_microseconds;

  if (!thread_meta->current_thread_seen) {
    initialize_thread_id(thread_meta);
    render_thread_metadata(thread_meta);
  }

  // Each event is converted into two events in the output: one that signals the end of the previous event
  // (whatever it was), and one that signals the start of the actual event we're processing.
  // Yes, this seems to be slightly bending the intention of the output format, but it seemed easier to do this way.

  // Important note: We've observed some rendering issues in perfetto if the tid or pid are numbers that are "too big",
  // see https://github.com/ivoanjo/gvl-tracing/pull/4#issuecomment-1196463364 for an example.

  fprintf(output_file,
    // Finish previous duration
    "  {\"ph\": \"E\", \"pid\": %"PRId64", \"tid\": %"PRIu64", \"ts\": %f},\n" \
    // Current event
    "  {\"ph\": \"B\", \"pid\": %"PRId64", \"tid\": %"PRIu64", \"ts\": %f, \"name\": \"%s\"},\n",
    // Args for first line
    process_id, thread_meta->thread_object_id, now_microseconds,
    // Args for second line
    process_id, thread_meta->thread_object_id, now_microseconds, event_name
  );
}

static void on_thread_event(rb_event_flag_t event_id, const rb_internal_thread_event_data_t *event_data, UNUSED_ARG void *_unused2) {
#if RUBY_3_3_PLUS
  thread_meta_t *thread_meta = current_thread_meta(event_data->thread);
#else
  thread_meta_t *thread_meta = &thread_local_meta;
#endif

  // In some cases, Ruby seems to emit multiple suspended events for the same thread in a row (e.g. when multiple threads)
  // are waiting on a Thread::ConditionVariable.new that gets signaled. We coalesce these events to make the resulting
  // timeline easier to see.
  //
  // I haven't observed other situations where we'd want to coalesce events, but we may apply this to all events in the
  // future. One annoying thing to remember when generalizing this is how to reset the `previous_state` across multiple
  // start/stop calls to GvlTracing.
  if (event_id == RUBY_INTERNAL_THREAD_EVENT_SUSPENDED && event_id == thread_meta->previous_state) return;
  thread_meta->previous_state = event_id;
  thread_meta->current_state = event_id;

  if (event_id == RUBY_INTERNAL_THREAD_EVENT_SUSPENDED && thread_meta->sleeping) {
    render_event("sleeping", thread_meta);
    return;
  } else {
    thread_meta->sleeping = false;
  }

  render_event(get_event_name(event_id), thread_meta);

#if RUBY_3_3_PLUS
  if (event_id == RUBY_INTERNAL_THREAD_EVENT_EXITED) {
    free_thread_meta(event_data->thread);
  }
#endif
}

static void on_gc_event(VALUE tpval, UNUSED_ARG void *_unused1) {
  int event_flag = rb_tracearg_event_flag(rb_tracearg_from_tracepoint(tpval));
  render_event(get_event_name(event_flag), current_thread_meta(rb_thread_current()));
}

static VALUE mark_sleeping(UNUSED_ARG VALUE _self) {
  thread_meta_t *thread_meta = current_thread_meta(rb_thread_current());
  thread_meta->sleeping = true;
  return Qnil;
}

static const char* get_event_name(rb_event_flag_t event_id) {
  switch (event_id) {
    case RUBY_INTERNAL_THREAD_EVENT_READY:     return "wants_gvl";
    case RUBY_INTERNAL_THREAD_EVENT_RESUMED:   return "running";
    case RUBY_INTERNAL_THREAD_EVENT_SUSPENDED: return "waiting";
    case RUBY_INTERNAL_THREAD_EVENT_STARTED:   return "started";
    case RUBY_INTERNAL_THREAD_EVENT_EXITED:    return "died";
    case RUBY_INTERNAL_EVENT_GC_ENTER:         return "gc";
    // TODO: is it possible the thread wasn't running? Might need to save the last state.
    case RUBY_INTERNAL_EVENT_GC_EXIT:          return "running";
    default:                                   return "bug_unknown_event";
  };
}