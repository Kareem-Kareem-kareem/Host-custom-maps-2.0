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

static void WalkDir(const std::string& dir, std::vector<MapEntry>& out) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;

        std::string full = dir + "\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            WalkDir(full, out);
        } else {
            std::string name = fd.cFileName;
            size_t dot = name.find_last_of('.');
            if (dot == std::string::npos) continue;

            std::string ext = name.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == "upk" || ext == "udk") {
                MapEntry me;
                me.extension = ext;
                me.fullPath = full;
                for (char& c : me.fullPath) if (c == '\\') c = '/';
                me.displayName = name.substr(0, dot);
                out.push_back(me);
            }
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

void HostWorkshopMaps::onLoad() {
    mapsDirectory_ = SanitizePath(mapsDirectory_);  // "C:/RLMAPS"

    cvarManager->registerNotifier("hwm_scan", [this](std::vector<std::string>){ ScanMaps(); }, "Scan maps", PERMISSION_ALL);
    cvarManager->registerNotifier("hwm_list", [this](std::vector<std::string>){
        for (const auto& m : mapList_) cvarManager->log(m.displayName + " -> " + m.fullPath);
    }, "List maps", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_load", [this](std::vector<std::string> params){
        if (!params.empty()) LoadMapPath(params[0]);
        else cvarManager->log("Usage: hwm_load \"full/path/to/map.udk\"");
    }, "Load map", PERMISSION_ALL);

    ScanMaps();  // Auto scan on load

    cvarManager->log("HostWorkshopMaps loaded - using hardcoded path: " + mapsDirectory_);
}

void HostWorkshopMaps::onUnload() {}

void HostWorkshopMaps::ScanMaps() {
    mapList_.clear();

    DWORD attr = GetFileAttributesA(mapsDirectory_.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        SetStatus("Directory not found: " + mapsDirectory_);
        return;
    }

    WalkDir(mapsDirectory_, mapList_);
    std::sort(mapList_.begin(), mapList_.end(), [](const MapEntry& a, const MapEntry& b){
        return a.displayName < b.displayName;
    });

    SetStatus(std::to_string(mapList_.size()) + " maps found");
}

void HostWorkshopMaps::LoadMapPath(const std::string& path) {
    if (path.empty()) { SetStatus("No map selected"); return; }
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        SetStatus("File not found: " + path); return;
    }

    SetStatus("Loading: " + MapNameFromPath(path));
    cvarManager->executeCommand("load_workshop_map \"" + path + "\"", false);
}
