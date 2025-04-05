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
static int total_wants_key;

static void on_thread_event(rb_event_flag_t event_id, const rb_internal_thread_event_data_t *event_data, void *data) {
  VALUE thread = event_data->thread;
  if (event_id == RUBY_INTERNAL_THREAD_EVENT_STARTED) {
    rb_ary_push((VALUE) data, thread);
  } else if (event_id == RUBY_INTERNAL_THREAD_EVENT_READY) {
    rb_internal_thread_specific_set(thread, wants_gvl_at_key, (void *) get_monotonic_time_ns());
  } else if (event_id == RUBY_INTERNAL_THREAD_EVENT_RESUMED) {
    uint64_t wants_gvl_at = (uint64_t) rb_internal_thread_specific_get(thread, wants_gvl_at_key);
    if (wants_gvl_at == 0) return;
    uint64_t total_wants = (uint64_t) rb_internal_thread_specific_get(thread, total_wants_key);
    rb_internal_thread_specific_set(
      thread, total_wants_key, (void *) (total_wants + (get_monotonic_time_ns() - wants_gvl_at)));
  }
}

VALUE track_wants_gvl(RB_UNUSED_VAR(VALUE _)) {
  VALUE seen_threads = rb_ary_new();
  rb_internal_thread_event_hook_t *hook =
    rb_internal_thread_add_event_hook(
      on_thread_event, RUBY_INTERNAL_THREAD_EVENT_STARTED | RUBY_INTERNAL_THREAD_EVENT_READY | RUBY_INTERNAL_THREAD_EVENT_RESUMED, (void *) seen_threads);
  rb_yield(Qnil);
  rb_internal_thread_remove_event_hook(hook);
  VALUE result = rb_hash_new();
  for (int i = 0; i < RARRAY_LEN(seen_threads); i++) {
    VALUE thread = rb_ary_entry(seen_threads, i);
    rb_hash_aset(result, thread, ULL2NUM((uint64_t) rb_internal_thread_specific_get(thread, total_wants_key)));
  }
  return result;
}

void init_track_wants_gvl(VALUE lowlevel_toolkit_module) {
  wants_gvl_at_key = rb_internal_thread_specific_key_create();
  total_wants_key = rb_internal_thread_specific_key_create();
  rb_define_singleton_method(lowlevel_toolkit_module, "track_wants_gvl", track_wants_gvl, 0);
}
