#include "HostworkshopMaps.h"
#include <filesystem>
#include <algorithm>
#include <cctype>
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/GameEvent/ServerWrapper.h"

// =============================================================================
// PluginWindow Methods - STUB IMPLEMENTATIONS (DON'T RENDER)
// =============================================================================

std::string HostWorkshopMaps::GetMenuName()
{
    return "Workshop Maps";
}

std::string HostWorkshopMaps::GetMenuTitle()
{
    return "Workshop Maps";
}

void HostWorkshopMaps::SetImGuiContext(uintptr_t ctx)
{
    // Do nothing - just satisfy the interface
}

bool HostWorkshopMaps::ShouldBlockInput()
{
    return false;
}

bool HostWorkshopMaps::IsActiveOverlay()
{
    return false;
}

void HostWorkshopMaps::OnOpen()
{
    // Do nothing
}

void HostWorkshopMaps::OnClose()
{
    // Do nothing
}

void HostWorkshopMaps::Render()
{
    // DO NOT RENDER ANYTHING - causes crashes
    // This is a console-only plugin
}

// =============================================================================
// onLoad — Register only console commands
// =============================================================================

void HostWorkshopMaps::onLoad()
{
    try {
        cvarManager->log("===================================");
        cvarManager->log("Workshop Map Loader v2.0 loading...");
        cvarManager->log("===================================");

        // Register maps directory cvar with safe default
        std::string defaultPath = getDefaultMapsPath();
        cvarManager->log("Default path: " + defaultPath);

        cvarManager->registerCvar(
            "hwm_maps_directory",
            defaultPath,
            "Directory to scan for .upk and .udk files",
            true,
            true
        );

        // ===== COMMAND 1: Scan for maps =====
        cvarManager->registerNotifier(
            "hwm_scan",
            [this](std::vector<std::string> params) {
                cvarManager->log("HWM: Scanning for maps...");
                safeScanMaps();
            },
            "Scan for .upk and .udk map files",
            PERMISSION_ALL
        );

        // ===== COMMAND 2: List maps =====
        cvarManager->registerNotifier(
            "hwm_list",
            [this](std::vector<std::string> params) {
                if (mapFiles.empty()) {
                    cvarManager->log("HWM: No maps found. Run 'hwm_scan' first.");
                    return;
                }
                cvarManager->log("HWM: Found " + std::to_string(mapFiles.size()) + " maps:");
                for (size_t i = 0; i < mapFiles.size(); i++) {
                    cvarManager->log("  [" + std::to_string(i) + "] " + mapNames[i]);
                }
            },
            "List all found maps with indices",
            PERMISSION_ALL
        );

        // ===== COMMAND 3: Load by index =====
        cvarManager->registerNotifier(
            "hwm_load_index",
            [this](std::vector<std::string> params) {
                if (params.size() < 2) {
                    cvarManager->log("Usage: hwm_load_index <index>");
                    if (!mapFiles.empty()) {
                        cvarManager->log("Available indices: 0 to " + std::to_string(mapFiles.size() - 1));
                    }
                    return;
                }
                try {
                    int idx = std::stoi(params[1]);
                    loadMapByIndex(idx);
                } catch (...) {
                    cvarManager->log("HWM: Invalid index number");
                }
            },
            "Load a map by index number",
            PERMISSION_ALL
        );

        // ===== COMMAND 4: Load by path =====
        cvarManager->registerNotifier(
            "hwm_load_path",
            [this](std::vector<std::string> params) {
                if (params.size() < 2) {
                    cvarManager->log("Usage: hwm_load_path <full_path_to_map>");
                    return;
                }
                loadMapByPath(params[1]);
            },
            "Load a map by full file path",
            PERMISSION_ALL
        );

        cvarManager->log("===================================");
        cvarManager->log("HWM: Ready!");
        cvarManager->log("Commands:");
        cvarManager->log("  hwm_scan          - Find maps");
        cvarManager->log("  hwm_list          - List found maps");
        cvarManager->log("  hwm_load_index 0  - Load a map");
        cvarManager->log("  hwm_load_path ... - Load by path");
        cvarManager->log("===================================");

    } catch (const std::exception& e) {
        cvarManager->log(std::string("HWM FATAL ERROR: ") + e.what());
    } catch (...) {
        cvarManager->log("HWM: Unknown fatal error in onLoad");
    }
}

// =============================================================================
// onUnload
// =============================================================================

void HostWorkshopMaps::onUnload()
{
    cvarManager->log("HWM: Unloading");
    mapFiles.clear();
    mapNames.clear();
}

// =============================================================================
// safeScanMaps — Scan for map files
// =============================================================================

