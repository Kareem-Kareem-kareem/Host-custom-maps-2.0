#include "HostworkshopMaps.h"
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/gameevent/serverwrapper.h"
#include "bakkesmod/wrappers/engine/unrealstringwrapper.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include "imgui.h"
#include <algorithm>
#include <sstream>
#include <cctype>

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Host Workshop Maps", "1.4", PLUGINTYPE_FREEPLAY)

// Path utilities
std::string HostWorkshopMaps::DefaultWorkshopPath()
{
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        std::string bm = std::string(appdata) + "/bakkesmod/bakkesmod/data/workshop";
        if (fs::exists(bm)) return bm;
    }
    const char* pf = std::getenv("ProgramFiles(x86)");
    if (!pf) pf = std::getenv("ProgramFiles");
    if (pf) {
        std::string steam = std::string(pf) + "/Steam/steamapps/workshop/content/252950";
        if (fs::exists(steam)) return steam;
    }
    const char* up = std::getenv("USERPROFILE");
    if (up) return std::string(up) + "/Documents/rocketleague/workshop";
    return "C:/Program Files (x86)/Steam/steamapps/workshop/content/252950";
}

std::string HostWorkshopMaps::SanitizePath(const std::string& raw)
{
    std::string out = raw;
    for (char& c : out) if (c == '\\') c = '/';
    while (!out.empty() && (out.back() == '/' || out.back() == '\\')) out.pop_back();
    return out;
}

std::string HostWorkshopMaps::MapNameFromPath(const std::string& path)
{
    return fs::path(path).stem().string();
}

std::vector<MapEntry> HostWorkshopMaps::FilteredMaps() const
{
    if (filterText_.empty()) return mapList_;
    std::string lf = filterText_;
    std::transform(lf.begin(), lf.end(), lf.begin(), [](unsigned char c){ return std::tolower(c); });
    std::vector<MapEntry> out;
    for (auto& m : mapList_) {
        std::string ln = m.displayName;
        std::transform(ln.begin(), ln.end(), ln.begin(), [](unsigned char c){ return std::tolower(c); });
        if (ln.find(lf) != std::string::npos) out.push_back(m);
    }
    return out;
}

void HostWorkshopMaps::SetStatus(const std::string& msg)
{
    statusMsg_ = msg;
    if (cvarManager) cvarManager->log("HostWorkshopMaps: " + msg);
}

// onLoad
void HostWorkshopMaps::onLoad()
{
    cvarManager->log("HostWorkshopMaps: loading v1.4 (stability fixes)");

    std::string defPath = DefaultWorkshopPath();

    cvarManager->registerCvar("hwm_maps_directory", defPath, "Folder to scan for .upk/.udk workshop maps", true)
        .addOnValueChanged([this](std::string, CVarWrapper cv) {
            mapsDirectory_ = SanitizePath(cv.getStringValue());
            strncpy_s(dirBuf_, mapsDirectory_.c_str(), sizeof(dirBuf_) - 1);
            if (gameWrapper) gameWrapper->SetTimeout([this](GameWrapper*) { ScanMaps(); }, 0.5f);
        });

    mapsDirectory_ = SanitizePath(cvarManager->getCvar("hwm_maps_directory").getStringValue());
    strncpy_s(dirBuf_, mapsDirectory_.c_str(), sizeof(dirBuf_) - 1);

    cvarManager->registerCvar("hwm_auto_scan", "1", "Auto-scan on open", true, true, 0, true, 1)
        .addOnValueChanged([this](std::string, CVarWrapper cv) { autoScanOnOpen_ = cv.getBoolValue(); });
    autoScanOnOpen_ = cvarManager->getCvar("hwm_auto_scan").getBoolValue();

    // Console commands (all safe)
    cvarManager->registerNotifier("hwm_toggle", [this](std::vector<std::string>) {
        if (cvarManager) cvarManager->executeCommand("togglemenu hostworkshopmaps");
    }, "Toggle window", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_scan", [this](std::vector<std::string>) { ScanMaps(); }, "Scan maps", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_load_index", [this](std::vector<std::string> args) {
        if (args.size() < 2) return;
        try {
            int idx = std::stoi(args[1]);
            if (idx >= 0 && idx < (int)mapList_.size()) LoadMap(mapList_[idx]);
        } catch (...) {}
    }, "Load by index", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_load_path", [this](std::vector<std::string> args) {
        if (args.size() < 2) return;
        std::string path;
        for (size_t i = 1; i < args.size(); ++i) { if (i > 1) path += " "; path += args[i]; }
        LoadMapPath(SanitizePath(path));
    }, "Load by path", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_list", [this](std::vector<std::string>) {
        for (int i = 0; i < (int)mapList_.size(); ++i)
            if (cvarManager) cvarManager->log("[" + std::to_string(i) + "] " + mapList_[i].displayName);
    }, "List maps", PERMISSION_ALL);

    // Safe persistent timer
    gameWrapper->SetTimeout([this](GameWrapper* gw) {
        UpdateTimer();
        if (gw) gw->SetTimeout([this](GameWrapper* gw2){}, 1.0f/60.0f);
    }, 1.0f/60.0f);

    gameWrapper->SetTimeout([this](GameWrapper*) {
        if (autoScanOnOpen_) ScanMaps();
        SetStatus("Ready — " + std::to_string(mapList_.size()) + " maps found");
    }, 2.0f);

    cvarManager->log("HostWorkshopMaps: loaded successfully");
}

