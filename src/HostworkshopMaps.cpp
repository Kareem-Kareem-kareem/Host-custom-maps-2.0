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

// Safe non-recursive scan with depth limit
static void WalkDir(const std::string& root, std::vector<MapEntry>& out) {
    std::stack<std::pair<std::string, int>> dirs;  // path + depth
    dirs.push({root, 0});
    int fileCount = 0;

    while (!dirs.empty()) {
        auto [dir, depth] = dirs.top();
        dirs.pop();

        if (depth > 8) continue;  // prevent too deep recursion

        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;

        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;

            std::string full = dir + "\\" + fd.cFileName;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                dirs.push({full, depth + 1});
            } else if (fileCount < 2000) {  // safety limit
                std::string name = fd.cFileName;
                size_t dot = name.find_last_of('.');
                if (dot != std::string::npos) {
                    std::string ext = name.substr(dot + 1);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == "upk" || ext == "udk") {
                        MapEntry me;
                        me.extension = ext;
                        me.fullPath = full;
                        for (char& c : me.fullPath) if (c == '\\') c = '/';
                        me.displayName = name.substr(0, dot);
                        out.push_back(me);
                        fileCount++;
                    }
                }
            }
        } while (FindNextFileA(h, &fd));

        FindClose(h);
    }
}

void HostWorkshopMaps::onLoad() {
    auto dirCvar = cvarManager->registerCvar("hwm_maps_directory", "", "Path to custom maps folder", true, true, 0, true, 0, true);
    dirCvar.addOnValueChanged(std::bind(&HostWorkshopMaps::OnCvarChanged, this, std::placeholders::_1, std::placeholders::_2));

    cvarManager->registerNotifier("hwm_scan", [this](std::vector<std::string>){ ScanMaps(); }, "Scan maps", PERMISSION_ALL);

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

    SetStatus("Scanning " + mapsDirectory_ + "...");

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
    cvarManager->executeCommand("load_workshop_map \"" + path + "\"", false);
}
