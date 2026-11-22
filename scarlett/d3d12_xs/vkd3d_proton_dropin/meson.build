# Proton-Scarlett Drop-In Overlay Meson Build for vkd3d-proton/libs/d3d12_xs/
# Target branch: vkd3d-proton (proton-scarlett-3.1)
#
# Installed into vkd3d-proton/libs/d3d12_xs/ via apply_to_vkd3d_proton.sh.
# Supports both Wine PE64 (x86_64-windows -> d3d12_xs.dll) and native Linux
# unixlib (x86_64-unix -> d3d12_xs_unix.so) builds without altering stock
# PC vkd3d-proton targets.

d3d12_xs_inc = include_directories(
  '.',
  '../../../scarlett',
  '../../../scarlett/include',
)

if target_machine.system() == 'windows'
  d3d12_xs_dll = shared_library('d3d12_xs',
    'd3d12_xs_pe.c',
    name_prefix: '',
    name_suffix: 'dll',
    vs_module_defs: 'd3d12_xs.def',
    include_directories: [d3d12_xs_inc],
    c_args: ['-std=c11', '-ffreestanding', '-Wall', '-Wextra', '-Werror'],
    install: true,
  )
else
  d3d12_xs_threads_dep = dependency('threads', required: true)
  d3d12_xs_dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)

  d3d12_xs_unix_sources = files(
    'd3d12_xs_vulkan_loader.cpp',
    'd3d12_xs_descriptors.cpp',
    'd3d12_xs_dxil_strip.cpp',
    'd3d12_xs_presentx.cpp',
    'd3d12_xs_core.cpp',
    'd3d12_xs_unix_dispatch.cpp',
  )

  d3d12_xs_unix_so = shared_library('d3d12_xs_unix',
    d3d12_xs_unix_sources,
    name_prefix: '',
    include_directories: [d3d12_xs_inc],
    cpp_args: ['-std=c++20', '-Wall', '-Wextra', '-Werror', '-fvisibility=hidden'],
    dependencies: [d3d12_xs_threads_dep, d3d12_xs_dl_dep],
    install: true,
  )
endif
