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
#include <imgui/imgui.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Host Workshop Maps", "2.0", 0)

// ---------------------------------------------------------------------------
// PluginWindow required overrides
// ---------------------------------------------------------------------------

std::string HostWorkshopMaps::GetPluginName()
{
    return "Host Workshop Maps";
}

void HostWorkshopMaps::SetImGuiContext(uintptr_t ctx)
{
    ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

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
// ImGui UI  (called every frame by BakkesMod via PluginWindow::Render)
// ---------------------------------------------------------------------------

void HostWorkshopMaps::Render()
{
    ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Host Workshop Maps", &isWindowOpen_, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    // ---- Status bar -------------------------------------------------------
    if (!statusMsg_.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
        ImGui::TextWrapped("%s", statusMsg_.c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    // ---- Directory input --------------------------------------------------
    ImGui::Text("Maps Directory:");
    ImGui::SetNextItemWidth(-140.0f);
    bool enterPressed = ImGui::InputText(
        "##dir", directoryBuffer_, IM_ARRAYSIZE(directoryBuffer_),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Scan", ImVec2(60, 0)) || enterPressed)
    {
        mapsDirectory_ = directoryBuffer_;
        ScanMaps();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(60, 0)))
    {
        memset(directoryBuffer_, 0, sizeof(directoryBuffer_));
        mapsDirectory_.clear();
        mapList_.clear();
        selectedMapIndex_ = -1;
        SetStatus("Cleared.");
    }

    // ---- Map list ---------------------------------------------------------
    ImGui::Separator();
    ImGui::Text("Available Maps (%zu)", mapList_.size());

    if (mapList_.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
        ImGui::TextWrapped("No maps found. Set a directory and click Scan.");
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::BeginChild("##maplist", ImVec2(0.0f, 210.0f), true);
        for (int i = 0; i < static_cast<int>(mapList_.size()); i++)
        {
            bool selected = (i == selectedMapIndex_);
            if (ImGui::Selectable(
                    mapList_[i].displayName.c_str(),
                    selected,
                    ImGuiSelectableFlags_AllowDoubleClick))
            {
                selectedMapIndex_ = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    LoadMap(i, false);
            }
        }
        ImGui::EndChild();
    }

    // ---- Action buttons ---------------------------------------------------
    ImGui::Separator();

    const char* selName = (selectedMapIndex_ >= 0 &&
                           selectedMapIndex_ < static_cast<int>(mapList_.size()))
                              ? mapList_[selectedMapIndex_].displayName.c_str()
                              : "None";
    ImGui::Text("Selected: %s", selName);

    bool hasSelection = selectedMapIndex_ >= 0 &&
                        selectedMapIndex_ < static_cast<int>(mapList_.size());

    if (!hasSelection)
    {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Load Solo", ImVec2(130, 0)))
        LoadMap(selectedMapIndex_, false);

    ImGui::SameLine();

    if (ImGui::Button("Host LAN", ImVec2(130, 0)))
        LoadMap(selectedMapIndex_, true);

    if (!hasSelection)
    {
        ImGui::EndDisabled();
    }

    // ---- Footer hint ------------------------------------------------------
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
    ImGui::TextWrapped(
        "Tip: double-click a map to load it in solo. "
        "Console: hwm_scan  hwm_list  hwm_load <n>  hwm_lan <n>");
    ImGui::PopStyleColor();

    ImGui::End();
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
        strncpy_s(directoryBuffer_, sizeof(directoryBuffer_),
                  mapsDirectory_.c_str(), _TRUNCATE);
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
    cvarManager->log("Open BakkesMod plugins tab to access the UI.");
    cvarManager->log("Console: hwm_maps_directory \"path\"  hwm_scan  hwm_list  hwm_load 0");
}

void HostWorkshopMaps::onUnload() {}

// ---------------------------------------------------------------------------
// Map scanning
// ---------------------------------------------------------------------------

void HostWorkshopMaps::ScanMaps()
{
    mapList_.clear();
    selectedMapIndex_ = -1;

    if (mapsDirectory_.empty())
    {
        SetStatus("Set hwm_maps_directory first.");
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

    SetStatus("Found " + std::to_string(mapList_.size()) + " map(s).");
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
                SetStatus("Not in a game.");
                return;
            }

            ServerWrapper server = gameWrapper->GetCurrentGameState();
            if (server.IsNull() || !server.HasAuthority())
            {
                SetStatus("Not host or not in a LAN match.");
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
        SetStatus("Error while loading map.");
    }
}
