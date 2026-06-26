#pragma once
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "imgui/imgui.h"
#include <string>
#include <vector>
#include <Windows.h>

struct MapEntry {
    std::string displayName;
    std::string fullPath;
};

class HostWorkshopMaps : public BakkesMod::Plugin::BakkesModPlugin
{
public:
    void onLoad() override;
    void onUnload() override;
    void Render(CanvasWrapper canvas);

private:
    void ScanMaps();
    void LoadMap(const std::string& path, bool isLAN);

    void SetStatus(const std::string& msg);
    std::string AutoDetectMapsPath();

    static std::string MapNameFromPath(const std::string& path);

    std::vector<MapEntry> mapList_;
    std::string subFolder_ = "Mods";
    std::string statusMsg_;
    bool showWindow_ = true;
};
