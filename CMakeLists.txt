cmake_minimum_required(VERSION 3.15)
project(HostWorkshopMaps)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED BAKKESMOD_SDK)
    set(BAKKESMOD_SDK "$ENV{BAKKESMOD_SDK}" CACHE PATH "Path to BakkesMod SDK")
endif()

if(NOT EXISTS "${BAKKESMOD_SDK}/include/bakkesmod/plugin/bakkesmodplugin.h")
    message(FATAL_ERROR "BakkesMod SDK not found at '${BAKKESMOD_SDK}'.")
endif()

add_library(HostWorkshopMaps SHARED
    src/HostWorkshopMaps.cpp
)

target_include_directories(HostWorkshopMaps PRIVATE
    ${BAKKESMOD_SDK}/include
    ${BAKKESMOD_SDK}/include/imgui
    src
)

target_link_directories(HostWorkshopMaps PRIVATE
    ${BAKKESMOD_SDK}/lib
)

target_link_libraries(HostWorkshopMaps PRIVATE
    pluginsdk
)

set_target_properties(HostWorkshopMaps PROPERTIES SUFFIX ".dll" PREFIX "")

target_compile_definitions(HostWorkshopMaps PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
)
