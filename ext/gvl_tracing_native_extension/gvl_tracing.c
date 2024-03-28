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

// Used to mark function arguments that are deliberately left unused
#ifdef __GNUC__
  #define UNUSED_ARG  __attribute__((unused))
#else
  #define UNUSED_ARG
#endif

#ifdef HAVE_RB_INTERNAL_THREAD_SPECIFIC_GET
  #define RUBY_3_3_PLUS
#else
  #define RUBY_3_2
#endif

typedef struct {
  bool initialized;
  int32_t current_thread_serial;
  #ifdef RUBY_3_2
    int32_t native_thread_id;
  #endif
  VALUE thread;
  rb_event_flag_t previous_state; // Used to coalesce similar events
  bool sleeping; // Used to track when a thread is sleeping
} thread_local_state;

// Global mutable state
static rb_atomic_t thread_serial = 0;
static FILE *output_file = NULL;
static rb_internal_thread_event_hook_t *current_hook = NULL;
static double started_tracing_at_microseconds = 0;
static int64_t process_id = 0;
static VALUE gc_tracepoint = Qnil;
#pragma GCC diagnostic ignored "-Wunused-variable"
static int thread_storage_key = 0;

static VALUE tracing_init_local_storage(VALUE, VALUE);
static VALUE tracing_start(VALUE _self, VALUE output_path);
static VALUE tracing_stop(VALUE _self);
static double timestamp_microseconds(void);
static void render_event(thread_local_state *, const char *event_name);
static void on_thread_event(rb_event_flag_t event, const rb_internal_thread_event_data_t *_unused1, void *_unused2);
static void on_gc_event(VALUE tpval, void *_unused1);
static VALUE mark_sleeping(VALUE _self);
static size_t thread_local_state_memsize(UNUSED_ARG const void *_unused);
static void thread_local_state_mark(void *data);
static inline int32_t thread_id_for(thread_local_state *state);
static VALUE ruby_thread_id_for(UNUSED_ARG VALUE _self, VALUE thread);

#pragma GCC diagnostic ignored "-Wunused-const-variable"
static const rb_data_type_t thread_local_state_type = {
  .wrap_struct_name = "GvlTracing::__threadLocal",
  .function = {
    .dmark = thread_local_state_mark,
    .dfree = RUBY_DEFAULT_FREE,
    .dsize = thread_local_state_memsize,
  },
  .flags = RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_WB_PROTECTED,
};

#ifdef RUBY_3_3_PLUS
  static inline thread_local_state *GT_LOCAL_STATE(VALUE thread, bool allocate);
  #define GT_EVENT_LOCAL_STATE(event_data, allocate) GT_LOCAL_STATE(event_data->thread, allocate)
  // Must only be called from a thread holding the GVL
  #define GT_CURRENT_THREAD_LOCAL_STATE() GT_LOCAL_STATE(rb_thread_current(), true)
#else
  // Thread-local state
  static _Thread_local thread_local_state __thread_local_state = { 0 };

  static inline thread_local_state *GT_CURRENT_THREAD_LOCAL_STATE(void);
  #define GT_LOCAL_STATE(thread, allocate) GT_CURRENT_THREAD_LOCAL_STATE()
  #define GT_EVENT_LOCAL_STATE(event_data, allocate) GT_CURRENT_THREAD_LOCAL_STATE()
#endif

void Init_gvl_tracing_native_extension(void) {
  #ifdef RUBY_3_3_PLUS
    thread_storage_key = rb_internal_thread_specific_key_create();
  #endif

  rb_global_variable(&gc_tracepoint);

  VALUE gvl_tracing_module = rb_define_module("GvlTracing");

  rb_define_singleton_method(gvl_tracing_module, "_init_local_storage", tracing_init_local_storage, 1);
  rb_define_singleton_method(gvl_tracing_module, "_start", tracing_start, 1);
  rb_define_singleton_method(gvl_tracing_module, "_stop", tracing_stop, 0);
  rb_define_singleton_method(gvl_tracing_module, "mark_sleeping", mark_sleeping, 0);
  rb_define_singleton_method(gvl_tracing_module, "_thread_id_for", ruby_thread_id_for, 1);
}

