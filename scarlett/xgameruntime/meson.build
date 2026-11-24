# Phase 0 smoke-test compatibility target
scarlett_xgameruntime_lib = static_library('scarlett_xgameruntime_scaffold',
  'xgameruntime_scaffold.cpp',
  include_directories: [scarlett_inc],
  install: false,
)

if is_windows_pe
  xgameruntime_dll = shared_module('xgameruntime',
    ['xgameruntime_pe.c', scarlett_pe_unixlib_src],
    vs_module_defs: 'xgameruntime.def',
    name_prefix: '',
    name_suffix: 'dll',
    include_directories: [scarlett_inc, include_directories('.')],
    c_args: ['-Wall', '-Wextra', '-Werror'],
    install: true,
    install_dir: scarlett_install_dir,
  )

  libhttpclient_dll = shared_module('libHttpClient',
    ['libHttpClient_pe.c', scarlett_pe_unixlib_src],
    vs_module_defs: 'libHttpClient.def',
    name_prefix: '',
    name_suffix: 'dll',
    include_directories: [scarlett_inc, include_directories('.')],
    c_args: ['-Wall', '-Wextra', '-Werror'],
    install: true,
    install_dir: scarlett_install_dir,
  )

  xcurl_dll = shared_module('XCurl',
    ['XCurl_pe.c', scarlett_pe_unixlib_src],
    vs_module_defs: 'XCurl.def',
    name_prefix: '',
    name_suffix: 'dll',
    include_directories: [scarlett_inc, include_directories('.')],
    c_args: ['-Wall', '-Wextra', '-Werror'],
    install: true,
    install_dir: scarlett_install_dir,
  )

  xcrdapi_dll = shared_module('XCRDAPI',
    ['xcrdapi_pe.c', scarlett_pe_unixlib_src],
    vs_module_defs: 'XCRDAPI.def',
    name_prefix: '',
    name_suffix: 'dll',
    include_directories: [scarlett_inc, include_directories('.')],
    c_args: ['-Wall', '-Wextra', '-Werror'],
    install: true,
    install_dir: scarlett_install_dir,
  )
else
  xgr_threads_dep = dependency('threads')
  xgr_dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)

  xgameruntime_unix_so = shared_library('xgameruntime_unix',
    [
      'xgr_task_queue.cpp',
      'xgr_system_platform.cpp',
      'xgr_gamesave.cpp',
      'xgr_network_services.cpp',
      'xgameruntime_unix_dispatch.cpp',
    ],
    name_prefix: '',
    include_directories: [scarlett_inc, include_directories('.')],
    cpp_args: ['-Wall', '-Wextra', '-Werror', '-fvisibility=hidden'],
    dependencies: [xgr_threads_dep, xgr_dl_dep],
    install: true,
    install_dir: scarlett_install_dir,
  )

  xgameruntime_pe_host_lib = static_library('scarlett_xgameruntime_pe_host',
    [
      'xgameruntime_pe.c',
      'libHttpClient_pe.c',
      'XCurl_pe.c',
      'xcrdapi_pe.c',
    ],
    include_directories: [scarlett_inc, include_directories('.')],
    c_args: ['-Wall', '-Wextra', '-Werror'],
    link_with: [xgameruntime_unix_so],
    install: false,
  )

  if get_option('build_tests')
    test_xgameruntime_exe = executable('test_xgameruntime',
      'tests/test_xgameruntime.cpp',
      include_directories: [scarlett_inc, include_directories('.'), include_directories('../xdl/include')],
      cpp_args: ['-Wall', '-Wextra', '-Werror'],
      link_with: [xgameruntime_pe_host_lib, xgameruntime_unix_so],
      dependencies: [xgr_threads_dep],
      install: false,
    )

    test('xgameruntime_unit_tests', test_xgameruntime_exe)
  endif
endif
