#include "HostWorkshopMaps.h"
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/gameevent/serverwrapper.h"
#include <algorithm>
#include <cctype>

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Host Workshop Maps", "2.0", 0)

std::string HostWorkshopMaps::SanitizePath(const std::string& raw) {
    std::string out = raw;
    for (char& c : out) if (c == '\\') c = '/';
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

std::string HostWorkshopMaps::MapNameFromPath(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string f = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = f.find_last_of('.');
    return (dot == std::string::npos) ? f : f.substr(0, dot);
}

void HostWorkshopMaps::SetStatus(const std::string& msg) {
    cvarManager->log("HostWorkshopMaps: " + msg);
}

std::string HostWorkshopMaps::AutoDetectMapsPath() {
    std::vector<std::string> paths = {
        "C:\\Program Files\\Epic Games\\rocketleague\\TAGame\\CookedPCConsole\\Mods",
        "C:\\Program Files\\Epic Games\\rocketleague\\TAGame\\CookedPCConsole\\maps",
        "D:\\Games\\rocketleague\\TAGame\\CookedPCConsole\\Mods",
        "C:\\RLMAPS",
        "D:\\RLMAPS"
    };

    for (const auto& p : paths) {
        if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return p;
        }
    }
    return "C:\\RLMAPS"; // fallback
}

void HostWorkshopMaps::onLoad() {
    mapsDirectory_ = AutoDetectMapsPath();
    SetStatus("Using maps path: " + mapsDirectory_);

    cvarManager->registerNotifier("hwm_load", [this](std::vector<std::string> params) {
        if (!params.empty()) {
            LoadMapPath(params[0]);
        } else {
            cvarManager->log("Usage: hwm_load \"D:/path/to/your/map.udk\"");
        }
    }, "Load a workshop map", PERMISSION_ALL);

    cvarManager->log("HostWorkshopMaps loaded successfully");
}

void HostWorkshopMaps::onUnload() {}

void HostWorkshopMaps::LoadMapPath(const std::string& path) {
    if (path.empty()) {
        SetStatus("No path provided");
        return;
    }

    SetStatus("Loading: " + MapNameFromPath(path));
    cvarManager->executeCommand("load_workshop_map \"" + path + "\"", false);
}
