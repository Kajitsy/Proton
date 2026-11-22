# Phase 0 smoke-test compatibility target (required by scarlett/tests/test_phase0_scaffold.cpp)
add_library(scarlett_xg_xs_scaffold STATIC
    xg_xs_scaffold.cpp
)
target_include_directories(scarlett_xg_xs_scaffold PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_options(scarlett_xg_xs_scaffold PRIVATE -Wall -Wextra -Werror)

find_package(Threads REQUIRED)

set(SCARLETT_XG_XS_UNIX_SOURCES
    xg_xs_tiling.cpp
    xbox_bvh_builder.cpp
    xg_sfs_coalescer.cpp
    pixevt_backend.cpp
    xg_xs_unix_dispatch.cpp
)

if(SCARLETT_BUILD_UNIXLIB)
    add_library(scarlett_xg_xs_unix_core STATIC
        ${SCARLETT_XG_XS_UNIX_SOURCES}
    )
    set_target_properties(scarlett_xg_xs_unix_core PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(scarlett_xg_xs_unix_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(scarlett_xg_xs_unix_core PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(scarlett_xg_xs_unix_core PUBLIC
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )

    add_library(xg_xs_unix SHARED
        ${SCARLETT_XG_XS_UNIX_SOURCES}
    )
    set_target_properties(xg_xs_unix PROPERTIES
        PREFIX ""
        OUTPUT_NAME "xg_xs_unix"
        C_VISIBILITY_PRESET default
        CXX_VISIBILITY_PRESET default
    )
    target_include_directories(xg_xs_unix PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(xg_xs_unix PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(xg_xs_unix PRIVATE
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    install(TARGETS xg_xs_unix
        LIBRARY DESTINATION "${SCARLETT_INSTALL_UNIX_DIR}"
    )

    add_library(scarlett_xg_xs_pe_host STATIC
        xg_xs_pe.c
        xbox_bvh_builder_pe.c
        pixevt_pe.c
    )
    target_include_directories(scarlett_xg_xs_pe_host PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(scarlett_xg_xs_pe_host PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(scarlett_xg_xs_pe_host PUBLIC
        scarlett_xg_xs_unix_core
    )
endif()

if(SCARLETT_BUILD_PE)
    set(_xg_pe_targets
        "xg_xs:xg_xs_pe.c:xg_xs.def"
        "XboxBvhBuilder:xbox_bvh_builder_pe.c:XboxBvhBuilder.def"
        "pixevt:pixevt_pe.c:pixevt.def"
    )
    foreach(_entry IN LISTS _xg_pe_targets)
        string(REPLACE ":" ";" _parts "${_entry}")
        list(GET _parts 0 _name)
        list(GET _parts 1 _src)
        list(GET _parts 2 _def)
        add_library(${_name} SHARED ${_src} ${_def})
        set_target_properties(${_name} PROPERTIES PREFIX "" SUFFIX ".dll")
        target_include_directories(${_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/include
        )
        target_compile_options(${_name} PRIVATE -ffreestanding -Wall -Wextra -Werror)
        install(TARGETS ${_name}
            RUNTIME DESTINATION "${SCARLETT_INSTALL_PE_DIR}"
        )
    endforeach()
endif()

if(SCARLETT_BUILD_HOST_TESTS AND SCARLETT_BUILD_UNIXLIB)
    enable_testing()
    add_executable(test_xg_bvh_pix
        tests/test_xg_bvh_pix.cpp
    )
    target_include_directories(test_xg_bvh_pix PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(test_xg_bvh_pix PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(test_xg_bvh_pix PRIVATE
        scarlett_xg_xs_pe_host
        scarlett_xg_xs_unix_core
        Threads::Threads
    )
    add_test(
        NAME xg_bvh_pix_unit_tests
        COMMAND test_xg_bvh_pix
    )

    find_program(SCARLETT_XG_CLANG_EXE clang)
    find_program(SCARLETT_XG_NM_EXE llvm-nm)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(SCARLETT_XG_CLANG_EXE)
        set(_xg_shims
            "xg_xs:xg_xs_pe.c"
            "xbox_bvh_builder:xbox_bvh_builder_pe.c"
            "pixevt:pixevt_pe.c"
        )
        foreach(_entry IN LISTS _xg_shims)
            string(REPLACE ":" ";" _pair "${_entry}")
            list(GET _pair 0 _name)
            list(GET _pair 1 _src)
            set(_obj_out "${CMAKE_CURRENT_BINARY_DIR}/${_name}_pe.obj")
            add_test(
                NAME "xg_pe64_${_name}_freestanding"
                COMMAND ${SCARLETT_XG_CLANG_EXE}
                    --target=x86_64-w64-mingw32
                    -std=c11
                    -ffreestanding
                    -O2
                    -Wall -Wextra -Werror
                    "-I${CMAKE_SOURCE_DIR}/include"
                    "-I${CMAKE_CURRENT_SOURCE_DIR}"
                    -c "${CMAKE_CURRENT_SOURCE_DIR}/${_src}"
                    -o "${_obj_out}"
            )
            if(SCARLETT_XG_NM_EXE AND Python3_FOUND)
                add_test(
                    NAME "xg_pe64_${_name}_nm_u_clean"
                    COMMAND ${Python3_EXECUTABLE} -c
                        "import subprocess, sys; out = subprocess.check_output([sys.argv[1], '-u', sys.argv[2]], text=True); allowed = {'__wine_init_unix_call', '__wine_unix_call_dispatcher', '__wine_unixlib_handle'}; syms = {line.split()[-1] for line in out.splitlines() if line.strip()}; bad = syms - allowed; assert not bad, f'Forbidden PE64 symbols: {bad}'; print('Clean PE64 imports:', sorted(syms))"
                        "${SCARLETT_XG_NM_EXE}"
                        "${_obj_out}"
                )
                set_tests_properties("xg_pe64_${_name}_nm_u_clean" PROPERTIES
                    DEPENDS "xg_pe64_${_name}_freestanding"
                )
            endif()
        endforeach()
    endif()

    find_program(SCARLETT_XG_DLLTOOL_EXE llvm-dlltool)
    if(SCARLETT_XG_DLLTOOL_EXE)
        foreach(_def IN ITEMS xg_xs.def XboxBvhBuilder.def pixevt.def)
            add_test(
                NAME "xg_def_${_def}_llvm_dlltool"
                COMMAND ${SCARLETT_XG_DLLTOOL_EXE}
                    -m i386:x86-64
                    -d "${CMAKE_CURRENT_SOURCE_DIR}/${_def}"
                    -l "${CMAKE_CURRENT_BINARY_DIR}/lib${_def}.a"
            )
        endforeach()
    endif()
endif()
