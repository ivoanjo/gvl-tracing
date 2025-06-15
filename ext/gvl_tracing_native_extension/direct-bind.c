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

// See direct-bind.h for details on using direct-bind and why you may be finding this file vendored inside another gem.

#include "direct-bind.h"

static bool direct_bind_self_test(bool raise_on_failure);

// # Initialization and version management

bool direct_bind_initialize(VALUE publish_version_under, bool raise_on_failure) {
  if (!direct_bind_self_test(raise_on_failure)) return false;

  if (publish_version_under != Qnil) {
    rb_define_const(rb_define_module_under(publish_version_under, "DirectBind"), "VERSION", rb_str_new_lit(DIRECT_BIND_VERSION));
  }

  return true;
}

// # Self-test implementation

#define SELF_TEST_ARITY 3

static VALUE self_test_target_func(
  __attribute__((unused)) VALUE _1,
  __attribute__((unused)) VALUE _2,
  __attribute__((unused)) VALUE _3,
  __attribute__((unused)) VALUE _4
) {
  return Qnil;
}

static bool direct_bind_self_test(bool raise_on_failure) {
  VALUE anonymous_module = rb_module_new();
  rb_define_method(anonymous_module, "direct_bind_self_test_target", self_test_target_func, SELF_TEST_ARITY);

  ID self_test_id = rb_intern("direct_bind_self_test_target");
  direct_bind_cfunc_result test_target = direct_bind_get_cfunc_with_arity(anonymous_module, self_test_id, SELF_TEST_ARITY, raise_on_failure);

  return test_target.ok && test_target.func == self_test_target_func;
}

// # Structure layouts and exported symbol definitions from Ruby

// ## From internal/gc.h
void rb_objspace_each_objects(int (*callback)(void *start, void *end, size_t stride, void *data), void *data);
int rb_objspace_internal_object_p(VALUE obj);

// ## From method.h
typedef struct rb_method_entry_struct {
  VALUE flags;
  VALUE defined_class;
  struct rb_method_definition_struct * const def;
  ID called_id;
  VALUE owner;
} rb_method_entry_t;

// ### This was simplified/inlined vs the original structure
struct rb_method_definition_struct {
  unsigned int type: 4;
  int _ignored;
  struct {
    VALUE (*func)(ANYARGS);
    void *_ignored;
    int argc;
  } cfunc;
};

// # This is where the magic happens: Using objectspace to find the method entry and retrieve the cfunc

typedef struct {
  VALUE target_klass;
  ID target_id;
  direct_bind_cfunc_result result;
} find_data_t;

static bool valid_method_entry(VALUE object);
static bool found_target_method_entry(rb_method_entry_t *method_entry, find_data_t *find_data);
static int find_cfunc(void *start, void *end, size_t stride, void *data);

direct_bind_cfunc_result direct_bind_get_cfunc(VALUE klass, ID method_name, bool raise_on_failure) {
  VALUE definition_not_found = rb_sprintf("method %"PRIsVALUE".%"PRIsVALUE" not found", klass, ID2SYM(method_name));

  find_data_t find_data = {.target_klass = klass, .target_id = method_name, .result = {.ok = false, .failure_reason = definition_not_found}};
  rb_objspace_each_objects(find_cfunc, &find_data);

  if (raise_on_failure && find_data.result.ok == false) {
    rb_raise(rb_eRuntimeError, "direct_bind_get_cfunc failed: %"PRIsVALUE, find_data.result.failure_reason);
  }

  return find_data.result;
}

direct_bind_cfunc_result direct_bind_get_cfunc_with_arity(VALUE klass, ID method_name, int arity, bool raise_on_failure) {
  direct_bind_cfunc_result result = direct_bind_get_cfunc(klass, method_name, raise_on_failure);

  if (result.ok && result.arity != arity) {
    VALUE unexpected_arity = rb_sprintf("method %"PRIsVALUE".%"PRIsVALUE" unexpected arity %d, expected %d", klass, ID2SYM(method_name), result.arity, arity);

    if (raise_on_failure) rb_raise(rb_eRuntimeError, "direct_bind_get_cfunc_with_arity failed: %"PRIsVALUE, unexpected_arity);
    else result = (direct_bind_cfunc_result) {.ok = false, .failure_reason = unexpected_arity};
  }

  return result;
}

// TODO: Maybe change this to use safe memory reads that can never segv (e.g. if structure layouts are off?)
static int find_cfunc(void *start, void *end, size_t stride, void *data) {
  const int stop_iteration = 1;
  const int continue_iteration = 0;
  const int vm_method_type_cfunc = 1;

  find_data_t *find_data = (find_data_t *) data;

  for (VALUE v = (VALUE) start; v != (VALUE) end; v += stride) {
    if (!valid_method_entry(v)) continue;

    rb_method_entry_t *method_entry = (rb_method_entry_t*) v;
    if (!found_target_method_entry(method_entry, find_data)) continue;

    if (method_entry->def == NULL) {
      find_data->result.failure_reason = rb_str_new_lit("method_entry->def is NULL");
    } else if (method_entry->def->type != vm_method_type_cfunc) {
      find_data->result.failure_reason = rb_str_new_lit("method_entry is not a cfunc");
    } else {
      find_data->result = (direct_bind_cfunc_result) {
        .ok = true,
        .failure_reason = Qnil,
        .arity = method_entry->def->cfunc.argc,
        .func = method_entry->def->cfunc.func,
      };
    }
    return stop_iteration;
  }

  return continue_iteration;
}

static bool is_method_entry(VALUE imemo) {
  const unsigned long method_entry_id = 6;
  return ((RBASIC(imemo)->flags >> FL_USHIFT) & method_entry_id) == method_entry_id;
}

static bool valid_method_entry(VALUE object) {
  return rb_objspace_internal_object_p(object) && RB_BUILTIN_TYPE(object) == RUBY_T_IMEMO && RB_TYPE_P(object, RUBY_T_IMEMO) && is_method_entry(object);
}

static bool found_target_method_entry(rb_method_entry_t *method_entry, find_data_t *find_data) {
  VALUE method_klass = method_entry->defined_class ? method_entry->defined_class : method_entry->owner;
  return method_klass == find_data->target_klass && method_entry->called_id == find_data->target_id;
}
