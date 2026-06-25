#include "HostworkshopMaps.h"
#include <windows.h>
#include <string>
#include <vector>
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/GameEvent/ServerWrapper.h"

// =========================================================================
// Plugin registration
// =========================================================================
BAKKESMOD_PLUGIN(HostWorkshopMaps, "Workshop Maps Loader", "2.0", PLUGINTYPE_FREEPLAY)

// =========================================================================
// PluginWindow interface (stubs - we do NOT render an ImGui window)
// =========================================================================

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
    // Intentionally empty. BakkesMod manages the ImGui context itself.
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
    cvarManager->log("HWM: Use the F6 console commands: hwm_scan, hwm_list, hwm_load");
}

void HostWorkshopMaps::OnClose()
{
}

void HostWorkshopMaps::Render()
{
    // Intentionally empty. Rendering is done purely via console commands
    // to avoid ImGui-related crashes.
}

// =========================================================================
// Helpers
// =========================================================================

std::string HostWorkshopMaps::toLower(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
    {
        c = (char)tolower((unsigned char)c);
    }
    return out;
}

bool HostWorkshopMaps::pathExists(const std::string& path)
{
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES);
}

// =========================================================================
// Win32 recursive directory scanner (safe - never throws)
// =========================================================================

void HostWorkshopMaps::scanDirectoryRecursive(const std::string& dir, int depth)
{
    // Prevent runaway recursion into deep/looping trees
    if (depth > 8) return;

    std::string pattern = dir + "\\*";

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;

        std::string fullPath = dir + "\\" + name;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            // Recurse into subfolder
            scanDirectoryRecursive(fullPath, depth + 1);
        }
        else
        {
            // It's a file - check extension
            size_t dot = name.find_last_of('.');
            if (dot == std::string::npos) continue;

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

    CVarWrapper cvar = cvarManager->getCvar("hwm_maps_directory");
    std::string dir = cvar.IsNull() ? "" : cvar.getStringValue();

    if (dir.empty())
    {
        cvarManager->log("HWM: Maps directory is empty. Set 'hwm_maps_directory'.");
        return;
    }

    DWORD attr = GetFileAttributesA(dir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        cvarManager->log("HWM: Directory does not exist: " + dir);
        return;
    }

    cvarManager->log("HWM: Scanning: " + dir);
    scanDirectoryRecursive(dir, 0);
    cvarManager->log("HWM: Found " + std::to_string(mapFiles.size()) + " maps.");
}

// =========================================================================
// Default path discovery (Win32 environment vars - no std::filesystem)
// =========================================================================

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

    for (const auto& c : candidates)
    {
        DWORD attr = GetFileAttributesA(c.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        {
            return c;
        }
    }

    return candidates.empty() ? "" : candidates[0];
}

// =========================================================================
// Map loading
// =========================================================================

void HostWorkshopMaps::loadMapByIndex(int index)
{
    if (index < 0 || index >= (int)mapFiles.size())
    {
        cvarManager->log("HWM: Invalid index " + std::to_string(index) +
                         " (valid: 0-" + std::to_string(mapFiles.size() - 1) + ")");
        return;
    }
    loadMapByPath(mapFiles[index]);
}

void HostWorkshopMaps::loadMapByPath(const std::string& path)
{
    if (path.empty() || !pathExists(path))
    {
        cvarManager->log("HWM: Map not found: " + path);
        return;
    }

    cvarManager->log("HWM: Loading map: " + path);

    bool lanHost = isLanHost();

    // Build the command. Capture by value to avoid dangling 'this'/'path'.
    std::string cmd = lanHost
        ? ("servertravel \"" + path + "\"")
        : ("open \"" + path + "\"");

    cvarManager->log(lanHost ? "HWM: LAN host -> servertravel" : "HWM: local -> open");

    // Defer to a timeout so it runs on the game thread safely.
    gameWrapper->SetTimeout(
        [this, cmd, path](GameWrapper* gw) {
            try
            {
                gw->ExecuteUnrealCommand(cmd);
            }
            catch (...)
            {
                cvarManager->log("HWM: ExecuteUnrealCommand failed for: " + path);
            }
        },
        0.3f
    );
}

