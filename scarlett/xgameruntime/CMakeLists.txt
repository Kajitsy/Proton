# Phase 0 smoke-test compatibility target (required by scarlett/tests/test_phase0_scaffold.cpp)
add_library(scarlett_xgameruntime_scaffold STATIC
    xgameruntime_scaffold.cpp
)
target_include_directories(scarlett_xgameruntime_scaffold PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)

# Native Linux unixlib backend: xgameruntime_unix.so (x86_64-unix)
if(SCARLETT_BUILD_UNIXLIB)
    add_library(xgameruntime_unix SHARED
        xgr_task_queue.cpp
        xgr_system_platform.cpp
        xgr_gamesave.cpp
        xgr_network_services.cpp
        xgameruntime_unix_dispatch.cpp
    )
    set_target_properties(xgameruntime_unix PROPERTIES
        PREFIX ""
        OUTPUT_NAME "xgameruntime_unix"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_include_directories(xgameruntime_unix PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(xgameruntime_unix PRIVATE
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(xgameruntime_unix PRIVATE
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    install(TARGETS xgameruntime_unix
        LIBRARY DESTINATION "${SCARLETT_INSTALL_UNIX_DIR}"
    )

    # Host-linked PE64 C layer (exact PE shims routed via WINE_UNIX_CALL bridge)
    add_library(scarlett_xgameruntime_pe_host STATIC
        xgameruntime_pe.c
        libHttpClient_pe.c
        XCurl_pe.c
    )
    target_include_directories(scarlett_xgameruntime_pe_host PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(scarlett_xgameruntime_pe_host PRIVATE
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(scarlett_xgameruntime_pe_host PUBLIC
        xgameruntime_unix
    )
endif()

# Windows PE64 DLL targets: xgameruntime.dll, libHttpClient.dll, XCurl.dll (x86_64-windows)
if(SCARLETT_BUILD_PE)
    add_library(xgameruntime SHARED
        xgameruntime_pe.c
        xgameruntime.def
    )
    set_target_properties(xgameruntime PROPERTIES PREFIX "" SUFFIX ".dll")
    target_include_directories(xgameruntime PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(xgameruntime PRIVATE -Wall -Wextra -Werror)

    add_library(libHttpClient SHARED
        libHttpClient_pe.c
        libHttpClient.def
    )
    set_target_properties(libHttpClient PROPERTIES PREFIX "" SUFFIX ".dll")
    target_include_directories(libHttpClient PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(libHttpClient PRIVATE -Wall -Wextra -Werror)

    add_library(XCurl SHARED
        XCurl_pe.c
        XCurl.def
    )
    set_target_properties(XCurl PROPERTIES PREFIX "" SUFFIX ".dll")
    target_include_directories(XCurl PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(XCurl PRIVATE -Wall -Wextra -Werror)

    install(TARGETS xgameruntime libHttpClient XCurl
        RUNTIME DESTINATION "${SCARLETT_INSTALL_PE_DIR}"
    )
endif()

# Comprehensive GDK Runtime unit tests
if(SCARLETT_BUILD_HOST_TESTS AND SCARLETT_BUILD_UNIXLIB)
    enable_testing()
    add_executable(test_xgameruntime
        tests/test_xgameruntime.cpp
    )
    add_dependencies(test_xgameruntime scarlett_xdl_generate_schemas)
    set_target_properties(test_xgameruntime PROPERTIES ENABLE_EXPORTS ON)
    target_include_directories(test_xgameruntime PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/xdl/include
        ${CMAKE_BINARY_DIR}/xdl/generated
    )
    target_compile_options(test_xgameruntime PRIVATE
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(test_xgameruntime PRIVATE
        scarlett_xgameruntime_pe_host
        xgameruntime_unix
        scarlett_xdl_runtime
        Threads::Threads
    )
    add_test(
        NAME xgameruntime_unit_tests
        COMMAND test_xgameruntime
    )
endif()
