#include <ruby/ruby.h>
#include <ruby/debug.h>

static uint64_t get_monotonic_time_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}

static void on_gc_event(VALUE tpval, RB_UNUSED_VAR(void *_)) {
  static uint64_t gc_start_time = 0;
  rb_event_flag_t event = rb_tracearg_event_flag(rb_tracearg_from_tracepoint(tpval));

  if (event == RUBY_INTERNAL_EVENT_GC_ENTER) {
    gc_start_time = get_monotonic_time_ns();
  } else if (event == RUBY_INTERNAL_EVENT_GC_EXIT) {
    fprintf(stdout, "GC worked for %.2f ms\n", ((get_monotonic_time_ns() - gc_start_time) / 1000000.0));
  }
}

static VALUE print_gc_timing(RB_UNUSED_VAR(VALUE _)) {
  VALUE tp = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_GC_ENTER | RUBY_INTERNAL_EVENT_GC_EXIT, on_gc_event, NULL);
  rb_tracepoint_enable(tp); rb_yield(Qnil); rb_tracepoint_disable(tp);
  return Qnil;
}

void init_print_gc_timing(VALUE lowlevel_toolkit_module) {
  rb_define_singleton_method(lowlevel_toolkit_module, "print_gc_timing", print_gc_timing, 0);
}
