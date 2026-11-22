d3d12_xs_unix_sources = files(
  'd3d12_xs_vulkan_loader.cpp',
  'd3d12_xs_descriptors.cpp',
  'd3d12_xs_dxil_strip.cpp',
  'd3d12_xs_presentx.cpp',
  'd3d12_xs_core.cpp',
  'd3d12_xs_unix_dispatch.cpp',
)

if is_windows_pe
  d3d12_xs_dll = shared_module('d3d12_xs',
    'd3d12_xs_pe.c',
    vs_module_defs: 'd3d12_xs.def',
    name_prefix: '',
    name_suffix: 'dll',
    include_directories: [scarlett_inc, include_directories('.')],
    c_args: ['-ffreestanding', '-Wall', '-Wextra', '-Werror'],
    install: true,
    install_dir: scarlett_install_dir,
  )
else
  d3d12_xs_threads_dep = dependency('threads')
  d3d12_xs_dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)

  d3d12_xs_unix_so = shared_library('d3d12_xs_unix',
    d3d12_xs_unix_sources,
    name_prefix: '',
    include_directories: [scarlett_inc, include_directories('.')],
    cpp_args: ['-Wall', '-Wextra', '-Werror', '-fvisibility=hidden'],
    link_with: [xmem_unix_so],
    dependencies: [d3d12_xs_threads_dep, d3d12_xs_dl_dep],
    install: true,
    install_dir: scarlett_install_dir,
  )

  d3d12_xs_unix_core_lib = static_library('scarlett_d3d12_xs_unix_core',
    d3d12_xs_unix_sources,
    include_directories: [scarlett_inc, include_directories('.')],
    cpp_args: ['-Wall', '-Wextra', '-Werror', '-DSCARLETT_D3D12XS_STATIC_HOST_LINK=1'],
    link_with: [xmem_unix_so],
    dependencies: [d3d12_xs_threads_dep, d3d12_xs_dl_dep],
    install: false,
  )

  d3d12_xs_pe_host_lib = static_library('scarlett_d3d12_xs_pe_host',
    'd3d12_xs_pe.c',
    include_directories: [scarlett_inc, include_directories('.')],
    c_args: ['-Wall', '-Wextra', '-Werror'],
    link_with: [d3d12_xs_unix_core_lib],
    install: false,
  )

  if get_option('build_tests')
    test_d3d12_xs_exe = executable('test_d3d12_xs',
      'tests/test_d3d12_xs.cpp',
      include_directories: [scarlett_inc, include_directories('.')],
      cpp_args: ['-Wall', '-Wextra', '-Werror'],
      link_with: [d3d12_xs_pe_host_lib, d3d12_xs_unix_core_lib, xmem_pe_host_lib, xmem_unix_so],
      dependencies: [d3d12_xs_threads_dep, d3d12_xs_dl_dep],
      install: false,
    )

    test('d3d12_xs_unit_tests', test_d3d12_xs_exe)
  endif
endif
