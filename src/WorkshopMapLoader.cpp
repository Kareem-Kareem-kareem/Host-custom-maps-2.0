#include "WorkshopMapLoader.h"
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/gameevent/serverwrapper.h"
#include "bakkesmod/wrappers/engine/unrealstringwrapper.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include "imgui.h"
#include <algorithm>
#include <sstream>
#include <cctype>

BAKKESMOD_PLUGIN(WorkshopMapLoader, "Workshop Map Loader", "1.1", PLUGINTYPE_FREEPLAY)

// ═══════════════════════════════════════════════════════════════════════════
//  Constants
// ═══════════════════════════════════════════════════════════════════════════
static constexpr int LAN_TRANSPORT_DELAY_TICKS = 60; // ~1 second at 60 fps

// ═══════════════════════════════════════════════════════════════════════════
//  Helpers – path utilities
// ═══════════════════════════════════════════════════════════════════════════

std::string WorkshopMapLoader::DefaultWorkshopPath()
{
    // Standard Steam Workshop location for Rocket League (UE3 era)
    // Tries APPDATA first, then common Steam paths
    const char* appdata = std::getenv("APPDATA");
    if (appdata)
    {
        // Some users have maps under %APPDATA%/bakkesmod/bakkesmod/data/workshop
        std::string bm = std::string(appdata) + "/bakkesmod/bakkesmod/data/workshop";
        if (fs::exists(bm)) return bm;
    }

    // Steam default library
    const char* programfiles = std::getenv("ProgramFiles(x86)");
    if (!programfiles) programfiles = std::getenv("ProgramFiles");
    if (programfiles)
    {
        std::string steam = std::string(programfiles)
            + "/Steam/steamapps/workshop/content/252950";
        if (fs::exists(steam)) return steam;
    }

    // Fallback: user's Documents
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile)
    {
        std::string docs = std::string(userprofile) + "/Documents/rocketleague/workshop";
        return docs; // return even if it doesn't exist yet
    }

    return "C:/Program Files (x86)/Steam/steamapps/workshop/content/252950";
}

std::string WorkshopMapLoader::SanitizePath(const std::string& raw)
{
    std::string out = raw;
    // Normalise backslash → forward slash for Unreal's ServerTravel
    for (char& c : out) if (c == '\\') c = '/';
    // Strip trailing slash
    while (!out.empty() && (out.back() == '/' || out.back() == '\\'))
        out.pop_back();
    return out;
}

std::string WorkshopMapLoader::MapNameFromPath(const std::string& path)
{
    fs::path p(path);
    return p.stem().string(); // filename without extension
}

