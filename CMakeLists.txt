cmake_minimum_required(VERSION 3.16)
project(HostworkshopMaps)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find BakkesMod SDK - check multiple common locations
if(NOT DEFINED BAKKESMOD_SDK_PATH)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bakkesmodsdk")
        set(BAKKESMOD_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/bakkesmodsdk")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/BakkesModSDK")
        set(BAKKESMOD_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/BakkesModSDK")
    elseif(EXISTS "$ENV{APPDATA}/bakkesmod/bakkesmod/bakkesmodsdk")
        set(BAKKESMOD_SDK_PATH "$ENV{APPDATA}/bakkesmod/bakkesmod/bakkesmodsdk")
    endif()
endif()

if(NOT BAKKESMOD_SDK_PATH OR NOT EXISTS "${BAKKESMOD_SDK_PATH}")
    message(FATAL_ERROR "BakkesMod SDK not found. Please clone it to 'bakkesmodsdk' folder or set -DBAKKESMOD_SDK_PATH=/path/to/sdk")
endif()

message(STATUS "Using BakkesMod SDK at: ${BAKKESMOD_SDK_PATH}")

add_library(HostworkshopMaps SHARED
    src/HostworkshopMaps.cpp
    src/HostworkshopMaps.h
)

target_include_directories(HostworkshopMaps PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${BAKKESMOD_SDK_PATH}/include"
)

target_link_directories(HostworkshopMaps PRIVATE
    "${BAKKESMOD_SDK_PATH}/lib"
)

target_link_libraries(HostworkshopMaps PRIVATE pluginsdk)
