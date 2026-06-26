#include "HostWorkshopMaps.h"
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/gameevent/serverwrapper.h"
#include <algorithm>
#include <cctype>
#include <stack>

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

// Non-recursive WalkDir to prevent stack overflow
static void WalkDir(const std::string& root, std::vector<MapEntry>& out) {
    std::stack<std::string> dirs;
    dirs.push(root);

    while (!dirs.empty()) {
        std::string dir = dirs.top();
        dirs.pop();

        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;

        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;

            std::string full = dir + "\\" + fd.cFileName;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                dirs.push(full);  // Add subdirectory
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
}

void HostWorkshopMaps::onLoad() {
    auto dirCvar = cvarManager->registerCvar("hwm_maps_directory", "", "Path to custom maps folder", true, true, 0, true, 0, true);
    dirCvar.addOnValueChanged(std::bind(&HostWorkshopMaps::OnCvarChanged, this, std::placeholders::_1, std::placeholders::_2));

    // Only command: hwm_scan
    cvarManager->registerNotifier("hwm_scan", [this](std::vector<std::string>){ ScanMaps(); }, "Scan / Refresh map list", PERMISSION_ALL);

    // Auto scan if path already saved
    std::string prev = dirCvar.getStringValue();
    if (!prev.empty()) {
        mapsDirectory_ = SanitizePath(prev);
        ScanMaps();
    }

    cvarManager->log("HostWorkshopMaps loaded successfully");
}

void HostWorkshopMaps::onUnload() {}

void HostWorkshopMaps::OnCvarChanged(const std::string& cvarName, CVarWrapper cvar) {
    if (cvarName == "hwm_maps_directory") {
        mapsDirectory_ = SanitizePath(cvar.getStringValue());
        if (!mapsDirectory_.empty()) ScanMaps();
    }
}

void HostWorkshopMaps::ScanMaps() {
    mapList_.clear();
    if (mapsDirectory_.empty()) {
        SetStatus("No directory set");
        return;
    }

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
    if (path.empty()) return;
    SetStatus("Loading: " + MapNameFromPath(path));
    cvarManager->executeCommand("load_workshop_map \"" + path + "\"", false);
}
