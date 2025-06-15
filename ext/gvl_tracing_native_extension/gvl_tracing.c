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
#include <pthread.h>
#include <stdint.h>

#include "direct-bind.h"

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

// For the OS threads view, we emit data as if it was for another pid so it gets grouped separately in perfetto.
// This is a really big hack, but I couldn't think of a better way?
#define OS_THREADS_VIEW_PID (INT64_C(0))

typedef struct {
  bool initialized;
  int32_t current_thread_serial;
  #ifdef RUBY_3_2
    int32_t native_thread_id;
  #endif
  VALUE thread;
  rb_event_flag_t previous_state; // Used to coalesce similar events
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
static VALUE all_seen_threads = Qnil;
static pthread_mutex_t all_seen_threads_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool os_threads_view_enabled;
static uint32_t timeslice_meta_ms = 0;

static ID sleep_id;
static VALUE (*is_thread_alive)(VALUE thread);

static inline void initialize_timeslice_meta(void);
static VALUE tracing_init_local_storage(VALUE, VALUE);
static VALUE tracing_start(UNUSED_ARG VALUE _self, VALUE output_path, VALUE os_threads_view_enabled_arg);
static VALUE tracing_stop(VALUE _self);
static double timestamp_microseconds(void);
static double render_event(thread_local_state *, const char *event_name);
static void on_thread_event(rb_event_flag_t event, const rb_internal_thread_event_data_t *_unused1, void *_unused2);
static void on_gc_event(VALUE tpval, void *_unused1);
static size_t thread_local_state_memsize(UNUSED_ARG const void *_unused);
static void thread_local_state_mark(void *data);
static inline int32_t thread_id_for(thread_local_state *state);
static VALUE ruby_thread_id_for(UNUSED_ARG VALUE _self, VALUE thread);
static VALUE trim_all_seen_threads(UNUSED_ARG VALUE _self);
static void render_os_thread_event(thread_local_state *state, double now_microseconds);
static void finish_previous_os_thread_event(double now_microseconds);
static inline uint32_t current_native_thread_id(void);

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
  rb_global_variable(&all_seen_threads);

  all_seen_threads = rb_ary_new();

  sleep_id = rb_intern("sleep");

  VALUE gvl_tracing_module = rb_define_module("GvlTracing");

  rb_define_singleton_method(gvl_tracing_module, "_init_local_storage", tracing_init_local_storage, 1);
  rb_define_singleton_method(gvl_tracing_module, "_start", tracing_start, 2);
  rb_define_singleton_method(gvl_tracing_module, "_stop", tracing_stop, 0);
  rb_define_singleton_method(gvl_tracing_module, "_thread_id_for", ruby_thread_id_for, 1);
  rb_define_singleton_method(gvl_tracing_module, "trim_all_seen_threads", trim_all_seen_threads, 0);

  initialize_timeslice_meta();

  direct_bind_initialize(gvl_tracing_module, true);
  is_thread_alive = direct_bind_get_cfunc_with_arity(rb_cThread, rb_intern("alive?"), 0, true).func;
}

static inline void initialize_timeslice_meta(void) {
  const char *timeslice = getenv("RUBY_THREAD_TIMESLICE");
  if (timeslice) {
    timeslice_meta_ms = (uint32_t) strtol(timeslice, NULL, 0);
  }
}

