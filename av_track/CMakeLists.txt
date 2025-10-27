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

message(STATUS "Found libdatachannel library: ${LIBDATACHANNEL_LIBRARY}")
message(STATUS "Found libav libraries: ${LIBAV_LIBRARIES}")

# Add executable
add_executable(webrtc_publisher
    src/main.cpp
    src/webrtc_publisher.cpp
    src/video_capturer.cpp
    src/audio_capturer.cpp
    src/capture.cpp
    src/encoder.cpp
    src/opus_encoder.cpp
    src/debug_utils.cpp
    src/parse_cl.cpp
    src/getopt.cpp
)

# Include directories
target_include_directories(webrtc_publisher PRIVATE
    ${LIBAV_INCLUDE_DIRS}
    ${LIBDATACHANNEL_INCLUDE_DIR}
    include
)

# Link libraries
target_link_libraries(webrtc_publisher
    ${LIBAV_LIBRARIES}
    ${LIBDATACHANNEL_LIBRARY}
    ssl
    crypto
)

# Windows specific libraries
if(WIN32)
    target_link_libraries(webrtc_publisher
        ws2_32
        crypt32
    )
else()
    target_link_libraries(webrtc_publisher
        pthread
    )
endif()

# C++17 is already set globally, but you can also set it per target
target_compile_features(webrtc_publisher PRIVATE cxx_std_17)