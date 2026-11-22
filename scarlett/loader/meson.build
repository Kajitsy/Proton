scarlett_loader_lib = static_library('scarlett_loader_scaffold',
  'loader_scaffold.cpp',
  include_directories: [scarlett_inc],
  install: false,
)

scarlett_loader_unix_sources = files(
  'game_config.cpp',
  'xvd_ntfs.cpp',
  'vfs_cow.cpp',
  'loader_unix.cpp',
)

if is_windows_pe
  xgameplatform_pe_dll = shared_library('xgameplatform',
    ['xgameplatform_pe.c', scarlett_pe_unixlib_src],
    name_prefix: '',
    vs_module_defs: 'xgameplatform.def',
    include_directories: [scarlett_inc, include_directories('.')],
    c_args: ['-ffreestanding'],
    install: true,
    install_dir: scarlett_install_dir,
  )

  psm_appnotify_pe_dll = shared_library('api-ms-win-core-psm-appnotify-l1-1-0',
    ['psm_appnotify_pe.c', scarlett_pe_unixlib_src],
    name_prefix: '',
    vs_module_defs: 'api-ms-win-core-psm-appnotify-l1-1-0.def',
    include_directories: [scarlett_inc, include_directories('.')],
    c_args: ['-ffreestanding'],
    install: true,
    install_dir: scarlett_install_dir,
  )
else
  loader_unix_so = shared_library('loader_unix',
    scarlett_loader_unix_sources,
    name_prefix: '',
    include_directories: [scarlett_inc, include_directories('.')],
    install: true,
    install_dir: scarlett_install_dir,
  )

  if get_option('build_tests')
    loader_pe_bridge_lib = static_library('scarlett_loader_pe_bridge',
      'xgameplatform_pe.c',
      'psm_appnotify_pe.c',
      include_directories: [scarlett_inc, include_directories('.')],
      install: false,
    )

    test_loader_vfs_exe = executable('test_loader_vfs',
      'tests/test_loader_vfs.cpp',
      include_directories: [scarlett_inc, include_directories('.')],
      link_with: [loader_pe_bridge_lib, loader_unix_so],
      install: false,
    )

    test('loader_vfs_unit_tests', test_loader_vfs_exe)
  endif
endif
