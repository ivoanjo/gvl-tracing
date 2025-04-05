#include <ruby/ruby.h>
#include <ruby/debug.h>
#include <ruby/thread.h>

VALUE rb_ident_hash_new(void);

static ID running = Qnil;
static ID wants_gvl = Qnil;

static void on_thread_started(RB_UNUSED_VAR(rb_event_flag_t _), const rb_internal_thread_event_data_t *event_data, void *data) {
  VALUE result = (VALUE) data;
  rb_hash_aset(result, event_data->thread, rb_ary_new_from_args(4, ID2SYM(running), INT2FIX(0), ID2SYM(wants_gvl), INT2FIX(0)));
}

VALUE gvl_track_waiting(RB_UNUSED_VAR(VALUE _)) {
  VALUE result = rb_ident_hash_new();
  rb_internal_thread_event_hook_t *started_hook =
    rb_internal_thread_add_event_hook(on_thread_started, RUBY_INTERNAL_THREAD_EVENT_STARTED, (void *) result);
  rb_yield(result);
  rb_internal_thread_remove_event_hook(started_hook);
  return result;
}

void init_gvl_track_waiting(VALUE lowlevel_toolkit_module) {
  running = rb_intern("running");
  wants_gvl = rb_intern("wants_gvl");
  rb_define_singleton_method(lowlevel_toolkit_module, "gvl_track_waiting", gvl_track_waiting, 0);
}
