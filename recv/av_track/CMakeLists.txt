cmake_minimum_required(VERSION 3.10)
project(WebRTCReceiver)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find pkg-config
find_package(PkgConfig REQUIRED)

# Find FFmpeg libraries
pkg_check_modules(FFMPEG REQUIRED
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
)

# Find V4L2 (for v4l2loopback output)
pkg_check_modules(LIBV4L2 REQUIRED libv4l2)

# Find PulseAudio
pkg_check_modules(PULSEAUDIO REQUIRED libpulse libpulse-simple)

# Find libdatachannel
find_library(DATACHANNEL_LIBRARY
    NAMES datachannel
    PATHS /usr/local/lib /usr/lib
)

find_path(DATACHANNEL_INCLUDE_DIR
    NAMES rtc/rtc.h
    PATHS /usr/local/include /usr/include
)

# Output dependency info for debugging
message(STATUS "Found FFmpeg: ${FFMPEG_LIBRARIES}")
message(STATUS "Found V4L2: ${LIBV4L2_LIBRARIES}")
message(STATUS "Found PulseAudio: ${PULSEAUDIO_LIBRARIES}")
message(STATUS "Found datachannel: ${DATACHANNEL_LIBRARY}")

# Main executable
add_executable(webrtc_receiver
    src/main.cpp
    src/webrtc_receiver.cpp
    src/signaling_client.cpp
    src/video_pipeline.cpp
    src/audio_pipeline.cpp
    src/v4l2_output.cpp
    src/pulse_audio_output.cpp
    src/config_parser.cpp
    src/latency_tracker.cpp
)

# Include directories
target_include_directories(webrtc_receiver PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
    ${LIBV4L2_INCLUDE_DIRS}
    ${PULSEAUDIO_INCLUDE_DIRS}
    ${DATACHANNEL_INCLUDE_DIR}
    include
)

# Link libraries
target_link_libraries(webrtc_receiver
    ${FFMPEG_LIBRARIES}
    ${LIBV4L2_LIBRARIES}
    ${PULSEAUDIO_LIBRARIES}
    ${DATACHANNEL_LIBRARY}
    ssl
    crypto
    pthread
)

# Compiler flags for better performance
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(webrtc_receiver PRIVATE -Wall -Wextra -O2)
endif()
