#include <ruby/ruby.h>
#include <ruby/debug.h>

int rb_objspace_internal_object_p(VALUE obj);

static void on_newobj_event(VALUE tpval, void *data) {
  VALUE obj = rb_tracearg_object(rb_tracearg_from_tracepoint(tpval));
  if (!rb_objspace_internal_object_p(obj)) rb_ary_push((VALUE) data, obj);
}

static VALUE filter_hidden_objects(VALUE result) {
  for (int i = 0; i < RARRAY_LEN(result); i++)
    if (!RBASIC_CLASS(rb_ary_entry(result, i))) rb_ary_store(result, i, Qnil);
  return result;
}

static VALUE track_objects_created(RB_UNUSED_VAR(VALUE _)) {
  VALUE result = rb_ary_new();
  VALUE tp = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_NEWOBJ, on_newobj_event, (void *) result);
  rb_tracepoint_enable(tp); rb_yield(Qnil); rb_tracepoint_disable(tp);
  return filter_hidden_objects(result);
}

void init_track_objects_created(VALUE lowlevel_toolkit_module) {
  rb_define_singleton_method(lowlevel_toolkit_module, "track_objects_created", track_objects_created, 0);
}
