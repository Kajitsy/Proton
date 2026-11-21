add_library(scarlett_audio_media_scaffold STATIC
    audio_media_scaffold.cpp
)
target_include_directories(scarlett_audio_media_scaffold PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_options(scarlett_audio_media_scaffold PRIVATE -Wall -Wextra -Werror)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(SCARLETT_OPUS REQUIRED IMPORTED_TARGET opus)
pkg_check_modules(SCARLETT_AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(SCARLETT_AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SCARLETT_OPENAL IMPORTED_TARGET openal)
pkg_check_modules(SCARLETT_PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(SCARLETT_VULKAN IMPORTED_TARGET vulkan)

set(SCARLETT_AUDIO_UNIX_SOURCES
    xdsp_math.cpp
    xapu_codecs.cpp
    xaudio2_mixer.cpp
    gameinput_backend.cpp
    media_bink2_fsr.cpp
    audio_scarlett_unix.cpp
)

add_library(scarlett_audio_unix_core STATIC
    ${SCARLETT_AUDIO_UNIX_SOURCES}
)
set_target_properties(scarlett_audio_unix_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(scarlett_audio_unix_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_options(scarlett_audio_unix_core PRIVATE -Wall -Wextra -Werror)
target_link_libraries(scarlett_audio_unix_core PUBLIC
    PkgConfig::SCARLETT_OPUS
    PkgConfig::SCARLETT_AVCODEC
    PkgConfig::SCARLETT_AVUTIL
    Threads::Threads
    ${CMAKE_DL_LIBS}
)
if(SCARLETT_OPENAL_FOUND)
    target_compile_definitions(scarlett_audio_unix_core PRIVATE SCARLETT_HAVE_OPENAL=1)
    target_link_libraries(scarlett_audio_unix_core PUBLIC PkgConfig::SCARLETT_OPENAL)
endif()
if(SCARLETT_PIPEWIRE_FOUND)
    target_compile_definitions(scarlett_audio_unix_core PRIVATE SCARLETT_HAVE_PIPEWIRE=1)
    target_link_libraries(scarlett_audio_unix_core PUBLIC PkgConfig::SCARLETT_PIPEWIRE)
endif()
if(SCARLETT_VULKAN_FOUND)
    target_compile_definitions(scarlett_audio_unix_core PRIVATE SCARLETT_HAVE_VULKAN=1)
    target_link_libraries(scarlett_audio_unix_core PUBLIC PkgConfig::SCARLETT_VULKAN)
endif()

if(SCARLETT_BUILD_UNIXLIB)
    add_library(audio_scarlett_unix SHARED
        ${SCARLETT_AUDIO_UNIX_SOURCES}
    )
    set_target_properties(audio_scarlett_unix PROPERTIES
        PREFIX ""
        OUTPUT_NAME "audio_scarlett_unix"
        C_VISIBILITY_PRESET default
        CXX_VISIBILITY_PRESET default
    )
    target_include_directories(audio_scarlett_unix PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(audio_scarlett_unix PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(audio_scarlett_unix PRIVATE
        PkgConfig::SCARLETT_OPUS
        PkgConfig::SCARLETT_AVCODEC
        PkgConfig::SCARLETT_AVUTIL
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    if(SCARLETT_OPENAL_FOUND)
        target_compile_definitions(audio_scarlett_unix PRIVATE SCARLETT_HAVE_OPENAL=1)
        target_link_libraries(audio_scarlett_unix PRIVATE PkgConfig::SCARLETT_OPENAL)
    endif()
    if(SCARLETT_PIPEWIRE_FOUND)
        target_compile_definitions(audio_scarlett_unix PRIVATE SCARLETT_HAVE_PIPEWIRE=1)
        target_link_libraries(audio_scarlett_unix PRIVATE PkgConfig::SCARLETT_PIPEWIRE)
    endif()
    if(SCARLETT_VULKAN_FOUND)
        target_compile_definitions(audio_scarlett_unix PRIVATE SCARLETT_HAVE_VULKAN=1)
        target_link_libraries(audio_scarlett_unix PRIVATE PkgConfig::SCARLETT_VULKAN)
    endif()
    install(TARGETS audio_scarlett_unix
        LIBRARY DESTINATION "${SCARLETT_INSTALL_UNIX_DIR}"
    )
endif()

add_library(scarlett_audio_pe_bridge STATIC
    xaudio2_9_pe.c
    acphal_pe.c
    gameinput_pe.c
    bink2_scarlett_pe.c
    ffx_loader_xbox_pe.c
    ffx_upscaler_xbox_pe.c
)
target_include_directories(scarlett_audio_pe_bridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_options(scarlett_audio_pe_bridge PRIVATE -Wall -Wextra -Werror)

if(SCARLETT_BUILD_HOST_TESTS)
    enable_testing()
    add_executable(test_audio_input_media
        tests/test_audio_input_media.cpp
    )
    target_include_directories(test_audio_input_media PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(test_audio_input_media PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(test_audio_input_media PRIVATE
        scarlett_audio_pe_bridge
        scarlett_audio_unix_core
    )

    add_test(
        NAME audio_input_media_unit_tests
        COMMAND test_audio_input_media
    )

    find_program(SCARLETT_CLANG_EXE clang)
    if(SCARLETT_CLANG_EXE)
        set(_pe_shims
            "xaudio2_9:xaudio2_9_pe.c"
            "acphal:acphal_pe.c"
            "gameinput:gameinput_pe.c"
            "bink2_scarlett:bink2_scarlett_pe.c"
            "ffx_loader_xbox:ffx_loader_xbox_pe.c"
            "ffx_upscaler_xbox:ffx_upscaler_xbox_pe.c"
        )
        foreach(_entry IN LISTS _pe_shims)
            string(REPLACE ":" ";" _pair "${_entry}")
            list(GET _pair 0 _name)
            list(GET _pair 1 _src)
            add_test(
                NAME "audio_pe64_${_name}_freestanding"
                COMMAND ${SCARLETT_CLANG_EXE}
                    --target=x86_64-w64-mingw32
                    -std=c11
                    -ffreestanding
                    -Wall -Wextra -Werror
                    "-I${CMAKE_SOURCE_DIR}/include"
                    "-I${CMAKE_CURRENT_SOURCE_DIR}"
                    -c "${CMAKE_CURRENT_SOURCE_DIR}/${_src}"
                    -o "${CMAKE_CURRENT_BINARY_DIR}/${_name}_pe.obj"
            )
        endforeach()
    endif()

    find_program(SCARLETT_DLLTOOL_EXE llvm-dlltool)
    if(SCARLETT_DLLTOOL_EXE)
        foreach(_def IN ITEMS
                xaudio2_9.def
                AcpHal.def
                GameInput.def
                Bink2Scarlett.def
                amd_fidelityfx_loader_xbox.def
                amd_fidelityfx_upscaler_xbox.def)
            add_test(
                NAME "audio_def_${_def}_llvm_dlltool"
                COMMAND ${SCARLETT_DLLTOOL_EXE}
                    -m i386:x86-64
                    -d "${CMAKE_CURRENT_SOURCE_DIR}/${_def}"
                    -l "${CMAKE_CURRENT_BINARY_DIR}/lib${_def}.a"
            )
        endforeach()
    endif()
endif()
