add_library(scarlett_xdl_scaffold STATIC
    xdl_scaffold.cpp
)
target_include_directories(scarlett_xdl_scaffold PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