// =========================================================================
// LAN host detection (heavily guarded - SEH crashes aren't catchable)
// =========================================================================

bool HostWorkshopMaps::isLanHost()
{
    try
    {
        // Guard BEFORE touching wrappers - bad pointers = access violation (uncatchable)
        if (!gameWrapper->IsInGame() && !gameWrapper->IsInFreeplay())
        {
            return false;
        }

        ServerWrapper server = gameWrapper->GetCurrentGameState();
        if (server.IsNull())
        {
            return false;
        }

        if (!server.HasAuthority())
        {
            return false;
        }

        return server.GetPRIs().Count() > 1;
    }
    catch (...)
    {
        return false;
    }
}

// =========================================================================
// onLoad - register cvars + console commands
// =========================================================================

void HostWorkshopMaps::onLoad()
{
    cvarManager->log("==============================");
    cvarManager->log("HWM: Workshop Maps Loader 2.0");
    cvarManager->log("==============================");

    // Register the maps directory cvar
    std::string defPath = getDefaultPath();
    cvarManager->registerCvar(
        "hwm_maps_directory",
        defPath,
        "Directory to scan for .upk and .udk Workshop maps",
        false,
        true
    );
    cvarManager->log("HWM: Maps directory = " + defPath);

    // Command: scan
    cvarManager->registerNotifier(
        "hwm_scan",
        [this](std::vector<std::string> params) {
            scanMapsDirectory();
        },
        "Scan the maps directory for .upk and .udk files",
        PERMISSION_ALL
    );

    // Command: list
    cvarManager->registerNotifier(
        "hwm_list",
        [this](std::vector<std::string> params) {
            if (mapFiles.empty())
            {
                cvarManager->log("HWM: No maps found. Run 'hwm_scan' first.");
                return;
            }
            cvarManager->log("HWM: " + std::to_string(mapFiles.size()) + " maps:");
            for (size_t i = 0; i < mapFiles.size(); i++)
            {
                cvarManager->log("  [" + std::to_string(i) + "] " + mapNames[i]);
            }
        },
        "List all found maps with their indices",
        PERMISSION_ALL
    );

    // Command: load by index
    cvarManager->registerNotifier(
        "hwm_load",
        [this](std::vector<std::string> params) {
            if (params.size() < 2)
            {
                cvarManager->log("Usage: hwm_load <index>  (see hwm_list)");
                return;
            }
            try
            {
                loadMapByIndex(std::stoi(params[1]));
            }
            catch (...)
            {
                cvarManager->log("HWM: Invalid index '" + (params.size() > 1 ? params[1] : "") + "'");
            }
        },
        "Load a map by its index",
        PERMISSION_ALL
    );

    // Command: load by path
    cvarManager->registerNotifier(
        "hwm_load_path",
        [this](std::vector<std::string> params) {
            if (params.size() < 2)
            {
                cvarManager->log("Usage: hwm_load_path <full path to .upk/.udk>");
                return;
            }
            loadMapByPath(params[1]);
        },
        "Load a map by its full file path",
        PERMISSION_ALL
    );

    cvarManager->log("HWM: Commands registered:");
    cvarManager->log("  hwm_scan            - scan for maps");
    cvarManager->log("  hwm_list            - list found maps");
    cvarManager->log("  hwm_load <index>    - load map by index");
    cvarManager->log("  hwm_load_path <p>   - load map by path");
    cvarManager->log("==============================");
}

// =========================================================================
// onUnload
// =========================================================================

void HostWorkshopMaps::onUnload()
{
    mapFiles.clear();
    mapNames.clear();
    cvarManager->log("HWM: Unloaded");
}
