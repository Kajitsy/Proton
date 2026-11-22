fs = import('fs')

uwp_bridge_inc = include_directories('include')
uwp_python3 = find_program('python3', required: true)

if fs.exists('uwphost/meson.build')
  subdir('uwphost')
endif

if fs.exists('winrt_ui/meson.build')
  subdir('winrt_ui')
endif

if fs.exists('winrt_sys/meson.build')
  subdir('winrt_sys')
endif

if fs.exists('forwarders/sys/meson.build')
  subdir('forwarders/sys')
endif

if fs.exists('forwarders/crt/meson.build')
  subdir('forwarders/crt')
endif

# Resolve uwphost / winrt_ui / winrt_sys source lists gracefully if a subdir did
# not define its *_sources variable via meson.build.
if not is_variable('uwphost_sources')
  uwphost_sources = files()
  if fs.is_dir('uwphost')
    _uwp_res = run_command(uwp_python3, '-c',
      'import os, sys; d=sys.argv[1]; print("\\n".join(sorted("uwphost/"+f for f in os.listdir(d) if os.path.isfile(os.path.join(d,f)) and f.endswith((".c",".cpp")))))',
      meson.current_source_dir() / 'uwphost',
      check: true)
    if _uwp_res.stdout().strip() != ''
      uwphost_sources = files(_uwp_res.stdout().strip().split('\n'))
    endif
  endif
endif

if not is_variable('winrt_ui_sources')
  winrt_ui_sources = files()
  if fs.exists('winrt_ui/sources.txt')
    _ui_list = []
    foreach _line : fs.read('winrt_ui/sources.txt').strip().split('\n')
      _s = _line.strip()
      if _s != '' and not _s.startswith('#')
        if _s.startswith('winrt_ui/')
          _ui_list += _s
        else
          _ui_list += 'winrt_ui/' + _s
        endif
      endif
    endforeach
    if _ui_list.length() > 0
      winrt_ui_sources = files(_ui_list)
    endif
  elif fs.is_dir('winrt_ui')
    _ui_res = run_command(uwp_python3, '-c',
      'import os, sys; d=sys.argv[1]; print("\\n".join(sorted("winrt_ui/"+f for f in os.listdir(d) if os.path.isfile(os.path.join(d,f)) and f.endswith((".c",".cpp")))))',
      meson.current_source_dir() / 'winrt_ui',
      check: true)
    if _ui_res.stdout().strip() != ''
      winrt_ui_sources = files(_ui_res.stdout().strip().split('\n'))
    endif
  endif
endif

if not is_variable('winrt_sys_sources')
  winrt_sys_sources = files()
  if fs.exists('winrt_sys/sources.txt')
    _sys_list = []
    foreach _line : fs.read('winrt_sys/sources.txt').strip().split('\n')
      _s = _line.strip()
      if _s != '' and not _s.startswith('#')
        if _s.startswith('winrt_sys/')
          _sys_list += _s
        else
          _sys_list += 'winrt_sys/' + _s
        endif
      endif
    endforeach
    if _sys_list.length() > 0
      winrt_sys_sources = files(_sys_list)
    endif
  elif fs.is_dir('winrt_sys')
    _sys_res = run_command(uwp_python3, '-c',
      'import os, sys; d=sys.argv[1]; print("\\n".join(sorted("winrt_sys/"+f for f in os.listdir(d) if os.path.isfile(os.path.join(d,f)) and f.endswith((".c",".cpp")))))',
      meson.current_source_dir() / 'winrt_sys',
      check: true)
    if _sys_res.stdout().strip() != ''
      winrt_sys_sources = files(_sys_res.stdout().strip().split('\n'))
    endif
  endif
endif

