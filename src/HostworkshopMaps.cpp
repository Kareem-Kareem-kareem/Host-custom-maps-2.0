#include "HostWorkshopMaps.h"
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/gameevent/serverwrapper.h"
#include <algorithm>
#include <cctype>
#include <functional>

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
    statusMsg_ = msg;
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
    cvarManager->registerCvar("hwm_maps_directory", "", "Maps folder (absolute path)", true, true, 0, true, 0, true)
        .addOnValueChanged(std::bind(&HostWorkshopMaps::OnCvarChanged, this, std::placeholders::_1, std::placeholders::_2));

    cvarManager->registerCvar("hwm_auto_scan", "1", "Auto scan on directory change", true, true, 0, true, 1);

    // Register commands from .set file
    cvarManager->registerNotifier("hwm_scan", [this](std::vector<std::string> params){ ScanMaps(); }, "Scan / Refresh map list", PERMISSION_ALL);
    cvarManager->registerNotifier("hwm_list", [this](std::vector<std::string> params){
        for (const auto& m : mapList_) cvarManager->log(m.displayName + " -> " + m.fullPath);
    }, "List maps in console", PERMISSION_ALL);

    // Tick hook for LAN countdown
    gameWrapper->HookEvent("Function TAGame.GameViewportClient_TA.Tick", std::bind(&HostWorkshopMaps::OnTick, this, std::placeholders::_1));

    cvarManager->log("HostWorkshopMaps: loaded successfully");
}

void HostWorkshopMaps::onUnload() {
    gameWrapper->UnhookEvent("Function TAGame.GameViewportClient_TA.Tick");
}

void HostWorkshopMaps::OnCvarChanged(const std::string& cvarName, CVarWrapper cvar) {
    if (cvarName == "hwm_maps_directory") {
        mapsDirectory_ = SanitizePath(cvar.getStringValue());
        if (!mapsDirectory_.empty() && cvarManager->getCvar("hwm_auto_scan").getBoolValue()) {
            ScanMaps();
        }
    }
}

void HostWorkshopMaps::ScanMaps() {
    mapList_.clear();
    selectedIndex_ = 0;

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
    std::sort(mapList_.begin(), mapList_.end(),
        [](const MapEntry& a, const MapEntry& b){ return a.displayName < b.displayName; });

    SetStatus(std::to_string(mapList_.size()) + " maps found");
}

void HostWorkshopMaps::LoadMapPath(const std::string& path) {
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
                pendingMapPath_ = path;
                pendingLANTransport_ = true;
                transportCountdown_ = 60;  // frames (~0.5s at 120 tick)
                return;
            }
        }
    }

    SetStatus("Loading: " + MapNameFromPath(path));
    cvarManager->executeCommand("load_workshop_map \"" + path + "\"", false);
}

void HostWorkshopMaps::TeleportLANPlayers(const std::string& path) {
    if (!gameWrapper->IsInGame()) return;
    ServerWrapper server = gameWrapper->GetCurrentGameState();
    if (server.IsNull() || !server.HasAuthority()) return;
    gameWrapper->ExecuteUnrealCommand("servertravel \"" + path + "\"");
}

void HostWorkshopMaps::OnTick(std::string) {
    if (!pendingLANTransport_) return;
    if (--transportCountdown_ > 0) return;

    pendingLANTransport_ = false;
    TeleportLANPlayers(pendingMapPath_);
    pendingMapPath_.clear();
}
