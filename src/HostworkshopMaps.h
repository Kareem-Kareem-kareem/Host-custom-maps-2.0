#pragma once
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include <vector>
#include <string>

struct MapEntry
{
    std::string displayName;
    std::string fullPath;
};

class HostWorkshopMaps : public BakkesMod::Plugin::BakkesModPlugin,
                         public BakkesMod::Plugin::PluginWindow
{
public:
    // BakkesModPlugin
    void onLoad() override;
    void onUnload() override;

    // PluginWindow — all 8 pure virtuals must be implemented
    void Render() override;
    std::string GetMenuName() override;
    std::string GetMenuTitle() override;
    void SetImGuiContext(uintptr_t ctx) override;
    bool ShouldBlockInput() override;
    bool IsActiveOverlay() override;
    void OnOpen() override;
    void OnClose() override;

private:
    void ScanMaps();
    void LoadMap(int index, bool isLAN);
    void SetStatus(const std::string& msg);
    static std::string MapNameFromPath(const std::string& path);

    std::vector<MapEntry> mapList_;
    std::string mapsDirectory_;
    std::string statusMsg_;
    int selectedMapIndex_ = -1;
    bool isWindowOpen_ = false;
    char directoryBuffer_[512] = {};
};
