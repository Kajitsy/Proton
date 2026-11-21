add_library(scarlett_dstorage_xs_scaffold STATIC
    dstorage_xs_scaffold.cpp
)
target_include_directories(scarlett_dstorage_xs_scaffold PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
