// direct-bind: Ruby gem for getting direct access to function pointers
// Copyright (c) 2025 Ivo Anjo <ivo@ivoanjo.me>
//
// This file is part of direct-bind.
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

// The recommended way to consume the direct-bind gem is to always vendor it.
// That is, use its rake task to automatically copy direct-bind.h and direct-bind.c into another gem's native extension
// sources folder. (P.s.: There's also a test helper to make sure copying is working fine and the gem is up-to-date.)
//
// This makes the actual Ruby direct-bind gem only a development dependency, simplifying distribution for the gem
// that uses it.
//
// For more details, check the direct-bind gem's documentation.

#pragma once

#include <stdbool.h>
#include <ruby.h>

#define DIRECT_BIND_VERSION "1.0.0.beta1"

typedef struct {
  bool ok;
  VALUE failure_reason;
  int arity;
  VALUE (*func)(ANYARGS);
} direct_bind_cfunc_result;

// Recommended to call once during your gem's initialization, to validate that direct-bind's Ruby hacking is in good shape and
// to make it easy to (optionally) validate what version you're using
bool direct_bind_initialize(VALUE publish_version_under, bool raise_on_failure);

// Provides the reverse of `rb_define_method`: Given a class and a method_name, retrieves the arity and func previously
// passed to `rb_define_method`.
//
// Performance note: As of this writing, this method scans objspace to find the definition of the method, so you
// most probably want to cache its result, rather than calling it very often.
direct_bind_cfunc_result direct_bind_get_cfunc(VALUE klass, ID method_name, bool raise_on_failure);

// Same as above, but automatically fails if arity isn't the expected value
direct_bind_cfunc_result direct_bind_get_cfunc_with_arity(VALUE klass, ID method_name, int arity, bool raise_on_failure);
