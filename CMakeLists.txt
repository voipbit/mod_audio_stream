# SPDX-License-Identifier: MIT

# ============================================================================
# mod_audio_stream - FreeSWITCH WebSocket Audio Streaming Module
# ============================================================================
#
# This CMake configuration builds mod_audio_stream as a standalone FreeSWITCH
# module. The module provides real-time audio streaming over WebSocket
# connections with support for bidirectional communication.
#
# Requirements:
# - CMake 3.15 or higher
# - FreeSWITCH development headers and libraries
# - libwebsockets with client support
# - speexdsp for audio resampling
# - g711 codec library (typically from spandsp or system)
# - C/C++ toolchain with C++11 support
#
# Build Instructions:
#   mkdir build && cd build
#   cmake -DFREESWITCH_INCLUDE_DIR=/path/to/freeswitch/headers ..
#   make -j$(nproc)
#
# Installation:
#   make install
# ============================================================================

cmake_minimum_required(VERSION 3.15)
project(voipbit_audio_stream 
        VERSION 2.0.0
        DESCRIPTION "VoipBit Enhanced Audio Streaming Module for FreeSWITCH"
        LANGUAGES C CXX)

# ============================================================================
# Build Configuration
# ============================================================================

# Use C++14 standard for advanced features (required for new components)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable position-independent code for shared library
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ============================================================================
# Configuration Options
# ============================================================================

# FreeSWITCH SDK configuration options
# These are used when pkg-config cannot automatically locate FreeSWITCH
set(FREESWITCH_INCLUDE_DIR "" CACHE PATH 
    "Path to FreeSWITCH headers directory (must contain switch.h)")
set(FREESWITCH_SOURCE_INCLUDE_DIR "" CACHE PATH 
    "Path to FreeSWITCH source headers directory (contains g711.h, etc.)")
set(FREESWITCH_LIBRARY "" CACHE FILEPATH 
    "Path to libfreeswitch shared library (.so/.dylib/.dll)")

# ============================================================================
# Dependency Discovery
# ============================================================================

find_package(PkgConfig REQUIRED)

# Find libwebsockets library (required for WebSocket client functionality)
pkg_check_modules(LIBWEBSOCKETS REQUIRED libwebsockets)
if(NOT LIBWEBSOCKETS_FOUND)
    message(FATAL_ERROR "libwebsockets not found. Install libwebsockets development package.")
endif()

# Find speexdsp library (required for audio resampling)
pkg_check_modules(SPEEXDSP REQUIRED speexdsp)
if(NOT SPEEXDSP_FOUND)
    message(FATAL_ERROR "speexdsp not found. Install speexdsp development package.")
endif()

# Find OpenSSL library (required for security features)
find_package(OpenSSL REQUIRED)
if(NOT OPENSSL_FOUND)
    message(FATAL_ERROR "OpenSSL not found. Install OpenSSL development package.")
endif()

# Find optional libraries for advanced features
# Note: These are optional - the module will build with reduced functionality if not found
pkg_check_modules(OPUS opus)
pkg_check_modules(JWT_CPP QUIET jwt-cpp)

# Threading support
find_package(Threads REQUIRED)

# ============================================================================
# FreeSWITCH SDK Discovery
# ============================================================================

# Try to discover FreeSWITCH installation via pkg-config first
# Some Linux distributions provide a 'freeswitch.pc' file for easy discovery
set(FREESWITCH_FOUND_BY_PKG FALSE)
pkg_check_modules(FREESWITCH_PC QUIET freeswitch)

if(FREESWITCH_PC_FOUND)
  message(STATUS "Found FreeSWITCH via pkg-config")
  set(FREESWITCH_FOUND_BY_PKG TRUE)
  set(FREESWITCH_INCLUDE_DIR ${FREESWITCH_PC_INCLUDE_DIRS})
  # The library is typically named libfreeswitch and found via -lfreeswitch
else()
  message(STATUS "FreeSWITCH pkg-config not found, using manual configuration")
endif()

if(NOT FREESWITCH_INCLUDE_DIR)
  message(FATAL_ERROR "FREESWITCH_INCLUDE_DIR is not set and freeswitch.pc was not found. Set -DFREESWITCH_INCLUDE_DIR=/path/to/include")
endif()

# If no pkg-config for freeswitch, user must specify FREESWITCH_LIBRARY
set(FREESWITCH_LIBRARIES "")
if(FREESWITCH_FOUND_BY_PKG)
  list(APPEND FREESWITCH_LIBRARIES freeswitch)
else()
  if(NOT FREESWITCH_LIBRARY)
    message(FATAL_ERROR "FREESWITCH_LIBRARY is not set. Set -DFREESWITCH_LIBRARY=/path/to/libfreeswitch.(so|dylib)")
  endif()
  list(APPEND FREESWITCH_LIBRARIES ${FREESWITCH_LIBRARY})
endif()

# ============================================================================
# Module Target Definition
# ============================================================================

