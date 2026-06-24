#include "HostworkshopMaps.h"
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/gameevent/serverwrapper.h"
#include "bakkesmod/wrappers/engine/unrealstringwrapper.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include "imgui.h"
#include <algorithm>
#include <sstream>
#include <cctype>

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Host Workshop Maps", "1.5", PLUGINTYPE_FREEPLAY)

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
    if (!cvarManager || !gameWrapper) return;

    cvarManager->log("HostWorkshopMaps: loading v1.5 (stability fixes)");

    std::string defPath = DefaultWorkshopPath();

    cvarManager->registerCvar("hwm_maps_directory", defPath,
        "Folder to scan for .upk/.udk workshop maps", true)
        .addOnValueChanged([this](std::string, CVarWrapper cv) {
            if (this) {
                mapsDirectory_ = SanitizePath(cv.getStringValue());
                strncpy_s(dirBuf_, mapsDirectory_.c_str(), sizeof(dirBuf_) - 1);
                if (gameWrapper) gameWrapper->SetTimeout([this](GameWrapper*) { ScanMaps(); }, 0.5f);
            }
        });

    mapsDirectory_ = SanitizePath(cvarManager->getCvar("hwm_maps_directory").getStringValue());
    strncpy_s(dirBuf_, mapsDirectory_.c_str(), sizeof(dirBuf_) - 1);

    cvarManager->registerCvar("hwm_auto_scan", "1",
        "Auto-scan on open", true, true, 0, true, 1)
        .addOnValueChanged([this](std::string, CVarWrapper cv) {
            if (this) autoScanOnOpen_ = cv.getBoolValue();
        });
    autoScanOnOpen_ = cvarManager->getCvar("hwm_auto_scan").getBoolValue();

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

    StartTransportTimer();

    gameWrapper->SetTimeout([this](GameWrapper*) {
        if (autoScanOnOpen_) ScanMaps();
        SetStatus("Ready — " + std::to_string(mapList_.size()) + " maps found");
    }, 2.5f);

    cvarManager->log("HostWorkshopMaps: loaded successfully");
}

void HostWorkshopMaps::onUnload()
{
    pendingLANTransport_ = false;
}

void HostWorkshopMaps::StartTransportTimer()
{
    if (!gameWrapper) return;
    gameWrapper->SetTimeout([this](GameWrapper* gw) {
        OnTick("");
        if (pendingLANTransport_ && gw) {
            gw->SetTimeout([this](GameWrapper* gw2) { StartTransportTimer(); }, 0.033f);
        }
    }, 0.5f);
}

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
                me.extension   = ext.substr(1);
                me.fullPath    = SanitizePath(entry.path().string());
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

void HostWorkshopMaps::OnTick(std::string)
{
    if (!pendingLANTransport_) return;
    if (--transportCountdown_ > 0) return;
    pendingLANTransport_ = false;
    TeleportLANPlayers(pendingMapPath_);
    pendingMapPath_.clear();
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

    // Directory bar
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 130);
    if (ImGui::InputText("##dir", dirBuf_, sizeof(dirBuf_), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (cvarManager) cvarManager->getCvar("hwm_maps_directory").setValue(std::string(dirBuf_));
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Apply", ImVec2(60, 0)) && cvarManager)
        cvarManager->getCvar("hwm_maps_directory").setValue(std::string(dirBuf_));
    ImGui::SameLine();
    if (ImGui::Button("Scan", ImVec2(55, 0)))
        ScanMaps();

    ImGui::Spacing();

    // Search
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputTextWithHint("##filter", "Search maps...", filterBuf_, sizeof(filterBuf_)))
        filterText_ = filterBuf_;

    ImGui::Spacing();

    auto filtered = FilteredMaps();
    float listHeight = ImGui::GetContentRegionAvail().y - 110;
    ImGui::BeginChild("##maplist", ImVec2(0, listHeight), true);

    if (filtered.empty()) {
        ImGui::Spacing();
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - 280) * 0.5f);
        ImGui::TextDisabled(mapList_.empty() ? "No maps found. Set directory and click Scan." : "No maps match your search.");
    } else {
        for (int i = 0; i < (int)filtered.size(); ++i) {
            auto& m = filtered[i];
            bool sel = (selectedIndex_ == i);

            if (sel) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.6f, 1.0f, 0.4f));

            std::string label = m.displayName + "  ##" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                selectedIndex_ = i;
                if (ImGui::IsMouseDoubleClicked(0))
                    LoadMap(filtered[i]);
            }

            if (sel) ImGui::PopStyleColor();

            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 35);
            ImGui::TextDisabled(".%s", m.extension.c_str());

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", m.fullPath.c_str());
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    bool canLoad = (selectedIndex_ >= 0 && selectedIndex_ < (int)filtered.size());
    if (!canLoad) ImGui::BeginDisabled();
    if (ImGui::Button("Load Map", ImVec2(120, 0)) && canLoad)
        LoadMap(filtered[selectedIndex_]);
    if (!canLoad) ImGui::EndDisabled();

    ImGui::SameLine();

    if (pendingLANTransport_) {
        ImGui::TextColored(ImVec4(1,0.8f,0,1), "Teleporting... (%d)", transportCountdown_);
    } else if (gameWrapper && gameWrapper->IsInGame()) {
        try {
            ServerWrapper server = gameWrapper->GetCurrentGameState();
            if (!server.IsNull() && server.HasAuthority()) {
                int remote = 0;
                ArrayWrapper<PriWrapper> pris = server.GetPRIs();
                for (int i = 0; i < pris.Count(); ++i) {
                    PriWrapper pri = pris.Get(i);
                    if (!pri.IsNull() && !pri.IsLocalPlayerPRI()) remote++;
                }
                if (remote > 0)
                    ImGui::TextColored(ImVec4(0.2f,1,0.5f,1), "LAN host | %d players", remote);
            }
        } catch (...) {}
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
    ImGui::TextDisabled("%d map(s)", (int)filtered.size());

    ImGui::Spacing();
    if (!statusMsg_.empty())
        ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "%s", statusMsg_.c_str());

    ImGui::End();
}
