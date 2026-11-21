# Phase 0 smoke-test compatibility target (required by scarlett/tests/test_phase0_scaffold.cpp)
add_library(scarlett_xmem_scaffold STATIC
    xmem_scaffold.cpp
)
target_include_directories(scarlett_xmem_scaffold PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)

# Native Linux unixlib backend: xmem_unix.so (x86_64-unix)
if(SCARLETT_BUILD_UNIXLIB)
    add_library(xmem_unix SHARED
        xmem_phys_virt.cpp
        xmem_uma_bridge.cpp
        xmem_thread_sync.cpp
        xmem_unix_dispatch.cpp
    )
    set_target_properties(xmem_unix PROPERTIES
        PREFIX ""
        OUTPUT_NAME "xmem_unix"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_include_directories(xmem_unix PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(xmem_unix PRIVATE
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(xmem_unix PRIVATE
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    install(TARGETS xmem_unix
        LIBRARY DESTINATION "${SCARLETT_INSTALL_UNIX_DIR}"
    )

    # Host-linked PE64 C layer (exact xmem_pe.c / kernelx_pe.c routed via WINE_UNIX_CALL bridge)
    add_library(scarlett_xmem_pe_host STATIC
        xmem_pe.c
        kernelx_pe.c
    )
    target_include_directories(scarlett_xmem_pe_host PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(scarlett_xmem_pe_host PRIVATE
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(scarlett_xmem_pe_host PUBLIC
        xmem_unix
    )
endif()

# Windows PE64 DLL targets: xmem.dll & kernelx.dll (x86_64-windows)
if(SCARLETT_BUILD_PE)
    add_library(xmem SHARED
        xmem_pe.c
        xmem.def
    )
    set_target_properties(xmem PROPERTIES PREFIX "" SUFFIX ".dll")
    target_include_directories(xmem PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(xmem PRIVATE -Wall -Wextra -Werror)

    add_library(kernelx SHARED
        kernelx_pe.c
        kernelx.def
    )
    set_target_properties(kernelx PROPERTIES PREFIX "" SUFFIX ".dll")
    target_include_directories(kernelx PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(kernelx PRIVATE -Wall -Wextra -Werror)

    install(TARGETS xmem kernelx
        RUNTIME DESTINATION "${SCARLETT_INSTALL_PE_DIR}"
    )
endif()

# Comprehensive XMem / KernelX / UMA Bridge unit tests
if(SCARLETT_BUILD_HOST_TESTS AND SCARLETT_BUILD_UNIXLIB)
    enable_testing()
    add_executable(test_xmem_uma
        tests/test_xmem_uma.cpp
    )
    target_include_directories(test_xmem_uma PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(test_xmem_uma PRIVATE
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(test_xmem_uma PRIVATE
        scarlett_xmem_pe_host
        xmem_unix
        Threads::Threads
    )
    add_test(
        NAME xmem_uma_unit_tests
        COMMAND test_xmem_uma
    )
endif()
