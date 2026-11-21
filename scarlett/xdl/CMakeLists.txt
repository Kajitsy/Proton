enable_language(ASM)

# Phase 0 scaffold library (preserved for phase0_cpp_scaffold_smoke compatibility)
add_library(scarlett_xdl_scaffold STATIC
    xdl_scaffold.cpp
)
target_include_directories(scarlett_xdl_scaffold PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# XDL C++20 runtime & x86-64 Win64 diagnostic auto-stub trampoline library
add_library(scarlett_xdl_runtime STATIC
    src/stub_thunk_runtime.cpp
    src/stub_trampoline_x86_64.S
)
target_include_directories(scarlett_xdl_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_options(scarlett_xdl_runtime PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Werror>
)
if(UNIX AND NOT APPLE)
    target_link_libraries(scarlett_xdl_runtime PUBLIC ${CMAKE_DL_LIBS})
endif()

# Host IDL compiler: scarlett-xdl (C++20)
add_executable(scarlett-xdl
    src/lexer.cpp
    src/parser.cpp
    src/layout.cpp
    src/codegen_cpp.cpp
    src/codegen_def.cpp
    src/main.cpp
)
target_include_directories(scarlett-xdl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_options(scarlett-xdl PRIVATE
    -Wall -Wextra -Werror
)

# Code generation for all 6 starter Xbox Scarlett Game OS 2020..2026 schemas
set(SCARLETT_XDL_SCHEMAS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/schemas")
set(SCARLETT_XDL_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(SCARLETT_XDL_TMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/tmp_tests")

set(SCARLETT_XDL_SCHEMA_SPECS
    "d3d12_xs:d3d12_xs"
    "dstorage_xs:dstorage_xs"
    "xg_xs:xg_xs"
    "xgameruntime:xgameruntime"
    "xaudio2_9_xs:xaudio2_9"
    "gameinput_xs:GameInput"
)

set(SCARLETT_XDL_GENERATED_FILES "")
foreach(spec IN LISTS SCARLETT_XDL_SCHEMA_SPECS)
    string(REPLACE ":" ";" parts "${spec}")
    list(GET parts 0 mod_name)
    list(GET parts 1 def_stem)

    set(in_xdl "${SCARLETT_XDL_SCHEMAS_DIR}/${mod_name}.xdl")
    set(out_hpp "${SCARLETT_XDL_GEN_DIR}/${mod_name}_xdl.hpp")
    set(out_def "${SCARLETT_XDL_GEN_DIR}/${def_stem}.def")

    add_custom_command(
        OUTPUT "${out_hpp}" "${out_def}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SCARLETT_XDL_GEN_DIR}"
        COMMAND $<TARGET_FILE:scarlett-xdl> "${in_xdl}"
                --emit-header "${out_hpp}"
                --emit-def "${out_def}"
                --target-abi 10.0.26200.0
                --console-model anaconda
        DEPENDS scarlett-xdl "${in_xdl}"
        COMMENT "scarlett-xdl: generating ${mod_name}_xdl.hpp and ${def_stem}.def"
        VERBATIM
    )
    list(APPEND SCARLETT_XDL_GENERATED_FILES "${out_hpp}" "${out_def}")
endforeach()

add_custom_target(scarlett_xdl_generate_schemas ALL
    DEPENDS ${SCARLETT_XDL_GENERATED_FILES}
)

if(SCARLETT_BUILD_HOST_TESTS)
    enable_testing()

    # 1. Parser, validator, negative diagnostics, and .def generator tests
    add_executable(test_xdl_parser_codegen
        tests/test_xdl_parser_codegen.cpp
    )
    add_dependencies(test_xdl_parser_codegen scarlett-xdl scarlett_xdl_generate_schemas)
    target_include_directories(test_xdl_parser_codegen PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SCARLETT_XDL_GEN_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_definitions(test_xdl_parser_codegen PRIVATE
        SCARLETT_XDL_BIN="$<TARGET_FILE:scarlett-xdl>"
        SCARLETT_XDL_SCHEMAS_DIR="${SCARLETT_XDL_SCHEMAS_DIR}"
        SCARLETT_XDL_GEN_DIR="${SCARLETT_XDL_GEN_DIR}"
        SCARLETT_XDL_TMP_DIR="${SCARLETT_XDL_TMP_DIR}"
    )
    target_compile_options(test_xdl_parser_codegen PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(test_xdl_parser_codegen PRIVATE scarlett_xdl_runtime)
    add_test(NAME xdl_parser_codegen COMMAND test_xdl_parser_codegen)

    # 2. Compile & static_assert verification of all 6 generated C++20 headers (Game OS 2020..2026 + 0x585D070E)
    add_executable(test_xdl_schemas_compile
        tests/test_xdl_schemas_compile.cpp
    )
    add_dependencies(test_xdl_schemas_compile scarlett_xdl_generate_schemas)
    target_include_directories(test_xdl_schemas_compile PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SCARLETT_XDL_GEN_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_definitions(test_xdl_schemas_compile PRIVATE
        SCARLETT_XDL_BIN="$<TARGET_FILE:scarlett-xdl>"
        SCARLETT_XDL_SCHEMAS_DIR="${SCARLETT_XDL_SCHEMAS_DIR}"
        SCARLETT_XDL_GEN_DIR="${SCARLETT_XDL_GEN_DIR}"
        SCARLETT_XDL_TMP_DIR="${SCARLETT_XDL_TMP_DIR}"
    )
    target_compile_options(test_xdl_schemas_compile PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(test_xdl_schemas_compile PRIVATE scarlett_xdl_runtime)
    add_test(NAME xdl_schemas_compile COMMAND test_xdl_schemas_compile)

    # 3. Runtime Win64 register ABI QueryApiImpl, 32-slot vtables & diagnostic auto-stub thunk (normal + STRICT=1)
    add_executable(test_xdl_stub_thunk
        tests/test_xdl_stub_thunk.cpp
    )
    add_dependencies(test_xdl_stub_thunk scarlett_xdl_generate_schemas)
    set_target_properties(test_xdl_stub_thunk PROPERTIES ENABLE_EXPORTS ON)
    target_include_directories(test_xdl_stub_thunk PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SCARLETT_XDL_GEN_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_definitions(test_xdl_stub_thunk PRIVATE
        SCARLETT_XDL_BIN="$<TARGET_FILE:scarlett-xdl>"
        SCARLETT_XDL_SCHEMAS_DIR="${SCARLETT_XDL_SCHEMAS_DIR}"
        SCARLETT_XDL_GEN_DIR="${SCARLETT_XDL_GEN_DIR}"
        SCARLETT_XDL_TMP_DIR="${SCARLETT_XDL_TMP_DIR}"
    )
    target_compile_options(test_xdl_stub_thunk PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(test_xdl_stub_thunk PRIVATE scarlett_xdl_runtime)
    add_test(NAME xdl_stub_thunk COMMAND test_xdl_stub_thunk)
endif()