static inline void initialize_thread_local_state(thread_local_state *state) {
  state->initialized = true;
  state->current_thread_serial = RUBY_ATOMIC_FETCH_ADD(thread_serial, 1);

  #ifdef RUBY_3_2
    state->native_thread_id = current_native_thread_id();
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

static VALUE tracing_start(UNUSED_ARG VALUE _self, VALUE output_path, VALUE os_threads_view_enabled_arg) {
  Check_Type(output_path, T_STRING);
  if (os_threads_view_enabled_arg != Qtrue && os_threads_view_enabled_arg != Qfalse) rb_raise(rb_eArgError, "os_threads_view_enabled must be true/false");

  trim_all_seen_threads(Qnil);

  if (output_file != NULL) rb_raise(rb_eRuntimeError, "Already started");
  output_file = fopen(StringValuePtr(output_path), "w");
  if (output_file == NULL) rb_syserr_fail(errno, "Failed to open GvlTracing output file");

  thread_local_state *state = GT_CURRENT_THREAD_LOCAL_STATE();
  started_tracing_at_microseconds = timestamp_microseconds();
  process_id = getpid();
  os_threads_view_enabled = (os_threads_view_enabled_arg == Qtrue);

  VALUE ruby_version = rb_const_get(rb_cObject, rb_intern("RUBY_VERSION"));
  Check_Type(ruby_version, T_STRING);

  VALUE metadata = rb_obj_dup(ruby_version);
  if (timeslice_meta_ms > 0) {
    rb_str_append(metadata, rb_sprintf(", %ums", timeslice_meta_ms));
  }

  fprintf(output_file, "[\n");
  fprintf(output_file,
    "  {\"ph\": \"M\", \"pid\": %"PRId64", \"name\": \"process_name\", \"args\": {\"name\": \"Ruby threads view (%s)\"}},\n",
    process_id, StringValuePtr(metadata)
  );

  double now_microseconds = render_event(state, "started_tracing");

  if (os_threads_view_enabled) {
    fprintf(output_file, "  {\"ph\": \"M\", \"pid\": %"PRId64", \"name\": \"process_name\", \"args\": {\"name\": \"OS threads view\"}},\n", OS_THREADS_VIEW_PID);
    render_os_thread_event(state, now_microseconds);
  }

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

  RB_GC_GUARD(metadata);

  return Qtrue;
}

static VALUE tracing_stop(UNUSED_ARG VALUE _self) {
  if (output_file == NULL) rb_raise(rb_eRuntimeError, "Tracing not running");

  thread_local_state *state = GT_CURRENT_THREAD_LOCAL_STATE();
  rb_internal_thread_remove_event_hook(current_hook);
  rb_tracepoint_disable(gc_tracepoint);
  gc_tracepoint = Qnil;

  double now_microseconds = render_event(state, "stopped_tracing");
  if (os_threads_view_enabled) finish_previous_os_thread_event(now_microseconds);

  // closing the json syntax in the output file is handled in GvlTracing.stop code

  if (fclose(output_file) != 0) rb_syserr_fail(errno, "Failed to close GvlTracing output file");

  output_file = NULL;

  #ifdef RUBY_3_3_PLUS
    return all_seen_threads;
  #else
    return rb_funcall(rb_cThread, rb_intern("list"), 0);
  #endif
}

static double timestamp_microseconds(void) {
  struct timespec current_monotonic;
  if (clock_gettime(CLOCK_MONOTONIC, &current_monotonic) != 0) rb_syserr_fail(errno, "Failed to read CLOCK_MONOTONIC");
  return (current_monotonic.tv_nsec / 1000.0) + (current_monotonic.tv_sec * 1000.0 * 1000.0);
}

// Render output using trace event format for perfetto:
// https://chromium.googlesource.com/catapult/+/refs/heads/main/docs/trace-event-format.md
static double render_event(thread_local_state *state, const char *event_name) {
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

  return now_microseconds;
}

static void on_thread_event(rb_event_flag_t event_id, const rb_internal_thread_event_data_t *event_data, UNUSED_ARG void *_unused2) {
  thread_local_state *state = GT_EVENT_LOCAL_STATE(event_data,
    // These events are guaranteed to hold the GVL, so they can allocate
    event_id & (RUBY_INTERNAL_THREAD_EVENT_STARTED | RUBY_INTERNAL_THREAD_EVENT_RESUMED));

  if (!state) return;

  if (!state->thread) {
    #ifdef RUBY_3_3_PLUS
      state->thread = event_data->thread;
    #else
      if (event_id == RUBY_INTERNAL_THREAD_EVENT_SUSPENDED) { state->thread = rb_thread_current(); }
    #endif
  }
  // In some cases, Ruby seems to emit multiple suspended events for the same thread in a row (e.g. when multiple threads)
  // are waiting on a Thread::ConditionVariable.new that gets signaled. We coalesce these events to make the resulting
  // timeline easier to see.
  //
  // I haven't observed other situations where we'd want to coalesce events, but we may apply this to all events in the
  // future. One annoying thing to remember when generalizing this is how to reset the `previous_state` across multiple
  // start/stop calls to GvlTracing.
  if (event_id == RUBY_INTERNAL_THREAD_EVENT_SUSPENDED && event_id == state->previous_state) return;
  state->previous_state = event_id;

  if (event_id == RUBY_INTERNAL_THREAD_EVENT_SUSPENDED &&
      // Check that thread is not being shut down
      (state->thread != Qnil && is_thread_alive(state->thread))
  ) {
    ID current_method = 0;
    VALUE current_method_owner = Qnil;
    rb_frame_method_id_and_class(&current_method, &current_method_owner);

    if (current_method == sleep_id && current_method_owner == rb_mKernel) {
      render_event(state, "sleeping");
      return;
    }
  }

  const char* event_name = "bug_unknown_event";
  switch (event_id) {
    case RUBY_INTERNAL_THREAD_EVENT_READY:     event_name = "wants_gvl"; break;
    case RUBY_INTERNAL_THREAD_EVENT_RESUMED:   event_name = "running";   break;
    case RUBY_INTERNAL_THREAD_EVENT_SUSPENDED: event_name = "waiting";   break;
    case RUBY_INTERNAL_THREAD_EVENT_STARTED:   event_name = "started";   break;
    case RUBY_INTERNAL_THREAD_EVENT_EXITED:    event_name = "died";      break;
  };
  double now_microseconds = render_event(state, event_name);

  if (os_threads_view_enabled) {
    if (event_id == RUBY_INTERNAL_THREAD_EVENT_RESUMED) {
      render_os_thread_event(state, now_microseconds);
    } else if (event_id == RUBY_INTERNAL_THREAD_EVENT_SUSPENDED || event_id == RUBY_INTERNAL_THREAD_EVENT_EXITED) {
      finish_previous_os_thread_event(now_microseconds);
    }
  }
}

static void on_gc_event(VALUE tpval, UNUSED_ARG void *_unused1) {
  const char* event_name = "bug_unknown_event";
  thread_local_state *state = GT_LOCAL_STATE(rb_thread_current(), false); // no alloc during GC

  if (!state) return;

  switch (rb_tracearg_event_flag(rb_tracearg_from_tracepoint(tpval))) {
    case RUBY_INTERNAL_EVENT_GC_ENTER: event_name = "gc"; break;
    // TODO: is it possible the thread wasn't running? Might need to save the last state.
    case RUBY_INTERNAL_EVENT_GC_EXIT: event_name = "running"; break;
  }
  render_event(state, event_name);
}

static size_t thread_local_state_memsize(UNUSED_ARG const void *_unused) { return sizeof(thread_local_state); }

static void thread_local_state_mark(void *data) {
  thread_local_state *state = (thread_local_state *)data;
  rb_gc_mark(state->thread); // Marking thread to make sure it stays pinned
}

static inline void all_seen_threads_mutex_lock(void) {
  int error = pthread_mutex_lock(&all_seen_threads_mutex);
  if (error) rb_syserr_fail(error, "Failed to lock GvlTracing mutex");
}

static inline void all_seen_threads_mutex_unlock(void) {
  int error = pthread_mutex_unlock(&all_seen_threads_mutex);
  if (error) rb_syserr_fail(error, "Failed to unlock GvlTracing mutex");
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

      // Keep thread around, to be able to extract names at the end
      // We grab a lock here since thread creation can happen in multiple Ractors, and we want to make sure only one
      // of them is mutating the array at a time. @ivoanjo: I think this is enough to make this safe....?
      all_seen_threads_mutex_lock();
      rb_ary_push(all_seen_threads, thread);
      all_seen_threads_mutex_unlock();
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

// Can only be called while GvlTracing is not active + while holding the GVL
static VALUE trim_all_seen_threads(UNUSED_ARG VALUE _self) {
  all_seen_threads_mutex_lock();

  VALUE alive_threads = rb_ary_new();

  for (long i = 0, len = RARRAY_LEN(all_seen_threads); i < len; i++) {
    VALUE thread = RARRAY_AREF(all_seen_threads, i);
    if (rb_funcall(thread, rb_intern("alive?"), 0) == Qtrue) {
      rb_ary_push(alive_threads, thread);
    }
  }

  rb_ary_replace(all_seen_threads, alive_threads);

  all_seen_threads_mutex_unlock();
  return Qtrue;
}

// Creates an event that follows the current native thread. Note that this assumes that whatever event
// made us call `render_os_thread_event` is an event about the current (native) thread; if the event is not about the
// current thread, the results will be incorrect.
static void render_os_thread_event(thread_local_state *state, double now_microseconds) {
  finish_previous_os_thread_event(now_microseconds);

  // Hack: If we name threads as "Thread N", perfetto seems to color them all with the same color, which looks awful.
  // I did not check the code, but in practice perfetto seems to be doing some kind of hashing based only on regular
  // chars, so here we append a different letter to each thread to cause the color hashing to differ.
  char color_suffix_hack = ('a' + (thread_id_for(state) % 26));

  fprintf(output_file,
    "  {\"ph\": \"B\", \"pid\": %"PRId64", \"tid\": %u, \"ts\": %f, \"name\": \"Thread %d (%c)\"},\n",
    OS_THREADS_VIEW_PID, current_native_thread_id(), now_microseconds, thread_id_for(state), color_suffix_hack
  );
}

static void finish_previous_os_thread_event(double now_microseconds) {
  fprintf(output_file,
    "  {\"ph\": \"E\", \"pid\": %"PRId64", \"tid\": %u, \"ts\": %f},\n",
    OS_THREADS_VIEW_PID, current_native_thread_id(), now_microseconds
  );
}

static inline uint32_t current_native_thread_id(void) {
  uint32_t native_thread_id = 0;

  #ifdef HAVE_PTHREAD_THREADID_NP
    uint64_t full_native_thread_id;
    pthread_threadid_np(pthread_self(), &full_native_thread_id);
    // Note: `pthread_threadid_np` is declared as taking in a `uint64_t` but I don't think macOS uses such really
    // high thread ids, and anyway perfetto doesn't like full 64-bit ids for threads so let's go with a simplification
    // for now.
    native_thread_id = (uint32_t) full_native_thread_id;
  #elif HAVE_GETTID
    native_thread_id = gettid();
  #else
    // Note: We could use a native thread-local crappy fallback, but I think the two above alternatives are available
    // on all OSs that support the GVL tracing API.
    #error No native thread id available?
  #endif

  return native_thread_id;
}
