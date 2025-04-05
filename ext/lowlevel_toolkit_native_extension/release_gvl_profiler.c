#include <ruby/ruby.h>
#include <ruby/thread.h>

static VALUE release_gvl_profiler(VALUE self) {
  rb_yield(Qnil);
  return Qnil;
}

void init_release_gvl_profiler(VALUE lowlevel_toolkit_module) {
  rb_define_singleton_method(lowlevel_toolkit_module, "release_gvl_profiler", release_gvl_profiler, 0);
}
