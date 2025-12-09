cmake_minimum_required(VERSION 3.10)
project(WebRTCPublisher)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find pkg-config
find_package(PkgConfig REQUIRED)

# Find libav libraries - SINGLE call with all required components
pkg_check_modules(LIBAV REQUIRED
        libavformat
        libavcodec
        libavutil
        libswscale
        libavdevice
        libswresample
)

# Find libdatachannel
find_library(LIBDATACHANNEL_LIBRARY
        NAMES datachannel
        PATHS /usr/local/lib
        NO_DEFAULT_PATH
        REQUIRED
)

find_path(LIBDATACHANNEL_INCLUDE_DIR
        NAMES rtc/rtc.h
        PATHS /usr/local/include
        NO_DEFAULT_PATH
        REQUIRED
)

# Find SDL2
pkg_check_modules(SDL2 REQUIRED sdl2)
# 输出详细信息用于调试
message(STATUS "Found FFmpeg libraries:")
message(STATUS "  LIBAV_LIBRARIES: ${LIBAV_LIBRARIES}")
message(STATUS "  LIBAV_INCLUDE_DIRS: ${LIBAV_INCLUDE_DIRS}")
message(STATUS "  LIBAV_LIBRARY_DIRS: ${LIBAV_LIBRARY_DIRS}")
message(STATUS "Found libdatachannel library: ${LIBDATACHANNEL_LIBRARY}")
message(STATUS "Found SDL2 libraries: ${SDL2_LIBRARIES}")

# Add executable
add_executable(webrtc_publisher
        src/main.cpp
        src/webrtc_publisher.cpp
        src/video_capturer.cpp
        src/audio_capturer.cpp
        src/audio_player.cpp
        src/capture.cpp
        src/opus_encoder.cpp
        src/opus_decoder.cpp
        src/debug_utils.cpp
        src/parse_cl.cpp
        src/getopt.cpp
        src/h264_encoder.cpp
)

# Include directories
target_include_directories(webrtc_publisher PRIVATE
        ${LIBAV_INCLUDE_DIRS}
        ${LIBDATACHANNEL_INCLUDE_DIR}
        ${SDL2_INCLUDE_DIRS}
        include
)

# Link libraries
target_link_libraries(webrtc_publisher
        ${LIBAV_LIBRARIES}
        ${LIBDATACHANNEL_LIBRARY}
        ${SDL2_LIBRARIES}
        ssl
        crypto
)

# Windows specific libraries
if (WIN32)
    target_link_libraries(webrtc_publisher
            ws2_32
            crypt32
    )
else ()
    target_link_libraries(webrtc_publisher
            pthread
    )
endif ()

# C++17 is already set globally, but you can also set it per target
target_compile_features(webrtc_publisher PRIVATE cxx_std_17)
