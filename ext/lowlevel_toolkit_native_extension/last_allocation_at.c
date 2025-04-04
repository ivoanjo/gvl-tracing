#include <ruby/ruby.h>
#include <ruby/debug.h>

int rb_objspace_internal_object_p(VALUE obj);

#define MAX_DEPTH 1000
static VALUE stack = Qnil;

static void on_newobj_event(VALUE tpval, RB_UNUSED_VAR(void *_)) {
  if (stack == Qnil ||
    rb_objspace_internal_object_p(rb_tracearg_object(rb_tracearg_from_tracepoint(tpval)))) return;
  VALUE buffer[MAX_DEPTH];
  int depth = rb_profile_frames(0, MAX_DEPTH, buffer, NULL);
  rb_ary_clear(stack);
  for (int i = 0; i < depth; i++) rb_ary_push(stack, buffer[i]);
}

static VALUE track_last_allocation_at(RB_UNUSED_VAR(VALUE _)) {
  stack = rb_ary_new_capa(MAX_DEPTH);
  VALUE tp = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_NEWOBJ, on_newobj_event, NULL);
  rb_tracepoint_enable(tp); rb_yield(Qnil); rb_tracepoint_disable(tp);
  return Qnil;
}

static VALUE last_allocation_at(RB_UNUSED_VAR(VALUE _)) {
  VALUE raw_stack = stack;
  stack = Qnil; // Pause recording of stacks while we're doing the copying below
  VALUE result = rb_ary_new();
  for (int i = 0; i < RARRAY_LEN(raw_stack); i++) {
    VALUE entry = rb_ary_entry(raw_stack, i);
    VALUE file = rb_profile_frame_path(entry);
    if (file != Qnil) rb_ary_push(result, rb_ary_new_from_args(2, file, rb_profile_frame_base_label(entry)));
  }
  stack = raw_stack; // Resume recording again
  return result;
}

void init_last_allocation_at(VALUE lowlevel_toolkit_module) {
  rb_global_variable(&stack);
  rb_define_singleton_method(lowlevel_toolkit_module, "track_last_allocation_at", track_last_allocation_at, 0);
  rb_define_singleton_method(lowlevel_toolkit_module, "last_allocation_at", last_allocation_at, 0);
}
