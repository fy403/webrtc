cmake_minimum_required(VERSION 3.10)
project(CmdTrack)

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
add_executable(cmd_track
        src/main.cpp
        src/config_parser.cpp
        src/shell_executor.cpp
)

# Include directories
target_include_directories(cmd_track PRIVATE
        ${LIBDATACHANNEL_INCLUDE_DIR}
        include
)

# Link libraries
target_link_libraries(cmd_track
        ${LIBDATACHANNEL_LIBRARY}
        ssl
        crypto
)

target_link_libraries(cmd_track
        pthread
)

# libutil provides forkpty() on Linux
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(cmd_track util)
endif()

# C++17 is already set globally, but you can also set it per target
target_compile_features(cmd_track PRIVATE cxx_std_17)
