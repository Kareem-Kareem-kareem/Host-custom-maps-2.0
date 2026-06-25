#include "HostWorkshopMaps.h"
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/gameevent/serverwrapper.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Host Workshop Maps", "1.3", PLUGINTYPE_FREEPLAY)

// ─── Helpers ────────────────────────────────────────────────────────────────

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

void HostWorkshopMaps::SetStatus(const std::string& msg)
{
    statusMsg_ = msg;
    cvarManager->log("HostWorkshopMaps: " + msg);
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

// ─── onLoad ─────────────────────────────────────────────────────────────────

void HostWorkshopMaps::onLoad()
{
    // ── CVars ── register only, NO filesystem work here ─────────────────
    cvarManager->registerCvar("hwm_maps_directory", "",
        "Folder to scan for .upk/.udk maps", true, false, 0, false, 0, true);

    cvarManager->registerCvar("hwm_auto_scan", "1",
        "Auto-scan when window opens", true, true, 0, true, 1, true);

    // ── Notifiers ────────────────────────────────────────────────────────
    cvarManager->registerNotifier("hwm_scan", [this](std::vector<std::string>) {
        ScanMaps();
    }, "Scan maps directory", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_load_path", [this](std::vector<std::string> args) {
        if (args.size() < 2) return;
        std::string path;
        for (size_t i = 1; i < args.size(); ++i) { if (i > 1) path += " "; path += args[i]; }
        LoadMapPath(SanitizePath(path));
    }, "Load map by path", PERMISSION_ALL);

    // ── Tick hook ────────────────────────────────────────────────────────
    gameWrapper->HookEvent("Function TAGame.Car_TA.SetVehicleInput",
        [this](std::string e) { OnTick(e); });

    // ── Read cvars AFTER everything is registered ────────────────────────
    // Use SetTimeout so we read saved values after BM finishes loading cfg
    gameWrapper->SetTimeout([this](GameWrapper*) {
        mapsDirectory_ = SanitizePath(
            cvarManager->getCvar("hwm_maps_directory").getStringValue());
        autoScanOnOpen_ = cvarManager->getCvar("hwm_auto_scan").getBoolValue();

        // Sync dir buffer for the UI
        strncpy_s(dirBuf_, mapsDirectory_.c_str(), sizeof(dirBuf_) - 1);

        if (!mapsDirectory_.empty())
            ScanMaps();
        else
            SetStatus("Set a maps directory and click Scan");
    }, 3.0f);

    cvarManager->log("HostWorkshopMaps: loaded — press F6 or use togglemenu hostworkshopmaps");
}

void HostWorkshopMaps::onUnload()
{
    gameWrapper->UnhookEvent("Function TAGame.Car_TA.SetVehicleInput");
}

// ─── ScanMaps ────────────────────────────────────────────────────────────────

void HostWorkshopMaps::ScanMaps()
{
    mapList_.clear();
    selectedIndex_ = -1;

    if (mapsDirectory_.empty()) { SetStatus("No directory set — enter one above and click Apply"); return; }

    fs::path dir(mapsDirectory_);
    if (!fs::exists(dir)) { SetStatus("Directory not found: " + mapsDirectory_); return; }

    try {
        for (auto& entry : fs::recursive_directory_iterator(
                dir, fs::directory_options::skip_permission_denied))
        {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](unsigned char c){ return std::tolower(c); });
            if (ext == ".upk" || ext == ".udk") {
                MapEntry me;
                me.extension   = ext.substr(1);
                me.fullPath    = SanitizePath(entry.path().string());
                me.displayName = MapNameFromPath(me.fullPath);
                mapList_.push_back(me);
            }
        }
    } catch (const std::exception& ex) {
        SetStatus(std::string("Scan error: ") + ex.what());
        return;
    }

    std::sort(mapList_.begin(), mapList_.end(),
        [](const MapEntry& a, const MapEntry& b){ return a.displayName < b.displayName; });

    SetStatus(std::to_string(mapList_.size()) + " maps found");
}

// ─── LoadMapPath ─────────────────────────────────────────────────────────────

void HostWorkshopMaps::LoadMapPath(const std::string& path)
{
    if (path.empty()) { SetStatus("No map selected"); return; }

    if (!fs::exists(fs::path(path))) { SetStatus("File not found: " + path); return; }

    // Check for LAN host with remote players
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
                pendingMapPath_      = path;
                pendingLANTransport_ = true;
                transportCountdown_  = 60;
                return;
            }
        }
    }

    SetStatus("Loading: " + MapNameFromPath(path));
    // load_workshop_map is BakkesMod's built-in command for .upk/.udk files
    cvarManager->executeCommand("load_workshop_map \"" + path + "\"", false);
}

