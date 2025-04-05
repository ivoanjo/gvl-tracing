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

void init_track_objects_created(VALUE lowlevel_toolkit_module);
void init_print_gc_timing(VALUE lowlevel_toolkit_module);
void init_on_gc_finish(VALUE lowlevel_toolkit_module);
void init_who_called_me(VALUE lowlevel_toolkit_module);
void init_last_allocation_at(VALUE lowlevel_toolkit_module);

void Init_lowlevel_toolkit_native_extension(void) {
  VALUE lowlevel_toolkit_module = rb_define_module("LowlevelToolkit");

  init_track_objects_created(lowlevel_toolkit_module);
  init_print_gc_timing(lowlevel_toolkit_module);
  init_on_gc_finish(lowlevel_toolkit_module);
  init_last_allocation_at(lowlevel_toolkit_module);
  init_who_called_me(lowlevel_toolkit_module);
}
