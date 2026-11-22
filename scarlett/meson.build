project('proton-scarlett', ['c', 'cpp'],
  version: '11.0-100',
  meson_version: '>= 0.60.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++20',
    'warning_level=2',
  ]
)

is_windows_pe = host_machine.system() == 'windows'

raw_libdir = get_option('libdir')
if is_windows_pe
  if raw_libdir.endswith('x86_64-windows')
    scarlett_install_dir = get_option('prefix') / raw_libdir
  else
    scarlett_install_dir = get_option('prefix') / 'lib/wine/scarlett/x86_64-windows'
  endif
else
  if raw_libdir.endswith('x86_64-unix')
    scarlett_install_dir = get_option('prefix') / raw_libdir
  else
    scarlett_install_dir = get_option('prefix') / 'lib/wine/scarlett/x86_64-unix'
  endif
endif

scarlett_inc = include_directories('.', 'include')

if get_option('install_version')
  version_conf = configuration_data()
  version_conf.set('SCARLETT_VERSION', meson.project_version())
  configure_file(
    input: 'version.in',
    output: 'version',
    configuration: version_conf,
    install: true,
    install_dir: get_option('prefix') / 'lib/wine/scarlett',
  )
endif

subdir('xdl')
subdir('loader')
subdir('xmem')
subdir('d3d12_xs')
subdir('dstorage_xs')
subdir('xg_xs')
subdir('xgameruntime')
subdir('audio_media')

if get_option('build_tests') and not is_windows_pe
  subdir('tests')
endif