if is_windows_pe
  uwp_cc = meson.get_compiler('c')
  uwp_k32_dep = uwp_cc.find_library('kernel32', required: true)

  # Build UWPHOST.dll combining uwphost + winrt_ui + winrt_sys
  if not is_variable('uwphost_dll') and fs.exists('uwphost/UWPHOST.def')
    _uwphost_all_sources = [uwphost_sources, winrt_ui_sources, winrt_sys_sources]
    _uwphost_all_incs = [scarlett_inc, uwp_bridge_inc]

    if is_variable('uwphost_inc')
      _uwphost_all_incs += uwphost_inc
    elif fs.is_dir('uwphost')
      _uwphost_all_incs += include_directories('uwphost')
    endif

    if is_variable('winrt_ui_inc')
      _uwphost_all_incs += winrt_ui_inc
    elif fs.is_dir('winrt_ui')
      _uwphost_all_incs += include_directories('winrt_ui')
    endif

    if is_variable('winrt_sys_inc')
      _uwphost_all_incs += winrt_sys_inc
    elif fs.is_dir('winrt_sys')
      _uwphost_all_incs += include_directories('winrt_sys')
    endif

    _uwphost_pe_lib_names = is_variable('uwphost_pe_libs') ? uwphost_pe_libs : ['kernel32', 'user32', 'gdi32', 'ole32', 'advapi32']
    _uwphost_deps = []
    foreach _lib : _uwphost_pe_lib_names
      _dep = uwp_cc.find_library(_lib, required: true)
      if _dep.found()
        _uwphost_deps += _dep
      endif
    endforeach
    foreach _opt_lib : ['combase', 'oleaut32', 'uuid']
      _opt_dep = uwp_cc.find_library(_opt_lib, required: false)
      if _opt_dep.found()
        _uwphost_deps += _opt_dep
      endif
    endforeach

    if is_variable('uwphost_deps')
      _uwphost_deps += uwphost_deps
    endif
    if is_variable('winrt_ui_deps')
      _uwphost_deps += winrt_ui_deps
    endif
    if is_variable('winrt_sys_deps')
      _uwphost_deps += winrt_sys_deps
    endif

    _uwphost_def = is_variable('uwphost_def') ? uwphost_def : 'uwphost/UWPHOST.def'

    uwphost_dll = shared_module('UWPHOST',
      _uwphost_all_sources,
      vs_module_defs: _uwphost_def,
      name_prefix: '',
      name_suffix: 'dll',
      include_directories: _uwphost_all_incs,
      c_args: ['-Wall', '-Wextra'],
      cpp_args: ['-Wall', '-Wextra'],
      dependencies: _uwphost_deps,
      install: true,
      install_dir: scarlett_install_dir,
    )
  endif

  # Build all 14 System *_APP.dll / XINPUTUAP.dll forwarders
  if not is_variable('uwp_sys_forwarders_built') and not is_variable('sys_forwarder_dlls') and fs.is_dir('forwarders/sys')
    _uwp_sys_dlls = [
      'KERNEL32_APP',
      'USER32_APP',
      'GDI32_APP',
      'ADVAPI32_APP',
      'OLE32_APP',
      'COMBASE_APP',
      'RPCRT4_APP',
      'WS2_32_APP',
      'WINHTTP_APP',
      'CRYPT32_APP',
      'BCRYPT_APP',
      'MMDEVAPI_APP',
      'XINPUT1_4_APP',
      'XINPUTUAP',
    ]
    _uwp_sys_inc = include_directories('forwarders/sys')
    foreach _dll : _uwp_sys_dlls
      _def_path = ''
      foreach _cand_def : ['forwarders/sys/' + _dll + '.def', 'forwarders/sys/' + _dll.to_lower() + '.def']
        if _def_path == '' and fs.exists(_cand_def)
          _def_path = _cand_def
        endif
      endforeach

      _src_path = ''
      foreach _cand_src : [
        'forwarders/sys/' + _dll + '.c',
        'forwarders/sys/' + _dll.to_lower() + '.c',
        'forwarders/sys/' + _dll + '_pe.c',
        'forwarders/sys/' + _dll.to_lower() + '_pe.c',
        'forwarders/sys/' + _dll + '_stubs.c',
        'forwarders/sys/' + _dll.to_lower() + '_stubs.c',
        'forwarders/sys/' + _dll + '.cpp',
        'forwarders/sys/' + _dll.to_lower() + '.cpp',
      ]
        if _src_path == '' and fs.exists(_cand_src)
          _src_path = _cand_src
        endif
      endforeach

      if _def_path != '' and _src_path != ''
        shared_module(_dll,
          [_src_path],
          vs_module_defs: _def_path,
          name_prefix: '',
          name_suffix: 'dll',
          include_directories: [scarlett_inc, uwp_bridge_inc, _uwp_sys_inc],
          c_args: ['-Wall', '-Wextra'],
          cpp_args: ['-Wall', '-Wextra'],
          dependencies: [uwp_k32_dep],
          install: true,
          install_dir: scarlett_install_dir,
        )
      endif
    endforeach
  endif

  # Build all 7 MSVC CRT *_APP.dll forwarders
  if not is_variable('uwp_crt_forwarders_built') and not is_variable('crt_app_targets') and fs.is_dir('forwarders/crt')
    _uwp_crt_dlls = [
      'VCRUNTIME140_APP',
      'VCRUNTIME140_1_APP',
      'MSVCP140_APP',
      'MSVCP140_1_APP',
      'MSVCP140_2_APP',
      'CONCRT140_APP',
      'vccorlib140_app',
    ]
    _uwp_crt_inc = include_directories('forwarders/crt')
    _uwp_ole32_dep = uwp_cc.find_library('ole32', required: false)
    _uwp_crt_deps = [uwp_k32_dep]
    if _uwp_ole32_dep.found()
      _uwp_crt_deps += _uwp_ole32_dep
    endif

    foreach _dll : _uwp_crt_dlls
      _def_path = ''
      foreach _cand_def : ['forwarders/crt/' + _dll + '.def', 'forwarders/crt/' + _dll.to_lower() + '.def']
        if _def_path == '' and fs.exists(_cand_def)
          _def_path = _cand_def
        endif
      endforeach

      _src_path = ''
      foreach _cand_src : [
        'forwarders/crt/' + _dll + '.c',
        'forwarders/crt/' + _dll.to_lower() + '.c',
        'forwarders/crt/' + _dll + '_pe.c',
        'forwarders/crt/' + _dll.to_lower() + '_pe.c',
        'forwarders/crt/' + _dll + '_stubs.c',
        'forwarders/crt/' + _dll.to_lower() + '_stubs.c',
        'forwarders/crt/' + _dll + '.cpp',
        'forwarders/crt/' + _dll.to_lower() + '.cpp',
      ]
        if _src_path == '' and fs.exists(_cand_src)
          _src_path = _cand_src
        endif
      endforeach

      if _def_path != '' and _src_path != ''
        shared_module(_dll,
          [_src_path],
          vs_module_defs: _def_path,
          name_prefix: '',
          name_suffix: 'dll',
          include_directories: [scarlett_inc, uwp_bridge_inc, _uwp_crt_inc],
          c_args: ['-Wall', '-Wextra'],
          cpp_args: ['-Wall', '-Wextra'],
          dependencies: _uwp_crt_deps,
          install: true,
          install_dir: scarlett_install_dir,
        )
      endif
    endforeach
  endif
