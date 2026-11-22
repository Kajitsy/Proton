crt_app_targets = [
  'VCRUNTIME140_APP',
  'VCRUNTIME140_1_APP',
  'MSVCP140_APP',
  'MSVCP140_1_APP',
  'MSVCP140_2_APP',
  'CONCRT140_APP',
  'vccorlib140_app',
]

if is_windows_pe
  crt_cc = meson.get_compiler('c')
  crt_kernel32_dep = crt_cc.find_library('kernel32', required: true)

  crt_pe_runtime_lib = static_library('scarlett_crt_app_runtime',
    'crt_app_runtime.c',
    include_directories: [scarlett_inc, include_directories('.')],
    c_args: ['-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-builtin'],
    install: false,
  )

  foreach dll_name : crt_app_targets
    shared_module(dll_name,
      dll_name + '.c',
      vs_module_defs: dll_name + '.def',
      name_prefix: '',
      name_suffix: 'dll',
      include_directories: [scarlett_inc, include_directories('.')],
      c_args: ['-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-builtin'],
      link_with: [crt_pe_runtime_lib],
      link_args: ['-nostdlib', '-Wl,-e,DllMain'],
      dependencies: [crt_kernel32_dep],
      install: true,
      install_dir: scarlett_install_dir,
    )
  endforeach
else
  if get_option('build_tests')
    crt_cc = meson.get_compiler('c')
    crt_threads_dep = dependency('threads')
    crt_dl_dep = crt_cc.find_library('dl', required: false)
    crt_m_dep = crt_cc.find_library('m', required: false)

    test_crt_app_runtime_exe = executable('test_crt_app_runtime',
      ['crt_app_runtime.c', 'tests/test_crt_app_runtime.c'],
      include_directories: [scarlett_inc, include_directories('.')],
      c_args: ['-Wall', '-Wextra', '-Werror'],
      dependencies: [crt_threads_dep, crt_dl_dep, crt_m_dep],
      install: false,
    )

    test('uwp_crt_app_runtime_tests', test_crt_app_runtime_exe)

    python3_prog = find_program('python3', required: false)
    if python3_prog.found()
      test('uwp_crt_imports_audit',
        python3_prog,
        args: [files('test_crt_imports_audit.py')],
      )
    endif
  endif
endif
