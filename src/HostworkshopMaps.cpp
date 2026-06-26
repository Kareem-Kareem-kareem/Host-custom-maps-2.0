#include "HostWorkshopMaps.h"
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/gameevent/serverwrapper.h"
#include <algorithm>
#include <cctype>

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Host Workshop Maps", "2.0", 0)

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

static void SafeWalkDir(const std::string& dir, std::vector<MapEntry>& out) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;

        std::string full = dir + "\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue; // no recursion for safety

        std::string name = fd.cFileName;
        size_t dot = name.find_last_of('.');
        if (dot == std::string::npos) continue;

        std::string ext = name.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == "upk" || ext == "udk") {
            MapEntry me;
            me.fullPath = full;
            for (char& c : me.fullPath) if (c == '\\') c = '/';
            me.displayName = name.substr(0, dot);
            out.push_back(me);
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

void HostWorkshopMaps::ScanMaps() {
    mapList_.clear();

    std::string rlPath = "C:\\Program Files\\Epic Games\\rocketleague\\TAGame\\CookedPCConsole"; // change if needed
    std::string fullDir = rlPath + "\\" + subFolder_;

    DWORD attr = GetFileAttributesA(fullDir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        SetStatus("Folder not found: " + fullDir);
        return;
    }

    SafeWalkDir(fullDir, mapList_);
    std::sort(mapList_.begin(), mapList_.end(), [](const MapEntry& a, const MapEntry& b){
        return a.displayName < b.displayName;
    });

    SetStatus(std::to_string(mapList_.size()) + " maps found in " + subFolder_);
}

void HostWorkshopMaps::LoadMap(const std::string& path, bool isLAN) {
    if (path.empty()) return;

    if (isLAN && gameWrapper->IsInGame()) {
        ServerWrapper server = gameWrapper->GetCurrentGameState();
        if (!server.IsNull() && server.HasAuthority()) {
            SetStatus("Hosting LAN map change...");
            gameWrapper->ExecuteUnrealCommand("servertravel \"" + path + "\"");
            return;
        }
    }

    SetStatus("Loading solo: " + MapNameFromPath(path));
    cvarManager->executeCommand("load_workshop \"" + path + "\"", false);
}

void HostWorkshopMaps::onLoad() {
    gameWrapper->RegisterDrawable([this](CanvasWrapper canvas) {
        if (showWindow_) Render();
    });

    cvarManager->registerNotifier("hwm_toggle", [this](std::vector<std::string>){
        showWindow_ = !showWindow_;
    }, "Toggle UI", PERMISSION_ALL);

    ScanMaps(); // initial scan

    cvarManager->log("HostWorkshopMaps loaded - Press F2 or type hwm_toggle");
}

void HostWorkshopMaps::onUnload() {}

void HostWorkshopMaps::Render() {
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Host Workshop Maps", &showWindow_)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Maps Subfolder (e.g. Mods, maps, Map):");
    if (ImGui::InputText("##subfolder", &subFolder_, ImGuiInputTextFlags_EnterReturnsTrue)) {
        ScanMaps();
    }
    if (ImGui::Button("Scan")) ScanMaps();
    ImGui::SameLine();
    ImGui::Text("%s", statusMsg_.c_str());

    ImGui::Separator();
    ImGui::Text("Found Maps (%d)", (int)mapList_.size());

    if (ImGui::BeginChild("MapList", ImVec2(0, 300), true)) {
        for (const auto& m : mapList_) {
            if (ImGui::Selectable(m.displayName.c_str())) {
                LoadMap(m.fullPath, false);
            }
        }
    }
    ImGui::EndChild();

    if (ImGui::Button("Host Solo", ImVec2(200, 40))) {
        if (!mapList_.empty()) LoadMap(mapList_[0].fullPath, false); // load first for demo
    }
    ImGui::SameLine();
    if (ImGui::Button("Host LAN", ImVec2(200, 40))) {
        if (!mapList_.empty()) LoadMap(mapList_[0].fullPath, true);
    }

    ImGui::End();
}
