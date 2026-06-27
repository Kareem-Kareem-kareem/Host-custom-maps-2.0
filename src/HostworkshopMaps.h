#pragma once
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include <vector>
#include <string>
#include <algorithm>

struct MapEntry
{
    std::string displayName;
    std::string fullPath;
};

class HostWorkshopMaps : public BakkesMod::Plugin::BakkesModPlugin,
                         public BakkesMod::Plugin::PluginWindow
{
public:
    void onLoad() override;
    void onUnload() override;

    // PluginWindow overrides
    void Render() override;
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
    int selectedMapIndex_ = -1;
    bool isWindowOpen_ = true;
    char directoryBuffer_[512] = {};
};
