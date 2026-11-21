add_library(scarlett_xgameruntime_scaffold STATIC
    xgameruntime_scaffold.cpp
)
target_include_directories(scarlett_xgameruntime_scaffold PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
