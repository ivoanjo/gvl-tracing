#include <ruby/ruby.h>
#include <ruby/thread.h>
#include <ruby/debug.h>
#include <time.h>

int ruby_thread_has_gvl_p(void);

#define MAX_STACK_DEPTH 1000
static int release_gvl_at_key;

static uint64_t get_monotonic_time_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}

static void on_thread_event(rb_event_flag_t event_id, const rb_internal_thread_event_data_t *event_data, void *data) {
  VALUE thread = event_data->thread;

  if (event_id == RUBY_INTERNAL_THREAD_EVENT_SUSPENDED && !ruby_thread_has_gvl_p()) {
    rb_internal_thread_specific_set(thread, release_gvl_at_key, (void *) get_monotonic_time_ns());
  } else if (event_id == RUBY_INTERNAL_THREAD_EVENT_RESUMED) {
    uint64_t release_gvl_at = (uint64_t) rb_internal_thread_specific_get(thread, release_gvl_at_key);
    if (release_gvl_at == 0) return;
    rb_internal_thread_specific_set(thread, release_gvl_at_key, 0);

    VALUE result = (VALUE) data;
    int lines[MAX_STACK_DEPTH];
    VALUE locations[MAX_STACK_DEPTH];
    int frame_count = rb_profile_frames(0, MAX_STACK_DEPTH, locations, lines);
    VALUE frames = rb_ary_new();
    for (int i = 0; i < frame_count; i++) {
      rb_ary_push(frames, rb_profile_frame_path(locations[i]));
      VALUE name = rb_profile_frame_base_label(locations[i]);
      if (name == Qnil) name = rb_profile_frame_method_name(locations[i]);
      rb_ary_push(frames, name);
      rb_ary_push(frames, INT2NUM(lines[i]));
    }

    VALUE stats = rb_hash_aref(result, frames);
    if (stats == Qnil) {
      stats = rb_ary_new_from_args(2, INT2FIX(0), INT2FIX(0));
      rb_hash_aset(result, frames, stats);
    }

    uint64_t time_spent = get_monotonic_time_ns() - release_gvl_at;
    rb_ary_store(stats, 0, ULL2NUM(NUM2ULL(rb_ary_entry(stats, 0)) + time_spent));
    rb_ary_store(stats, 1, ULL2NUM(NUM2ULL(rb_ary_entry(stats, 1)) + 1));
  }
}

static VALUE release_gvl_profiler(RB_UNUSED_VAR(VALUE _)) {
  VALUE result = rb_hash_new();
  rb_internal_thread_event_hook_t *hook = rb_internal_thread_add_event_hook(
    on_thread_event,
    RUBY_INTERNAL_THREAD_EVENT_SUSPENDED | RUBY_INTERNAL_THREAD_EVENT_RESUMED,
    (void *) result
  );
  rb_yield(Qnil);
  rb_internal_thread_remove_event_hook(hook);
  return result;
}

void init_release_gvl_profiler(VALUE lowlevel_toolkit_module) {
  release_gvl_at_key = rb_internal_thread_specific_key_create();
  rb_define_singleton_method(lowlevel_toolkit_module, "release_gvl_profiler", release_gvl_profiler, 0);
}
