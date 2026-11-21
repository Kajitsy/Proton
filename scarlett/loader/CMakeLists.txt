add_library(scarlett_loader_scaffold STATIC
    loader_scaffold.cpp
)
target_include_directories(scarlett_loader_scaffold PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_options(scarlett_loader_scaffold PRIVATE -Wall -Wextra -Werror)

set(SCARLETT_LOADER_UNIX_SOURCES
    game_config.cpp
    xvd_ntfs.cpp
    vfs_cow.cpp
    loader_unix.cpp
)

add_library(scarlett_loader_unix_core STATIC
    ${SCARLETT_LOADER_UNIX_SOURCES}
)
set_target_properties(scarlett_loader_unix_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(scarlett_loader_unix_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_options(scarlett_loader_unix_core PRIVATE -Wall -Wextra -Werror)

if(SCARLETT_BUILD_UNIXLIB)
    add_library(loader_unix SHARED
        ${SCARLETT_LOADER_UNIX_SOURCES}
    )
    set_target_properties(loader_unix PROPERTIES
        PREFIX ""
        OUTPUT_NAME "loader_unix"
        C_VISIBILITY_PRESET default
        CXX_VISIBILITY_PRESET default
    )
    target_include_directories(loader_unix PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(loader_unix PRIVATE -Wall -Wextra -Werror)
    install(TARGETS loader_unix
        LIBRARY DESTINATION "${SCARLETT_INSTALL_UNIX_DIR}"
    )
endif()

add_library(scarlett_loader_pe_bridge STATIC
    xgameplatform_pe.c
    psm_appnotify_pe.c
)
target_include_directories(scarlett_loader_pe_bridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_options(scarlett_loader_pe_bridge PRIVATE -Wall -Wextra -Werror)

if(SCARLETT_BUILD_HOST_TESTS)
    enable_testing()
    add_executable(test_loader_vfs
        tests/test_loader_vfs.cpp
    )
    target_include_directories(test_loader_vfs PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(test_loader_vfs PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(test_loader_vfs PRIVATE
        scarlett_loader_pe_bridge
        scarlett_loader_unix_core
    )

    add_test(
        NAME loader_vfs_unit_tests
        COMMAND test_loader_vfs
    )

    find_program(SCARLETT_CLANG_EXE clang)
    if(SCARLETT_CLANG_EXE)
        add_test(
            NAME loader_pe64_xgameplatform_freestanding
            COMMAND ${SCARLETT_CLANG_EXE}
                --target=x86_64-w64-mingw32
                -std=c11
                -ffreestanding
                -Wall -Wextra -Werror
                "-I${CMAKE_SOURCE_DIR}/include"
                "-I${CMAKE_CURRENT_SOURCE_DIR}"
                -c "${CMAKE_CURRENT_SOURCE_DIR}/xgameplatform_pe.c"
                -o "${CMAKE_CURRENT_BINARY_DIR}/xgameplatform_pe.obj"
        )
        add_test(
            NAME loader_pe64_psm_appnotify_freestanding
            COMMAND ${SCARLETT_CLANG_EXE}
                --target=x86_64-w64-mingw32
                -std=c11
                -ffreestanding
                -Wall -Wextra -Werror
                "-I${CMAKE_SOURCE_DIR}/include"
                "-I${CMAKE_CURRENT_SOURCE_DIR}"
                -c "${CMAKE_CURRENT_SOURCE_DIR}/psm_appnotify_pe.c"
                -o "${CMAKE_CURRENT_BINARY_DIR}/psm_appnotify_pe.obj"
        )
    endif()
endif()
