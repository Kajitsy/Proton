# Phase 0 smoke-test compatibility target
scarlett_dstorage_xs_lib = static_library('scarlett_dstorage_xs_scaffold',
  'dstorage_xs_scaffold.cpp',
  include_directories: [scarlett_inc],
  install: false,
)

dstorage_xs_unix_sources = files(
  'dstorage_xs_shaders.cpp',
  'dstorage_xs_io.cpp',
  'dstorage_xs_decomp.cpp',
  'dstorage_xs_core.cpp',
  'dstorage_xs_unix_dispatch.cpp',
)

if is_windows_pe
  dstorage_xs_dll = shared_module('dstorage_xs',
    ['dstorage_xs_pe.c', scarlett_pe_unixlib_src],
    vs_module_defs: 'dstorage_xs.def',
    name_prefix: '',
    name_suffix: 'dll',
    include_directories: [scarlett_inc, include_directories('.')],
    c_args: ['-ffreestanding', '-O2', '-Wall', '-Wextra', '-Werror'],
    install: true,
    install_dir: scarlett_install_dir,
  )
else
  dstorage_threads_dep = dependency('threads')
  dstorage_dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)
  dstorage_uring_dep = dependency('liburing', required: false)
  dstorage_deflate_dep = dependency('libdeflate')
  dstorage_zng_dep = dependency('zlib-ng', required: false)

  dstorage_unix_cpp_args = ['-O2', '-Wall', '-Wextra', '-Werror', '-fvisibility=hidden']
  dstorage_unix_deps = [dstorage_threads_dep, dstorage_dl_dep, dstorage_deflate_dep]
  if dstorage_uring_dep.found()
    dstorage_unix_deps += [dstorage_uring_dep]
  else
    dstorage_unix_cpp_args += ['-DSCARLETT_NO_LIBURING=1']
  endif
  if dstorage_zng_dep.found()
    dstorage_unix_deps += [dstorage_zng_dep]
  else
    dstorage_unix_deps += [dependency('zlib')]
    dstorage_unix_cpp_args += ['-DSCARLETT_USE_SYSTEM_ZLIB=1']
  endif

  dstorage_xs_unix_so = shared_library('dstorage_xs_unix',
    dstorage_xs_unix_sources,
    name_prefix: '',
    include_directories: [scarlett_inc, include_directories('.')],
    cpp_args: dstorage_unix_cpp_args,
    link_with: [xmem_unix_so],
    dependencies: dstorage_unix_deps,
    install: true,
    install_dir: scarlett_install_dir,
  )

  dstorage_xs_unix_core_lib = static_library('scarlett_dstorage_xs_unix_core',
    dstorage_xs_unix_sources,
    include_directories: [scarlett_inc, include_directories('.')],
    cpp_args: dstorage_unix_cpp_args + ['-DSCARLETT_DSTORAGEXS_STATIC_HOST_LINK=1'],
    link_with: [xmem_unix_so],
    dependencies: dstorage_unix_deps,
    install: false,
  )

  dstorage_xs_pe_host_lib = static_library('scarlett_dstorage_xs_pe_host',
    'dstorage_xs_pe.c',
    include_directories: [scarlett_inc, include_directories('.')],
    c_args: ['-O2', '-Wall', '-Wextra', '-Werror'],
    link_with: [dstorage_xs_unix_core_lib],
    install: false,
  )

  if get_option('build_tests')
    test_bcpack_plugin_so = shared_library('test_bcpack_plugin',
      'tests/test_bcpack_plugin.cpp',
      name_prefix: '',
      include_directories: [scarlett_inc, include_directories('.')],
      cpp_args: ['-O2', '-Wall', '-Wextra', '-Werror', '-fvisibility=hidden'],
      install: false,
    )

    test_dstorage_xs_exe = executable('test_dstorage_xs',
      'tests/test_dstorage_xs.cpp',
      include_directories: [scarlett_inc, include_directories('.')],
      cpp_args: [
        '-O2', '-Wall', '-Wextra', '-Werror',
        '-DSCARLETT_DSTORAGEXS_UNIX_SO_PATH="' + dstorage_xs_unix_so.full_path() + '"',
        '-DSCARLETT_TEST_BCPACK_PLUGIN_PATH="' + test_bcpack_plugin_so.full_path() + '"',
      ],
      link_with: [dstorage_xs_pe_host_lib, dstorage_xs_unix_core_lib, xmem_pe_host_lib, xmem_unix_so],
      dependencies: [dstorage_threads_dep, dstorage_dl_dep, dstorage_deflate_dep],
      install: false,
    )

    test('dstorage_xs_unit_tests', test_dstorage_xs_exe,
      depends: [dstorage_xs_unix_so, test_bcpack_plugin_so],
    )
  endif
endif
