# Proton-Scarlett Drop-In Overlay Meson Build for vkd3d-proton/subprojects/dxil-spirv/rdna2-lifter/
# Target branch: vkd3d-proton (proton-scarlett-3.1)
#
# Installed into vkd3d-proton/subprojects/dxil-spirv/rdna2-lifter/ via
# apply_to_dxil_spirv.sh. Builds a clean static/shared RDNA2 -> SPIR-V 1.5/1.6
# lifter library linked into dxil-spirv without altering stock PC DXIL paths.

rdna2_lifter_inc = include_directories(
  '.',
  '../../../../scarlett',
  '../../../../scarlett/include',
)

rdna2_lifter_sources = files(
  'rdna2_containers.cpp',
  'rdna2_decoder.cpp',
  'rdna2_ir.cpp',
  'rdna2_spirv_emitter.cpp',
  'rdna2_microprobes.cpp',
  'rdna2_spv_eval.cpp',
  'rdna2_lifter.cpp',
)

rdna2_lifter_threads_dep = dependency('threads', required: true)
rdna2_lifter_dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)

rdna2_lifter_lib = static_library('scarlett_rdna2_lifter',
  rdna2_lifter_sources,
  include_directories: [rdna2_lifter_inc],
  cpp_args: ['-std=c++20', '-Wall', '-Wextra', '-Werror'],
  dependencies: [rdna2_lifter_threads_dep, rdna2_lifter_dl_dep],
  install: false,
)

rdna2_lifter_dep = declare_dependency(
  include_directories: [rdna2_lifter_inc],
  link_with: rdna2_lifter_lib,
  dependencies: [rdna2_lifter_threads_dep, rdna2_lifter_dl_dep],
)
