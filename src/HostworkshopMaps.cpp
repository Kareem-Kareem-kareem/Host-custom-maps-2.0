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

std::string HostWorkshopMaps::AutoDetectMapsPath() {
    std::vector<std::string> commonPaths = {
        "C:\\Program Files\\Epic Games\\rocketleague\\TAGame\\CookedPCConsole\\Mods",
        "C:\\Program Files\\Epic Games\\rocketleague\\TAGame\\CookedPCConsole\\maps",
        "C:\\RLMAPS",
        "D:\\RLMAPS",
        "C:\\Games\\rocketleague\\TAGame\\CookedPCConsole\\Mods",
        "D:\\Games\\rocketleague\\TAGame\\CookedPCConsole\\Mods"
    };

    for (const auto& p : commonPaths) {
        if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return p;
        }
    }
    return "C:\\RLMAPS"; // fallback
}

// Safe non-recursive scan
static void WalkDir(const std::string& root, std::vector<MapEntry>& out) {
    std::stack<std::pair<std::string, int>> dirs;
    dirs.push({root, 0});

    while (!dirs.empty()) {
        auto [dir, depth] = dirs.top();
        dirs.pop();
        if (depth > 6) continue;

        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;

        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;

            std::string full = dir + "\\" + fd.cFileName;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                dirs.push({full, depth + 1});
            } else {
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
                    }
                }
            }
        } while (FindNextFileA(h, &fd));

        FindClose(h);
    }
}

void HostWorkshopMaps::onLoad() {
    mapsDirectory_ = AutoDetectMapsPath();
    ScanMaps();   // try to scan immediately

    cvarManager->registerNotifier("hwm_scan", [this](std::vector<std::string>){ ScanMaps(); }, "Scan maps", PERMISSION_ALL);

    cvarManager->log("HostWorkshopMaps loaded - using path: " + mapsDirectory_);
}

void HostWorkshopMaps::onUnload() {}

void HostWorkshopMaps::ScanMaps() {
    mapList_.clear();

    if (mapsDirectory_.empty()) {
        SetStatus("No maps directory");
        return;
    }

    DWORD attr = GetFileAttributesA(mapsDirectory_.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        SetStatus("Directory not found: " + mapsDirectory_);
        return;
    }

    SetStatus("Scanning...");
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
