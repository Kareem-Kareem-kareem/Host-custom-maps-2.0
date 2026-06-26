#include "HostWorkshopMaps.h"
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/gameevent/serverwrapper.h"
#include <algorithm>
#include <cctype>

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Host Workshop Maps", "2.0", 0)

std::string HostWorkshopMaps::MapNameFromPath(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string f = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = f.find_last_of('.');
    return (dot == std::string::npos) ? f : f.substr(0, dot);
}

void HostWorkshopMaps::SetStatus(const std::string& msg) {
    statusMsg_ = msg;
    cvarManager->log("HostWorkshopMaps: " + msg);
}

static void SafeWalkDir(const std::string& dir, std::vector<MapEntry>& out) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;

        std::string full = dir + "\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        std::string name = fd.cFileName;
        size_t dot = name.find_last_of('.');
        if (dot == std::string::npos) continue;

        std::string ext = name.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == "upk" || ext == "udk") {
            MapEntry me;
            me.fullPath = full;
            for (char& c : me.fullPath) if (c == '\\') c = '/';
            me.displayName = name.substr(0, dot);
            out.push_back(me);
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

void HostWorkshopMaps::onLoad() {
    auto dirCvar = cvarManager->registerCvar("hwm_maps_directory", "", "Full path to your maps folder", true, true, 0, true, 0, true);
    dirCvar.addOnValueChanged([this](std::string, CVarWrapper cvar){
        mapsDirectory_ = cvar.getStringValue();
        if (!mapsDirectory_.empty()) ScanMaps();
    });

    cvarManager->registerNotifier("hwm_scan", [this](std::vector<std::string>){ ScanMaps(); }, "Scan maps", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_load", [this](std::vector<std::string> params){
        if (params.empty()) {
            cvarManager->log("Usage: hwm_load <number>");
            return;
        }
        LoadMap(std::stoi(params[0]), false);
    }, "Load map (Solo)", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_lan", [this](std::vector<std::string> params){
        if (params.empty()) {
            cvarManager->log("Usage: hwm_lan <number>");
            return;
        }
        LoadMap(std::stoi(params[0]), true);
    }, "Load map (LAN)", PERMISSION_ALL);

    cvarManager->log("HostWorkshopMaps loaded. Set path with: hwm_maps_directory \"D:/RLMAPS\"");
}

void HostWorkshopMaps::onUnload() {}

void HostWorkshopMaps::ScanMaps() {
    mapList_.clear();
    if (mapsDirectory_.empty()) {
        SetStatus("No directory set. Use: hwm_maps_directory \"full/path\"");
        return;
    }

    DWORD attr = GetFileAttributesA(mapsDirectory_.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        SetStatus("Directory not found: " + mapsDirectory_);
        return;
    }

    SafeWalkDir(mapsDirectory_, mapList_);
    std::sort(mapList_.begin(), mapList_.end(), [](const MapEntry& a, const MapEntry& b){
        return a.displayName < b.displayName;
    });

    SetStatus(std::to_string(mapList_.size()) + " maps found");
}

void HostWorkshopMaps::LoadMap(int index, bool isLAN) {
    if (index < 0 || index >= (int)mapList_.size()) {
        SetStatus("Invalid map number");
        return;
    }

    const auto& m = mapList_[index];

    if (isLAN && gameWrapper->IsInGame()) {
        ServerWrapper server = gameWrapper->GetCurrentGameState();
        if (!server.IsNull() && server.HasAuthority()) {
            SetStatus("LAN: Changing to " + m.displayName);
            gameWrapper->ExecuteUnrealCommand("servertravel \"" + m.fullPath + "\"");
            return;
        }
    }

    SetStatus("Loading: " + m.displayName);
    cvarManager->executeCommand("load_workshop \"" + m.fullPath + "\"", false);
}
