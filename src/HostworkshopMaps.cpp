#include "HostworkshopMaps.h"
#include <filesystem>
#include <algorithm>
#include <cctype>
#include "bakkesmod/wrappers/canvaswrapper.h"
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/GameEvent/ServerWrapper.h"
#include "imgui.h"

// =============================================================================
// Plugin Metadata Methods
// =============================================================================

std::string HostWorkshopMaps::GetMenuName()
{
    return "Workshop Map Loader";
}

std::string HostWorkshopMaps::GetMenuTitle()
{
    return "Workshop Maps";
}

void HostWorkshopMaps::SetImGuiContext(uintptr_t ctx)
{
    // Just accept the context, don't do anything with it
    // BakkesMod handles the context management
}

bool HostWorkshopMaps::ShouldBlockInput()
{
    return isWindowOpen;
}

bool HostWorkshopMaps::IsActiveOverlay()
{
    return false;
}

void HostWorkshopMaps::OnOpen()
{
    isWindowOpen = true;
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
        cvarManager->log("HWM: Loading Workshop Map Loader v2.0");

        // Register the maps directory cvar
        cvarManager->registerCvar(
            "hwm_maps_directory",
            getDefaultMapsPath(),
            "Directory to scan for .upk and .udk files",
            true,
            true
        );

        // Command: Re-scan maps
        cvarManager->registerNotifier(
            "hwm_scan",
            [this](std::vector<std::string> params) {
                try {
                    cvarManager->log("HWM: Scanning for maps...");
                    safeScanMaps();
                } catch (const std::exception& e) {
                    cvarManager->log(std::string("HWM Scan error: ") + e.what());
                } catch (...) {
                    cvarManager->log("HWM: Scan failed");
                }
            },
            "Rescan for maps",
            PERMISSION_ALL
        );

        // Command: List maps
        cvarManager->registerNotifier(
            "hwm_list",
            [this](std::vector<std::string> params) {
                try {
                    if (mapFiles.empty()) {
                        cvarManager->log("HWM: No maps found");
                        return;
                    }
                    cvarManager->log("HWM: Found " + std::to_string(mapFiles.size()) + " maps:");
                    for (size_t i = 0; i < mapFiles.size() && i < 50; ++i) {
                        cvarManager->log("  [" + std::to_string(i) + "] " + mapNames[i]);
                    }
                    if (mapFiles.size() > 50) {
                        cvarManager->log("  ... and " + std::to_string(mapFiles.size() - 50) + " more");
                    }
                } catch (const std::exception& e) {
                    cvarManager->log(std::string("HWM List error: ") + e.what());
                } catch (...) {
                    cvarManager->log("HWM: List failed");
                }
            },
            "List all maps",
            PERMISSION_ALL
        );

        // Command: Load by index
        cvarManager->registerNotifier(
            "hwm_load_index",
            [this](std::vector<std::string> params) {
                try {
                    if (params.size() < 2) {
                        cvarManager->log("Usage: hwm_load_index <index>");
                        return;
                    }
                    int idx = std::stoi(params[1]);
                    loadMapByIndex(idx);
                } catch (const std::exception& e) {
                    cvarManager->log(std::string("HWM Load error: ") + e.what());
                } catch (...) {
                    cvarManager->log("HWM: Invalid index");
                }
            },
            "Load map by index",
            PERMISSION_ALL
        );

        // Command: Load by path
        cvarManager->registerNotifier(
            "hwm_load_path",
            [this](std::vector<std::string> params) {
                try {
                    if (params.size() < 2) {
                        cvarManager->log("Usage: hwm_load_path <path>");
                        return;
                    }
                    loadMapByPath(params[1]);
                } catch (const std::exception& e) {
                    cvarManager->log(std::string("HWM Load error: ") + e.what());
                } catch (...) {
                    cvarManager->log("HWM: Load failed");
                }
            },
            "Load map by path",
            PERMISSION_ALL
        );

        cvarManager->log("HWM: Commands registered. Use 'hwm_scan' to find maps.");

    } catch (const std::exception& e) {
        cvarManager->log(std::string("HWM ERROR: ") + e.what());
    } catch (...) {
        cvarManager->log("HWM: Unknown error in onLoad");
    }
}

// =============================================================================
// onUnload
// =============================================================================

void HostWorkshopMaps::onUnload()
{
    try {
        cvarManager->log("HWM: Unloading");
        isWindowOpen = false;
        mapFiles.clear();
        mapNames.clear();
    } catch (...) {
    }
}

