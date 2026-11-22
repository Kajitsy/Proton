# Phase 0 smoke-test compatibility target (required by scarlett/tests/test_phase0_scaffold.cpp)
add_library(scarlett_dstorage_xs_scaffold STATIC
    dstorage_xs_scaffold.cpp
)
target_include_directories(scarlett_dstorage_xs_scaffold PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)
pkg_check_modules(LIBDEFLATE REQUIRED libdeflate)
pkg_check_modules(ZLIBNG REQUIRED zlib-ng)

set(SCARLETT_DSTORAGE_XS_UNIX_SOURCES
    dstorage_xs_shaders.cpp
    dstorage_xs_io.cpp
    dstorage_xs_decomp.cpp
    dstorage_xs_core.cpp
    dstorage_xs_unix_dispatch.cpp
)

# Native Linux unixlib backend: dstorage_xs_unix.so (x86_64-unix)
if(SCARLETT_BUILD_UNIXLIB)
    add_library(dstorage_xs_unix SHARED
        ${SCARLETT_DSTORAGE_XS_UNIX_SOURCES}
    )
    set_target_properties(dstorage_xs_unix PROPERTIES
        PREFIX ""
        OUTPUT_NAME "dstorage_xs_unix"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_include_directories(dstorage_xs_unix PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
        ${LIBURING_INCLUDE_DIRS}
        ${LIBDEFLATE_INCLUDE_DIRS}
        ${ZLIBNG_INCLUDE_DIRS}
    )
    target_compile_options(dstorage_xs_unix PRIVATE
        -O2
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(dstorage_xs_unix PRIVATE
        xmem_unix
        ${LIBURING_LIBRARIES}
        ${LIBDEFLATE_LIBRARIES}
        ${ZLIBNG_LIBRARIES}
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    install(TARGETS dstorage_xs_unix
        LIBRARY DESTINATION "${SCARLETT_INSTALL_UNIX_DIR}"
    )

    # Static host core (avoids ELF global __wine_unix_call_funcs collision when
    # host test binaries link both xmem_unix.so and dstorage_xs in one process)
    add_library(scarlett_dstorage_xs_unix_core STATIC
        ${SCARLETT_DSTORAGE_XS_UNIX_SOURCES}
    )
    set_target_properties(scarlett_dstorage_xs_unix_core PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
    target_compile_definitions(scarlett_dstorage_xs_unix_core PRIVATE
        SCARLETT_DSTORAGEXS_STATIC_HOST_LINK=1
    )
    target_include_directories(scarlett_dstorage_xs_unix_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
        ${LIBURING_INCLUDE_DIRS}
        ${LIBDEFLATE_INCLUDE_DIRS}
        ${ZLIBNG_INCLUDE_DIRS}
    )
    target_compile_options(scarlett_dstorage_xs_unix_core PRIVATE
        -O2
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(scarlett_dstorage_xs_unix_core PUBLIC
        xmem_unix
        ${LIBURING_LIBRARIES}
        ${LIBDEFLATE_LIBRARIES}
        ${ZLIBNG_LIBRARIES}
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )

    # Host-linked PE64 C layer (exact dstorage_xs_pe.c routed via WINE_UNIX_CALL bridge)
    add_library(scarlett_dstorage_xs_pe_host STATIC
        dstorage_xs_pe.c
    )
    target_include_directories(scarlett_dstorage_xs_pe_host PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    )
    target_compile_options(scarlett_dstorage_xs_pe_host PRIVATE
        -O2
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(scarlett_dstorage_xs_pe_host PUBLIC
        scarlett_dstorage_xs_unix_core
    )
endif()

# Windows PE64 DLL target: dstorage_xs.dll (x86_64-windows)
if(SCARLETT_BUILD_PE)
    add_library(dstorage_xs SHARED
        dstorage_xs_pe.c
        dstorage_xs.def
    )
    set_target_properties(dstorage_xs PROPERTIES PREFIX "" SUFFIX ".dll")
    target_include_directories(dstorage_xs PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    )
    target_compile_options(dstorage_xs PRIVATE
        -ffreestanding
        -O2
        -Wall
        -Wextra
        -Werror
    )
    install(TARGETS dstorage_xs
        RUNTIME DESTINATION "${SCARLETT_INSTALL_PE_DIR}"
    )
endif()

# Exhaustive DirectStorage_XS unit, io_uring, SPIR-V, BCPack & throughput tests
if(SCARLETT_BUILD_HOST_TESTS AND SCARLETT_BUILD_UNIXLIB)
    enable_testing()

    add_library(test_bcpack_plugin SHARED
        tests/test_bcpack_plugin.cpp
    )
    set_target_properties(test_bcpack_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "test_bcpack_plugin"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_include_directories(test_bcpack_plugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(test_bcpack_plugin PRIVATE
        -O2
        -Wall
        -Wextra
        -Werror
    )

    add_executable(test_dstorage_xs
        tests/test_dstorage_xs.cpp
    )
    target_include_directories(test_dstorage_xs PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
        ${LIBDEFLATE_INCLUDE_DIRS}
    )
    target_compile_options(test_dstorage_xs PRIVATE
        -O2
        -Wall
        -Wextra
        -Werror
    )
    target_compile_definitions(test_dstorage_xs PRIVATE
        SCARLETT_DSTORAGEXS_UNIX_SO_PATH="$<TARGET_FILE:dstorage_xs_unix>"
        SCARLETT_TEST_BCPACK_PLUGIN_PATH="$<TARGET_FILE:test_bcpack_plugin>"
    )
    target_link_libraries(test_dstorage_xs PRIVATE
        scarlett_dstorage_xs_pe_host
        scarlett_dstorage_xs_unix_core
        scarlett_xmem_pe_host
        xmem_unix
        ${LIBDEFLATE_LIBRARIES}
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    add_dependencies(test_dstorage_xs dstorage_xs_unix test_bcpack_plugin)
    add_test(
        NAME dstorage_xs_unit_tests
        COMMAND test_dstorage_xs
    )

    find_program(SCARLETT_DSTORAGE_CLANG clang)
    find_program(SCARLETT_DSTORAGE_LLVM_NM llvm-nm)
    find_program(SCARLETT_DSTORAGE_LLVM_DLLTOOL llvm-dlltool)
    find_package(Python3 COMPONENTS Interpreter)

    if(SCARLETT_DSTORAGE_CLANG)
        add_test(
            NAME dstorage_xs_pe64_freestanding
            COMMAND ${SCARLETT_DSTORAGE_CLANG}
                --target=x86_64-w64-mingw32
                -std=c11
                -ffreestanding
                -O2
                -Wall -Wextra -Werror
                "-I${CMAKE_SOURCE_DIR}/include"
                "-I${CMAKE_CURRENT_SOURCE_DIR}"
                -c "${CMAKE_CURRENT_SOURCE_DIR}/dstorage_xs_pe.c"
                -o "${CMAKE_CURRENT_BINARY_DIR}/dstorage_xs_pe.obj"
        )

        if(SCARLETT_DSTORAGE_LLVM_NM AND Python3_FOUND)
            add_test(
                NAME dstorage_xs_pe64_nm_isolation
                COMMAND ${Python3_EXECUTABLE} -c
                    "import subprocess, sys; out = subprocess.check_output([sys.argv[1], '-u', sys.argv[2]], text=True); syms = {line.strip().split()[-1] for line in out.splitlines() if line.strip()}; allowed = {'__wine_init_unix_call', '__wine_unix_call_dispatcher', '__wine_unixlib_handle'}; extra = syms - allowed; assert not extra and syms == allowed, f'unexpected symbols: {syms}'; print('PE64 llvm-nm -u OK:', sorted(syms))"
                    "${SCARLETT_DSTORAGE_LLVM_NM}"
                    "${CMAKE_CURRENT_BINARY_DIR}/dstorage_xs_pe.obj"
            )
            set_tests_properties(dstorage_xs_pe64_nm_isolation PROPERTIES
                DEPENDS dstorage_xs_pe64_freestanding
            )
        endif()
    endif()

    if(SCARLETT_DSTORAGE_LLVM_DLLTOOL)
        add_test(
            NAME dstorage_xs_def_dlltool
            COMMAND ${SCARLETT_DSTORAGE_LLVM_DLLTOOL}
                -m i386:x86-64
                -d "${CMAKE_CURRENT_SOURCE_DIR}/dstorage_xs.def"
                -l "${CMAKE_CURRENT_BINARY_DIR}/libdstorage_xs.a"
        )
    endif()
endif()
