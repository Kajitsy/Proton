find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(ARENA_OPUS REQUIRED IMPORTED_TARGET opus)
pkg_check_modules(ARENA_LIBDEFLATE REQUIRED libdeflate)

add_executable(test_phase0_scaffold
    test_phase0_scaffold.cpp
)

get_filename_component(SCARLETT_REPO_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)

target_compile_definitions(test_phase0_scaffold PRIVATE
    SCARLETT_REPO_ROOT="${SCARLETT_REPO_ROOT}"
    SCARLETT_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(test_phase0_scaffold PRIVATE
    scarlett_xdl_scaffold
    scarlett_loader_scaffold
    scarlett_xmem_scaffold
    scarlett_dstorage_xs_scaffold
    scarlett_xg_xs_scaffold
    scarlett_xgameruntime_scaffold
    scarlett_audio_media_scaffold
)

add_test(
    NAME phase0_cpp_scaffold_smoke
    COMMAND test_phase0_scaffold
)

add_test(
    NAME phase0_proton_scarlett_env_py
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_proton_scarlett_env.py"
)

add_test(
    NAME phase0_proton_uwp_env_py
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_proton_uwp_env.py"
)

# ============================================================================
# Phase 6: ScarlettArena-Linux (PE64 Binary + 148B DDS+DX10 + 6 Stress Scenes)
# ============================================================================

find_program(SCARLETT_ARENA_CLANG clang)
find_program(SCARLETT_ARENA_LLD_LINK lld-link)
find_program(SCARLETT_ARENA_DLLTOOL llvm-dlltool)
find_program(SCARLETT_ARENA_READOBJ llvm-readobj)

if(SCARLETT_ARENA_CLANG AND SCARLETT_ARENA_LLD_LINK AND SCARLETT_ARENA_DLLTOOL)
    set(ARENA_PE_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/arena")
    set(ARENA_PE_OBJ "${ARENA_PE_BIN_DIR}/ScarlettArena_pe.obj")
    set(ARENA_PE_EXE "${ARENA_PE_BIN_DIR}/ScarlettArena.exe")

    set(_arena_def_specs
        "xmem:${CMAKE_SOURCE_DIR}/xmem/xmem.def"
        "d3d12_xs:${CMAKE_SOURCE_DIR}/d3d12_xs/d3d12_xs.def"
        "xgameruntime:${CMAKE_SOURCE_DIR}/xgameruntime/xgameruntime.def"
        "dstorage_xs:${CMAKE_SOURCE_DIR}/dstorage_xs/dstorage_xs.def"
        "xg_xs:${CMAKE_SOURCE_DIR}/xg_xs/xg_xs.def"
        "pixevt:${CMAKE_SOURCE_DIR}/xg_xs/pixevt.def"
        "xgameplatform:${CMAKE_SOURCE_DIR}/loader/xgameplatform.def"
        "xaudio2_9:${CMAKE_SOURCE_DIR}/audio_media/xaudio2_9.def"
        "GameInput:${CMAKE_SOURCE_DIR}/audio_media/GameInput.def"
        "amd_fidelityfx_upscaler_xbox:${CMAKE_SOURCE_DIR}/audio_media/amd_fidelityfx_upscaler_xbox.def"
    )
    set(_arena_import_libs "")
    foreach(_spec IN LISTS _arena_def_specs)
        string(REPLACE ":" ";" _parts "${_spec}")
        list(GET _parts 0 _lib_name)
        list(GET _parts 1 _def_path)
        set(_lib_out "${ARENA_PE_BIN_DIR}/libarena_${_lib_name}.lib")
        add_custom_command(
            OUTPUT "${_lib_out}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${ARENA_PE_BIN_DIR}"
            COMMAND ${SCARLETT_ARENA_DLLTOOL}
                -m i386:x86-64
                -d "${_def_path}"
                -l "${_lib_out}"
            DEPENDS "${_def_path}"
            COMMENT "Generating PE64 import library for ${_lib_name}.dll"
        )
        list(APPEND _arena_import_libs "${_lib_out}")
    endforeach()

    add_custom_command(
        OUTPUT "${ARENA_PE_OBJ}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ARENA_PE_BIN_DIR}"
        COMMAND ${SCARLETT_ARENA_CLANG}
            --target=x86_64-w64-mingw32
            -std=c11
            -ffreestanding
            -O2
            -Wall -Wextra -Werror
            -c "${CMAKE_CURRENT_SOURCE_DIR}/arena/ScarlettArena_pe.c"
            -o "${ARENA_PE_OBJ}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/arena/ScarlettArena_pe.c"
        COMMENT "Compiling freestanding PE64 object ScarlettArena_pe.obj"
    )

    add_custom_command(
        OUTPUT "${ARENA_PE_EXE}"
        COMMAND ${SCARLETT_ARENA_LLD_LINK}
            /subsystem:console
            /entry:ScarlettArenaEntry
            /machine:x64
            /nodefaultlib
            "/out:${ARENA_PE_EXE}"
            "${ARENA_PE_OBJ}"
            ${_arena_import_libs}
        DEPENDS "${ARENA_PE_OBJ}" ${_arena_import_libs}
        COMMENT "Linking freestanding PE64 executable ScarlettArena.exe"
    )

    add_custom_target(scarlett_arena_pe64_exe ALL
        DEPENDS "${ARENA_PE_EXE}"
    )

    add_test(
        NAME arena_pe64_binary_check
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/arena/test_arena_pe64_and_dds.py"
            "${SCARLETT_REPO_ROOT}"
            "${CMAKE_CURRENT_SOURCE_DIR}/arena/package"
            "${ARENA_PE_EXE}"
            "${SCARLETT_ARENA_READOBJ}"
    )
endif()

if(NOT DEFINED ARENA_PE_EXE)
    set(ARENA_PE_EXE "")
endif()

add_test(
    NAME test_proton_scarlett_inspect
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/test_proton_scarlett_inspect.py"
        "${SCARLETT_REPO_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/arena/package"
        "${ARENA_PE_EXE}"
)

if(SCARLETT_BUILD_UNIXLIB)
    add_executable(scarlett_arena_linux
        arena/main.cpp
        arena/scene1_uma.cpp
        arena/scene2_rdna2.cpp
        arena/scene3_dstorage.cpp
        arena/scene4_dx12u_aaa.cpp
        arena/scene5_presentx_pacing.cpp
        arena/scene6_runtime_audio_vfs.cpp
    )

    add_dependencies(scarlett_arena_linux
        scarlett_xdl_generate_schemas
        d3d12_xs_unix
        dstorage_xs_unix
        xgameruntime_unix
        test_bcpack_plugin
    )

    target_include_directories(scarlett_arena_linux PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/arena
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/xdl/include
        ${CMAKE_BINARY_DIR}/xdl/generated
        ${ARENA_LIBDEFLATE_INCLUDE_DIRS}
    )

    target_compile_options(scarlett_arena_linux PRIVATE
        -O2
        -Wall
        -Wextra
        -Werror
    )

    target_compile_definitions(scarlett_arena_linux PRIVATE
        SCARLETT_ARENA_PACKAGE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/arena/package"
        SCARLETT_TEST_BCPACK_PLUGIN_PATH="$<TARGET_FILE:test_bcpack_plugin>"
        SCARLETT_XGR_UNIX_SO_PATH="$<TARGET_FILE:xgameruntime_unix>"
    )

    target_link_options(scarlett_arena_linux PRIVATE
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/arena/arena.map"
    )

    target_link_libraries(scarlett_arena_linux PRIVATE
        scarlett_xdl_runtime
        scarlett_loader_pe_bridge
        scarlett_xmem_pe_host
        scarlett_rdna2_lifter
        scarlett_d3d12_xs_pe_host
        scarlett_d3d12_xs_unix_core
        scarlett_dstorage_xs_pe_host
        scarlett_dstorage_xs_unix_core
        xmem_unix
        scarlett_xg_xs_pe_host
        scarlett_xg_xs_unix_core
        scarlett_xgameruntime_pe_host
        scarlett_audio_pe_bridge
        loader_unix
        audio_scarlett_unix
        PkgConfig::ARENA_OPUS
        ${ARENA_LIBDEFLATE_LIBRARIES}
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )

    add_test(
        NAME scarlett_arena_anaconda
        COMMAND scarlett_arena_linux
            --model=anaconda
            "--report-json=${CMAKE_CURRENT_BINARY_DIR}/arena/arena_report_anaconda.json"
    )
    set_tests_properties(scarlett_arena_anaconda PROPERTIES
        ENVIRONMENT "PROTON_XBOX_MODEL=anaconda"
    )

    add_test(
        NAME scarlett_arena_lockhart
        COMMAND scarlett_arena_linux
            --model=lockhart
            "--report-json=${CMAKE_CURRENT_BINARY_DIR}/arena/arena_report_lockhart.json"
    )
    set_tests_properties(scarlett_arena_lockhart PROPERTIES
        ENVIRONMENT "PROTON_XBOX_MODEL=lockhart"
    )
endif()
