winrt_sys_sources = files(
  'winrt_sys_common.c',
  'winrt_sys_storage.c',
  'winrt_sys_package.c',
  'winrt_sys_gamepad.c',
  'winrt_sys_system_ui.c',
  'winrt_sys_uwp_extras.c',
)

winrt_sys_inc = include_directories('.')

if not is_windows_pe and get_option('build_tests')
  winrt_sys_threads_dep = dependency('threads')

  test_winrt_sys_exe = executable('test_uwp_winrt_sys',
    [
      winrt_sys_sources,
      'tests/win32_host_shim.c',
      'tests/test_winrt_sys.c',
    ],
    include_directories: [scarlett_inc, winrt_sys_inc, include_directories('../include')],
    c_args: ['-Wall', '-Wextra', '-Werror'],
    dependencies: [winrt_sys_threads_dep],
    install: false,
  )

  test('uwp_bridge_winrt_sys_unit_tests', test_winrt_sys_exe)
endif
