xdl_inc = include_directories('.', 'include', 'src')

# Phase 0 scaffold library (preserved for phase0_cpp_scaffold_smoke compatibility)
scarlett_xdl_lib = static_library('scarlett_xdl_scaffold',
  'xdl_scaffold.cpp',
  include_directories: [scarlett_inc],
  install: false,
)

# Host compiler executable (always native to build machine)
scarlett_xdl_exe = executable('scarlett-xdl',
  [
    'src/lexer.cpp',
    'src/parser.cpp',
    'src/layout.cpp',
    'src/codegen_cpp.cpp',
    'src/codegen_def.cpp',
    'src/main.cpp',
  ],
  include_directories: [scarlett_inc, xdl_inc],
  native: true,
  install: false,
)

if not is_windows_pe
  cc = meson.get_compiler('c')
  dl_dep = cc.find_library('dl', required: false)

  scarlett_xdl_runtime_lib = static_library('scarlett_xdl_runtime',
    [
      'src/stub_thunk_runtime.cpp',
      'src/stub_trampoline_x86_64.S',
    ],
    include_directories: [scarlett_inc, xdl_inc],
    dependencies: [dl_dep],
    install: false,
  )

  xdl_schema_specs = [
    ['d3d12_xs', 'd3d12_xs'],
    ['dstorage_xs', 'dstorage_xs'],
    ['xg_xs', 'xg_xs'],
    ['xgameruntime', 'xgameruntime'],
    ['xaudio2_9_xs', 'xaudio2_9'],
    ['gameinput_xs', 'GameInput'],
  ]

  xdl_gen_targets = []
  foreach spec : xdl_schema_specs
    mod_name = spec[0]
    def_stem = spec[1]
    xdl_gen_targets += custom_target('xdl_gen_' + mod_name,
      input: 'schemas' / (mod_name + '.xdl'),
      output: [mod_name + '_xdl.hpp', def_stem + '.def'],
      command: [
        scarlett_xdl_exe,
        '@INPUT@',
        '--emit-header', '@OUTPUT0@',
        '--emit-def', '@OUTPUT1@',
        '--target-abi', '10.0.26200.0',
        '--console-model', 'anaconda',
      ],
      build_by_default: true,
    )
  endforeach

  if get_option('build_tests')
    xdl_test_defs = [
      '-DSCARLETT_XDL_BIN="' + scarlett_xdl_exe.full_path() + '"',
      '-DSCARLETT_XDL_SCHEMAS_DIR="' + (meson.current_source_dir() / 'schemas') + '"',
      '-DSCARLETT_XDL_GEN_DIR="' + meson.current_build_dir() + '"',
      '-DSCARLETT_XDL_TMP_DIR="' + (meson.current_build_dir() / 'tmp_tests') + '"',
    ]

    test_xdl_parser_exe = executable('test_xdl_parser_codegen',
      ['tests/test_xdl_parser_codegen.cpp', xdl_gen_targets],
      include_directories: [scarlett_inc, xdl_inc],
      cpp_args: xdl_test_defs,
      link_with: [scarlett_xdl_runtime_lib],
      install: false,
    )
    test('xdl_parser_codegen', test_xdl_parser_exe)

    test_xdl_schemas_exe = executable('test_xdl_schemas_compile',
      ['tests/test_xdl_schemas_compile.cpp', xdl_gen_targets],
      include_directories: [scarlett_inc, xdl_inc],
      cpp_args: xdl_test_defs,
      link_with: [scarlett_xdl_runtime_lib],
      install: false,
    )
    test('xdl_schemas_compile', test_xdl_schemas_exe)

    test_xdl_stub_exe = executable('test_xdl_stub_thunk',
      ['tests/test_xdl_stub_thunk.cpp', xdl_gen_targets],
      include_directories: [scarlett_inc, xdl_inc],
      cpp_args: xdl_test_defs,
      link_with: [scarlett_xdl_runtime_lib],
      export_dynamic: true,
      install: false,
    )
    test('xdl_stub_thunk', test_xdl_stub_exe)
  endif
endif
