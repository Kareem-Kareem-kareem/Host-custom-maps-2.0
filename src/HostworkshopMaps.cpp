#include "HostworkshopMaps.h"

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Workshop Maps", "2.0", PLUGINTYPE_FREEPLAY)

// PluginWindow interface
std::string HostWorkshopMaps::GetMenuName() { return "Workshop Maps"; }
std::string HostWorkshopMaps::GetMenuTitle() { return "Workshop Maps"; }

void HostWorkshopMaps::SetImGuiContext(uintptr_t ctx) {
    ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

bool HostWorkshopMaps::ShouldBlockInput() { return isWindowOpen; }
bool HostWorkshopMaps::IsActiveOverlay() { return false; }
void HostWorkshopMaps::OnOpen() { isWindowOpen = true; }
void HostWorkshopMaps::OnClose() { isWindowOpen = false; }

// Feature 2: Basic ImGui Render
void HostWorkshopMaps::Render() {
    if (!isWindowOpen) return;
    
    if (ImGui::Begin(GetMenuTitle().c_str(), &isWindowOpen, ImGuiWindowFlags_None)) {
        if (ImGui::Button("Scan Maps")) {
            scanMapsDirectory();
        }
        
        ImGui::Separator();
        
        if (mapNames.empty()) {
            ImGui::Text("No maps found. Click 'Scan Maps'.");
        } else {
            ImGui::BeginChild("MapList", ImVec2(0, 200), true);
            for (size_t i = 0; i < mapNames.size(); i++) {
                if (ImGui::Selectable(mapNames[i].c_str(), selectedMapIndex == (int)i)) {
                    selectedMapIndex = (int)i;
                }
            }
            ImGui::EndChild();
            
            if (selectedMapIndex >= 0 && selectedMapIndex < (int)mapFiles.size()) {
                if (ImGui::Button("Load Selected Map")) {
                    loadMap(mapFiles[selectedMapIndex]);
                }
            }
        }
    }
    ImGui::End();
}

void HostWorkshopMaps::registerCommands() {
    cvarManager->registerNotifier(
        "hwm_scan",
        [this](std::vector<std::string> params) { scanMapsDirectory(); },
        "Scan for maps",
        PERMISSION_ALL
    );

    cvarManager->registerNotifier(
        "hwm_list",
        [this](std::vector<std::string> params) {
            if (mapFiles.empty()) {
                cvarManager->log("HWM: No maps found");
                return;
            }
            cvarManager->log("HWM: Found " + std::to_string(mapFiles.size()) + " maps");
            for (size_t i = 0; i < mapNames.size(); i++) {
                cvarManager->log(" [" + std::to_string(i) + "] " + mapNames[i]);
            }
        },
        "List maps",
        PERMISSION_ALL
    );
    
    // Command for Feature 1
    cvarManager->registerNotifier(
        "hwm_load",
        [this](std::vector<std::string> params) {
            if (params.size() < 2) {
                cvarManager->log("Usage: hwm_load <index>");
                return;
            }
            int idx = std::stoi(params[1]);
            if (idx >= 0 && idx < (int)mapFiles.size()) {
                loadMap(mapFiles[idx]);
            } else {
                cvarManager->log("Invalid map index");
            }
        },
        "Load a map by index",
        PERMISSION_ALL
    );
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
    if (dir.empty()) {
        cvarManager->log("HWM: No directory");
        return;
    }
    DWORD attr = GetFileAttributesA(dir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        cvarManager->log("HWM: Directory not found: " + dir);
        return;
    }
    cvarManager->log("HWM: Scanning: " + dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        cvarManager->log("HWM: Scan failed");
        return;
    }
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::string fullPath = dir + "\\" + name;
            mapFiles.push_back(fullPath);
            mapNames.push_back(name);
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    cvarManager->log("HWM: Found " + std::to_string(mapFiles.size()) + " items");
}

// Feature 1: Map Loading implementation
void HostWorkshopMaps::loadMap(const std::string& path) {
    cvarManager->log("HWM: Loading map " + path);
    // Escape the backslashes or just execute standard load_workshop command
    cvarManager->executeCommand("load_workshop \"" + path + "\"");
}

void HostWorkshopMaps::onLoad() {
    cvarManager->log("HWM: Loading v2.0");
    registerCommands();
    cvarManager->log("HWM: Ready - use hwm_scan, hwm_list, hwm_load, or F3 menu");
}

void HostWorkshopMaps::onUnload() {
    mapFiles.clear();
    mapNames.clear();
    cvarManager->log("HWM: Unloaded");
}

