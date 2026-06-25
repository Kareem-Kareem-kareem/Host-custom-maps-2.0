#include "HostworkshopMaps.h"
#include <windows.h>

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Workshop Maps", "2.0", PLUGINTYPE_FREEPLAY)

// PluginWindow interface (stubs)
std::string HostWorkshopMaps::GetMenuName()
{
    return "Workshop Maps";
}

std::string HostWorkshopMaps::GetMenuTitle()
{
    return "Workshop Maps";
}

void HostWorkshopMaps::SetImGuiContext(uintptr_t ctx)
{
}

bool HostWorkshopMaps::ShouldBlockInput()
{
    return false;
}

bool HostWorkshopMaps::IsActiveOverlay()
{
    return false;
}

void HostWorkshopMaps::OnOpen()
{
}

void HostWorkshopMaps::OnClose()
{
}

void HostWorkshopMaps::Render()
{
}

// Feature 1+2: Command registration
void HostWorkshopMaps::registerCommands()
{
    cvarManager->registerNotifier(
        "hwm_scan",
        [this](std::vector<std::string> params) {
            scanMapsDirectory();
        },
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
            for (size_t i = 0; i < mapFiles.size(); i++) {
                cvarManager->log("  [" + std::to_string(i) + "] " + mapNames[i]);
            }
        },
        "List maps",
        PERMISSION_ALL
    );
}

// Feature 3+4: Directory scanning with GetEnvironmentVariableA
std::string HostWorkshopMaps::getDefaultPath()
{
    char buf[MAX_PATH];
    
    if (GetEnvironmentVariableA("APPDATA", buf, MAX_PATH) > 0) {
        return std::string(buf) + "\\bakkesmod\\bakkesmod\\data\\workshop";
    }
    
    return "";
}

void HostWorkshopMaps::scanMapsDirectory()
{
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

    // Simple scan - just one level for now
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((dir + "\\*").c_str(), &fd);

    if (hFind == INVALID_HANDLE_VALUE) {
        cvarManager->log("HWM: Scan failed");
        return;
    }

    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..")
            continue;

        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::string fullPath = dir + "\\" + name;
            mapFiles.push_back(fullPath);
            mapNames.push_back(name);
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);

    cvarManager->log("HWM: Found " + std::to_string(mapFiles.size()) + " items");
}

// Plugin lifecycle
void HostWorkshopMaps::onLoad()
{
    cvarManager->log("HWM: Loading v2.0");
    registerCommands();
    cvarManager->log("HWM: Ready - use hwm_scan, hwm_list");
}

void HostWorkshopMaps::onUnload()
{
    mapFiles.clear();
    mapNames.clear();
    cvarManager->log("HWM: Unloaded");
}
