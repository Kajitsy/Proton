scarlett_audio_media_lib = static_library('scarlett_audio_media_scaffold',
  'audio_media_scaffold.cpp',
  include_directories: [scarlett_inc],
  install: false,
)

scarlett_audio_unix_sources = files(
  'xdsp_math.cpp',
  'xapu_codecs.cpp',
  'xaudio2_mixer.cpp',
  'gameinput_backend.cpp',
  'media_bink2_fsr.cpp',
  'audio_scarlett_unix.cpp',
)

if is_windows_pe
  pe_shims = [
    ['xaudio2_9', 'xaudio2_9_pe.c', 'xaudio2_9.def'],
    ['AcpHal', 'acphal_pe.c', 'AcpHal.def'],
    ['GameInput', 'gameinput_pe.c', 'GameInput.def'],
    ['Bink2Scarlett', 'bink2_scarlett_pe.c', 'Bink2Scarlett.def'],
    ['amd_fidelityfx_loader_xbox', 'ffx_loader_xbox_pe.c', 'amd_fidelityfx_loader_xbox.def'],
    ['amd_fidelityfx_upscaler_xbox', 'ffx_upscaler_xbox_pe.c', 'amd_fidelityfx_upscaler_xbox.def'],
  ]
  foreach shim : pe_shims
    shared_library(shim[0],
      shim[1],
      name_prefix: '',
      vs_module_defs: shim[2],
      include_directories: [scarlett_inc, include_directories('.')],
      c_args: ['-ffreestanding'],
      install: true,
      install_dir: scarlett_install_dir,
    )
  endforeach
else
  dep_opus = dependency('opus', required: true)
  dep_avcodec = dependency('libavcodec', required: true)
  dep_avutil = dependency('libavutil', required: true)
  dep_threads = dependency('threads', required: true)
  dep_openal = dependency('openal', required: false)
  dep_pipewire = dependency('libpipewire-0.3', required: false)
  dep_vulkan = dependency('vulkan', required: false)

  unix_deps = [dep_opus, dep_avcodec, dep_avutil, dep_threads]
  unix_cpp_args = []
  if dep_openal.found()
    unix_deps += dep_openal
    unix_cpp_args += '-DSCARLETT_HAVE_OPENAL=1'
  endif
  if dep_pipewire.found()
    unix_deps += dep_pipewire
    unix_cpp_args += '-DSCARLETT_HAVE_PIPEWIRE=1'
  endif
  if dep_vulkan.found()
    unix_deps += dep_vulkan
    unix_cpp_args += '-DSCARLETT_HAVE_VULKAN=1'
  endif

  audio_scarlett_unix_so = shared_library('audio_scarlett_unix',
    scarlett_audio_unix_sources,
    name_prefix: '',
    include_directories: [scarlett_inc, include_directories('.')],
    cpp_args: unix_cpp_args,
    dependencies: unix_deps,
    install: true,
    install_dir: scarlett_install_dir,
  )

  if get_option('build_tests')
    audio_pe_bridge_lib = static_library('scarlett_audio_pe_bridge',
      'xaudio2_9_pe.c',
      'acphal_pe.c',
      'gameinput_pe.c',
      'bink2_scarlett_pe.c',
      'ffx_loader_xbox_pe.c',
      'ffx_upscaler_xbox_pe.c',
      include_directories: [scarlett_inc, include_directories('.')],
      install: false,
    )

    test_audio_input_media_exe = executable('test_audio_input_media',
      'tests/test_audio_input_media.cpp',
      include_directories: [scarlett_inc, include_directories('.')],
      link_with: [audio_pe_bridge_lib, audio_scarlett_unix_so],
      dependencies: unix_deps,
      install: false,
    )

    test('audio_input_media_unit_tests', test_audio_input_media_exe)
  endif
endif
