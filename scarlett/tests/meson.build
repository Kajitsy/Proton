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
