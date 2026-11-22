add_executable(test_uwphost
    test_uwphost.c
    ../uwphost_log.c
    ../uwphost_winstring.c
    ../uwphost_manifest.c
    ../uwphost_events.c
    ../uwphost_corewindow.c
    ../uwphost_coreapp.c
    ../uwphost_main.c
)
target_include_directories(test_uwphost PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../include"
)
target_compile_options(test_uwphost PRIVATE -Wall -Wextra -Werror)
target_link_libraries(test_uwphost PRIVATE Threads::Threads)
add_test(NAME uwphost_unit_tests COMMAND test_uwphost)
