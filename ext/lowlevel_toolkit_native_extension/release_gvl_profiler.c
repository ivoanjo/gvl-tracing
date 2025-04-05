#include <ruby/ruby.h>
#include <ruby/thread.h>
#include <ruby/debug.h>
#include <time.h>

int ruby_thread_has_gvl_p(void);

static int release_gvl_at_key;

static uint64_t get_monotonic_time_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}

static void on_thread_event(rb_event_flag_t event_id, const rb_internal_thread_event_data_t *event_data, RB_UNUSED_VAR(void *_)) {
  VALUE thread = event_data->thread;

  if (event_id == RUBY_INTERNAL_THREAD_EVENT_SUSPENDED && !ruby_thread_has_gvl_p()) {
    rb_internal_thread_specific_set(thread, release_gvl_at_key, (void *) get_monotonic_time_ns());
  } else if (event_id == RUBY_INTERNAL_THREAD_EVENT_RESUMED) {
    uint64_t release_gvl_at = (uint64_t) rb_internal_thread_specific_get(thread, release_gvl_at_key);
    if (release_gvl_at == 0) return;
    rb_internal_thread_specific_set(thread, release_gvl_at_key, 0);
  }
}

static VALUE release_gvl_profiler(RB_UNUSED_VAR(VALUE _)) {
  rb_internal_thread_event_hook_t *hook = rb_internal_thread_add_event_hook(
    on_thread_event,
    RUBY_INTERNAL_THREAD_EVENT_SUSPENDED | RUBY_INTERNAL_THREAD_EVENT_RESUMED,
    NULL
  );

  rb_yield(Qnil);
  rb_internal_thread_remove_event_hook(hook);
  return Qnil;
}

void init_release_gvl_profiler(VALUE lowlevel_toolkit_module) {
  release_gvl_at_key = rb_internal_thread_specific_key_create();
  rb_define_singleton_method(lowlevel_toolkit_module, "release_gvl_profiler", release_gvl_profiler, 0);
}
