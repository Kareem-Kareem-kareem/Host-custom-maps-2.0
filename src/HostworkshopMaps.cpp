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

    int count = 0;
    do {
        if (count > 300) break;
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
            count++;
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

void HostWorkshopMaps::onLoad() {
    auto dirCvar = cvarManager->registerCvar("hwm_maps_directory", "", "Full path to maps folder", true, true, 0, true, 0, true);
    dirCvar.addOnValueChanged([this](std::string, CVarWrapper cvar) {
        mapsDirectory_ = cvar.getStringValue();
        if (!mapsDirectory_.empty()) ScanMaps();
    });

    cvarManager->registerNotifier("hwm_scan", [this](std::vector<std::string>){ ScanMaps(); }, "Refresh map list", PERMISSION_ALL);
    cvarManager->registerNotifier("hwm_list", [this](std::vector<std::string>){
        cvarManager->log("--- Available Maps ---");
        for (size_t i = 0; i < mapList_.size(); ++i) {
            cvarManager->log(std::to_string(i) + ": " + mapList_[i].displayName);
        }
    }, "List maps with numbers", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_load", [this](std::vector<std::string> params){
        if (params.empty()) {
            cvarManager->log("Usage: hwm_load <number>");
            return;
        }
        int idx = std::stoi(params[0]);
        LoadMap(idx, false);
    }, "Load Solo", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_lan", [this](std::vector<std::string> params){
        if (params.empty()) {
            cvarManager->log("Usage: hwm_lan <number>");
            return;
        }
        int idx = std::stoi(params[0]);
        LoadMap(idx, true);
    }, "Host LAN", PERMISSION_ALL);

    cvarManager->log("HostWorkshopMaps loaded. Use hwm_maps_directory first.");
}

void HostWorkshopMaps::onUnload() {}

void HostWorkshopMaps::ScanMaps() {
    mapList_.clear();
    if (mapsDirectory_.empty()) {
        SetStatus("Set hwm_maps_directory first");
        return;
    }

    DWORD attr = GetFileAttributesA(mapsDirectory_.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        SetStatus("Directory not found");
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
        SetStatus("Invalid map number! Use hwm_list first");
        return;
    }

    const auto& m = mapList_[index];

    if
