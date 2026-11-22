sys_forwarder_dlls = [
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

if is_windows_pe
  foreach dll_name : sys_forwarder_dlls
    shared_module(dll_name,
      [dll_name + '.c'],
      vs_module_defs: dll_name + '.def',
      name_prefix: '',
      name_suffix: 'dll',
      include_directories: [scarlett_inc, include_directories('.')],
      c_args: ['-Wall', '-Wextra', '-Werror'],
      install: true,
      install_dir: scarlett_install_dir,
    )
  endforeach
else
  if get_option('build_tests')
    test_pkg_stubs_exe = executable('test_uwp_sys_pkg_and_stubs',
      [
        'tests/test_pkg_identity_and_stubs.c',
      ],
      include_directories: [
        scarlett_inc,
        include_directories('.'),
        include_directories('../../../xmem'),
      ],
      c_args: ['-Wall', '-Wextra', '-Werror'],
      link_with: [xmem_pe_host_lib, xmem_unix_so],
      install: false,
    )
    test('uwp_sys_pkg_identity_and_stubs', test_pkg_stubs_exe)

    py3 = find_program('python3', required: false)
    if py3.found()
      test('uwp_sys_forwarders_def_audit', py3,
        args: [files('tests/test_sys_forwarders.py')],
      )
    endif
  endif
endif
