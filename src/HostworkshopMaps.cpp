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

void HostWorkshopMaps::ScanMaps() {
    mapList_.clear();

    std::string rlBase = "C:\\Program Files\\Epic Games\\rocketleague\\TAGame\\CookedPCConsole";
    std::string fullDir = rlBase + "\\" + subFolder_;

    DWORD attr = GetFileAttributesA(fullDir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        SetStatus("Folder not found: " + fullDir);
        return;
    }

    SafeWalkDir(fullDir, mapList_);
    std::sort(mapList_.begin(), mapList_.end(), [](const MapEntry& a, const MapEntry& b){
        return a.displayName < b.displayName;
    });

    SetStatus(std::to_string(mapList_.size()) + " maps found in '" + subFolder_ + "'");
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
            SetStatus("LAN teleport: " + m.displayName);
            gameWrapper->ExecuteUnrealCommand("servertravel \"" + m.fullPath + "\"");
            return;
        }
    }

    SetStatus("Loading solo: " + m.displayName);
    cvarManager->executeCommand("load_workshop \"" + m.fullPath + "\"", false);
}

void HostWorkshopMaps::onLoad() {
    // Main commands
    cvarManager->registerCvar("hwm_subfolder", "mods", "Subfolder name (mods, maps, Mods...)", true, true, 0, true, 0, true)
        .addOnValueChanged([this](std::string, CVarWrapper cvar) {
            subFolder_ = cvar.getStringValue();
            ScanMaps();
        });

    cvarManager->registerNotifier("hwm_scan", [this](std::vector<std::string>){ ScanMaps(); }, "Scan maps", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_load", [this](std::vector<std::string> params){
        if (params.empty()) {
            cvarManager->log("Usage: hwm_load <number>   (example: hwm_load 0)");
            return;
        }
        int idx = std::stoi(params[0]);
        LoadMap(idx, false);
    }, "Load map solo", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_lan", [this](std::vector<std::string> params){
        if (params.empty()) {
            cvarManager->log("Usage: hwm_lan <number>");
            return;
        }
        int idx = std::stoi(params[0]);
        LoadMap(idx, true);
    }, "Host LAN map change", PERMISSION_ALL);

    ScanMaps(); // initial scan with "mods"

    cvarManager->log("HostWorkshopMaps loaded (default = mods folder)");
    cvarManager->log("Commands: hwm_subfolder, hwm_scan, hwm_load <num>, hwm_lan <num>");
}

void HostWorkshopMaps::onUnload() {}
