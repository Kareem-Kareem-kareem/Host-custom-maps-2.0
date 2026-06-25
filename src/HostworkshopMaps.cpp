#include "HostworkshopMaps.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/GameEvent/ServerWrapper.h"
#include "imgui.h"

// ═══════════════════════════════════════════════════════════════════════════
//  Plugin registration macro
// ═══════════════════════════════════════════════════════════════════════════
BAKKESMOD_PLUGIN(HostWorkshopMaps, "Workshop Maps Loader", "2.0", PLUGINTYPE_FREEPLAY)

// ═══════════════════════════════════════════════════════════════════════════
//  PluginWindow — required pure-virtual implementations
// ═══════════════════════════════════════════════════════════════════════════

std::string HostWorkshopMaps::GetMenuName()
{
    return "Workshop Maps";
}

std::string HostWorkshopMaps::GetMenuTitle()
{
    return "Workshop Maps Loader";
}

void HostWorkshopMaps::SetImGuiContext(uintptr_t /*ctx*/)
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

// ═══════════════════════════════════════════════════════════════════════════
//  onLoad / onUnload
// ═══════════════════════════════════════════════════════════════════════════

void HostWorkshopMaps::onLoad()
{
    std::string defaultPath = getDefaultPath();

    cvarManager->registerCvar(
        "hwm_maps_directory",
        defaultPath,
        "Directory to scan for workshop map files (*.upk, *.udk)",
        true,
        true
    );

    cvarManager->log("HWM === Workshop Maps Loader v2.0 ===");
    cvarManager->log("HWM default path: " + defaultPath);

    // ── hwm_scan ──
    cvarManager->registerNotifier(
        "hwm_scan",
        [this](std::vector<std::string> /*params*/) {
            scanMapsDirectory();
            if (mapFiles.empty()) {
                cvarManager->log("HWM: no maps found. Check hwm_maps_directory.");
            } else {
                cvarManager->log(
                    "HWM: found " + std::to_string(mapFiles.size())
                    + " maps. Use hwm_list."
                );
            }
        },
        "Scan for workshop maps",
        PERMISSION_ALL
    );

    // ── hwm_list ──
    cvarManager->registerNotifier(
        "hwm_list",
        [this](std::vector<std::string> /*params*/) {
            if (mapFiles.empty()) {
                cvarManager->log("HWM: no maps. Run hwm_scan first.");
                return;
            }
            cvarManager->log("HWM === " + std::to_string(mapFiles.size()) + " maps ===");
            for (size_t i = 0; i < mapFiles.size(); ++i) {
                cvarManager->log(
                    "  [" + std::to_string(i) + "] " + mapNames[i]
                );
            }
        },
        "List found maps",
        PERMISSION_ALL
    );

    // ── hwm_load ──
    cvarManager->registerNotifier(
        "hwm_load",
        [this](std::vector<std::string> params) {
            if (params.size() < 2) {
                cvarManager->log("HWM: usage: hwm_load <index>");
                return;
            }
            try {
                int idx = std::stoi(params[1]);
                loadMapByIndex(idx);
            } catch (...) {
                cvarManager->log("HWM: invalid index.");
            }
        },
        "Load map by index",
        PERMISSION_ALL
    );

    // ── hwm_load_path ──
    cvarManager->registerNotifier(
        "hwm_load_path",
        [this](std::vector<std::string> params) {
            if (params.size() < 2) {
                cvarManager->log("HWM: usage: hwm_load_path <path>");
                return;
            }
            loadMapByPath(params[1]);
        },
        "Load map by path",
        PERMISSION_ALL
    );

    cvarManager->log("HWM === ready === ");
    cvarManager->log("HWM commands: hwm_scan | hwm_list | hwm_load <idx> | hwm_load_path <path>");
}

