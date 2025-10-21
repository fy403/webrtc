cmake_minimum_required(VERSION 3.15)
project(WebRTCPublisher)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖包
find_package(PkgConfig REQUIRED)

# 查找 libdatachannel
pkg_check_modules(LIBDATACHANNEL libdatachannel)

if(LIBDATACHANNEL_FOUND)
    message(STATUS "Found libdatachannel via pkg-config")
else()
    # 手动查找 libdatachannel
    find_path(LIBDATACHANNEL_INCLUDE_DIRS 
        NAMES rtc/rtc.hpp
        PATHS /usr/local/include /usr/include
    )
    find_library(LIBDATACHANNEL_LIBRARIES 
        NAMES datachannel
        PATHS /usr/local/lib /usr/lib
    )
    
    if(LIBDATACHANNEL_INCLUDE_DIRS AND LIBDATACHANNEL_LIBRARIES)
        message(STATUS "Found libdatachannel manually")
    else()
        message(FATAL_ERROR "libdatachannel not found")
    endif()
endif()

# nlohmann/json
find_package(nlohmann_json REQUIRED)

# Boost (for boost::beast WebSocket)
find_package(Boost REQUIRED COMPONENTS system)

# 查找 FFmpeg
pkg_check_modules(FFMPEG REQUIRED 
    libavcodec 
    libavformat 
    libavdevice 
    libavutil 
    libswscale
)

if(FFMPEG_FOUND)
    message(STATUS "Found FFmpeg libraries:")
    message(STATUS "  Include directories: ${FFMPEG_INCLUDE_DIRS}")
    message(STATUS "  Libraries: ${FFMPEG_LIBRARIES}")
    message(STATUS "  Cflags: ${FFMPEG_CFLAGS}")
endif()

# 添加可执行文件
add_executable(webrtc_publisher webrtc_publisher.cpp)

# 包含目录
target_include_directories(webrtc_publisher PRIVATE
    ${LIBDATACHANNEL_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)

# 链接库
target_link_libraries(webrtc_publisher 
    ${LIBDATACHANNEL_LIBRARIES}
    Boost::system
    nlohmann_json::nlohmann_json
    ${FFMPEG_LIBRARIES}
    pthread
    ssl
    crypto
)

# 编译器标志
target_compile_options(webrtc_publisher PRIVATE 
    -Wall 
    -Wextra
    ${FFMPEG_CFLAGS_OTHER}
)

# 添加 FFmpeg 定义（如果需要）
target_compile_definitions(webrtc_publisher PRIVATE
    __STDC_CONSTANT_MACROS
)