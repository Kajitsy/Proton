uwphost_inc = include_directories('.', '../include')

uwphost_sources = files(
  'uwphost_log.c',
  'uwphost_winstring.c',
  'uwphost_manifest.c',
  'uwphost_events.c',
  'uwphost_corewindow.c',
  'uwphost_coreapp.c',
  'uwphost_main.c',
)

uwphost_def = files('UWPHOST.def')[0]

uwphost_pe_libs = [
  'kernel32',
  'user32',
  'gdi32',
  'ole32',
  'advapi32',
]

if not is_windows_pe
  uwphost_threads_dep = dependency('threads')

  uwphost_host_lib = static_library('scarlett_uwphost_host',
    uwphost_sources,
    include_directories: [scarlett_inc, uwphost_inc],
    c_args: ['-Wall', '-Wextra', '-Werror'],
    dependencies: [uwphost_threads_dep],
    install: false,
  )

  if get_option('build_tests')
    test_uwphost_exe = executable('test_uwphost',
      'tests/test_uwphost.c',
      include_directories: [scarlett_inc, uwphost_inc],
      c_args: ['-Wall', '-Wextra', '-Werror'],
      link_with: [uwphost_host_lib],
      dependencies: [uwphost_threads_dep],
      install: false,
    )

    test('uwphost_unit_tests', test_uwphost_exe)
  endif
endif
