cmake_minimum_required(VERSION 3.15)
project(WorkshopMapLoader)

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
add_library(WorkshopMapLoader SHARED
    src/WorkshopMapLoader.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(WorkshopMapLoader PRIVATE
    ${BAKKESMOD_SDK}/include
    ${IMGUI_DIR}
    src
)

target_link_directories(WorkshopMapLoader PRIVATE
    ${BAKKESMOD_SDK}/lib
)

target_link_libraries(WorkshopMapLoader PRIVATE
    pluginsdk
)

if(WIN32)
    set_target_properties(WorkshopMapLoader PROPERTIES
        SUFFIX ".dll"
        PREFIX ""
    )
    target_compile_definitions(WorkshopMapLoader PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# ── Optional: auto-copy into local BakkesMod folder on Windows dev builds ───
if(DEFINED ENV{APPDATA} AND NOT DEFINED ENV{CI})
    set(BM_PLUGINS  "$ENV{APPDATA}/bakkesmod/bakkesmod/plugins")
    set(BM_SETTINGS "$ENV{APPDATA}/bakkesmod/bakkesmod/plugins/settings")
    add_custom_command(TARGET WorkshopMapLoader POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BM_PLUGINS}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BM_SETTINGS}"
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:WorkshopMapLoader>
            "${BM_PLUGINS}/WorkshopMapLoader.dll"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/WorkshopMapLoader.set"
            "${BM_SETTINGS}/WorkshopMapLoader.set"
    )
    message(STATUS "Will auto-copy built DLL + .set to BakkesMod plugins folder")
endif()
