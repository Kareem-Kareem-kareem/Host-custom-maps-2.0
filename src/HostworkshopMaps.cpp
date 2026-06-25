#include "HostWorkshopMaps.h"
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/gameevent/serverwrapper.h"
#include "bakkesmod/wrappers/canvaswrapper.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Host Workshop Maps", "1.6", PLUGINTYPE_FREEPLAY)

// ─── Helpers ─────────────────────────────────────────────────────────────────

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
    std::string filename = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = filename.find_last_of('.');
    return (dot == std::string::npos) ? filename : filename.substr(0, dot);
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

static void WalkDir(const std::string& dir, std::vector<MapEntry>& out)
{
    std::string pattern = dir + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        std::string fullPath = dir + "\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            WalkDir(fullPath, out);
        } else {
            std::string name = fd.cFileName;
            size_t dot = name.find_last_of('.');
            if (dot == std::string::npos) continue;
            std::string ext = name.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
            if (ext == "upk" || ext == "udk") {
                MapEntry me;
                me.extension = ext;
                me.fullPath = fullPath;
                for (char& c : me.fullPath) if (c == '\\') c = '/';
                me.displayName = name.substr(0, dot);
                out.push_back(me);
            }
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
}

// ─── onLoad ──────────────────────────────────────────────────────────────────

void HostWorkshopMaps::onLoad()
{
    cvarManager->registerCvar("hwm_maps_directory", "", "Maps folder", true);
    cvarManager->registerCvar("hwm_auto_scan", "1", "Auto-scan", true);

    cvarManager->registerNotifier("hwm_open", [this](std::vector<std::string>) {
        isWindowOpen_ = !isWindowOpen_;
    }, "Toggle Host Workshop Maps window", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_scan", [this](std::vector<std::string>) {
        ScanMaps();
    }, "Scan maps directory", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_load_path", [this](std::vector<std::string> args) {
        if (args.size() < 2) return;
        std::string path;
        for (size_t i = 1; i < args.size(); ++i) { if (i > 1) path += " "; path += args[i]; }
        LoadMapPath(SanitizePath(path));
    }, "Load map by path", PERMISSION_ALL);

    // Render hook — fires every frame on the game thread, safe for ImGui
    gameWrapper->RegisterDrawable([this](CanvasWrapper canvas) {
        OnRender(canvas);
    });

    gameWrapper->HookEvent("Function TAGame.Car_TA.SetVehicleInput",
        [this](std::string e) { OnTick(e); });

    gameWrapper->SetTimeout([this](GameWrapper*) {
        mapsDirectory_ = SanitizePath(
            cvarManager->getCvar("hwm_maps_directory").getStringValue());
        autoScanOnOpen_ = cvarManager->getCvar("hwm_auto_scan").getBoolValue();
        strncpy_s(dirBuf_, mapsDirectory_.c_str(), sizeof(dirBuf_) - 1);
        if (!mapsDirectory_.empty()) ScanMaps();
        else SetStatus("Set a maps directory and click Scan");
    }, 5.0f);

    cvarManager->log("HostWorkshopMaps: loaded — use hwm_open or bind a key to open the window");
}

void HostWorkshopMaps::onUnload()
{
    gameWrapper->UnregisterDrawables();
    gameWrapper->UnhookEvent("Function TAGame.Car_TA.SetVehicleInput");
}

// ─── ScanMaps ────────────────────────────────────────────────────────────────

void HostWorkshopMaps::ScanMaps()
{
    mapList_.clear();
    selectedIndex_ = -1;
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

// ─── LoadMapPath ─────────────────────────────────────────────────────────────

void HostWorkshopMaps::LoadMapPath(const std::string& path)
{
    if (path.empty()) { SetStatus("No map selected"); return; }
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) { SetStatus("File not found: " + path); return; }

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

void HostWorkshopMaps::TeleportLANPlayers(const std::string& mapPath)
{
    if (!gameWrapper->IsInGame()) return;
    ServerWrapper server = gameWrapper->GetCurrentGameState();
    if (server.IsNull() || !server.HasAuthority()) return;
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

// ─── ImGui render (via RegisterDrawable) ─────────────────────────────────────

void HostWorkshopMaps::OnRender(CanvasWrapper canvas)
{
    if (!isWindowOpen_) return;

    ImGui::SetNextWindowSize(ImVec2(620, 540), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(100, 100),  ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    if (!ImGui::Begin("Host Workshop Maps", &isWindowOpen_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End(); return;
    }

    // Directory bar
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 130);
    bool dirEnter = ImGui::InputText("##dir", dirBuf_, sizeof(dirBuf_), ImGuiInputTextFlags_EnterReturnsTrue);
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
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputTextWithHint("##filter", "Search maps...", filterBuf_, sizeof(filterBuf_)))
        filterText_ = filterBuf_;
    ImGui::Spacing();

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
                if (ImGui::IsMouseDoubleClicked(0)) LoadMapPath(filtered[i].fullPath);
            }
            if (sel) ImGui::PopStyleColor();
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30);
            ImGui::TextDisabled(".%s", m.extension.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m.fullPath.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::Spacing();

    bool canLoad = (selectedIndex_ >= 0 && selectedIndex_ < (int)filtered.size());
    if (!canLoad) ImGui::BeginDisabled();
    if (ImGui::Button("Load Map", ImVec2(120, 0)) && canLoad)
        LoadMapPath(filtered[selectedIndex_].fullPath);
    if (!canLoad) ImGui::EndDisabled();

    ImGui::SameLine();
    if (pendingLANTransport_) {
        ImGui::TextColored(ImVec4(1,0.8f,0,1), "Teleporting... (%d)", transportCountdown_);
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
                ImGui::TextColored(ImVec4(0.2f,1,0.5f,1), "LAN host | %d player(s)", remote);
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
