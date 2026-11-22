# Phase 0 smoke-test compatibility target
scarlett_xmem_lib = static_library('scarlett_xmem_scaffold',
  'xmem_scaffold.cpp',
  include_directories: [scarlett_inc],
  install: false,
)

if is_windows_pe
  xmem_dll = shared_module('xmem',
    ['xmem_pe.c', scarlett_pe_unixlib_src],
    vs_module_defs: 'xmem.def',
    name_prefix: '',
    name_suffix: 'dll',
    include_directories: [scarlett_inc, include_directories('.')],
    c_args: ['-Wall', '-Wextra', '-Werror'],
    install: true,
    install_dir: scarlett_install_dir,
  )

  kernelx_dll = shared_module('kernelx',
    ['kernelx_pe.c', scarlett_pe_unixlib_src],
    vs_module_defs: 'kernelx.def',
    name_prefix: '',
    name_suffix: 'dll',
    include_directories: [scarlett_inc, include_directories('.')],
    c_args: ['-Wall', '-Wextra', '-Werror'],
    install: true,
    install_dir: scarlett_install_dir,
  )
else
  xmem_threads_dep = dependency('threads')
  xmem_dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)

  xmem_unix_so = shared_library('xmem_unix',
    [
      'xmem_phys_virt.cpp',
      'xmem_uma_bridge.cpp',
      'xmem_thread_sync.cpp',
      'xmem_unix_dispatch.cpp',
    ],
    name_prefix: '',
    include_directories: [scarlett_inc, include_directories('.')],
    cpp_args: ['-Wall', '-Wextra', '-Werror', '-fvisibility=hidden'],
    dependencies: [xmem_threads_dep, xmem_dl_dep],
    install: true,
    install_dir: scarlett_install_dir,
  )

  xmem_pe_host_lib = static_library('scarlett_xmem_pe_host',
    [
      'xmem_pe.c',
      'kernelx_pe.c',
    ],
    include_directories: [scarlett_inc, include_directories('.')],
    c_args: ['-Wall', '-Wextra', '-Werror'],
    link_with: [xmem_unix_so],
    install: false,
  )

  if get_option('build_tests')
    test_xmem_uma_exe = executable('test_xmem_uma',
      'tests/test_xmem_uma.cpp',
      include_directories: [scarlett_inc, include_directories('.')],
      cpp_args: ['-Wall', '-Wextra', '-Werror'],
      link_with: [xmem_pe_host_lib, xmem_unix_so],
      dependencies: [xmem_threads_dep],
      install: false,
    )

    test('xmem_uma_unit_tests', test_xmem_uma_exe)
  endif
endif
