find_package(Python3 COMPONENTS Interpreter REQUIRED)

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
