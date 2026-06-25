#include "HostworkshopMaps.h"
#include <algorithm>
#include <cctype>
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/GameEvent/ServerWrapper.h"

// Plugin registration
BAKKESMOD_PLUGIN(HostWorkshopMaps, "Workshop Maps Loader", "2.0", PLUGINTYPE_FREEPLAY)

// PluginWindow interface implementations
std::string HostWorkshopMaps::GetMenuName()
{
    return "Workshop Maps";
}

std::string HostWorkshopMaps::GetMenuTitle()
{
    return "Workshop Maps Loader";
}

void HostWorkshopMaps::SetImGuiContext(uintptr_t ctx)
{
    // Empty - BakkesMod handles ImGui context
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
    cvarManager->log("Workshop Maps panel opened");
}

void HostWorkshopMaps::OnClose()
{
    cvarManager->log("Workshop Maps panel closed");
}

void HostWorkshopMaps::Render()
{
    // Render is called by BakkesMod if registered
    // We keep it minimal to avoid crashes
}

// Plugin load - register console commands
void HostWorkshopMaps::onLoad()
{
    cvarManager->log("==== Workshop Maps Loader v2.0 ====");
    
    // Register the maps directory cvar
    std::string defaultPath = getDefaultPath();
    cvarManager->registerCvar(
        "hwm_maps_directory",
        defaultPath,
        "Directory containing workshop maps",
        true,
        true
    );
    
    cvarManager->log("Default path: " + defaultPath);
    
    // Register scan command
    cvarManager->registerNotifier(
        "hwm_scan",
        [this](std::vector<std::string> params) {
            cvarManager->log("Scanning for maps...");
            scanMapsDirectory();
            if (mapFiles.empty()) {
                cvarManager->log("No maps found. Check hwm_maps_directory");
            } else {
                cvarManager->log("Found " + std::to_string(mapFiles.size()) + " maps");
            }
        },
        "Scan for workshop maps",
        PERMISSION_ALL
    );
    
    // Register list command
    cvarManager->registerNotifier(
        "hwm_list",
        [this](std::vector<std::string> params) {
            if (mapFiles.empty()) {
                cvarManager->log("No maps loaded. Run 'hwm_scan' first");
                return;
            }
            cvarManager->log("=== Workshop Maps ===");
            for (size_t i = 0; i < mapFiles.size(); i++) {
                cvarManager->log("[" + std::to_string(i) + "] " + mapNames[i]);
            }
        },
        "List loaded maps",
        PERMISSION_ALL
    );
    
    // Register load by index command
    cvarManager->registerNotifier(
        "hwm_load",
        [this](std::vector<std::string> params) {
            if (params.size() < 2) {
                cvarManager->log("Usage: hwm_load <index>");
                return;
            }
            try {
                int index = std::stoi(params[1]);
                loadMapByIndex(index);
            } catch (...) {
                cvarManager->log("Invalid index");
            }
        },
        "Load map by index (hwm_list to see indices)",
        PERMISSION_ALL
    );
    
    // Register load by path command
    cvarManager->registerNotifier(
        "hwm_load_path",
        [this](std::vector<std::string> params) {
            if (params.size() < 2) {
                cvarManager->log("Usage: hwm_load_path <path>");
                return;
            }
            loadMapByPath(params[1]);
        },
        "Load map by file path",
        PERMISSION_ALL
    );
    
    cvarManager->log("==== Ready! ====");
    cvarManager->log("Commands: hwm_scan, hwm_list, hwm_load");
}

void HostWorkshopMaps::onUnload()
{
    cvarManager->log("Workshop Maps unloaded");
    mapFiles.clear();
    mapNames.clear();
}

// Directory scanning
std::string HostWorkshopMaps::getDefaultPath()
{
    // Try multiple common locations
    std::vector<std::string> paths;
    
    // BakkesMod workshop folder
    char* appData = nullptr;
    size_t len = 0;
    if (_dupenv_s(&appData, &len, "APPDATA") == 0 && appData) {
        paths.push_back(std::string(appData) + "\\bakkesmod\\bakkesmod\\data\\workshop");
        free(appData);
    }
    
    // Steam workshop folder
    char* progFiles = nullptr;
    if (_dupenv_s(&progFiles, &len, "ProgramFiles(x86)") == 0 && progFiles) {
        paths.push_back(std::string(progFiles) + "\\Steam\\steamapps\\workshop\\content\\252950");
        free(progFiles);
    }
    
    // Documents folder
    char* docs = nullptr;
    if (_dupenv_s(&docs, &len, "USERPROFILE") == 0 && docs) {
        paths.push_back(std::string(docs) + "\\Documents\\rocketleague\\workshop");
        free(docs);
    }
    
    // Return first existing path
    for (const auto& path : paths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    
    // Return first path as default
    return paths.empty() ? "" : paths[0];
}

void HostWorkshopMaps::scanMapsDirectory()
{
    mapFiles.clear();
    mapNames.clear();
    
    std::string dir = cvarManager->getCvar("hwm_maps_directory").getStringValue();
    
    if (dir.empty()) {
        cvarManager->log("Maps directory not set");
        return;
    }
    
    if (!std::filesystem::exists(dir)) {
        cvarManager->log("Maps directory does not exist: " + dir);
        return;
    }
    
    try {
        auto options = std::filesystem::directory_options::skip_permission_denied;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, options)) {
            if (!entry.is_regular_file()) continue;
            
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            if (ext == ".upk" || ext == ".udk") {
                mapFiles.push_back(entry.path().string());
                mapNames.push_back(entry.path().filename().string());
            }
        }
    } catch (const std::exception& e) {
        cvarManager->log(std::string("Scan error: ") + e.what());
    }
}

// Map loading
void HostWorkshopMaps::loadMapByIndex(int index)
{
    if (index < 0 || index >= (int)mapFiles.size()) {
        cvarManager->log("Invalid index: " + std::to_string(index));
        return;
    }
    loadMapByPath(mapFiles[index]);
}

void HostWorkshopMaps::loadMapByPath(const std::string& path)
{
    if (!std::filesystem::exists(path)) {
        cvarManager->log("Map not found: " + path);
        return;
    }
    
    cvarManager->log("Loading map: " + path);
    
    if (isLanHost()) {
        cvarManager->log("LAN host detected - using servertravel");
        gameWrapper->SetTimeout(
            [this, path](GameWrapper* gw) {
                gameWrapper->ExecuteUnrealCommand("servertravel \"" + path + "\"");
            },
            1.0f
        );
    } else {
        cvarManager->log("Loading locally");
        gameWrapper->SetTimeout(
            [this, path](GameWrapper* gw) {
                gameWrapper->ExecuteUnrealCommand("open \"" + path + "\"");
            },
            0.5f
        );
    }
}

bool HostWorkshopMaps::isLanHost()
{
    try {
        ServerWrapper server = gameWrapper->GetCurrentGameState();
        if (server.IsNull()) return false;
        if (!server.HasAuthority()) return false;
        return server.GetPRIs().Count() > 1;
    } catch (...) {
        return false;
    }
}
