// lowlevel-toolkit: Ruby gem for calling observability APIs
// Copyright (c) 2025 Ivo Anjo <ivo@ivoanjo.me>
//
// This file is part of lowlevel-toolkit.
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
#include <time.h>
#include "extconf.h"

int rb_objspace_internal_object_p(VALUE obj);

#ifdef __GNUC__
  #define UNUSED_ARG  __attribute__((unused))
#else
  #define UNUSED_ARG
#endif

static void on_newobj_event(VALUE tpval, void *data);
static void on_gc_event_timing(VALUE tpval, void *data);
static VALUE track_objects_created(VALUE self);
static VALUE print_gc_timing(VALUE self);

void init_on_gc_finish(VALUE lowlevel_toolkit_module);
void init_who_called_me(VALUE lowlevel_toolkit_module);
void init_last_allocation_at(VALUE lowlevel_toolkit_module);

void Init_lowlevel_toolkit_native_extension(void) {
  VALUE lowlevel_toolkit_module = rb_define_module("LowlevelToolkit");

  rb_define_singleton_method(lowlevel_toolkit_module, "track_objects_created", track_objects_created, 0);
  rb_define_singleton_method(lowlevel_toolkit_module, "print_gc_timing", print_gc_timing, 0);
  init_on_gc_finish(lowlevel_toolkit_module);
  init_last_allocation_at(lowlevel_toolkit_module);
  init_who_called_me(lowlevel_toolkit_module);
}

static VALUE track_objects_created(VALUE self) {
  VALUE result = rb_ary_new();
  VALUE tp = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_NEWOBJ, on_newobj_event, (void *) result);
  rb_tracepoint_enable(tp);
  rb_yield(Qnil);
  rb_tracepoint_disable(tp);
  // Filter out any objects that were marked as hidden after being created
  for (int i = 0; i < RARRAY_LEN(result); i++) if (RBASIC_CLASS(rb_ary_entry(result, i)) == 0) rb_ary_store(result, i, Qnil);
  return result;
}

static void on_newobj_event(VALUE tpval, void *data) {
  VALUE result = (VALUE) data;
  VALUE obj = rb_tracearg_object(rb_tracearg_from_tracepoint(tpval));
  if (!rb_objspace_internal_object_p(obj)) rb_ary_push(result, obj);
}

static uint64_t get_monotonic_time_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static VALUE print_gc_timing(VALUE self) {
  VALUE tp = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_GC_ENTER | RUBY_INTERNAL_EVENT_GC_EXIT, on_gc_event_timing, NULL);
  rb_tracepoint_enable(tp);
  rb_yield(Qnil);
  rb_tracepoint_disable(tp);
  return Qnil;
}

static void on_gc_event_timing(VALUE tpval, UNUSED_ARG void *data) {
  static uint64_t gc_start_time = 0;
  rb_event_flag_t event = rb_tracearg_event_flag(rb_tracearg_from_tracepoint(tpval));

  if (event == RUBY_INTERNAL_EVENT_GC_ENTER) {
    gc_start_time = get_monotonic_time_ns();
  } else if (event == RUBY_INTERNAL_EVENT_GC_EXIT) {
    uint64_t gc_end_time = get_monotonic_time_ns();
    uint64_t gc_duration_ns = gc_end_time - gc_start_time;

    fprintf(stdout, "GC worked for %.2f ms\n", (gc_duration_ns / 1000000.0));
  }
}