std::vector<MapEntry> WorkshopMapLoader::FilteredMaps() const
{
    if (filterText_.empty()) return mapList_;

    std::string lf = filterText_;
    std::transform(lf.begin(), lf.end(), lf.begin(), [](unsigned char c){ return std::tolower(c); });

    std::vector<MapEntry> out;
    for (auto& m : mapList_)
    {
        std::string ln = m.displayName;
        std::transform(ln.begin(), ln.end(), ln.begin(), [](unsigned char c){ return std::tolower(c); });
        if (ln.find(lf) != std::string::npos) out.push_back(m);
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
//  onLoad – register cvars, notifiers, hooks
// ═══════════════════════════════════════════════════════════════════════════
void WorkshopMapLoader::onLoad()
{
    cvarManager->log("WorkshopMapLoader: loading");

    // ── CVars ──────────────────────────────────────────────────────────
    std::string defPath = DefaultWorkshopPath();

    cvarManager->registerCvar("wml_maps_directory", defPath,
        "Folder to scan for .upk/.udk workshop maps", true)
        .addOnValueChanged([this](std::string, CVarWrapper cv) {
            mapsDirectory_ = SanitizePath(cv.getStringValue());
            cvarManager->log("WorkshopMapLoader: maps directory set to: " + mapsDirectory_);
            ScanMaps();
        });
    mapsDirectory_ = SanitizePath(
        cvarManager->getCvar("wml_maps_directory").getStringValue());

    cvarManager->registerCvar("wml_auto_scan", "1",
        "Automatically re-scan the maps folder when the plugin window opens",
        true, true, 0, true, 1)
        .addOnValueChanged([this](std::string, CVarWrapper cv) {
            autoScanOnOpen_ = cv.getBoolValue();
        });
    autoScanOnOpen_ = cvarManager->getCvar("wml_auto_scan").getBoolValue();

    // ── Notifiers ──────────────────────────────────────────────────────

    // Manual scan
    cvarManager->registerNotifier("wml_scan", [this](std::vector<std::string>) {
        ScanMaps();
        cvarManager->log("WorkshopMapLoader: scan complete — " + std::to_string(mapList_.size()) + " maps found");
    }, "Scan the maps directory for .upk/.udk files", PERMISSION_ALL);

    // Load map by index in the list
    cvarManager->registerNotifier("wml_load_index", [this](std::vector<std::string> args) {
        if (args.size() < 2) {
            cvarManager->log("WorkshopMapLoader: wml_load_index <index>");
            return;
        }
        int idx = std::stoi(args[1]);
        if (idx < 0 || idx >= (int)mapList_.size()) {
            cvarManager->log("WorkshopMapLoader: index out of range");
            return;
        }
        LoadMap(mapList_[idx]);
    }, "Load a map by its index in the scanned list", PERMISSION_ALL);

    // Load map by full path  
    cvarManager->registerNotifier("wml_load_path", [this](std::vector<std::string> args) {
        if (args.size() < 2) {
            cvarManager->log("WorkshopMapLoader: wml_load_path <full path>");
            return;
        }
        // Rejoin args (path may contain spaces)
        std::string path;
        for (size_t i = 1; i < args.size(); ++i) {
            if (i > 1) path += " ";
            path += args[i];
        }
        LoadMapPath(SanitizePath(path));
    }, "Load a map by its absolute file path", PERMISSION_ALL);

    // List maps in console
    cvarManager->registerNotifier("wml_list", [this](std::vector<std::string>) {
        if (mapList_.empty()) {
            cvarManager->log("WorkshopMapLoader: no maps found. Run wml_scan first.");
            return;
        }
        for (int i = 0; i < (int)mapList_.size(); ++i)
            cvarManager->log("[" + std::to_string(i) + "] " + mapList_[i].displayName
                + "  (" + mapList_[i].extension + ")  " + mapList_[i].fullPath);
    }, "List all scanned maps in the BakkesMod console", PERMISSION_ALL);

    // ── Tick hook (for delayed LAN transport) ──────────────────────────
    gameWrapper->HookEvent("Function TAGame.Car_TA.SetVehicleInput",
        [this](std::string e) { OnTick(e); });

    // ── Initial scan ───────────────────────────────────────────────────
    ScanMaps();

    cvarManager->log("WorkshopMapLoader: loaded OK — " + std::to_string(mapList_.size()) + " maps in " + mapsDirectory_);
}

void WorkshopMapLoader::onUnload()
{
    gameWrapper->UnhookEvent("Function TAGame.Car_TA.SetVehicleInput");
}

// ═══════════════════════════════════════════════════════════════════════════
//  ScanMaps – walk the maps directory recursively for .upk / .udk
// ═══════════════════════════════════════════════════════════════════════════
void WorkshopMapLoader::ScanMaps()
{
    mapList_.clear();
    selectedIndex_ = -1;

    if (mapsDirectory_.empty()) {
        cvarManager->log("WorkshopMapLoader: maps directory is empty – set wml_maps_directory");
        return;
    }

    fs::path dir(mapsDirectory_);
    if (!fs::exists(dir)) {
        cvarManager->log("WorkshopMapLoader: directory does not exist: " + mapsDirectory_);
        return;
    }

    try {
        for (auto& entry : fs::recursive_directory_iterator(
                dir, fs::directory_options::skip_permission_denied))
        {
            if (!entry.is_regular_file()) continue;

            std::string ext = entry.path().extension().string();
            // normalise to lower-case
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](unsigned char c){ return std::tolower(c); });

            if (ext == ".upk" || ext == ".udk") {
                MapEntry me;
                me.extension   = ext.substr(1); // strip leading '.'
                me.fullPath    = SanitizePath(entry.path().string());
                me.displayName = MapNameFromPath(me.fullPath);
                mapList_.push_back(me);
            }
        }
    } catch (const std::exception& ex) {
        cvarManager->log(std::string("WorkshopMapLoader: scan error: ") + ex.what());
    }

    // Sort alphabetically
    std::sort(mapList_.begin(), mapList_.end(),
        [](const MapEntry& a, const MapEntry& b){
            return a.displayName < b.displayName;
        });

    cvarManager->log("WorkshopMapLoader: scan done — " + std::to_string(mapList_.size()) + " maps found");
}

// ═══════════════════════════════════════════════════════════════════════════
//  LoadMap / LoadMapPath
// ═══════════════════════════════════════════════════════════════════════════
void WorkshopMapLoader::LoadMap(const MapEntry& entry)
{
    LoadMapPath(entry.fullPath);
}

