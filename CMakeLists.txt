cmake_minimum_required(VERSION 3.15)
project(HostWorkshopMaps)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── BakkesMod SDK path ───────────────────────────────────────────────────────
if(NOT DEFINED BAKKESMOD_SDK)
    set(BAKKESMOD_SDK "$ENV{BAKKESMOD_SDK}" CACHE PATH "Path to BakkesMod SDK")
endif()

if(NOT EXISTS "${BAKKESMOD_SDK}/include/bakkesmod/plugin/bakkesmodplugin.h")
    message(FATAL_ERROR
        "BakkesMod SDK not found at '${BAKKESMOD_SDK}'.\n"
        "Pass -DBAKKESMOD_SDK=<path to the cloned BakkesModSDK repo root>.")
endif()

# ── ImGui (vendored under third_party/imgui) ────────────────────────────────
# build.yml downloads imgui automatically into third_party/imgui.
# For local builds: clone https://github.com/ocornut/imgui into third_party/imgui
if(NOT DEFINED IMGUI_DIR)
    set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
endif()

if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
    message(FATAL_ERROR
        "ImGui not found at '${IMGUI_DIR}'.\n"
        "The CI workflow downloads it automatically.\n"
        "For local builds: git clone https://github.com/ocornut/imgui third_party/imgui")
endif()

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)

# ── Plugin target ────────────────────────────────────────────────────────────
add_library(HostWorkshopMaps SHARED
    src/HostWorkshopMaps.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(HostWorkshopMaps PRIVATE
    ${BAKKESMOD_SDK}/include
    ${IMGUI_DIR}
    src
)

target_link_directories(HostWorkshopMaps PRIVATE
    ${BAKKESMOD_SDK}/lib
)

target_link_libraries(HostWorkshopMaps PRIVATE
    pluginsdk
)

if(WIN32)
    set_target_properties(HostWorkshopMaps PROPERTIES
        SUFFIX ".dll"
        PREFIX ""
    )
    target_compile_definitions(HostWorkshopMaps PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# ── Optional: auto-copy into local BakkesMod folder on Windows dev builds ───
if(DEFINED ENV{APPDATA} AND NOT DEFINED ENV{CI})
    set(BM_PLUGINS  "$ENV{APPDATA}/bakkesmod/bakkesmod/plugins")
    set(BM_SETTINGS "$ENV{APPDATA}/bakkesmod/bakkesmod/plugins/settings")
    add_custom_command(TARGET HostWorkshopMaps POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BM_PLUGINS}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BM_SETTINGS}"
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:HostWorkshopMaps>
            "${BM_PLUGINS}/HostWorkshopMaps.dll"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/HostWorkshopMaps.set"
            "${BM_SETTINGS}/HostWorkshopMaps.set"
    )
    message(STATUS "Will auto-copy built DLL + .set to BakkesMod plugins folder")
endif()
