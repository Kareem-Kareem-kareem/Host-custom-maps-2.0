cmake_minimum_required(VERSION 3.15)
project(RLDisasters)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── BakkesMod SDK path ───────────────────────────────────────────────
# Pass the SDK root explicitly, e.g.:
#   cmake -B build -S . -DBAKKESMOD_SDK="C:/path/to/BakkesModSDK"
if(NOT DEFINED BAKKESMOD_SDK)
    set(BAKKESMOD_SDK "$ENV{BAKKESMOD_SDK}" CACHE PATH "Path to BakkesMod SDK")
endif()

if(NOT EXISTS "${BAKKESMOD_SDK}/include/bakkesmod/plugin/bakkesmodplugin.h")
    message(FATAL_ERROR
        "BakkesMod SDK not found at '${BAKKESMOD_SDK}'.\n"
        "Pass -DBAKKESMOD_SDK=<path to the cloned BakkesModSDK repo root>.")
endif()

# ── Sources ──────────────────────────────────────────────────────────
# This plugin has no custom GUI, so it doesn't need to compile ImGui at
# all — settings are handled by RLDisasters.set, which BakkesMod renders
# natively in the F2 > Plugins tab.
add_library(RLDisasters SHARED
    src/RLDisasters.cpp
)

target_include_directories(RLDisasters PRIVATE
    ${BAKKESMOD_SDK}/include
    src
)

target_link_directories(RLDisasters PRIVATE
    ${BAKKESMOD_SDK}/lib
)

target_link_libraries(RLDisasters PRIVATE
    pluginsdk
)

if(WIN32)
    set_target_properties(RLDisasters PROPERTIES
        SUFFIX ".dll"
        PREFIX ""
    )
    target_compile_definitions(RLDisasters PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# ── Optional: auto-copy the built DLL + .set file into your local
#    BakkesMod folders when building locally on Windows (skipped in CI) ──
if(DEFINED ENV{APPDATA} AND NOT DEFINED ENV{CI})
    set(BM_PLUGINS "$ENV{APPDATA}/bakkesmod/bakkesmod/plugins")
    set(BM_SETTINGS "$ENV{APPDATA}/bakkesmod/bakkesmod/plugins/settings")
    add_custom_command(TARGET RLDisasters POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BM_PLUGINS}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BM_SETTINGS}"
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:RLDisasters>
            "${BM_PLUGINS}/RLDisasters.dll"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/RLDisasters.set"
            "${BM_SETTINGS}/RLDisasters.set"
    )
    message(STATUS "Will also copy built DLL + .set to your local BakkesMod folders")
endif()
