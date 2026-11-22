cmake_minimum_required(VERSION 3.20)
project(scarlett_uwp_winrt_ui C CXX)

set(UWP_WINRT_UI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/winrt_ui_common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/winrt_display_info.c
    ${CMAKE_CURRENT_SOURCE_DIR}/winrt_app_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/winrt_sys_nav.c
    ${CMAKE_CURRENT_SOURCE_DIR}/winrt_core_cursor_mouse.c
    ${CMAKE_CURRENT_SOURCE_DIR}/winrt_display_request_hdmi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/winrt_api_info.c
)

set(UWP_WINRT_UI_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_library(uwp_winrt_ui OBJECT ${UWP_WINRT_UI_SOURCES})
target_include_directories(uwp_winrt_ui PUBLIC ${UWP_WINRT_UI_INCLUDE_DIRS})
set_target_properties(uwp_winrt_ui PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(uwp_winrt_ui PRIVATE /W4 /WX)
else()
    target_compile_options(uwp_winrt_ui PRIVATE -Wall -Wextra -Werror)
endif()
