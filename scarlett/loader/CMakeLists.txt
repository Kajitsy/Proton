add_library(scarlett_loader_scaffold STATIC
    loader_scaffold.cpp
)
target_include_directories(scarlett_loader_scaffold PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
