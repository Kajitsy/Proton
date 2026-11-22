rdna2_lifter_sources = files(
  'rdna2_containers.cpp',
  'rdna2_decoder.cpp',
  'rdna2_ir.cpp',
  'rdna2_spirv_emitter.cpp',
  'rdna2_microprobes.cpp',
  'rdna2_spv_eval.cpp',
  'rdna2_lifter.cpp',
)

if not is_windows_pe
  rdna2_threads_dep = dependency('threads')
  rdna2_dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)

  rdna2_lifter_lib = static_library('scarlett_rdna2_lifter',
    rdna2_lifter_sources,
    include_directories: [scarlett_inc, include_directories('.')],
    cpp_args: ['-Wall', '-Wextra', '-Werror'],
    dependencies: [rdna2_threads_dep, rdna2_dl_dep],
    install: false,
  )

  rdna2_lifter_unix_so = shared_library('rdna2_lifter_unix',
    rdna2_lifter_sources,
    name_prefix: '',
    include_directories: [scarlett_inc, include_directories('.')],
    cpp_args: ['-Wall', '-Wextra', '-Werror'],
    dependencies: [rdna2_threads_dep, rdna2_dl_dep],
    install: true,
    install_dir: scarlett_install_dir,
  )

  if get_option('build_tests')
    test_rdna2_lifter_exe = executable('test_rdna2_lifter',
      'tests/test_rdna2_lifter.cpp',
      include_directories: [scarlett_inc, include_directories('.')],
      cpp_args: ['-Wall', '-Wextra', '-Werror'],
      link_with: [rdna2_lifter_lib],
      dependencies: [rdna2_threads_dep, rdna2_dl_dep],
      install: false,
    )
    test('rdna2_lifter_unit_tests', test_rdna2_lifter_exe)

    test_rdna2_corpus_exe = executable('test_rdna2_corpus',
      'tests/test_rdna2_corpus.cpp',
      include_directories: [scarlett_inc, include_directories('.')],
      cpp_args: ['-Wall', '-Wextra', '-Werror'],
      link_with: [rdna2_lifter_lib],
      dependencies: [rdna2_threads_dep, rdna2_dl_dep],
      install: false,
    )
    test('rdna2_lifter_corpus_2048_exit_gate', test_rdna2_corpus_exe)
  endif
endif
