set(CRT_APP_TARGETS
    VCRUNTIME140_APP
    VCRUNTIME140_1_APP
    MSVCP140_APP
    MSVCP140_1_APP
    MSVCP140_2_APP
    CONCRT140_APP
    vccorlib140_app
)

if(SCARLETT_BUILD_PE)
    add_library(scarlett_crt_app_runtime STATIC
        crt_app_runtime.c
    )
    target_include_directories(scarlett_crt_app_runtime PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(scarlett_crt_app_runtime PRIVATE
        -Wall -Wextra -Werror -ffreestanding -fno-builtin
    )

    foreach(dll_name ${CRT_APP_TARGETS})
        add_library(${dll_name} MODULE
            ${dll_name}.c
            ${dll_name}.def
        )
        set_target_properties(${dll_name} PROPERTIES
            PREFIX ""
            SUFFIX ".dll"
            OUTPUT_NAME "${dll_name}"
        )
        target_include_directories(${dll_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/include
        )
        target_compile_options(${dll_name} PRIVATE
            -Wall -Wextra -Werror -ffreestanding -fno-builtin
        )
        target_link_libraries(${dll_name} PRIVATE
            scarlett_crt_app_runtime
            kernel32
        )
        target_link_options(${dll_name} PRIVATE
            -nostdlib -Wl,-e,DllMain
        )
        install(TARGETS ${dll_name}
            LIBRARY DESTINATION "${SCARLETT_INSTALL_PE_DIR}"
            RUNTIME DESTINATION "${SCARLETT_INSTALL_PE_DIR}"
        )
    endforeach()
endif()

if(SCARLETT_BUILD_HOST_TESTS)
    find_package(Threads REQUIRED)
    add_executable(test_crt_app_runtime
        crt_app_runtime.c
        tests/test_crt_app_runtime.c
    )
    target_include_directories(test_crt_app_runtime PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(test_crt_app_runtime PRIVATE
        -Wall -Wextra -Werror
    )
    target_link_libraries(test_crt_app_runtime PRIVATE
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m
    )
    add_test(NAME uwp_crt_app_runtime_tests COMMAND test_crt_app_runtime)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME uwp_crt_imports_audit
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_crt_imports_audit.py"
        )
    endif()
endif()
