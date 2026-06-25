#pragma once

#include <filesystem>
#include <vector>
#include <string>
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"

class HostWorkshopMaps : public BakkesMod::Plugin::BakkesModPlugin, public BakkesMod::Plugin::PluginWindow
{
public:
    // BakkesModPlugin overrides
    virtual void onLoad() override;
    virtual void onUnload() override;

    // PluginWindow overrides - THESE ARE ALL REQUIRED
    virtual void Render() override;
    virtual std::string GetMenuName() override;
    virtual std::string GetMenuTitle() override;
    virtual void SetImGuiContext(uintptr_t ctx) override;  // THIS WAS MISSING!
    virtual bool ShouldBlockInput() override;
    virtual bool IsActiveOverlay() override;
    virtual void OnOpen() override;
    virtual void OnClose() override;

private:
    // Map scanning
    void safeScanMaps();
    std::string getDefaultMapsPath();

    // Map loading
    void loadMapByIndex(int index);
    void loadMapByPath(const std::string& path);
    void loadSelectedMap();

    // LAN teleport
    bool isLanHost();

    // UI helpers
    void renderMapList();
    void renderSettings();

    // Data
    std::vector<std::string> mapFiles;
    std::vector<std::string> mapNames;
    int selectedMapIndex = -1;
    char searchFilter[256] = {0};
    bool isWindowOpen = false;
    bool renderInitialized = false;
    float scanDelay = 0.0f;
    bool pendingScan = false;
};