static inline void initialize_thread_local_state(thread_local_state *state) {
  state->initialized = true;
  state->current_thread_serial = RUBY_ATOMIC_FETCH_ADD(thread_serial, 1);

  #ifdef RUBY_3_2
    uint32_t native_thread_id = 0;

    #ifdef HAVE_PTHREAD_THREADID_NP
      pthread_threadid_np(pthread_self(), &native_thread_id);
    #elif HAVE_GETTID
      native_thread_id = gettid();
    #else
      // Note: We could use the current_thread_serial as a crappy fallback, but this would make getting thread names
      // not work very well
      #error No native thread id available?
    #endif

    state->native_thread_id = native_thread_id;
  #endif
}

static VALUE tracing_init_local_storage(UNUSED_ARG VALUE _self, VALUE threads) {
  #ifdef RUBY_3_3_PLUS
    for (long i = 0, len = RARRAY_LEN(threads); i < len; i++) {
        VALUE thread = RARRAY_AREF(threads, i);
        GT_LOCAL_STATE(thread, true);
    }
  #endif
  return Qtrue;
}

static VALUE tracing_start(UNUSED_ARG VALUE _self, VALUE output_path) {
  Check_Type(output_path, T_STRING);

  if (output_file != NULL) rb_raise(rb_eRuntimeError, "Already started");
  output_file = fopen(StringValuePtr(output_path), "w");
  if (output_file == NULL) rb_syserr_fail(errno, "Failed to open GvlTracing output file");

  fprintf(output_file, "[\n");

  thread_local_state *state = GT_CURRENT_THREAD_LOCAL_STATE();
  started_tracing_at_microseconds = timestamp_microseconds();
  process_id = getpid();

  render_event(state, "started_tracing");

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

  gc_tracepoint = rb_tracepoint_new(0, (RUBY_INTERNAL_EVENT_GC_ENTER | RUBY_INTERNAL_EVENT_GC_EXIT), on_gc_event, NULL);

  rb_tracepoint_enable(gc_tracepoint);

  return Qtrue;
}