# Create the FreeSWITCH module as a shared library
# Note: MODULE type creates a plugin that can be loaded dynamically
add_library(mod_audio_stream MODULE
    # Main module implementation
    src/mod_audio_stream.c
    src/mod_audio_stream.h
    
    # WebSocket integration layer
    src/lws_glue.cpp
    src/lws_glue.h
    
    # Audio pipeline management
    src/audio_pipe.cpp
    src/audio_pipe.hpp
    
    # Utility functions and classes
    src/stream_utils.cpp
    src/stream_utils.hpp
    
    # Adaptive buffer system
    src/adaptive_buffer.hpp
    src/adaptive_buffer.cpp
    src/adaptive_buffer_wrapper.h
    src/adaptive_buffer_wrapper.cpp
    
    # Connection management
    src/connection_manager.hpp
    src/connection_manager.cpp
    
    # Advanced API definitions
    src/advanced_api.h
    
    # VoipBit stub implementations
    src/voipbit_stubs.c
)

# ============================================================================
# Module Properties and Configuration
# ============================================================================

# Configure module properties for FreeSWITCH compatibility
set_target_properties(mod_audio_stream PROPERTIES
    # Remove 'lib' prefix to match FreeSWITCH naming convention
    PREFIX ""
    OUTPUT_NAME "mod_audio_stream"
    
    # Ensure proper versioning
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Add debug information in debug builds
target_compile_options(mod_audio_stream PRIVATE
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O2>
)

# Includes
target_include_directories(mod_audio_stream PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${FREESWITCH_INCLUDE_DIR}
  ${LIBWEBSOCKETS_INCLUDE_DIRS}
  ${SPEEXDSP_INCLUDE_DIRS}
  ${OPENSSL_INCLUDE_DIR}
)

# Add optional includes if available
if(OPUS_FOUND)
  target_include_directories(mod_audio_stream PRIVATE ${OPUS_INCLUDE_DIRS})
  target_compile_definitions(mod_audio_stream PRIVATE HAVE_OPUS=1)
endif()

if(JWT_CPP_FOUND)
  target_include_directories(mod_audio_stream PRIVATE ${JWT_CPP_INCLUDE_DIRS})
  target_compile_definitions(mod_audio_stream PRIVATE HAVE_JWT_CPP=1)
endif()

# Add FreeSWITCH source include directory if provided
if(FREESWITCH_SOURCE_INCLUDE_DIR)
  target_include_directories(mod_audio_stream PRIVATE ${FREESWITCH_SOURCE_INCLUDE_DIR})
endif()

# Defines/flags inherited from pkg-config
target_compile_options(mod_audio_stream PRIVATE
  ${LIBWEBSOCKETS_CFLAGS_OTHER}
  ${SPEEXDSP_CFLAGS_OTHER}
)

# Link
target_link_libraries(mod_audio_stream PRIVATE
  ${FREESWITCH_LIBRARIES}
  ${LIBWEBSOCKETS_LIBRARIES}
  ${SPEEXDSP_LIBRARIES}
  OpenSSL::SSL
  OpenSSL::Crypto
  Threads::Threads
)

# Link optional libraries if available
if(OPUS_FOUND)
  target_link_libraries(mod_audio_stream PRIVATE ${OPUS_LIBRARIES})
endif()

if(JWT_CPP_FOUND)
  target_link_libraries(mod_audio_stream PRIVATE ${JWT_CPP_LIBRARIES})
endif()

# ============================================================================
# Installation Configuration
# ============================================================================

# Configure installation paths for FreeSWITCH modules
# Default: /usr/lib/freeswitch/mod (can be overridden)
set(FS_MOD_DIR "lib/freeswitch/mod" CACHE PATH 
    "Installation directory for FreeSWITCH modules (relative to CMAKE_INSTALL_PREFIX)")

# Install the compiled module
install(TARGETS mod_audio_stream 
        LIBRARY DESTINATION ${FS_MOD_DIR}
        COMPONENT Runtime)

# Optionally install configuration samples
install(FILES conf/audio_stream.conf.xml.sample
        DESTINATION etc/freeswitch/autoload_configs
        COMPONENT Configuration
        OPTIONAL)

# Install advanced configuration if it exists
install(FILES conf/audio_stream_advanced.conf.xml
        DESTINATION etc/freeswitch/autoload_configs
        COMPONENT Configuration
        OPTIONAL)

# Display installation information
message(STATUS "")
message(STATUS "Installation Summary:")
message(STATUS "  Module will be installed to: ${CMAKE_INSTALL_PREFIX}/${FS_MOD_DIR}")
message(STATUS "  Configuration sample: ${CMAKE_INSTALL_PREFIX}/etc/freeswitch/autoload_configs")
message(STATUS "")
message(STATUS "After installation, add the following to FreeSWITCH modules.conf.xml:")
message(STATUS "  <load module=\"mod_audio_stream\"/>")
message(STATUS "")
