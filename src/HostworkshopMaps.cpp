#include "HostworkshopMaps.h"
#include <filesystem>
#include <algorithm>
#include <cctype>
#include "bakkesmod/wrappers/canvaswrapper.h"
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/GameEvent/ServerWrapper.h"
#include "imgui.h"

// =============================================================================
// Plugin Metadata
// =============================================================================

std::string HostWorkshopMaps::GetMenuName()
{
    return "Host Workshop Maps";
}

std::string HostWorkshopMaps::GetMenuTitle()
{
    return "Host Workshop Maps 2.0";
}

void HostWorkshopMaps::SetImGuiContext(uintptr_t ctx)
{
    ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

bool HostWorkshopMaps::ShouldBlockInput()
{
    return true;
}

bool HostWorkshopMaps::IsActiveOverlay()
{
    return false;
}

void HostWorkshopMaps::OnOpen()
{
    isWindowOpen = true;
    // Re-scan when the panel opens, but only if render is initialized
    if (renderInitialized) {
        safeScanMaps();
    }
}

void HostWorkshopMaps::OnClose()
{
    isWindowOpen = false;
}

// =============================================================================
// onLoad — Plugin initialization
// =============================================================================

void HostWorkshopMaps::onLoad()
{
    try {
        cvarManager->log("=== HostWorkshopMaps v2.0 loading... ===");

        // ---------------------------------------------------------------
        // Step 1: Register CVars and commands (no game state needed)
        // ---------------------------------------------------------------
        cvarManager->registerCvar(
            "hwm_maps_directory",
            getDefaultMapsPath(),
            "Directory to scan for .upk and .udk Workshop maps",
            true,  // persist
            true   // has min
        );

        // Register console command: re-scan directory
        cvarManager->registerNotifier(
            "hwm_scan",
            [this](std::vector<std::string> params) {
                cvarManager->log("Manual scan triggered via hwm_scan");
                safeScanMaps();
            },
            "Re-scan the maps directory for .upk and .udk files",
            PERMISSION_ALL
        );

        // Register console command: list all found maps
        cvarManager->registerNotifier(
            "hwm_list",
            [this](std::vector<std::string> params) {
                if (mapFiles.empty()) {
                    cvarManager->log("No maps found. Run hwm_scan first.");
                    return;
                }
                cvarManager->log("=== Found " + std::to_string(mapFiles.size()) + " maps ===");
                for (size_t i = 0; i < mapFiles.size(); ++i) {
                    cvarManager->log("[" + std::to_string(i) + "] " + mapNames[i]);
                }
            },
            "List all found maps with their indices",
            PERMISSION_ALL
        );

        // Register console command: load map by index
        cvarManager->registerNotifier(
            "hwm_load_index",
            [this](std::vector<std::string> params) {
                if (params.size() < 2) {
                    cvarManager->log("Usage: hwm_load_index <index>");
                    return;
                }
                try {
                    int idx = std::stoi(params[1]);
                    loadMapByIndex(idx);
                } catch (...) {
                    cvarManager->log("Invalid index: " + params[1]);
                }
            },
            "Load a map by its index (0-based, from hwm_list)",
            PERMISSION_ALL
        );

        // Register console command: load map by full path
        cvarManager->registerNotifier(
            "hwm_load_path",
            [this](std::vector<std::string> params) {
                if (params.size() < 2) {
                    cvarManager->log("Usage: hwm_load_path <path>");
                    return;
                }
                loadMapByPath(params[1]);
            },
            "Load a map by full absolute file path",
            PERMISSION_ALL
        );

        cvarManager->log("CVars and commands registered.");

        // ---------------------------------------------------------------
        // Step 2: Defer directory scanning (avoid blocking onLoad)
        // ---------------------------------------------------------------
        gameWrapper->SetTimeout(
            [this](GameWrapper* gw) {
                cvarManager->log("Performing deferred map scan...");
                safeScanMaps();
                pendingScan = false;
            },
            1.5f  // Wait 1.5 seconds after injection
        );
        pendingScan = true;
        cvarManager->log("Scan scheduled for 1.5s delay.");

        // ---------------------------------------------------------------
        // Step 3: REGISTER DRAWABLE — CAREFULLY!
        //
        // THIS WAS CAUSING YOUR CRASH!
        // - Defer it so all data structures exist before Render() is called
        // - Wrap in try-catch to prevent game crashes
        // - Only render when window is actually open
        // ---------------------------------------------------------------
        gameWrapper->SetTimeout(
            [this](GameWrapper* gw) {
                cvarManager->log("Registering drawable...");

                // Double-check scan is done before we start rendering
                if (pendingScan) {
                    safeScanMaps();
                    pendingScan = false;
                }

                renderInitialized = true;

                // Register the drawable with a SAFE lambda wrapper
                gameWrapper->RegisterDrawable(
                    [this](CanvasWrapper canvas) {
                        // -----------------------------------------------
                        // EVERYTHING in here runs 60+ times per second.
                        // ANY crash here = game crash.
                        // -----------------------------------------------
                        try {
                            // Only render if the panel is actually open
                            if (!isWindowOpen) {
                                return;
                            }

                            // Only render if fully initialized
                            if (!renderInitialized) {
                                return;
                            }

                            // Call the actual Render method
                            Render();
                        } catch (const std::exception& e) {
                            // Log and swallow — prevent crash
                            cvarManager->log(
                                "HWM RENDER ERROR: " + std::string(e.what())
                            );
                        } catch (...) {
                            cvarManager->log(
                                "HWM RENDER: UNKNOWN EXCEPTION"
                            );
                        }
                    }
                );

                cvarManager->log("Drawable registered successfully.");
            },
            3.0f  // Wait 3 full seconds — ensure everything is ready
        );

        cvarManager->log("=== HostWorkshopMaps v2.0 loaded successfully! ===");

    } catch (const std::exception& e) {
        if (cvarManager) {
            cvarManager->log(
                "HWM FATAL onLoad ERROR: " + std::string(e.what())
            );
        }
    } catch (...) {
        if (cvarManager) {
            cvarManager->log(
                "HWM FATAL onLoad: UNKNOWN EXCEPTION"
            );
        }
    }
}

// =============================================================================
// onUnload
// =============================================================================

void HostWorkshopMaps::onUnload()
{
    cvarManager->log("HostWorkshopMaps unloading...");
    // No explicit UnregisterDrawable needed — BakkesMod handles it
}

// =============================================================================
// Render — ImGui panel (called every frame when window is open)
// =============================================================================

void HostWorkshopMaps::Render()
{
    // Safety: if ImGui context isn't ready, bail
    if (!ImGui::GetCurrentContext()) {
        return;
    }

    // Main panel window
    ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Host Workshop Maps 2.0", &isWindowOpen)) {
        ImGui::End();
        return;
    }

    // ---- Search bar ----
    ImGui::Text("Search:");
    ImGui::SameLine();
    ImGui::InputText("##search", searchFilter, sizeof(searchFilter));
    ImGui::Separator();

    // ---- Map list ----
    renderMapList();
    ImGui::Separator();

    // ---- Load button ----
    ImGui::Text("Selected: %s", selectedMapIndex >= 0 && selectedMapIndex < (int)mapNames.size()
                    ? mapNames[selectedMapIndex].c_str()
                    : "(none)");

    if (ImGui::Button("Load Selected Map", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
        loadSelectedMap();
    }

    ImGui::Separator();

    // ---- Settings ----
    renderSettings();

    ImGui::End();
}

// =============================================================================
// renderMapList — Draw the scrollable map list
// =============================================================================

void HostWorkshopMaps::renderMapList()
{
    ImGui::Text("Maps found: %zu", mapFiles.size());

    if (ImGui::BeginChild("MapList", ImVec2(0, 250), true)) {
        std::string filter(searchFilter);
        std::transform(filter.begin(), filter.end(), filter.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        for (size_t i = 0; i < mapNames.size(); ++i) {
            // Apply search filter
            std::string mapLower = mapNames[i];
            std::transform(mapLower.begin(), mapLower.end(), mapLower.begin(),
                          [](unsigned char c) { return std::tolower(c); });

            if (!filter.empty() && mapLower.find(filter) == std::string::npos) {
                continue;
            }

            bool isSelected = (int)i == selectedMapIndex;
            if (ImGui::Selectable(mapNames[i].c_str(), isSelected)) {
                selectedMapIndex = i;
            }
        }
    }
    ImGui::EndChild();
}

// =============================================================================
// renderSettings — Advanced settings tree
// =============================================================================

void HostWorkshopMaps::renderSettings()
{
    if (ImGui::TreeNode("Settings")) {
        ImGui::Text("Maps Directory:");

        static char dirBuffer[512] = {0};
        std::string currentDir = cvarManager->getCvar("hwm_maps_directory").getStringValue();
        if (dirBuffer[0] == 0) {
            strncpy(dirBuffer, currentDir.c_str(), sizeof(dirBuffer) - 1);
        }

        ImGui::InputText("##mapsdir", dirBuffer, sizeof(dirBuffer), ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine();
        if (ImGui::Button("Scan Now")) {
            safeScanMaps();
        }

        ImGui::TreePop();
    }

    ImGui::Separator();

    if (ImGui::Button("Scan for Maps", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
        safeScanMaps();
    }
}

// =============================================================================
// safeScanMaps — Scan directory with full error handling
// =============================================================================

void HostWorkshopMaps::safeScanMaps()
{
    try {
        mapFiles.clear();
        mapNames.clear();
        selectedMapIndex = -1;

        std::string mapsDir = cvarManager->getCvar("hwm_maps_directory").getStringValue();

        if (mapsDir.empty()) {
            cvarManager->log("HWM: No maps directory configured!");
            return;
        }

        // Check directory exists
        if (!std::filesystem::exists(mapsDir)) {
            cvarManager->log("HWM: Directory does not exist: " + mapsDir);
            return;
        }

        // Check it's actually a directory
        if (!std::filesystem::is_directory(mapsDir)) {
            cvarManager->log("HWM: Path is not a directory: " + mapsDir);
            return;
        }

        // Recursively scan with skip_permission_denied
        auto options = std::filesystem::directory_options::skip_permission_denied;
        for (auto& entry : std::filesystem::recursive_directory_iterator(mapsDir, options)) {
            try {
                if (!entry.is_regular_file()) {
                    continue;
                }

                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                              [](unsigned char c) { return std::tolower(c); });

                if (ext == ".upk" || ext == ".udk") {
                    std::string fullPath = entry.path().string();
                    std::string fileName = entry.path().filename().string();

                    mapFiles.push_back(fullPath);
                    mapNames.push_back(fileName);
                }
            } catch (...) {
                // Skip individual file errors
                continue;
            }
        }

        cvarManager->log(
            "HWM: Scan complete. Found " + std::to_string(mapFiles.size()) +
            " maps in: " + mapsDir
        );

    } catch (const std::exception& e) {
        cvarManager->log(
            "HWM: Scan error: " + std::string(e.what())
        );
    } catch (...) {
        cvarManager->log("HWM: Unknown scan error");
    }
}

// =============================================================================
// getDefaultMapsPath
// =============================================================================

std::string HostWorkshopMaps::getDefaultMapsPath()
{
    // Try multiple common locations
    std::vector<std::string> candidates;

    // 1. BakkesMod data folder
    char* appData = nullptr;
    size_t len = 0;
    if (_dupenv_s(&appData, &len, "APPDATA") == 0 && appData) {
        candidates.push_back(
            std::string(appData) + "\\bakkesmod\\bakkesmod\\data\\workshop"
        );
        free(appData);
    }

    // 2. Steam workshop folder
    char* progFiles = nullptr;
    if (_dupenv_s(&progFiles, &len, "ProgramFiles(x86)") == 0 && progFiles) {
        candidates.push_back(
            std::string(progFiles) + "\\Steam\\steamapps\\workshop\\content\\252950"
        );
        free(progFiles);
    }

    // 3. Documents folder
    char* userProfile = nullptr;
    if (_dupenv_s(&userProfile, &len, "USERPROFILE") == 0 && userProfile) {
        candidates.push_back(
            std::string(userProfile) + "\\Documents\\rocketleague\\workshop"
        );
        free(userProfile);
    }

    // Return first existing directory
    for (const auto& path : candidates) {
        try {
            if (std::filesystem::exists(path)) {
                return path;
            }
        } catch (...) {}
    }

    // Default to the BakkesMod data folder (will be created if needed)
    return candidates.empty() ? "" : candidates[0];
}

// =============================================================================
// loadMapByIndex
// =============================================================================

void HostWorkshopMaps::loadMapByIndex(int index)
{
    if (index < 0 || index >= (int)mapFiles.size()) {
        cvarManager->log(
            "HWM: Invalid map index: " + std::to_string(index) +
            " (total: " + std::to_string(mapFiles.size()) + ")"
        );
        return;
    }

    loadMapByPath(mapFiles[index]);
}

// =============================================================================
// loadMapByPath — The core map loading function
// =============================================================================

void HostWorkshopMaps::loadMapByPath(const std::string& path)
{
    try {
        cvarManager->log("HWM: Loading map: " + path);

        // Validate file exists
        if (!std::filesystem::exists(path)) {
            cvarManager->log("HWM: Map file not found: " + path);
            return;
        }

        // Check if we are a LAN host with connected players
        if (isLanHost()) {
            // Use ServerTravel to move all connected clients
            cvarManager->log("HWM: LAN host detected — using servertravel");

            gameWrapper->SetTimeout(
                [this, path](GameWrapper* gw) {
                    try {
                        std::string cmd = "servertravel \"" + path + "\"";
                        cvarManager->log("HWM: Executing: " + cmd);
                        gameWrapper->ExecuteUnrealCommand(cmd);
                    } catch (...) {
                        cvarManager->log("HWM: ServerTravel failed");
                    }
                },
                1.0f  // 1 second delay for Unreal readiness
            );
        } else {
            // Not a LAN host — just load locally
            cvarManager->log("HWM: Loading map locally");

            gameWrapper->SetTimeout(
                [this, path](GameWrapper* gw) {
                    try {
                        std::string cmd = "open \"" + path + "\"";
                        cvarManager->log("HWM: Executing: " + cmd);
                        gameWrapper->ExecuteUnrealCommand(cmd);
                    } catch (...) {
                        cvarManager->log("HWM: Local map load failed");
                    }
                },
                0.5f
            );
        }

    } catch (const std::exception& e) {
        cvarManager->log(
            "HWM: loadMapByPath error: " + std::string(e.what())
        );
    } catch (...) {
        cvarManager->log("HWM: loadMapByPath: unknown error");
    }
}

// =============================================================================
// loadSelectedMap — Load whatever user selected in the UI
// =============================================================================

void HostWorkshopMaps::loadSelectedMap()
{
    if (selectedMapIndex < 0 || selectedMapIndex >= (int)mapFiles.size()) {
        cvarManager->log("HWM: No map selected or invalid selection");
        return;
    }

    loadMapByIndex(selectedMapIndex);
}

// =============================================================================
// isLanHost — Check if we're hosting a LAN match with players connected
// =============================================================================

bool HostWorkshopMaps::isLanHost()
{
    try {
        ServerWrapper server = gameWrapper->GetCurrentGameState();

        if (server.IsNull()) {
            return false;
        }

        // Check we have hosting authority
        if (!server.HasAuthority()) {
            return false;
        }

        // Check there are remote players (more than just us)
        auto members = server.GetPRIs();

        // If there's more than 1 player and we have authority, we're hosting
        return members.Count() > 1;

    } catch (...) {
        return false;
    }
}

// =============================================================================
// CRITICAL: Export the plugin class so BakkesMod can load it
// =============================================================================

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Host Workshop Maps", "2.0", PLUGINTYPE_FREEPLAY)
