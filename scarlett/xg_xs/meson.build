# Phase 0 smoke-test compatibility target
scarlett_xg_xs_lib = static_library('scarlett_xg_xs_scaffold',
  'xg_xs_scaffold.cpp',
  include_directories: [scarlett_inc],
  install: false,
)

scarlett_xg_xs_unix_sources = files(
  'xg_xs_tiling.cpp',
  'xbox_bvh_builder.cpp',
  'xg_sfs_coalescer.cpp',
  'pixevt_backend.cpp',
  'xg_xs_unix_dispatch.cpp',
)

if is_windows_pe
  xg_pe_shims = [
    ['xg_xs', 'xg_xs_pe.c', 'xg_xs.def'],
    ['XboxBvhBuilder', 'xbox_bvh_builder_pe.c', 'XboxBvhBuilder.def'],
    ['pixevt', 'pixevt_pe.c', 'pixevt.def'],
  ]
  foreach shim : xg_pe_shims
    shared_library(shim[0],
      shim[1],
      name_prefix: '',
      vs_module_defs: shim[2],
      include_directories: [scarlett_inc, include_directories('.')],
      c_args: ['-ffreestanding', '-Wall', '-Wextra', '-Werror'],
      install: true,
      install_dir: scarlett_install_dir,
    )
  endforeach
else
  xg_threads_dep = dependency('threads', required: true)
  xg_dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)

  xg_xs_unix_core = static_library('scarlett_xg_xs_unix_core',
    scarlett_xg_xs_unix_sources,
    include_directories: [scarlett_inc, include_directories('.')],
    cpp_args: ['-Wall', '-Wextra', '-Werror'],
    dependencies: [xg_threads_dep, xg_dl_dep],
    install: false,
  )

  xg_xs_unix_so = shared_library('xg_xs_unix',
    scarlett_xg_xs_unix_sources,
    name_prefix: '',
    include_directories: [scarlett_inc, include_directories('.')],
    cpp_args: ['-Wall', '-Wextra', '-Werror'],
    dependencies: [xg_threads_dep, xg_dl_dep],
    install: true,
    install_dir: scarlett_install_dir,
  )

  if get_option('build_tests')
    xg_pe_host_lib = static_library('scarlett_xg_xs_pe_host',
      [
        'xg_xs_pe.c',
        'xbox_bvh_builder_pe.c',
        'pixevt_pe.c',
      ],
      include_directories: [scarlett_inc, include_directories('.')],
      c_args: ['-Wall', '-Wextra', '-Werror'],
      link_with: [xg_xs_unix_core],
      install: false,
    )

    test_xg_bvh_pix_exe = executable('test_xg_bvh_pix',
      'tests/test_xg_bvh_pix.cpp',
      include_directories: [scarlett_inc, include_directories('.')],
      cpp_args: ['-Wall', '-Wextra', '-Werror'],
      link_with: [xg_pe_host_lib, xg_xs_unix_core],
      dependencies: [xg_threads_dep],
      install: false,
    )

    test('xg_bvh_pix_unit_tests', test_xg_bvh_pix_exe)
  endif
endif
