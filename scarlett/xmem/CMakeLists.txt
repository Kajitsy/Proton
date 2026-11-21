add_library(scarlett_xmem_scaffold STATIC
    xmem_scaffold.cpp
)
target_include_directories(scarlett_xmem_scaffold PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