void WorkshopMapLoader::LoadMapPath(const std::string& path)
{
    if (path.empty()) {
        cvarManager->log("WorkshopMapLoader: LoadMapPath called with empty path");
        return;
    }

    // Validate the file still exists
    if (!fs::exists(fs::path(path))) {
        cvarManager->log("WorkshopMapLoader: map file not found: " + path);
        return;
    }

    cvarManager->log("WorkshopMapLoader: loading map: " + path);

    // ── Check if we're currently hosting a LAN match ──────────────────
    bool inGame = gameWrapper->IsInGame();
    if (inGame)
    {
        ServerWrapper server = gameWrapper->GetCurrentGameState();
        if (!server.IsNull() && server.HasAuthority())
        {
            // Count remote players: if > 0 we're a LAN host with guests
            ArrayWrapper<PriWrapper> pris = server.GetPRIs();
            int remotePlayers = 0;
            for (int i = 0; i < pris.Count(); ++i) {
                PriWrapper pri = pris.Get(i);
                if (pri.IsNull()) continue;
                if (!pri.IsLocalPlayerPRI()) remotePlayers++;
            }

            if (remotePlayers > 0) {
                cvarManager->log("WorkshopMapLoader: LAN host detected — "
                    + std::to_string(remotePlayers) + " remote player(s). Scheduling teleport.");
                isHostingLAN_        = true;
                pendingMapPath_       = path;
                pendingLANTransport_  = true;
                transportCountdown_   = LAN_TRANSPORT_DELAY_TICKS;
                return; // actual travel issued by OnTick
            }
        }
    }

    // ── Single-player / freeplay / offline load ───────────────────────
    // Use the BakkesMod loadmap console command which handles UE3 map loading
    // Format: TAGame.GFxShell_TA.LoadMap|<path>
    std::string cmd = "load_workshop_map \"" + path + "\"";
    cvarManager->log("WorkshopMapLoader: issuing: " + cmd);
    gameWrapper->ExecuteUnrealCommand("open \"" + path + "\"");
}

// ═══════════════════════════════════════════════════════════════════════════
//  TeleportLANPlayers – called by the tick handler after the delay
// ═══════════════════════════════════════════════════════════════════════════
void WorkshopMapLoader::TeleportLANPlayers(const std::string& mapPath)
{
    if (!gameWrapper->IsInGame()) {
        cvarManager->log("WorkshopMapLoader: TeleportLANPlayers — not in game, aborting");
        return;
    }

    ServerWrapper server = gameWrapper->GetCurrentGameState();
    if (server.IsNull()) {
        cvarManager->log("WorkshopMapLoader: TeleportLANPlayers — server null, aborting");
        return;
    }
    if (!server.HasAuthority()) {
        cvarManager->log("WorkshopMapLoader: TeleportLANPlayers — no authority, aborting");
        return;
    }

    cvarManager->log("WorkshopMapLoader: TeleportLANPlayers — ServerTravel to: " + mapPath);

    // ServerTravel is the Unreal Engine 3 mechanism that migrates ALL connected
    // clients to a new map while keeping them in the same session.
    // We use ExecuteUnrealCommand so it runs on the game thread.
    gameWrapper->ExecuteUnrealCommand("servertravel \"" + mapPath + "\"");
}

// ═══════════════════════════════════════════════════════════════════════════
//  OnTick – drains the pending LAN transport countdown
// ═══════════════════════════════════════════════════════════════════════════
void WorkshopMapLoader::OnTick(std::string)
{
    if (!pendingLANTransport_) return;

    --transportCountdown_;
    if (transportCountdown_ > 0) return;

    // Countdown expired – fire the travel
    pendingLANTransport_ = false;
    isHostingLAN_        = false;
    TeleportLANPlayers(pendingMapPath_);
    pendingMapPath_.clear();
}

