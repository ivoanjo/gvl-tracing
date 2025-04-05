#include <ruby/ruby.h>
#include <ruby/debug.h>

static VALUE callback = Qnil;
static rb_postponed_job_handle_t postponed_id = 0;

static void postponed(RB_UNUSED_VAR(void *data)) { rb_funcall(callback, rb_intern("call"), 0); }
static void on_gc_finish_event(RB_UNUSED_VAR(VALUE _), RB_UNUSED_VAR(void *__)) { rb_postponed_job_trigger(postponed_id); }

static VALUE on_gc_finish(RB_UNUSED_VAR(VALUE _), VALUE user_callback) {
  callback = user_callback;
  VALUE tp = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_GC_EXIT, on_gc_finish_event, NULL);
  rb_tracepoint_enable(tp); rb_yield(Qnil); rb_tracepoint_disable(tp);
  return Qnil;
}

void init_on_gc_finish(VALUE lowlevel_toolkit_module) {
  postponed_id = rb_postponed_job_preregister(0, postponed, NULL);
  if (postponed_id == POSTPONED_JOB_HANDLE_INVALID) rb_raise(rb_eRuntimeError, "Failed to register postponed job");
  rb_global_variable(&callback);
  rb_define_singleton_method(lowlevel_toolkit_module, "on_gc_finish", on_gc_finish, 1);
}
