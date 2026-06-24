cmake_minimum_required(VERSION 3.15)
project(WorkshopMapLoader)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── BakkesMod SDK path ───────────────────────────────────────────────────────
# Pass the SDK root on the command line, e.g.:
#   cmake -B build -S . -DBAKKESMOD_SDK="C:/path/to/BakkesModSDK"
if(NOT DEFINED BAKKESMOD_SDK)
    set(BAKKESMOD_SDK "$ENV{BAKKESMOD_SDK}" CACHE PATH "Path to BakkesMod SDK")
endif()

if(NOT EXISTS "${BAKKESMOD_SDK}/include/bakkesmod/plugin/bakkesmodplugin.h")
    message(FATAL_ERROR
        "BakkesMod SDK not found at '${BAKKESMOD_SDK}'.\n"
        "Pass -DBAKKESMOD_SDK=<path to the cloned BakkesModSDK repo root>.")
endif()

# ── ImGui source (included in the BakkesMod SDK) ────────────────────────────
# The SDK ships imgui under ${BAKKESMOD_SDK}/include/imgui or similar;
# if it doesn't, add your own copy here.
file(GLOB IMGUI_SOURCES
    "${BAKKESMOD_SDK}/include/imgui/*.cpp"
)

# ── Plugin target ────────────────────────────────────────────────────────────
add_library(WorkshopMapLoader SHARED
    src/WorkshopMapLoader.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(WorkshopMapLoader PRIVATE
    ${BAKKESMOD_SDK}/include
    ${BAKKESMOD_SDK}/include/imgui
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
