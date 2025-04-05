#include <ruby/ruby.h>
#include <ruby/debug.h>
#include <ruby/thread.h>

VALUE rb_ident_hash_new(void);

static uint64_t get_monotonic_time_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}

static int wants_gvl_at_key;

static void on_thread_event(rb_event_flag_t event_id, const rb_internal_thread_event_data_t *event_data, void *data) {
  VALUE thread = event_data->thread;
  if (event_id == RUBY_INTERNAL_THREAD_EVENT_STARTED) {
    rb_hash_aset((VALUE) data, thread, INT2FIX(0));
  } else if (event_id == RUBY_INTERNAL_THREAD_EVENT_READY) {
    rb_internal_thread_specific_set(thread, wants_gvl_at_key, (void *) get_monotonic_time_ns());
  } else if (event_id == RUBY_INTERNAL_THREAD_EVENT_RESUMED) {
    uint64_t wants_gvl_at = (uint64_t) rb_internal_thread_specific_get(thread, wants_gvl_at_key);
    VALUE total = rb_hash_lookup2((VALUE) data, thread, Qnil);
    if (wants_gvl_at == 0 || total == Qnil) return;
    rb_hash_aset((VALUE) data, thread, ULL2NUM(NUM2ULONG(total) + (get_monotonic_time_ns() - wants_gvl_at)));
  }
}

VALUE track_wants_gvl(RB_UNUSED_VAR(VALUE _)) {
  VALUE result = rb_ident_hash_new();
  rb_internal_thread_event_hook_t *hook =
    rb_internal_thread_add_event_hook(
      on_thread_event, RUBY_INTERNAL_THREAD_EVENT_STARTED | RUBY_INTERNAL_THREAD_EVENT_READY | RUBY_INTERNAL_THREAD_EVENT_RESUMED, (void *) result);
  rb_yield(Qnil);
  rb_internal_thread_remove_event_hook(hook);
  return result;
}

void init_track_wants_gvl(VALUE lowlevel_toolkit_module) {
  wants_gvl_at_key = rb_internal_thread_specific_key_create();
  rb_define_singleton_method(lowlevel_toolkit_module, "track_wants_gvl", track_wants_gvl, 0);
}
