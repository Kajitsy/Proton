test_phase0_exe = executable('test_phase0_scaffold',
  'test_phase0_scaffold.cpp',
  include_directories: [scarlett_inc],
  cpp_args: [
    '-DSCARLETT_REPO_ROOT="' + (meson.project_source_root() / '..') + '"',
    '-DSCARLETT_SOURCE_ROOT="' + meson.project_source_root() + '"',
  ],
  link_with: [
    scarlett_xdl_lib,
    scarlett_loader_lib,
    scarlett_xmem_lib,
    scarlett_dstorage_xs_lib,
    scarlett_xg_xs_lib,
    scarlett_xgameruntime_lib,
    scarlett_audio_media_lib,
  ],
  install: false,
)

test('phase0_cpp_scaffold_smoke', test_phase0_exe)

arena_opus_dep = dependency('opus', required: true)
arena_deflate_dep = dependency('libdeflate', required: true)
arena_threads_dep = dependency('threads', required: true)
arena_dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)

scarlett_arena_linux_exe = executable('scarlett_arena_linux',
  [
    'arena/main.cpp',
    'arena/scene1_uma.cpp',
    'arena/scene2_rdna2.cpp',
    'arena/scene3_dstorage.cpp',
    'arena/scene4_dx12u_aaa.cpp',
    'arena/scene5_presentx_pacing.cpp',
    'arena/scene6_runtime_audio_vfs.cpp',
  ],
  include_directories: [
    scarlett_inc,
    include_directories('..', '../xdl/include', 'arena'),
  ],
  cpp_args: [
    '-O2', '-Wall', '-Wextra', '-Werror',
    '-DSCARLETT_ARENA_PACKAGE_DIR="' + (meson.current_source_dir() / 'arena/package') + '"',
    '-DSCARLETT_TEST_BCPACK_PLUGIN_PATH="' + test_bcpack_plugin_so.full_path() + '"',
    '-DSCARLETT_XGR_UNIX_SO_PATH="' + xgameruntime_unix_so.full_path() + '"',
  ],
  link_args: ['-Wl,--version-script=' + (meson.current_source_dir() / 'arena/arena.map')],
  link_with: [
    scarlett_xdl_runtime_lib,
    loader_pe_bridge_lib,
    xmem_pe_host_lib,
    rdna2_lifter_lib,
    d3d12_xs_pe_host_lib,
    d3d12_xs_unix_core_lib,
    dstorage_xs_pe_host_lib,
    dstorage_xs_unix_core_lib,
    xmem_unix_so,
    xg_pe_host_lib,
    xg_xs_unix_core,
    xgameruntime_pe_host_lib,
    audio_pe_bridge_lib,
    loader_unix_so,
    audio_scarlett_unix_so,
  ],
  dependencies: [arena_opus_dep, arena_deflate_dep, arena_threads_dep, arena_dl_dep],
  install: false,
)

test('scarlett_arena_anaconda', scarlett_arena_linux_exe,
  args: ['--model=anaconda'],
  env: ['PROTON_XBOX_MODEL=anaconda'],
  depends: [d3d12_xs_unix_so, dstorage_xs_unix_so, xgameruntime_unix_so, test_bcpack_plugin_so],
)

test('scarlett_arena_lockhart', scarlett_arena_linux_exe,
  args: ['--model=lockhart'],
  env: ['PROTON_XBOX_MODEL=lockhart'],
  depends: [d3d12_xs_unix_so, dstorage_xs_unix_so, xgameruntime_unix_so, test_bcpack_plugin_so],
)