// ═══════════════════════════════════════════════════════════════════════════
//  ImGui window (F2 > Plugins > Workshop Map Loader)
// ═══════════════════════════════════════════════════════════════════════════
void WorkshopMapLoader::SetImGuiContext(uintptr_t ctx)
{
    ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

void WorkshopMapLoader::Render()
{
    // ── Header / directory ─────────────────────────────────────────────
    ImGui::TextDisabled("Maps directory:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", mapsDirectory_.c_str());

    if (ImGui::Button("  Scan / Refresh  "))
    {
        ScanMaps();
    }
    ImGui::SameLine();
    if (ImGui::Button("  Open Folder  "))
    {
        // Open in Explorer
        std::string cmd = "explorer \"" + mapsDirectory_ + "\"";
        system(cmd.c_str()); // harmless Windows-only call
    }

    ImGui::Separator();

    // ── Search filter ──────────────────────────────────────────────────
    if (ImGui::InputText("Search", filterBuf_, sizeof(filterBuf_)))
        filterText_ = filterBuf_;

    ImGui::Spacing();

    // ── Map list ───────────────────────────────────────────────────────
    auto filtered = FilteredMaps();

    if (filtered.empty())
    {
        ImGui::TextColored(ImVec4(1,0.4f,0.4f,1),
            mapList_.empty()
                ? "No maps found. Make sure the directory is correct and click Scan."
                : "No maps match the filter.");
    }
    else
    {
        ImGui::Text("%d map(s)", (int)filtered.size());
        ImGui::Separator();

        // Scrollable child region so the list doesn't push the buttons off-screen
        ImGui::BeginChild("##maplist", ImVec2(0, 300), true);

        for (int i = 0; i < (int)filtered.size(); ++i)
        {
            auto& m = filtered[i];
            bool sel = (selectedIndex_ == i);

            // Tag .udk vs .upk
            std::string label = m.displayName + "  [" + m.extension + "]";
            if (ImGui::Selectable(label.c_str(), sel))
                selectedIndex_ = i;

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", m.fullPath.c_str());
        }

        ImGui::EndChild();

        // ── Load button ────────────────────────────────────────────────
        ImGui::Spacing();

        bool canLoad = (selectedIndex_ >= 0 && selectedIndex_ < (int)filtered.size());

        if (!canLoad) ImGui::BeginDisabled();
        if (ImGui::Button("  Load Selected Map  ", ImVec2(200, 0)))
        {
            LoadMap(filtered[selectedIndex_]);
        }
        if (!canLoad) ImGui::EndDisabled();

        if (canLoad)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", filtered[selectedIndex_].fullPath.c_str());
        }
    }

    ImGui::Separator();

    // ── LAN status indicator ───────────────────────────────────────────
    if (pendingLANTransport_)
    {
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1),
            "Teleporting LAN players in %d ticks...", transportCountdown_);
    }
    else
    {
        // Show live LAN hosting status
        bool inGame = gameWrapper->IsInGame();
        if (inGame)
        {
            ServerWrapper server = gameWrapper->GetCurrentGameState();
            if (!server.IsNull() && server.HasAuthority())
            {
                ArrayWrapper<PriWrapper> pris = server.GetPRIs();
                int remote = 0;
                for (int i = 0; i < pris.Count(); ++i) {
                    PriWrapper pri = pris.Get(i);
                    if (!pri.IsNull() && !pri.IsLocalPlayerPRI()) remote++;
                }
                if (remote > 0)
                    ImGui::TextColored(ImVec4(0.2f,1,0.4f,1),
                        "LAN host  |  %d remote player(s) connected  —  load will teleport all", remote);
                else
                    ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1),
                        "Hosting (no remote players yet)");
            }
            else if (inGame)
            {
                ImGui::TextDisabled("In game (not hosting)");
            }
        }
        else
        {
            ImGui::TextDisabled("Not in a game");
        }
    }

    ImGui::Spacing();

    // ── Settings ───────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Settings"))
    {
        // Directory picker (text box)
        static char dirBuf[512] = {};
        if (dirBuf[0] == '\0')
            strncpy_s(dirBuf, mapsDirectory_.c_str(), sizeof(dirBuf)-1);

        ImGui::Text("Maps Directory:");
        if (ImGui::InputText("##dir", dirBuf, sizeof(dirBuf),
                ImGuiInputTextFlags_EnterReturnsTrue))
        {
            cvarManager->getCvar("wml_maps_directory").setValue(std::string(dirBuf));
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply##dir"))
            cvarManager->getCvar("wml_maps_directory").setValue(std::string(dirBuf));

        ImGui::Spacing();
        bool autoScan = autoScanOnOpen_;
        if (ImGui::Checkbox("Auto-scan on panel open", &autoScan))
            cvarManager->getCvar("wml_auto_scan").setValue(autoScan ? 1 : 0);

        ImGui::Spacing();
        ImGui::TextDisabled("Console commands:");
        ImGui::BulletText("wml_scan                – re-scan maps folder");
        ImGui::BulletText("wml_list                – list maps in console");
        ImGui::BulletText("wml_load_index <n>      – load map by list index");
        ImGui::BulletText("wml_load_path <path>    – load map by full path");
        ImGui::BulletText("wml_maps_directory <p>  – set maps folder");
    }
}
