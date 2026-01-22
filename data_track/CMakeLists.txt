cmake_minimum_required(VERSION 3.10)
project(WebRTCPublisher)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find pkg-config
find_package(PkgConfig REQUIRED)

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

# Add executable
add_executable(webrtc_publisher
        src/4g_tty.cpp
        src/getopt.cpp
        src/main.cpp
        src/parse_cl.cpp
        src/rc_client.cpp
        src/motor_controller.cpp
        src/system_monitor.cpp
        src/uart_motor_driver.cpp
        src/crsf_motor_driver.cpp
        src/message_handler.cpp
)

# Include directories
target_include_directories(webrtc_publisher PRIVATE
        ${LIBDATACHANNEL_INCLUDE_DIR}
        include
)

# Link libraries
target_link_libraries(webrtc_publisher
        ${LIBDATACHANNEL_LIBRARY}
        ssl
        crypto
)

target_link_libraries(webrtc_publisher
        pthread
)

# C++17 is already set globally, but you can also set it per target
target_compile_features(webrtc_publisher PRIVATE cxx_std_17)