void HostWorkshopMaps::TeleportLANPlayers(const std::string& mapPath)
{
    if (!gameWrapper->IsInGame()) return;
    ServerWrapper server = gameWrapper->GetCurrentGameState();
    if (server.IsNull() || !server.HasAuthority()) return;
    SetStatus("ServerTravel to: " + MapNameFromPath(mapPath));
    gameWrapper->ExecuteUnrealCommand("servertravel \"" + mapPath + "\"");
}

void HostWorkshopMaps::OnTick(std::string)
{
    if (!pendingLANTransport_) return;
    if (--transportCountdown_ > 0) return;
    pendingLANTransport_ = false;
    TeleportLANPlayers(pendingMapPath_);
    pendingMapPath_.clear();
}

// ─── ImGui window ────────────────────────────────────────────────────────────

void HostWorkshopMaps::SetImGuiContext(uintptr_t ctx)
{
    ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

void HostWorkshopMaps::Render()
{
    if (!isWindowOpen_) return;

    ImGui::SetNextWindowSize(ImVec2(620, 540), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(100, 100),  ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    if (!ImGui::Begin("Host Workshop Maps", &isWindowOpen_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // ── Directory bar ──────────────────────────────────────────────────
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 130);
    bool dirEnter = ImGui::InputText("##dir", dirBuf_, sizeof(dirBuf_),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Apply", ImVec2(60, 0)) || dirEnter) {
        mapsDirectory_ = SanitizePath(std::string(dirBuf_));
        cvarManager->getCvar("hwm_maps_directory").setValue(mapsDirectory_);
        ScanMaps();
    }
    ImGui::SameLine();
    if (ImGui::Button("Scan", ImVec2(55, 0))) ScanMaps();

    ImGui::Spacing();

    // ── Search ─────────────────────────────────────────────────────────
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputTextWithHint("##filter", "Search maps...", filterBuf_, sizeof(filterBuf_)))
        filterText_ = filterBuf_;

    ImGui::Spacing();

    // ── Map list ───────────────────────────────────────────────────────
    auto filtered = FilteredMaps();
    float listHeight = ImGui::GetContentRegionAvail().y - 85;
    ImGui::BeginChild("##maplist", ImVec2(0, listHeight), true);

    if (filtered.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled(mapList_.empty()
            ? "No maps found. Set your directory and click Scan."
            : "No maps match your search.");
    } else {
        for (int i = 0; i < (int)filtered.size(); ++i) {
            auto& m = filtered[i];
            bool sel = (selectedIndex_ == i);

            if (sel) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.6f, 1.0f, 0.4f));

            std::string label = m.displayName + "##" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                selectedIndex_ = i;
                if (ImGui::IsMouseDoubleClicked(0))
                    LoadMapPath(filtered[i].fullPath);
            }

            if (sel) ImGui::PopStyleColor();

            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30);
            ImGui::TextDisabled(".%s", m.extension.c_str());

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", m.fullPath.c_str());
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // ── Bottom bar ─────────────────────────────────────────────────────
    bool canLoad = (selectedIndex_ >= 0 && selectedIndex_ < (int)filtered.size());
    if (!canLoad) ImGui::BeginDisabled();
    if (ImGui::Button("Load Map", ImVec2(120, 0)) && canLoad)
        LoadMapPath(filtered[selectedIndex_].fullPath);
    if (!canLoad) ImGui::EndDisabled();

    ImGui::SameLine();

    // LAN status
    if (pendingLANTransport_) {
        ImGui::TextColored(ImVec4(1,0.8f,0,1), "Teleporting LAN players... (%d)", transportCountdown_);
    } else if (gameWrapper->IsInGame()) {
        ServerWrapper server = gameWrapper->GetCurrentGameState();
        if (!server.IsNull() && server.HasAuthority()) {
            ArrayWrapper<PriWrapper> pris = server.GetPRIs();
            int remote = 0;
            for (int i = 0; i < pris.Count(); ++i) {
                PriWrapper pri = pris.Get(i);
                if (!pri.IsNull() && !pri.IsLocalPlayerPRI()) remote++;
            }
            if (remote > 0)
                ImGui::TextColored(ImVec4(0.2f,1,0.5f,1), "LAN host  |  %d player(s)", remote);
            else
                ImGui::TextDisabled("Hosting (no guests yet)");
        }
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70);
    ImGui::TextDisabled("%d map(s)", (int)filtered.size());

    ImGui::Spacing();
    if (!statusMsg_.empty())
        ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "%s", statusMsg_.c_str());

    ImGui::End();
}
