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
#include "extconf.h"

int rb_objspace_internal_object_p(VALUE obj);

// Used to mark function arguments that are deliberately left unused
#ifdef __GNUC__
  #define UNUSED_ARG  __attribute__((unused))
#else
  #define UNUSED_ARG
#endif

static void on_newobj_event(VALUE tpval, void *data);
static VALUE track_objects_created(VALUE self);

void Init_lowlevel_toolkit_native_extension(void) {
  VALUE lowlevel_toolkit_module = rb_define_module("LowlevelToolkit");

  rb_define_singleton_method(lowlevel_toolkit_module, "track_objects_created", track_objects_created, 0);
}

static VALUE track_objects_created(VALUE self) {
  VALUE result = rb_ary_new();
  VALUE tp = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_NEWOBJ, on_newobj_event, (void *) result);
  rb_tracepoint_enable(tp);
  rb_yield(Qnil);
  rb_tracepoint_disable(tp);
  return result;
}

static void on_newobj_event(VALUE tpval, void *data) {
  VALUE result = (VALUE) data;
  VALUE obj = rb_tracearg_object(rb_tracearg_from_tracepoint(tpval));
  if (!rb_objspace_internal_object_p(obj)) rb_ary_push(result, obj);
}
