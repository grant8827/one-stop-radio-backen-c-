cmake_minimum_required(VERSION 3.16)
project(OneStopRadio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Find required packages
find_package(PkgConfig REQUIRED)

# FFmpeg libraries
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec>=58.0.0
    libavformat>=58.0.0 
    libavutil>=56.0.0
    libswresample>=3.0.0
    libswscale>=5.0.0
)

# Boost libraries (for Beast HTTP/WebSocket)
find_package(Boost REQUIRED COMPONENTS system filesystem thread)

# libshout for streaming
pkg_check_modules(SHOUT REQUIRED IMPORTED_TARGET shout>=2.4.0)

# PortAudio for real-time audio I/O
pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0>=19.0.0)

# libsndfile for audio file I/O
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile>=1.0.0)

# FFTW3 for FFT processing
pkg_check_modules(FFTW3F REQUIRED IMPORTED_TARGET fftw3f>=3.3.0)

# libsamplerate for sample rate conversion
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate>=0.1.0)

# Vorbis/Ogg libraries for OGG Vorbis encoding
pkg_check_modules(VORBIS REQUIRED IMPORTED_TARGET vorbis>=1.3.0)
pkg_check_modules(VORBISENC REQUIRED IMPORTED_TARGET vorbisenc>=1.3.0) 
pkg_check_modules(OGG REQUIRED IMPORTED_TARGET ogg>=1.3.0)

# Opus library for OGG Opus encoding
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus>=1.1.0)

# LAME for MP3 encoding
pkg_check_modules(LAME REQUIRED IMPORTED_TARGET lame)

# OpenSSL for secure connections
find_package(OpenSSL REQUIRED)

# SQLite3 for database operations
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3>=3.0.0)

# nlohmann/json for JSON parsing
find_package(nlohmann_json 3.2.0 REQUIRED)

# pthread
find_package(Threads REQUIRED)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Common source files
set(COMMON_SOURCES
    src/stream_manager.cpp
    src/webrtc_server.cpp
    src/http_server.cpp
    src/audio_encoder.cpp
    src/audio_system.cpp
    src/audio_stream_encoder.cpp
    src/video_stream_manager.cpp
    src/social_media_streamer.cpp
    src/config_manager.cpp
    src/radio_control.cpp
    src/database_manager.cpp
    src/logger.cpp
)

# Stream Controller specific sources
set(STREAM_CONTROLLER_SOURCES
    src/stream_controller.cpp
    src/stream_controller_api.cpp
    src/http_server.cpp
    src/logger.cpp
)

# Main server executable
add_executable(onestop-radio-server 
    src/main.cpp
    ${COMMON_SOURCES}
)

# Video API server executable  
add_executable(video-api-server
    src/video_api_server.cpp
    ${COMMON_SOURCES}
)

# Test server executable
add_executable(test-server
    src/test_server.cpp
    ${COMMON_SOURCES}
)

# Stream Controller API server executable
add_executable(stream-controller-api
    stream_controller_main.cpp
    ${STREAM_CONTROLLER_SOURCES}
)

# Common libraries for all executables
set(COMMON_LIBRARIES
    PkgConfig::FFMPEG
    PkgConfig::SHOUT
    PkgConfig::PORTAUDIO
    PkgConfig::SNDFILE
    PkgConfig::FFTW3F
    PkgConfig::SAMPLERATE
    PkgConfig::VORBIS
    PkgConfig::VORBISENC
    PkgConfig::OGG
    PkgConfig::OPUS
    PkgConfig::LAME
    PkgConfig::SQLITE3
    Boost::system
    Boost::filesystem
    Boost::thread
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    m
)

# Stream Controller specific libraries
set(STREAM_CONTROLLER_LIBRARIES
    PkgConfig::SHOUT
    Boost::system
    Boost::filesystem  
    Boost::thread
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    nlohmann_json::nlohmann_json
    m
)

# Link libraries to executables
target_link_libraries(onestop-radio-server ${COMMON_LIBRARIES})
target_link_libraries(video-api-server ${COMMON_LIBRARIES})
target_link_libraries(test-server ${COMMON_LIBRARIES})
target_link_libraries(stream-controller-api ${STREAM_CONTROLLER_LIBRARIES})

# Install targets
install(TARGETS onestop-radio-server video-api-server test-server stream-controller-api
    RUNTIME DESTINATION bin
)

# Copy configuration files
install(FILES config/server.json
    DESTINATION etc/onestop-radio
)