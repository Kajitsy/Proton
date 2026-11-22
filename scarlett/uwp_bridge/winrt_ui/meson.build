# Proton-Scarlett — WinRT UI subsystem for UWPHOST.dll

winrt_ui_inc = include_directories('.', '../include')

winrt_ui_sources = files(
  'winrt_ui_common.c',
  'winrt_display_info.c',
  'winrt_app_view.c',
  'winrt_sys_nav.c',
  'winrt_core_cursor_mouse.c',
  'winrt_display_request_hdmi.c',
  'winrt_api_info.c',
)

winrt_ui_pe_libs = ['kernel32', 'user32', 'gdi32', 'ole32']

if get_option('build_tests')
  winrt_ui_test_exe = executable(
    'test_winrt_ui',
    sources: winrt_ui_sources + files('tests/test_winrt_ui.c', 'tests/test_winrt_ui_cxx.cpp'),
    include_directories: [winrt_ui_inc, scarlett_inc],
    c_args: ['-std=c11', '-Wall', '-Wextra', '-Werror'],
    cpp_args: ['-std=c++20', '-Wall', '-Wextra', '-Werror'],
    link_args: is_windows_pe ? ['-lkernel32', '-luser32', '-lgdi32', '-lole32'] : ['-lm'],
  )
  test('winrt_ui_unit', winrt_ui_test_exe)
endif
