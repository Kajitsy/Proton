set(WINRT_SYS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/winrt_sys_common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/winrt_sys_storage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/winrt_sys_package.c
    ${CMAKE_CURRENT_SOURCE_DIR}/winrt_sys_gamepad.c
    ${CMAKE_CURRENT_SOURCE_DIR}/winrt_sys_system_ui.c
    ${CMAKE_CURRENT_SOURCE_DIR}/winrt_sys_uwp_extras.c
)

add_library(scarlett_uwp_winrt_sys OBJECT ${WINRT_SYS_SOURCES})
target_include_directories(scarlett_uwp_winrt_sys PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_options(scarlett_uwp_winrt_sys PRIVATE -Wall -Wextra -Werror)

if(SCARLETT_BUILD_HOST_TESTS AND NOT WIN32)
    find_package(Threads REQUIRED)
    add_executable(test_uwp_winrt_sys
        ${WINRT_SYS_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/win32_host_shim.c
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_winrt_sys.c
    )
    target_include_directories(test_uwp_winrt_sys PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    target_compile_options(test_uwp_winrt_sys PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(test_uwp_winrt_sys PRIVATE Threads::Threads)
    add_test(NAME uwp_bridge_winrt_sys_unit_tests COMMAND test_uwp_winrt_sys)
endif()
