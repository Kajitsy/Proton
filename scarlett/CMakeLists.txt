cmake_minimum_required(VERSION 3.20)

project(proton_scarlett
    VERSION 11.0.100
    DESCRIPTION "Proton-Scarlett Xbox Series X|S Runtime and Translation Layer for Proton 11.0"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SCARLETT_BUILD_HOST_TESTS "Build Proton-Scarlett host verification and smoke tests" ON)
option(SCARLETT_BUILD_PE "Build Proton-Scarlett Windows PE64 DLL targets (x86_64-windows)" OFF)
option(SCARLETT_BUILD_UNIXLIB "Build Proton-Scarlett Linux native unixlib bridge targets (x86_64-unix)" ON)

set(SCARLETT_INSTALL_PE_DIR "${CMAKE_INSTALL_PREFIX}/lib/wine/scarlett/x86_64-windows"
    CACHE PATH "Installation directory for Proton-Scarlett PE64 DLLs")
set(SCARLETT_INSTALL_UNIX_DIR "${CMAKE_INSTALL_PREFIX}/lib/wine/scarlett/x86_64-unix"
    CACHE PATH "Installation directory for Proton-Scarlett native unixlib shared objects")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(xdl)
add_subdirectory(loader)
add_subdirectory(xmem)
add_subdirectory(rdna2_lifter)
add_subdirectory(d3d12_xs)
add_subdirectory(dstorage_xs)
add_subdirectory(xg_xs)
add_subdirectory(xgameruntime)
add_subdirectory(audio_media)
add_subdirectory(uwp_bridge)

# Mirror compile_commands.json into compile_commands/scarlett.json for IDE and CI tooling
add_custom_target(scarlett_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/compile_commands"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_BINARY_DIR}/compile_commands/scarlett.json"
    COMMENT "Publishing compile_commands/scarlett.json"
)

if(SCARLETT_BUILD_HOST_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
