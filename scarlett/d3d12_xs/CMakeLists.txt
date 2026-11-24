find_package(Threads REQUIRED)

set(SCARLETT_D3D12_XS_UNIX_SOURCES
    d3d12_xs_vulkan_loader.cpp
    d3d12_xs_descriptors.cpp
    d3d12_xs_dxil_strip.cpp
    d3d12_xs_presentx.cpp
    d3d12_xs_core.cpp
    d3d12_xs_unix_dispatch.cpp
)

# Native Linux unixlib backend: d3d12_xs_unix.so (x86_64-unix)
if(SCARLETT_BUILD_UNIXLIB)
    add_library(d3d12_xs_unix SHARED
        ${SCARLETT_D3D12_XS_UNIX_SOURCES}
    )
    set_target_properties(d3d12_xs_unix PROPERTIES
        PREFIX ""
        OUTPUT_NAME "d3d12_xs_unix"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_include_directories(d3d12_xs_unix PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    )
    target_compile_options(d3d12_xs_unix PRIVATE
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(d3d12_xs_unix PRIVATE
        xmem_unix
        scarlett_rdna2_lifter
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    install(TARGETS d3d12_xs_unix
        LIBRARY DESTINATION "${SCARLETT_INSTALL_UNIX_DIR}"
    )

    # Static host core (avoids ELF global __wine_unix_call_funcs collision when
    # host test binaries link both xmem_unix.so and d3d12_xs in one process)
    add_library(scarlett_d3d12_xs_unix_core STATIC
        ${SCARLETT_D3D12_XS_UNIX_SOURCES}
    )
    set_target_properties(scarlett_d3d12_xs_unix_core PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
    target_compile_definitions(scarlett_d3d12_xs_unix_core PRIVATE
        SCARLETT_D3D12XS_STATIC_HOST_LINK=1
    )
    target_include_directories(scarlett_d3d12_xs_unix_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    )
    target_compile_options(scarlett_d3d12_xs_unix_core PRIVATE
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(scarlett_d3d12_xs_unix_core PUBLIC
        xmem_unix
        scarlett_rdna2_lifter
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )

    # Host-linked PE64 C layer (exact d3d12_xs_pe.c routed via WINE_UNIX_CALL bridge)
    add_library(scarlett_d3d12_xs_pe_host STATIC
        d3d12_xs_pe.c
    )
    target_include_directories(scarlett_d3d12_xs_pe_host PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    )
    target_compile_options(scarlett_d3d12_xs_pe_host PRIVATE
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(scarlett_d3d12_xs_pe_host PUBLIC
        scarlett_d3d12_xs_unix_core
    )
endif()

# Windows PE64 DLL target: d3d12_xs.dll (x86_64-windows)
if(SCARLETT_BUILD_PE)
    add_library(d3d12_xs SHARED
        d3d12_xs_pe.c
        d3d12_xs.def
    )
    set_target_properties(d3d12_xs PROPERTIES PREFIX "" SUFFIX ".dll")
    target_include_directories(d3d12_xs PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    )
    target_compile_options(d3d12_xs PRIVATE
        -ffreestanding
        -Wall
        -Wextra
        -Werror
    )
    install(TARGETS d3d12_xs
        RUNTIME DESTINATION "${SCARLETT_INSTALL_PE_DIR}"
    )
endif()

# Exhaustive unit & integration tests (Vulkan 1.4, NVIDIA 610.xx/1M guard, UMA 0-frame, PresentX)
if(SCARLETT_BUILD_HOST_TESTS AND SCARLETT_BUILD_UNIXLIB)
    enable_testing()
    add_executable(test_d3d12_xs
        tests/test_d3d12_xs.cpp
    )
    target_include_directories(test_d3d12_xs PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    )
    target_compile_options(test_d3d12_xs PRIVATE
        -Wall
        -Wextra
        -Werror
    )
    target_compile_definitions(test_d3d12_xs PRIVATE
        SCARLETT_D3D12XS_UNIX_SO_PATH="$<TARGET_FILE:d3d12_xs_unix>"
    )
    target_link_libraries(test_d3d12_xs PRIVATE
        scarlett_d3d12_xs_pe_host
        scarlett_d3d12_xs_unix_core
        scarlett_xmem_pe_host
        xmem_unix
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    add_dependencies(test_d3d12_xs d3d12_xs_unix)
    add_test(
        NAME d3d12_xs_unit_tests
        COMMAND test_d3d12_xs
    )

    find_program(SCARLETT_D3D12_CLANG clang)
    find_program(SCARLETT_D3D12_LLVM_NM llvm-nm)
    find_program(SCARLETT_D3D12_LLVM_DLLTOOL llvm-dlltool)
    find_package(Python3 COMPONENTS Interpreter)

    if(SCARLETT_D3D12_CLANG)
        add_test(
            NAME d3d12_xs_pe64_freestanding
            COMMAND ${SCARLETT_D3D12_CLANG}
                --target=x86_64-w64-mingw32
                -std=c11
                -ffreestanding
                -O2
                -Wall -Wextra -Werror
                "-I${CMAKE_SOURCE_DIR}/include"
                "-I${CMAKE_CURRENT_SOURCE_DIR}"
                -c "${CMAKE_CURRENT_SOURCE_DIR}/d3d12_xs_pe.c"
                -o "${CMAKE_CURRENT_BINARY_DIR}/d3d12_xs_pe.obj"
        )

        if(SCARLETT_D3D12_LLVM_NM AND Python3_FOUND)
            add_test(
                NAME d3d12_xs_pe64_nm_isolation
                COMMAND ${Python3_EXECUTABLE} -c
                    "import subprocess, sys; out = subprocess.check_output([sys.argv[1], '-u', sys.argv[2]], text=True); syms = {line.strip().split()[-1] for line in out.splitlines() if line.strip()}; allowed = {'__wine_init_unix_call', '__wine_unix_call_dispatcher', '__wine_unixlib_handle'}; extra = syms - allowed; assert not extra and syms == allowed, f'unexpected symbols: {syms}'; print('PE64 llvm-nm -u OK:', sorted(syms))"
                    "${SCARLETT_D3D12_LLVM_NM}"
                    "${CMAKE_CURRENT_BINARY_DIR}/d3d12_xs_pe.obj"
            )
            set_tests_properties(d3d12_xs_pe64_nm_isolation PROPERTIES
                DEPENDS d3d12_xs_pe64_freestanding
            )
        endif()
    endif()

    if(SCARLETT_D3D12_LLVM_DLLTOOL)
        add_test(
            NAME d3d12_xs_def_dlltool
            COMMAND ${SCARLETT_D3D12_LLVM_DLLTOOL}
                -m i386:x86-64
                -d "${CMAKE_CURRENT_SOURCE_DIR}/d3d12_xs.def"
                -l "${CMAKE_CURRENT_BINARY_DIR}/libd3d12_xs.a"
        )
    endif()
endif()