// =============================================================================
// Render — ImGui panel
// =============================================================================

void HostWorkshopMaps::Render()
{
    // Bail if window not open
    if (!isWindowOpen) {
        return;
    }

    // Bail if ImGui isn't ready
    if (!ImGui::GetCurrentContext()) {
        return;
    }

    try {
        // Set window size and position
        ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);

        // Main window
        if (!ImGui::Begin("Workshop Maps", &isWindowOpen, ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            return;
        }

        // Search bar
        ImGui::Text("Search:");
        ImGui::SameLine();
        ImGui::InputText("##search", searchFilter, sizeof(searchFilter));
        ImGui::Separator();

        // Map list with safe bounds checking
        ImGui::Text("Maps: %zu found", mapFiles.size());
        if (ImGui::BeginChild("MapList", ImVec2(0, 300), true)) {
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
                    selectedMapIndex = (int)i;
                }
            }
            ImGui::EndChild();
        }
        ImGui::Separator();

        // Show selection
        if (selectedMapIndex >= 0 && selectedMapIndex < (int)mapNames.size()) {
            ImGui::Text("Selected: %s", mapNames[selectedMapIndex].c_str());
        } else {
            ImGui::Text("Selected: (none)");
        }

        // Load button
        if (ImGui::Button("Load Selected", ImVec2(150, 30))) {
            loadSelectedMap();
        }
        ImGui::SameLine();

        // Scan button
        if (ImGui::Button("Scan", ImVec2(150, 30))) {
            safeScanMaps();
        }
        ImGui::SameLine();

        // Close button
        if (ImGui::Button("Close", ImVec2(150, 30))) {
            isWindowOpen = false;
        }

        ImGui::Separator();

        // Settings
        if (ImGui::TreeNode("Settings")) {
            static char dirBuffer[512] = {0};
            std::string currentDir = cvarManager->getCvar("hwm_maps_directory").getStringValue();
            if (dirBuffer[0] == 0 && !currentDir.empty()) {
                strncpy_s(dirBuffer, sizeof(dirBuffer), currentDir.c_str(), sizeof(dirBuffer) - 1);
            }

            ImGui::Text("Maps Directory:");
            ImGui::InputText("##dir", dirBuffer, sizeof(dirBuffer), ImGuiInputTextFlags_ReadOnly);

            ImGui::TreePop();
        }

        ImGui::End();

    } catch (const std::exception& e) {
        cvarManager->log(std::string("HWM Render error: ") + e.what());
    } catch (...) {
        cvarManager->log("HWM: Render failed");
    }
}

// =============================================================================
// safeScanMaps — Scan directory safely
// =============================================================================

void HostWorkshopMaps::safeScanMaps()
{
    try {
        mapFiles.clear();
        mapNames.clear();
        selectedMapIndex = -1;

        std::string mapsDir = cvarManager->getCvar("hwm_maps_directory").getStringValue();

        if (mapsDir.empty()) {
            cvarManager->log("HWM: No maps directory set");
            return;
        }

        if (!std::filesystem::exists(mapsDir)) {
            cvarManager->log("HWM: Directory does not exist: " + mapsDir);
            return;
        }

        if (!std::filesystem::is_directory(mapsDir)) {
            cvarManager->log("HWM: Path is not a directory: " + mapsDir);
            return;
        }

        // Scan with error handling
        auto options = std::filesystem::directory_options::skip_permission_denied;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(mapsDir, options)) {
            try {
                if (!entry.is_regular_file()) {
                    continue;
                }

                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                              [](unsigned char c) { return std::tolower(c); });

                if (ext == ".upk" || ext == ".udk") {
                    mapFiles.push_back(entry.path().string());
                    mapNames.push_back(entry.path().filename().string());
                }
            } catch (...) {
                // Skip individual file errors silently
                continue;
            }
        }

        cvarManager->log("HWM: Scan complete. Found " + std::to_string(mapFiles.size()) + " maps.");

    } catch (const std::filesystem::filesystem_error& e) {
        cvarManager->log(std::string("HWM Filesystem error: ") + e.what());
    } catch (const std::exception& e) {
        cvarManager->log(std::string("HWM Scan error: ") + e.what());
    } catch (...) {
        cvarManager->log("HWM: Scan failed with unknown error");
    }
}

// =============================================================================
// getDefaultMapsPath
// =============================================================================

