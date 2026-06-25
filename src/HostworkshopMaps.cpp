#include "HostworkshopMaps.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Workshop Maps", "2.0", PLUGINTYPE_FREEPLAY)

std::string HostWorkshopMaps::GetMenuName() { return "Workshop Maps"; }
std::string HostWorkshopMaps::GetMenuTitle() { return "Workshop Maps"; }

void HostWorkshopMaps::SetImGuiContext(uintptr_t ctx) {
    ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

bool HostWorkshopMaps::ShouldBlockInput() { return isWindowOpen; }
bool HostWorkshopMaps::IsActiveOverlay() { return false; }
void HostWorkshopMaps::OnOpen() { isWindowOpen = true; }
void HostWorkshopMaps::OnClose() { isWindowOpen = false; }

void HostWorkshopMaps::Render() {
    if (!isWindowOpen) return;
    
    // Feature 5: UI Styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.9f, 1.0f));
    
    if (ImGui::Begin(GetMenuTitle().c_str(), &isWindowOpen, ImGuiWindowFlags_None)) {
        if (ImGui::Button("Scan Maps", ImVec2(120, 0))) {
            scanMapsDirectory();
        }
        
        ImGui::Separator();
        
        if (mapNames.empty()) {
            ImGui::Text("No maps found. Click 'Scan Maps'.");
        } else {
            // Feature 7: Search Filter UI
            ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer));
            std::string searchStr = searchBuffer;
            std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

            ImGui::BeginChild("MapList", ImVec2(0, 200), true);
            for (size_t i = 0; i < mapNames.size(); i++) {
                std::string mapNameLower = mapNames[i];
                std::transform(mapNameLower.begin(), mapNameLower.end(), mapNameLower.begin(), ::tolower);
                
                // Skip rendering this item if it doesn't match the search
                if (!searchStr.empty() && mapNameLower.find(searchStr) == std::string::npos) {
                    continue;
                }

                if (ImGui::Selectable(mapNames[i].c_str(), selectedMapIndex == (int)i)) {
                    selectedMapIndex = (int)i;
                }
            }
            ImGui::EndChild();
            
            if (selectedMapIndex >= 0 && selectedMapIndex < (int)mapFiles.size()) {
                if (ImGui::Button("Load Local (Freeplay)", ImVec2(160, 0))) {
                    loadMap(mapFiles[selectedMapIndex]);
                }
                
                ImGui::SameLine();
                
                // Feature 8: Host Listen Server UI
                if (ImGui::Button("Host Listen Server", ImVec2(150, 0))) {
                    hostMap(mapFiles[selectedMapIndex]);
                }
                
                ImGui::SameLine();
                
                if (ImGui::Button("Send to Party", ImVec2(120, 0))) {
                    sendMapToParty(mapNames[selectedMapIndex]);
                }
            }
        }
    }
    ImGui::End();
    
    // Pop the styles we pushed
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

void HostWorkshopMaps::registerCommands() {
    cvarManager->registerNotifier("hwm_scan", [this](std::vector<std::string> params) { scanMapsDirectory(); }, "Scan for maps", PERMISSION_ALL);
    
    cvarManager->registerNotifier("hwm_list", [this](std::vector<std::string> params) {
        if (mapFiles.empty()) { cvarManager->log("HWM: No maps found"); return; }
        for (size_t i = 0; i < mapNames.size(); i++) { cvarManager->log(" [" + std::to_string(i) + "] " + mapNames[i]); }
    }, "List maps", PERMISSION_ALL);
    
    cvarManager->registerNotifier("hwm_load", [this](std::vector<std::string> params) {
        if (params.size() < 2) return;
        int idx = std::stoi(params[1]);
        if (idx >= 0 && idx < (int)mapFiles.size()) loadMap(mapFiles[idx]);
    }, "Load a map locally by index", PERMISSION_ALL);
    
    // Feature 8: Command equivalent for hosting
    cvarManager->registerNotifier("hwm_host", [this](std::vector<std::string> params) {
        if (params.size() < 2) return;
        int idx = std::stoi(params[1]);
        if (idx >= 0 && idx < (int)mapFiles.size()) hostMap(mapFiles[idx]);
    }, "Host a map by index", PERMISSION_ALL);
}

std::string HostWorkshopMaps::getDefaultPath() {
    char buf[MAX_PATH];
    if (GetEnvironmentVariableA("APPDATA", buf, MAX_PATH) > 0) {
        return std::string(buf) + "\\bakkesmod\\bakkesmod\\data\\workshop";
    }
    return "";
}

void HostWorkshopMaps::scanMapsDirectory() {
    mapFiles.clear();
    mapNames.clear();
    std::string dir = getDefaultPath();
    if (dir.empty()) return;

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (name.find(".upk") != std::string::npos || name.find(".udk") != std::string::npos) {
                mapFiles.push_back(dir + "\\" + name);
                mapNames.push_back(cleanMapName(name));
            }
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
}

std::string HostWorkshopMaps::cleanMapName(const std::string& filename) {
    std::string clean = filename;
    size_t lastDot = clean.find_last_of(".");
    if (lastDot != std::string::npos) clean = clean.substr(0, lastDot);
    for (size_t i = 0; i < clean.length(); ++i) { if (clean[i] == '_') clean[i] = ' '; }
    return clean;
}

void HostWorkshopMaps::loadMap(const std::string& path) {
    cvarManager->log("HWM: Loading local map " + path);
    cvarManager->executeCommand("load_workshop \"" + path + "\"");
}

// Feature 8: Start a Listen Server instead of Freeplay
void HostWorkshopMaps::hostMap(const std::string& path) {
    cvarManager->log("HWM: Hosting listen server for map " + path);
    // Uses standard UE3 open command to host
    cvarManager->executeCommand("unreal_command \"open " + path + "?Listen\"");
}

void HostWorkshopMaps::onJoinParty(std::string eventName) {
    cvarManager->log("HWM: Joined party!");
}

void HostWorkshopMaps::sendMapToParty(const std::string& mapName) {
    cvarManager->log("HWM: Sending map info to party: " + mapName);
}

void HostWorkshopMaps::onLoad() {
    cvarManager->log("HWM: Loading v2.0");
    registerCommands();
    
    // Feature 6: Network Hook 
    gameWrapper->HookEvent("Function TAGame.Party_TA.OnPartyJoined", std::bind(&HostWorkshopMaps::onJoinParty, this, std::placeholders::_1));
    
    cvarManager->log("HWM: Ready");
}

void HostWorkshopMaps::onUnload() {
    mapFiles.clear();
    mapNames.clear();
    gameWrapper->UnhookEvent("Function TAGame.Party_TA.OnPartyJoined");
    cvarManager->log("HWM: Unloaded");
}
