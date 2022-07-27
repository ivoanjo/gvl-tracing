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
#include <ruby/thread.h>
#include <ruby/atomic.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>

static VALUE tracing_start(VALUE _self, VALUE output_path);
static VALUE tracing_stop(VALUE _self);
static double timestamp_microseconds(void);
static void render_event(const char *event_name);
static void on_event(rb_event_flag_t event, const rb_internal_thread_event_data_t *_unused1, void *_unused2);
static rb_atomic_t thread_serial = 0;
static _Thread_local bool current_thread_serial_set = false;
static _Thread_local unsigned int current_thread_serial = 0;

// Global mutable state
static FILE *output_file = NULL;
static rb_internal_thread_event_hook_t *current_hook = NULL;
static double started_tracing_at_microseconds = 0;
static pid_t process_id = 0;

void Init_gvl_tracing_native_extension(void) {
  VALUE gvl_tracing_module = rb_define_module("GvlTracing");

  rb_define_singleton_method(gvl_tracing_module, "start", tracing_start, 1);
  rb_define_singleton_method(gvl_tracing_module, "stop", tracing_stop, 0);
}

static inline unsigned int current_thread_id(void) {
    if (!current_thread_serial_set) {
        current_thread_serial_set = true;
        current_thread_serial = RUBY_ATOMIC_FETCH_ADD(thread_serial, 1);
    }
    return (unsigned int)current_thread_serial;
}

static VALUE tracing_start(VALUE _self, VALUE output_path) {
  Check_Type(output_path, T_STRING);

  if (output_file != NULL) rb_raise(rb_eRuntimeError, "Already started");
  output_file = fopen(StringValuePtr(output_path), "w");
  if (output_file == NULL) rb_syserr_fail(errno, "Failed to open GvlTracing output file");

  started_tracing_at_microseconds = timestamp_microseconds();
  process_id = getpid();

  fprintf(output_file, "[\n");
  render_event("started_tracing");

  current_hook = rb_internal_thread_add_event_hook(
    on_event,
    (
      RUBY_INTERNAL_THREAD_EVENT_READY |
      RUBY_INTERNAL_THREAD_EVENT_RESUMED |
      RUBY_INTERNAL_THREAD_EVENT_SUSPENDED |
      RUBY_INTERNAL_THREAD_EVENT_STARTED |
      RUBY_INTERNAL_THREAD_EVENT_EXITED
    ),
    NULL
  );

  return Qtrue;
}

static VALUE tracing_stop(VALUE _self) {
  if (output_file == NULL) rb_raise(rb_eRuntimeError, "Tracing not running");

  rb_internal_thread_remove_event_hook(current_hook);

  render_event("stopped_tracing");
  fprintf(output_file, "]\n");

  if (fclose(output_file) != 0) rb_syserr_fail(errno, "Failed to close GvlTracing output file");

  output_file == NULL;

  return Qtrue;
}

static double timestamp_microseconds(void) {
  struct timespec current_monotonic;
  if (clock_gettime(CLOCK_MONOTONIC, &current_monotonic) != 0) rb_syserr_fail(errno, "Failed to read CLOCK_MONOTONIC");
  return (current_monotonic.tv_nsec / 1000.0) + (current_monotonic.tv_sec * 1000.0 * 1000.0);
}

// Render output using trace event format for perfetto:
// https://chromium.googlesource.com/catapult/+/refs/heads/main/docs/trace-event-format.md
static void render_event(const char *event_name) {
  // Event data
  double now_microseconds = timestamp_microseconds() - started_tracing_at_microseconds;
  unsigned int thread_id = current_thread_id();

  // Each event is converted into two events in the output: one that signals the end of the previous event
  // (whatever it was), and one that signals the start of the actual event we're processing.
  // Yes this is seems to be bending a bit the intention of the output format, but it seemed easier to do this way.

  fprintf(output_file,
    // Finish previous duration
    "  {\"ph\": \"E\", \"pid\": %u, \"tid\": %u, \"ts\": %f},\n" \
    // Current event
    "  {\"ph\": \"B\", \"pid\": %u, \"tid\": %u, \"ts\": %f, \"name\": \"%s\"},\n",
    // Args for first line
    process_id, thread_id, now_microseconds,
    // Args for second line
    process_id, thread_id, now_microseconds, event_name
  );
}

static void on_event(rb_event_flag_t event_id, const rb_internal_thread_event_data_t *_unused1, void *_unused2) {
  const char* event_name = "bug_unknown_event";
  switch (event_id) {
    case RUBY_INTERNAL_THREAD_EVENT_READY:     event_name = "ready";     break;
    case RUBY_INTERNAL_THREAD_EVENT_RESUMED:   event_name = "resumed";   break;
    case RUBY_INTERNAL_THREAD_EVENT_SUSPENDED: event_name = "suspended"; break;
    case RUBY_INTERNAL_THREAD_EVENT_STARTED:   event_name = "started";   break;
    case RUBY_INTERNAL_THREAD_EVENT_EXITED:    event_name = "exited";    break;
  };
  render_event(event_name);
}
