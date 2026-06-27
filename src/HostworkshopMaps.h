#pragma once
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/PluginSettingsWindow.h"
#include <vector>
#include <string>
#include <algorithm>

struct MapEntry {
    std::string displayName;
    std::string fullPath;
};

class HostWorkshopMaps : public BakkesMod::Plugin::BakkesModPlugin, public BakkesMod::Plugin::PluginSettingsWindow
{
public:
    void onLoad() override;
    void onUnload() override;

    // PluginSettingsWindow implementation (F2 -> Plugins -> Host Workshop Maps)
    void RenderSettings() override;
    std::string GetPluginName() override;
    void SetImGuiContext(uintptr_t ctx) override;

private:
    void ScanMaps();
    void LoadMap(int index, bool isLAN);
    void SetStatus(const std::string& msg);
    static std::string MapNameFromPath(const std::string& path);

    std::vector<MapEntry> mapList_;
    std::string mapsDirectory_;
    std::string statusMsg_;
};
