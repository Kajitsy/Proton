find_package(Threads REQUIRED)

set(SCARLETT_RDNA2_LIFTER_SOURCES
    rdna2_containers.cpp
    rdna2_decoder.cpp
    rdna2_ir.cpp
    rdna2_spirv_emitter.cpp
    rdna2_microprobes.cpp
    rdna2_spv_eval.cpp
    rdna2_lifter.cpp
)

add_library(scarlett_rdna2_lifter STATIC
    ${SCARLETT_RDNA2_LIFTER_SOURCES}
)
set_target_properties(scarlett_rdna2_lifter PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(scarlett_rdna2_lifter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)
target_compile_options(scarlett_rdna2_lifter PRIVATE
    -Wall
    -Wextra
    -Werror
)
target_link_libraries(scarlett_rdna2_lifter PUBLIC
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

if(SCARLETT_BUILD_UNIXLIB)
    add_library(rdna2_lifter_unix SHARED
        ${SCARLETT_RDNA2_LIFTER_SOURCES}
    )
    set_target_properties(rdna2_lifter_unix PROPERTIES
        PREFIX ""
        OUTPUT_NAME "rdna2_lifter_unix"
        CXX_VISIBILITY_PRESET default
    )
    target_include_directories(rdna2_lifter_unix PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    )
    target_compile_options(rdna2_lifter_unix PRIVATE
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(rdna2_lifter_unix PRIVATE
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    install(TARGETS rdna2_lifter_unix
        LIBRARY DESTINATION "${SCARLETT_INSTALL_UNIX_DIR}"
    )
endif()

if(SCARLETT_BUILD_HOST_TESTS)
    enable_testing()

    add_executable(test_rdna2_lifter
        tests/test_rdna2_lifter.cpp
    )
    target_include_directories(test_rdna2_lifter PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    )
    target_compile_options(test_rdna2_lifter PRIVATE
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(test_rdna2_lifter PRIVATE
        scarlett_rdna2_lifter
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    add_test(
        NAME rdna2_lifter_unit_tests
        COMMAND test_rdna2_lifter
    )

    add_executable(test_rdna2_corpus
        tests/test_rdna2_corpus.cpp
    )
    target_include_directories(test_rdna2_corpus PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    )
    target_compile_options(test_rdna2_corpus PRIVATE
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(test_rdna2_corpus PRIVATE
        scarlett_rdna2_lifter
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    add_test(
        NAME rdna2_lifter_corpus_2048_exit_gate
        COMMAND test_rdna2_corpus
    )

    add_test(
        NAME rdna2_lifter_dropin_check
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/vkd3d_proton_dropin/apply_to_dxil_spirv.sh" --check
    )
    add_test(
        NAME rdna2_lifter_dropin_dry_run
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/vkd3d_proton_dropin/apply_to_dxil_spirv.sh" --dry-run
    )
endif()