std::string HostWorkshopMaps::getDefaultMapsPath()
{
    try {
        std::vector<std::string> candidates;

        // BakkesMod data folder
        char* appData = nullptr;
        size_t len = 0;
        if (_dupenv_s(&appData, &len, "APPDATA") == 0 && appData) {
            candidates.push_back(
                std::string(appData) + "\\bakkesmod\\bakkesmod\\data\\workshop"
            );
            free(appData);
        }

        // Steam workshop folder
        char* progFiles = nullptr;
        if (_dupenv_s(&progFiles, &len, "ProgramFiles(x86)") == 0 && progFiles) {
            candidates.push_back(
                std::string(progFiles) + "\\Steam\\steamapps\\workshop\\content\\252950"
            );
            free(progFiles);
        }

        // Documents folder
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
            } catch (...) {
                continue;
            }
        }

        // Return first candidate even if it doesn't exist
        return candidates.empty() ? "" : candidates[0];

    } catch (const std::exception& e) {
        cvarManager->log(std::string("HWM Error getting default path: ") + e.what());
        return "";
    } catch (...) {
        return "";
    }
}

// =============================================================================
// loadMapByIndex
// =============================================================================

void HostWorkshopMaps::loadMapByIndex(int index)
{
    try {
        if (index < 0 || index >= (int)mapFiles.size()) {
            cvarManager->log("HWM: Invalid map index: " + std::to_string(index));
            return;
        }
        loadMapByPath(mapFiles[index]);
    } catch (const std::exception& e) {
        cvarManager->log(std::string("HWM Load error: ") + e.what());
    } catch (...) {
        cvarManager->log("HWM: Load by index failed");
    }
}

// =============================================================================
// loadMapByPath — Load a map
// =============================================================================

void HostWorkshopMaps::loadMapByPath(const std::string& path)
{
    try {
        if (path.empty()) {
            cvarManager->log("HWM: Empty path");
            return;
        }

        if (!std::filesystem::exists(path)) {
            cvarManager->log("HWM: File not found: " + path);
            return;
        }

        cvarManager->log("HWM: Loading map: " + path);

        // Determine load method based on whether we're a LAN host
        if (isLanHost()) {
            cvarManager->log("HWM: Using servertravel (LAN host detected)");
            gameWrapper->SetTimeout(
                [this, path](GameWrapper* gw) {
                    try {
                        gameWrapper->ExecuteUnrealCommand("servertravel \"" + path + "\"");
                    } catch (const std::exception& e) {
                        cvarManager->log(std::string("HWM: Servertravel error: ") + e.what());
                    } catch (...) {
                        cvarManager->log("HWM: Servertravel failed");
                    }
                },
                1.0f
            );
        } else {
            cvarManager->log("HWM: Loading map locally");
            gameWrapper->SetTimeout(
                [this, path](GameWrapper* gw) {
                    try {
                        gameWrapper->ExecuteUnrealCommand("open \"" + path + "\"");
                    } catch (const std::exception& e) {
                        cvarManager->log(std::string("HWM: Open error: ") + e.what());
                    } catch (...) {
                        cvarManager->log("HWM: Open failed");
                    }
                },
                0.5f
            );
        }

    } catch (const std::exception& e) {
        cvarManager->log(std::string("HWM Load path error: ") + e.what());
    } catch (...) {
        cvarManager->log("HWM: Load path failed");
    }
}

// =============================================================================
// loadSelectedMap
// =============================================================================

void HostWorkshopMaps::loadSelectedMap()
{
    try {
        if (selectedMapIndex < 0 || selectedMapIndex >= (int)mapFiles.size()) {
            cvarManager->log("HWM: No map selected");
            return;
        }
        loadMapByIndex(selectedMapIndex);
    } catch (const std::exception& e) {
        cvarManager->log(std::string("HWM Selection error: ") + e.what());
    } catch (...) {
        cvarManager->log("HWM: Selection failed");
    }
}

// =============================================================================
// isLanHost — Check if we're hosting a LAN match
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

        // Check there are remote players
        auto members = server.GetPRIs();
        return members.Count() > 1;

    } catch (const std::exception& e) {
        cvarManager->log(std::string("HWM isLanHost error: ") + e.what());
        return false;
    } catch (...) {
        return false;
    }
}

// =============================================================================
// Export plugin to BakkesMod
// =============================================================================

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Workshop Map Loader", "2.0", PLUGINTYPE_FREEPLAY)
