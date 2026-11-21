add_library(scarlett_audio_media_scaffold STATIC
    audio_media_scaffold.cpp
)
target_include_directories(scarlett_audio_media_scaffold PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