else
  if get_option('build_tests')
    if fs.exists('tests/meson.build')
      subdir('tests')
    endif
    if not fs.exists('uwphost/meson.build') and fs.exists('uwphost/tests/meson.build')
      subdir('uwphost/tests')
    endif
    if not fs.exists('winrt_ui/meson.build') and fs.exists('winrt_ui/tests/meson.build')
      subdir('winrt_ui/tests')
    endif
    if not fs.exists('winrt_sys/meson.build') and fs.exists('winrt_sys/tests/meson.build')
      subdir('winrt_sys/tests')
    endif
    if not fs.exists('forwarders/sys/meson.build') and fs.exists('forwarders/sys/tests/meson.build')
      subdir('forwarders/sys/tests')
    endif
    if not fs.exists('forwarders/crt/meson.build') and fs.exists('forwarders/crt/test_crt_imports_audit.py')
      test('uwp_crt_imports_audit', uwp_python3,
        args: [meson.current_source_dir() / 'forwarders/crt/test_crt_imports_audit.py'],
      )
    endif
    foreach _py_test_dir : ['tests', 'uwphost/tests', 'winrt_ui/tests', 'winrt_sys/tests', 'forwarders/sys/tests']
      if fs.is_dir(_py_test_dir)
        _py_res = run_command(uwp_python3, '-c',
          'import os, sys; d=sys.argv[1]; print("\\n".join(sorted(f for f in os.listdir(d) if f.startswith("test_") and f.endswith(".py"))))',
          meson.current_source_dir() / _py_test_dir,
          check: true)
        foreach _py_file : _py_res.stdout().strip().split('\n')
          if _py_file != ''
            _tname = 'uwp_' + _py_test_dir.replace('/', '_') + '_' + _py_file.replace('.py', '')
            test(_tname, uwp_python3,
              args: [meson.current_source_dir() / _py_test_dir / _py_file],
            )
          endif
        endforeach
      endif
    endforeach
  endif
endif