void HostWorkshopMaps::safeScanMaps()
{
    try {
        mapFiles.clear();
        mapNames.clear();

        std::string mapsDir = cvarManager->getCvar("hwm_maps_directory").getStringValue();

        if (mapsDir.empty()) {
            cvarManager->log("HWM: Maps directory is empty");
            return;
        }

        cvarManager->log("HWM: Scanning: " + mapsDir);

        if (!std::filesystem::exists(mapsDir)) {
            cvarManager->log("HWM: Directory does not exist");
            return;
        }

        // Scan recursively
        auto options = std::filesystem::directory_options::skip_permission_denied;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(mapsDir, options)) {
            try {
                if (!entry.is_regular_file()) {
                    continue;
                }

                std::string ext = entry.path().extension().string();
                
                // Convert to lowercase
                std::transform(ext.begin(), ext.end(), ext.begin(),
                              [](unsigned char c) { return std::tolower(c); });

                if (ext == ".upk" || ext == ".udk") {
                    mapFiles.push_back(entry.path().string());
                    mapNames.push_back(entry.path().filename().string());
                }
            } catch (...) {
                // Skip errors on individual files
            }
        }

        cvarManager->log("HWM: Scan complete. Found " + std::to_string(mapFiles.size()) + " maps.");

    } catch (const std::exception& e) {
        cvarManager->log(std::string("HWM Scan error: ") + e.what());
    } catch (...) {
        cvarManager->log("HWM: Scan failed");
    }
}

// =============================================================================
// getDefaultMapsPath
// =============================================================================

std::string HostWorkshopMaps::getDefaultMapsPath()
{
    try {
        // Try AppData first
        char* appData = nullptr;
        size_t len = 0;
        if (_dupenv_s(&appData, &len, "APPDATA") == 0 && appData) {
            std::string path = std::string(appData) + "\\bakkesmod\\bakkesmod\\data\\workshop";
            free(appData);
            if (std::filesystem::exists(path)) {
                return path;
            }
            return path; // Return even if doesn't exist - user might create it
        }

        // Try Steam folder
        char* progFiles = nullptr;
        if (_dupenv_s(&progFiles, &len, "ProgramFiles(x86)") == 0 && progFiles) {
            std::string path = std::string(progFiles) + "\\Steam\\steamapps\\workshop\\content\\252950";
            free(progFiles);
            if (std::filesystem::exists(path)) {
                return path;
            }
        }

        // Try Documents
        char* userProfile = nullptr;
        if (_dupenv_s(&userProfile, &len, "USERPROFILE") == 0 && userProfile) {
            std::string path = std::string(userProfile) + "\\Documents\\rocketleague\\workshop";
            free(userProfile);
            if (std::filesystem::exists(path)) {
                return path;
            }
        }

        // Default fallback
        return "C:\\workshop";

    } catch (...) {
        return "C:\\workshop";
    }
}

// =============================================================================
// loadMapByIndex
// =============================================================================

void HostWorkshopMaps::loadMapByIndex(int index)
{
    if (index < 0 || index >= (int)mapFiles.size()) {
        cvarManager->log("HWM: Invalid index " + std::to_string(index) + 
                        " (valid range: 0-" + std::to_string(mapFiles.size() - 1) + ")");
        return;
    }

    loadMapByPath(mapFiles[index]);
}

// =============================================================================
// loadMapByPath — Load a map file
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

        if (isLanHost()) {
            cvarManager->log("HWM: LAN host detected - using servertravel");
            gameWrapper->SetTimeout(
                [this, path](GameWrapper* gw) {
                    try {
                        gameWrapper->ExecuteUnrealCommand("servertravel \"" + path + "\"");
                        cvarManager->log("HWM: ServerTravel executed");
                    } catch (...) {
                        cvarManager->log("HWM: ServerTravel failed");
                    }
                },
                1.0f
            );
        } else {
            cvarManager->log("HWM: Not LAN host - loading locally");
            gameWrapper->SetTimeout(
                [this, path](GameWrapper* gw) {
                    try {
                        gameWrapper->ExecuteUnrealCommand("open \"" + path + "\"");
                        cvarManager->log("HWM: Open command executed");
                    } catch (...) {
                        cvarManager->log("HWM: Open command failed");
                    }
                },
                0.5f
            );
        }

    } catch (const std::exception& e) {
        cvarManager->log(std::string("HWM Load error: ") + e.what());
    } catch (...) {
        cvarManager->log("HWM: Load failed");
    }
}

// =============================================================================
// isLanHost — Check if hosting LAN match
// =============================================================================

bool HostWorkshopMaps::isLanHost()
{
    try {
        ServerWrapper server = gameWrapper->GetCurrentGameState();

        if (server.IsNull()) {
            return false;
        }

        if (!server.HasAuthority()) {
            return false;
        }

        auto members = server.GetPRIs();
        return members.Count() > 1;

    } catch (...) {
        return false;
    }
}

// =============================================================================
// Export the plugin
// =============================================================================

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Workshop Map Loader", "2.0", PLUGINTYPE_FREEPLAY)