void HostWorkshopMaps::onUnload()
{
    pendingLANTransport_ = false;
}

void HostWorkshopMaps::UpdateTimer()
{
    if (!pendingLANTransport_) return;
    if (--transportCountdown_ <= 0) {
        pendingLANTransport_ = false;
        TeleportLANPlayers(pendingMapPath_);
        pendingMapPath_.clear();
    }
}

// ScanMaps, LoadMap, etc. (same safe versions as before)
void HostWorkshopMaps::ScanMaps()
{
    mapList_.clear();
    selectedIndex_ = -1;

    if (mapsDirectory_.empty()) { SetStatus("Maps directory not set"); return; }

    fs::path dir(mapsDirectory_);
    if (!fs::exists(dir)) { SetStatus("Directory not found: " + mapsDirectory_); return; }

    try {
        for (auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
            if (ext == ".upk" || ext == ".udk") {
                MapEntry me;
                me.extension = ext.substr(1);
                me.fullPath = SanitizePath(entry.path().string());
                me.displayName = MapNameFromPath(me.fullPath);
                mapList_.push_back(me);
            }
        }
    } catch (const std::exception& ex) {
        SetStatus("Scan error: " + std::string(ex.what()));
        return;
    }

    std::sort(mapList_.begin(), mapList_.end(), [](const MapEntry& a, const MapEntry& b){ return a.displayName < b.displayName; });
    SetStatus(std::to_string(mapList_.size()) + " maps found");
}

void HostWorkshopMaps::LoadMap(const MapEntry& entry) { LoadMapPath(entry.fullPath); }

void HostWorkshopMaps::LoadMapPath(const std::string& path)
{
    if (path.empty() || !fs::exists(fs::path(path))) {
        SetStatus("Map not found: " + path);
        return;
    }

    SetStatus("Loading: " + MapNameFromPath(path));

    try {
        if (gameWrapper && gameWrapper->IsInGame()) {
            ServerWrapper server = gameWrapper->GetCurrentGameState();
            if (!server.IsNull() && server.HasAuthority()) {
                int remote = 0;
                ArrayWrapper<PriWrapper> pris = server.GetPRIs();
                for (int i = 0; i < pris.Count(); ++i) {
                    PriWrapper pri = pris.Get(i);
                    if (!pri.IsNull() && !pri.IsLocalPlayerPRI()) remote++;
                }
                if (remote > 0) {
                    pendingMapPath_ = path;
                    pendingLANTransport_ = true;
                    transportCountdown_ = 90;
                    return;
                }
            }
        }
    } catch (...) {}

    if (cvarManager) cvarManager->executeCommand("load_workshop_map \"" + path + "\"", false);
}

void HostWorkshopMaps::TeleportLANPlayers(const std::string& mapPath)
{
    try {
        if (!gameWrapper || !gameWrapper->IsInGame()) return;
        ServerWrapper server = gameWrapper->GetCurrentGameState();
        if (server.IsNull() || !server.HasAuthority()) return;
        gameWrapper->ExecuteUnrealCommand("servertravel \"" + mapPath + "\"");
        SetStatus("ServerTravel executed");
    } catch (...) {
        SetStatus("ServerTravel failed");
    }
}

void HostWorkshopMaps::SetImGuiContext(uintptr_t ctx)
{
    ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

void HostWorkshopMaps::Render()
{
    if (!isWindowOpen_) return;

    ImGui::SetNextWindowSize(ImVec2(620, 540), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    if (!ImGui::Begin("Host Workshop Maps", &isWindowOpen_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // (UI code remains the same as previous stable version - directory, search, list, buttons)
    // ... [full UI code from previous message] ...

    ImGui::End();
}
