// CRITICAL: Do NOT include pch.h here.
// BakkesMod x86 plugins have precompiled header issues.

#include "HostworkshopMaps.h"

// Standard library headers (after pch if included in other files)
#include <string>
#include <vector>
#include <cctype>

// Windows API
#include <windows.h>

// BakkesMod wrappers
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/GameEvent/ServerWrapper.h"

// Plugin registration
BAKKESMOD_PLUGIN(HostWorkshopMaps, "Workshop Maps Loader", "2.0", PLUGINTYPE_FREEPLAY)

// ===== PluginWindow stubs (no rendering) =====

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
    // Intentionally empty
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
    cvarManager->log("HWM: Console commands: hwm_scan, hwm_list, hwm_load");
}

void HostWorkshopMaps::OnClose()
{
    // Intentionally empty
}

void HostWorkshopMaps::Render()
{
    // Intentionally empty - no ImGui rendering
}

// ===== Helpers =====

std::string HostWorkshopMaps::toLower(const std::string& s)
{
    std::string out = s;
    for (size_t i = 0; i < out.size(); i++)
    {
        out[i] = (char)tolower((unsigned char)out[i]);
    }
    return out;
}

// ===== File scanning (Win32 API) =====

void HostWorkshopMaps::scanDirectoryRecursive(const std::string& dir, int depth)
{
    if (depth > 8)
        return;

    std::string pattern = dir + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);

    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        std::string name = fd.cFileName;
        if (name == "." || name == "..")
            continue;

        std::string fullPath = dir + "\\" + name;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            scanDirectoryRecursive(fullPath, depth + 1);
        }
        else
        {
            size_t dot = name.find_last_of('.');
            if (dot == std::string::npos)
                continue;

            std::string ext = toLower(name.substr(dot));
            if (ext == ".upk" || ext == ".udk")
            {
                mapFiles.push_back(fullPath);
                mapNames.push_back(name);
            }
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
}

void HostWorkshopMaps::scanMapsDirectory()
{
    mapFiles.clear();
    mapNames.clear();

    std::string dir = cvarManager->getCvar("hwm_maps_directory").getStringValue();

    if (dir.empty())
    {
        cvarManager->log("HWM: Maps directory not set");
        return;
    }

    DWORD attr = GetFileAttributesA(dir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        cvarManager->log("HWM: Directory not found: " + dir);
        return;
    }

    cvarManager->log("HWM: Scanning: " + dir);
    scanDirectoryRecursive(dir, 0);
    cvarManager->log("HWM: Found " + std::to_string(mapFiles.size()) + " maps");
}

std::string HostWorkshopMaps::getDefaultPath()
{
    char buf[MAX_PATH];
    std::vector<std::string> candidates;

    if (GetEnvironmentVariableA("APPDATA", buf, MAX_PATH) > 0)
    {
        candidates.push_back(std::string(buf) + "\\bakkesmod\\bakkesmod\\data\\workshop");
    }

    if (GetEnvironmentVariableA("ProgramFiles(x86)", buf, MAX_PATH) > 0)
    {
        candidates.push_back(std::string(buf) + "\\Steam\\steamapps\\workshop\\content\\252950");
    }

    if (GetEnvironmentVariableA("USERPROFILE", buf, MAX_PATH) > 0)
    {
        candidates.push_back(std::string(buf) + "\\Documents\\rocketleague\\workshop");
    }

    for (size_t i = 0; i < candidates.size(); i++)
    {
        DWORD attr = GetFileAttributesA(candidates[i].c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        {
            return candidates[i];
        }
    }

    return candidates.empty() ? "" : candidates[0];
}

// ===== Map loading =====

void HostWorkshopMaps::loadMapByIndex(int index)
{
    if (index < 0 || index >= (int)mapFiles.size())
    {
        cvarManager->log("HWM: Invalid index");
        return;
    }
    loadMapByPath(mapFiles[index]);
}

void HostWorkshopMaps::loadMapByPath(const std::string& path)
{
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        cvarManager->log("HWM: File not found");
        return;
    }

    cvarManager->log("HWM: Loading map");

    bool lanHost = isLanHost();
    std::string cmd = lanHost
        ? ("servertravel \"" + path + "\"")
        : ("open \"" + path + "\"");

    gameWrapper->SetTimeout(
        [this, cmd](GameWrapper* gw) {
            try
            {
                gw->ExecuteUnrealCommand(cmd);
            }
            catch (...)
            {
                cvarManager->log("HWM: Command failed");
            }
        },
        0.3f
    );
}

bool HostWorkshopMaps::isLanHost()
{
    try
    {
        if (!gameWrapper)
            return false;

        ServerWrapper server = gameWrapper->GetCurrentGameState();
        if (server.IsNull())
            return false;

        if (!server.HasAuthority())
            return false;

        return server.GetPRIs().Count() > 1;
    }
    catch (...)
    {
        return false;
    }
}

// ===== Plugin lifecycle =====

void HostWorkshopMaps::onLoad()
{
    cvarManager->log("=== HWM v2.0 loaded ===");

    std::string defPath = getDefaultPath();
    cvarManager->registerCvar(
        "hwm_maps_directory",
        defPath,
        "Directory to scan for maps",
        false,
        true
    );

    cvarManager->registerNotifier(
        "hwm_scan",
        [this](std::vector<std::string> params) {
            scanMapsDirectory();
        },
        "Scan maps directory",
        PERMISSION_ALL
    );

    cvarManager->registerNotifier(
        "hwm_list",
        [this](std::vector<std::string> params) {
            if (mapFiles.empty())
            {
                cvarManager->log("HWM: No maps. Run hwm_scan");
                return;
            }
            cvarManager->log("HWM: Maps:");
            for (size_t i = 0; i < mapFiles.size(); i++)
            {
                cvarManager->log("  [" + std::to_string(i) + "] " + mapNames[i]);
            }
        },
        "List maps",
        PERMISSION_ALL
    );

    cvarManager->registerNotifier(
        "hwm_load",
        [this](std::vector<std::string> params) {
            if (params.size() < 2)
            {
                cvarManager->log("Usage: hwm_load <index>");
                return;
            }
            try
            {
                loadMapByIndex(std::stoi(params[1]));
            }
            catch (...)
            {
                cvarManager->log("HWM: Invalid index");
            }
        },
        "Load map by index",
        PERMISSION_ALL
    );

    cvarManager->registerNotifier(
        "hwm_load_path",
        [this](std::vector<std::string> params) {
            if (params.size() < 2)
            {
                cvarManager->log("Usage: hwm_load_path <path>");
                return;
            }
            loadMapByPath(params[1]);
        },
        "Load map by path",
        PERMISSION_ALL
    );

    cvarManager->log("HWM: Ready");
}

void HostWorkshopMaps::onUnload()
{
    mapFiles.clear();
    mapNames.clear();
    cvarManager->log("HWM: Unloaded");
}
