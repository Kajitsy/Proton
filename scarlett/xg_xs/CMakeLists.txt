add_library(scarlett_xg_xs_scaffold STATIC
    xg_xs_scaffold.cpp
)
target_include_directories(scarlett_xg_xs_scaffold PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
