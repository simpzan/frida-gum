trace_src = ['trace.cpp', 'build-id.c']
libtrace = shared_library('trace', trace_src,
  override_options: ['cpp_std=c++14'],
  dependencies: [gum_dep, gum_heap_dep, gum_prof_dep],
  install: true,
)
