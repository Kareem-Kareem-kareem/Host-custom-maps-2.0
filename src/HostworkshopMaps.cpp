#include "HostWorkshopMaps.h"
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/gameevent/serverwrapper.h"
#include <algorithm>
#include <cctype>

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Host Workshop Maps", "1.8", PLUGINTYPE_FREEPLAY)

std::string HostWorkshopMaps::SanitizePath(const std::string& raw)
{
    std::string out = raw;
    for (char& c : out) if (c == '\\') c = '/';
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

std::string HostWorkshopMaps::MapNameFromPath(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    std::string f = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = f.find_last_of('.');
    return (dot == std::string::npos) ? f : f.substr(0, dot);
}

void HostWorkshopMaps::SetStatus(const std::string& msg)
{
    statusMsg_ = msg;
    cvarManager->log("HostWorkshopMaps: " + msg);
}

static void WalkDir(const std::string& dir, std::vector<MapEntry>& out)
{
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

void HostWorkshopMaps::onLoad()
{
    cvarManager->registerCvar("hwm_maps_directory", "", "Maps folder", true);

    cvarManager->registerNotifier("hwm_scan", [this](std::vector<std::string>) {
        ScanMaps();
    }, "Scan maps directory", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_list", [this](std::vector<std::string>) {
        for (int i = 0; i < (int)mapList_.size(); ++i)
            cvarManager->log("[" + std::to_string(i) + "] " + mapList_[i].displayName);
    }, "List maps in console", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_load", [this](std::vector<std::string> args) {
        if (args.size() < 2) {
            if (!mapList_.empty()) LoadMapPath(mapList_[selectedIndex_].fullPath);
            return;
        }
        int idx = std::stoi(args[1]);
        if (idx >= 0 && idx < (int)mapList_.size()) {
            selectedIndex_ = idx;
            LoadMapPath(mapList_[idx].fullPath);
        }
    }, "Load map by index", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_next", [this](std::vector<std::string>) {
        if (mapList_.empty()) return;
        selectedIndex_ = (selectedIndex_ + 1) % mapList_.size();
        SetStatus("Selected [" + std::to_string(selectedIndex_) + "]: " + mapList_[selectedIndex_].displayName);
    }, "Select next map", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_prev", [this](std::vector<std::string>) {
        if (mapList_.empty()) return;
        selectedIndex_ = (selectedIndex_ - 1 + mapList_.size()) % mapList_.size();
        SetStatus("Selected [" + std::to_string(selectedIndex_) + "]: " + mapList_[selectedIndex_].displayName);
    }, "Select previous map", PERMISSION_ALL);


    cvarManager->log("HostWorkshopMaps: loaded");
}

void HostWorkshopMaps::onUnload()
{
}

void HostWorkshopMaps::ScanMaps()
{
    mapList_.clear(); selectedIndex_ = 0;
    if (mapsDirectory_.empty()) { SetStatus("No directory set"); return; }
    DWORD attr = GetFileAttributesA(mapsDirectory_.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        SetStatus("Directory not found: " + mapsDirectory_); return;
    }
    WalkDir(mapsDirectory_, mapList_);
    std::sort(mapList_.begin(), mapList_.end(),
        [](const MapEntry& a, const MapEntry& b){ return a.displayName < b.displayName; });
    SetStatus(std::to_string(mapList_.size()) + " maps found");
}

void HostWorkshopMaps::LoadMapPath(const std::string& path)
{
    if (path.empty()) { SetStatus("No map selected"); return; }
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        SetStatus("File not found: " + path); return;
    }
    if (gameWrapper->IsInGame()) {
        ServerWrapper server = gameWrapper->GetCurrentGameState();
        if (!server.IsNull() && server.HasAuthority()) {
            ArrayWrapper<PriWrapper> pris = server.GetPRIs();
            int remote = 0;
            for (int i = 0; i < pris.Count(); ++i) {
                PriWrapper pri = pris.Get(i);
                if (!pri.IsNull() && !pri.IsLocalPlayerPRI()) remote++;
            }
            if (remote > 0) {
                SetStatus("LAN: teleporting " + std::to_string(remote) + " player(s)...");
                pendingMapPath_ = path; pendingLANTransport_ = true; transportCountdown_ = 60;
                return;
            }
        }
    }
    SetStatus("Loading: " + MapNameFromPath(path));
    cvarManager->executeCommand("load_workshop_map \"" + path + "\"", false);
}

void HostWorkshopMaps::TeleportLANPlayers(const std::string& path)
{
    if (!gameWrapper->IsInGame()) return;
    ServerWrapper server = gameWrapper->GetCurrentGameState();
    if (server.IsNull() || !server.HasAuthority()) return;
    gameWrapper->ExecuteUnrealCommand("servertravel \"" + path + "\"");
}