static VALUE tracing_stop(UNUSED_ARG VALUE _self) {
  if (output_file == NULL) rb_raise(rb_eRuntimeError, "Tracing not running");

  thread_local_state *state = GT_CURRENT_THREAD_LOCAL_STATE();
  rb_internal_thread_remove_event_hook(current_hook);
  rb_tracepoint_disable(gc_tracepoint);
  gc_tracepoint = Qnil;

  render_event(state, "stopped_tracing");
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
static void render_event(thread_local_state *state, const char *event_name) {
  // Event data
  double now_microseconds = timestamp_microseconds() - started_tracing_at_microseconds;

  // Each event is converted into two events in the output: one that signals the end of the previous event
  // (whatever it was), and one that signals the start of the actual event we're processing.
  // Yes, this seems to be slightly bending the intention of the output format, but it seemed easier to do this way.

  // Important note: We've observed some rendering issues in perfetto if the tid or pid are numbers that are "too big",
  // see https://github.com/ivoanjo/gvl-tracing/pull/4#issuecomment-1196463364 for an example.

  fprintf(output_file,
    // Finish previous duration
    "  {\"ph\": \"E\", \"pid\": %"PRId64", \"tid\": %d, \"ts\": %f},\n" \
    // Current event
    "  {\"ph\": \"B\", \"pid\": %"PRId64", \"tid\": %d, \"ts\": %f, \"name\": \"%s\"},\n",
    // Args for first line
    process_id, thread_id_for(state), now_microseconds,
    // Args for second line
    process_id, thread_id_for(state), now_microseconds, event_name
  );
}

static void on_thread_event(rb_event_flag_t event_id, const rb_internal_thread_event_data_t *event_data, UNUSED_ARG void *_unused2) {
  thread_local_state *state = GT_EVENT_LOCAL_STATE(event_data,
    // These events are guaranteed to hold the GVL, so they can allocate
    event_id & (RUBY_INTERNAL_THREAD_EVENT_STARTED | RUBY_INTERNAL_THREAD_EVENT_RESUMED));
  if (!state) return;
  #ifdef RUBY_3_3_PLUS
    if (!state->thread) state->thread = event_data->thread;
  #endif
  // In some cases, Ruby seems to emit multiple suspended events for the same thread in a row (e.g. when multiple threads)
  // are waiting on a Thread::ConditionVariable.new that gets signaled. We coalesce these events to make the resulting
  // timeline easier to see.
  //
  // I haven't observed other situations where we'd want to coalesce events, but we may apply this to all events in the
  // future. One annoying thing to remember when generalizing this is how to reset the `previous_state` across multiple
  // start/stop calls to GvlTracing.
  if (event_id == RUBY_INTERNAL_THREAD_EVENT_SUSPENDED && event_id == state->previous_state) return;
  state->previous_state = event_id;

  if (event_id == RUBY_INTERNAL_THREAD_EVENT_SUSPENDED && state->sleeping) {
    render_event(state, "sleeping");
    return;
  } else {
    state->sleeping = false;
  }

  const char* event_name = "bug_unknown_event";
  switch (event_id) {
    case RUBY_INTERNAL_THREAD_EVENT_READY:     event_name = "wants_gvl"; break;
    case RUBY_INTERNAL_THREAD_EVENT_RESUMED:   event_name = "running";   break;
    case RUBY_INTERNAL_THREAD_EVENT_SUSPENDED: event_name = "waiting";   break;
    case RUBY_INTERNAL_THREAD_EVENT_STARTED:   event_name = "started";   break;
    case RUBY_INTERNAL_THREAD_EVENT_EXITED:    event_name = "died";      break;
  };
  render_event(state, event_name);
}

static void on_gc_event(VALUE tpval, UNUSED_ARG void *_unused1) {
  const char* event_name = "bug_unknown_event";
  thread_local_state *state = GT_LOCAL_STATE(rb_thread_current(), false); // no alloc during GC
  switch (rb_tracearg_event_flag(rb_tracearg_from_tracepoint(tpval))) {
    case RUBY_INTERNAL_EVENT_GC_ENTER: event_name = "gc"; break;
    // TODO: is it possible the thread wasn't running? Might need to save the last state.
    case RUBY_INTERNAL_EVENT_GC_EXIT: event_name = "running"; break;
  }
  render_event(state, event_name);
}

static VALUE mark_sleeping(UNUSED_ARG VALUE _self) {
  GT_CURRENT_THREAD_LOCAL_STATE()->sleeping = true;
  return Qnil;
}

static size_t thread_local_state_memsize(UNUSED_ARG const void *_unused) { return sizeof(thread_local_state); }

static void thread_local_state_mark(void *data) {
  thread_local_state *state = (thread_local_state *)data;
  rb_gc_mark(state->thread); // Marking thread to make sure it stays pinned
}

#ifdef RUBY_3_3_PLUS
  static inline thread_local_state *GT_LOCAL_STATE(VALUE thread, bool allocate) {
    thread_local_state *state = rb_internal_thread_specific_get(thread, thread_storage_key);
    if (!state && allocate) {
      VALUE wrapper = TypedData_Make_Struct(rb_cObject, thread_local_state, &thread_local_state_type, state);
      state->thread = thread;
      rb_thread_local_aset(thread, rb_intern("__gvl_tracing_local_state"), wrapper);
      rb_internal_thread_specific_set(thread, thread_storage_key, state);
      RB_GC_GUARD(wrapper);
      initialize_thread_local_state(state);
    }
    return state;
  }
#endif

#ifdef RUBY_3_2
  static inline thread_local_state *GT_CURRENT_THREAD_LOCAL_STATE(void) {
    thread_local_state *state = &__thread_local_state;
    if (!state->initialized) {
      initialize_thread_local_state(state);
    }
    return state;
  }
#endif

static inline int32_t thread_id_for(thread_local_state *state) {
  // We use different strategies for 3.2 vs 3.3+ to identify threads. This is because:
  //
  // 1. On 3.2 we have no way of associating the actual thread VALUE object with the state/serial, so instead we identify
  //    threads by their native ids. This is not entirely correct, since Ruby can reuse native threads (e.g. if a thread
  //    dies and another immediately gets created) but it's good enough for our purposes. (Associating the thread VALUE
  //    object is useful to, e.g. get thread names later.)
  //
  // 2. On 3.3 we can associate the state/serial with the thread VALUE object AND additionally with the MN scheduler
  //    the same thread VALUE can end up executing on different native threads so using the native thread id as an
  //    identifier would be wrong.
  #ifdef RUBY_3_3_PLUS
    return state->current_thread_serial;
  #else
    return state->native_thread_id;
  #endif
}

static VALUE ruby_thread_id_for(UNUSED_ARG VALUE _self, VALUE thread) {
  #ifdef RUBY_3_2
    rb_raise(rb_eRuntimeError, "On Ruby 3.2 we should use the native thread id directly");
  #endif

  thread_local_state *state = GT_LOCAL_STATE(thread, true);
  return INT2FIX(thread_id_for(state));
}
