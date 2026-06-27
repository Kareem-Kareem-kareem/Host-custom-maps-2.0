#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "HostWorkshopMaps.h"
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/gameevent/serverwrapper.h"
#include "imgui/imgui.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdio>

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Host Workshop Maps", "2.0", 0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string HostWorkshopMaps::MapNameFromPath(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    std::string f = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = f.find_last_of('.');
    return (dot == std::string::npos) ? f : f.substr(0, dot);
}

void HostWorkshopMaps::SetStatus(const std::string& msg)
{
    statusMsg_ = msg;
    cvarManager->log("HostWorkshopMaps: " + msg);
}

// ---------------------------------------------------------------------------
// Directory scan
// ---------------------------------------------------------------------------

static void SafeWalkDir(const std::string& dir, std::vector<MapEntry>& out)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    int count = 0;
    do
    {
        if (count > 300) break;
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        std::string name = fd.cFileName;
        size_t dot = name.find_last_of('.');
        if (dot == std::string::npos) continue;

        std::string ext = name.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == "upk" || ext == "udk")
        {
            MapEntry me;
            me.fullPath = dir + "\\" + name;
            for (char& c : me.fullPath) if (c == '\\') c = '/';
            me.displayName = name.substr(0, dot);
            out.push_back(me);
            ++count;
        }
    }
    while (FindNextFileA(h, &fd));

    FindClose(h);
}

// ---------------------------------------------------------------------------
// Plugin lifecycle
// ---------------------------------------------------------------------------

void HostWorkshopMaps::onLoad()
{
    auto dirCvar = cvarManager->registerCvar(
        "hwm_maps_directory", "", "Full path to your maps folder",
        true, true, 0, true, 0, true);

    dirCvar.addOnValueChanged([this](std::string, CVarWrapper cvar)
    {
        mapsDirectory_ = cvar.getStringValue();
        if (!mapsDirectory_.empty()) ScanMaps();
    });

    cvarManager->registerNotifier("hwm_scan",
        [this](std::vector<std::string>)
        {
            ScanMaps();
        },
        "Scan maps directory", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_list",
        [this](std::vector<std::string>)
        {
            cvarManager->log("--- Maps List ---");
            for (size_t i = 0; i < mapList_.size(); i++)
                cvarManager->log(std::to_string(i) + ": " + mapList_[i].displayName);
        },
        "List scanned maps", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_load",
        [this](std::vector<std::string> params)
        {
            if (params.size() < 2)
            {
                cvarManager->log("Usage: hwm_load <index>");
                return;
            }
            try
            {
                LoadMap(std::stoi(params[1]), false);
            }
            catch (const std::invalid_argument&)
            {
                cvarManager->log("hwm_load: <index> must be a number");
            }
            catch (const std::out_of_range&)
            {
                cvarManager->log("hwm_load: <index> is out of integer range");
            }
        },
        "Load map in solo", PERMISSION_ALL);

    cvarManager->registerNotifier("hwm_lan",
        [this](std::vector<std::string> params)
        {
            if (params.size() < 2)
            {
                cvarManager->log("Usage: hwm_lan <index>");
                return;
            }
            try
            {
                LoadMap(std::stoi(params[1]), true);
            }
            catch (const std::invalid_argument&)
            {
                cvarManager->log("hwm_lan: <index> must be a number");
            }
            catch (const std::out_of_range&)
            {
                cvarManager->log("hwm_lan: <index> is out of integer range");
            }
        },
        "Host map over LAN", PERMISSION_ALL);

    cvarManager->log("HostWorkshopMaps loaded.");
    cvarManager->log("Commands: hwm_maps_directory \"path\" -> hwm_scan -> hwm_list -> hwm_load 0");
}

void HostWorkshopMaps::onUnload() {}

// ---------------------------------------------------------------------------
// Map scanning
// ---------------------------------------------------------------------------

void HostWorkshopMaps::ScanMaps()
{
    mapList_.clear();

    if (mapsDirectory_.empty())
    {
        SetStatus("Set hwm_maps_directory first");
        return;
    }

    DWORD attr = GetFileAttributesA(mapsDirectory_.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        SetStatus("Directory not found: " + mapsDirectory_);
        return;
    }

    SafeWalkDir(mapsDirectory_, mapList_);

    std::sort(mapList_.begin(), mapList_.end(),
        [](const MapEntry& a, const MapEntry& b)
        {
            return a.displayName < b.displayName;
        });

    SetStatus("Found " + std::to_string(mapList_.size()) + " map(s)");
}

// ---------------------------------------------------------------------------
// Map loading
// ---------------------------------------------------------------------------

void HostWorkshopMaps::LoadMap(int index, bool isLAN)
{
    if (index < 0 || index >= static_cast<int>(mapList_.size()))
    {
        SetStatus("Invalid index. Run hwm_list to see available maps.");
        return;
    }

    const MapEntry& m = mapList_[index];

    try
    {
        if (isLAN)
        {
            if (!gameWrapper->IsInGame())
            {
                SetStatus("Not in a game");
                return;
            }

            ServerWrapper server = gameWrapper->GetCurrentGameState();
            if (server.IsNull() || !server.HasAuthority())
            {
                SetStatus("Not host or not in a LAN match");
                return;
            }

            SetStatus("LAN travel: " + m.displayName);
            gameWrapper->ExecuteUnrealCommand("servertravel " + m.displayName);
        }
        else
        {
            SetStatus("Loading solo: " + m.displayName);
            gameWrapper->ExecuteUnrealCommand("open \"" + m.fullPath + "\"");
        }
    }
    catch (...)
    {
        SetStatus("Error while loading map");
    }
}

// ---------------------------------------------------------------------------
// ImGui Settings Window (BakkesMod F2 Menu -> Plugins -> Host Workshop Maps)
// ---------------------------------------------------------------------------

std::string HostWorkshopMaps::GetPluginName()
{
    return "Host Workshop Maps";
}

void HostWorkshopMaps::SetImGuiContext(uintptr_t ctx)
{
    ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

void HostWorkshopMaps::RenderSettings()
{
    ImGui::TextUnformatted("Workshop Maps Directory:");

    char dirBuf[512];
    snprintf(dirBuf, sizeof(dirBuf), "%s", mapsDirectory_.c_str());

    if (ImGui::InputText("##mapsDir", dirBuf, sizeof(dirBuf)))
    {
        mapsDirectory_ = dirBuf;
        cvarManager->getCvar("hwm_maps_directory").setValue(mapsDirectory_);
    }

    ImGui::SameLine();
    if (ImGui::Button("Scan Folder"))
    {
        ScanMaps();
    }

    if (!statusMsg_.empty())
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.2f, 1.0f), "Status: %s", statusMsg_.c_str());
    }

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Scanned Maps (%d):", static_cast<int>(mapList_.size()));

    if (mapList_.empty())
    {
        ImGui::TextDisabled("No maps scanned. Configure path above and click Scan.");
        return;
    }

    if (ImGui::BeginChild("MapsListChild", ImVec2(0, 320), true))
    {
        for (int i = 0; i < static_cast<int>(mapList_.size()); ++i)
        {
            const auto& m = mapList_[i];
            ImGui::PushID(i);

            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d. %s", i, m.displayName.c_str());

            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 150);

            if (ImGui::Button("Solo", ImVec2(65, 0)))
            {
                LoadMap(i, false);
            }
            ImGui::SameLine();
            if (ImGui::Button("Host LAN", ImVec2(75, 0)))
            {
                LoadMap(i, true);
            }

            ImGui::PopID();
        }
        ImGui::EndChild();
    }
}