void HostWorkshopMaps::onUnload()
{
    mapFiles.clear();
    mapNames.clear();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Default maps path (tries AppData → Steam → Documents)
// ═══════════════════════════════════════════════════════════════════════════

std::string HostWorkshopMaps::getDefaultPath()
{
    // Try AppData
    {
        char* buf = nullptr;
        size_t len = 0;
        if (_dupenv_s(&buf, &len, "APPDATA") == 0 && buf != nullptr) {
            std::string p = std::string(buf) + "\\bakkesmod\\bakkesmod\\data\\workshop";
            free(buf);
            if (std::filesystem::exists(p)) return p;
        }
    }

    // Try Steam
    {
        char* buf = nullptr;
        size_t len = 0;
        if (_dupenv_s(&buf, &len, "ProgramFiles(x86)") == 0 && buf != nullptr) {
            std::string p = std::string(buf) + "\\Steam\\steamapps\\workshop\\content\\252950";
            free(buf);
            if (std::filesystem::exists(p)) return p;
        }
    }

    // Try Documents
    {
        char* buf = nullptr;
        size_t len = 0;
        if (_dupenv_s(&buf, &len, "USERPROFILE") == 0 && buf != nullptr) {
            std::string p = std::string(buf) + "\\Documents\\rocketleague\\workshop";
            free(buf);
            if (std::filesystem::exists(p)) return p;
        }
    }

    // Fallback
    return "";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Map scanning
// ═══════════════════════════════════════════════════════════════════════════

void HostWorkshopMaps::scanMapsDirectory()
{
    mapFiles.clear();
    mapNames.clear();

    std::string dir = cvarManager->getCvar("hwm_maps_directory").getStringValue();

    if (dir.empty()) {
        cvarManager->log("HWM: hwm_maps_directory is empty.");
        return;
    }

    if (!std::filesystem::exists(dir)) {
        cvarManager->log("HWM: directory does not exist: " + dir);
        return;
    }

    if (!std::filesystem::is_directory(dir)) {
        cvarManager->log("HWM: path is not a directory: " + dir);
        return;
    }

    try {
        namespace fs = std::filesystem;
        auto opts = fs::directory_options::skip_permission_denied;

        for (auto const& entry : fs::recursive_directory_iterator(dir, opts)) {
            try {
                if (!entry.is_regular_file()) continue;

                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return (char)std::tolower(c); });

                if (ext == ".upk" || ext == ".udk") {
                    mapFiles.push_back(entry.path().string());
                    mapNames.push_back(entry.path().filename().string());
                }
            } catch (...) {
                // skip single file errors
            }
        }
    } catch (std::exception const& e) {
        cvarManager->log(std::string("HWM scan error: ") + e.what());
    } catch (...) {
        cvarManager->log("HWM scan: unknown error.");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Map loading
// ═══════════════════════════════════════════════════════════════════════════

void HostWorkshopMaps::loadMapByIndex(int index)
{
    if (index < 0 || index >= (int)mapFiles.size()) {
        cvarManager->log("HWM: index out of range (" + std::to_string(index) + ")");
        return;
    }
    loadMapByPath(mapFiles[index]);
}

void HostWorkshopMaps::loadMapByPath(std::string const& path)
{
    if (path.empty()) {
        cvarManager->log("HWM: empty path.");
        return;
    }

    if (!std::filesystem::exists(path)) {
        cvarManager->log("HWM: file not found: " + path);
        return;
    }

    cvarManager->log("HWM: loading " + path);

    if (isLanHost()) {
        cvarManager->log("HWM: LAN host → servertravel");
        gameWrapper->SetTimeout(
            [this, path](GameWrapper* /*gw*/) {
                gameWrapper->ExecuteUnrealCommand(
                    "servertravel \"" + path + "\""
                );
            },
            1.0f
        );
    } else {
        cvarManager->log("HWM: loading locally");
        gameWrapper->SetTimeout(
            [this, path](GameWrapper* /*gw*/) {
                gameWrapper->ExecuteUnrealCommand(
                    "open \"" + path + "\""
                );
            },
            0.5f
        );
    }
}

bool HostWorkshopMaps::isLanHost()
{
    try {
        if (!gameWrapper) return false;

        ServerWrapper server = gameWrapper->GetCurrentGameState();
        if (server.IsNull())  return false;
        if (!server.HasAuthority()) return false;
        return server.GetPRIs().Count() > 1;
    } catch (...) {
        return false;
    }
}
