#pragma once
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

struct MapEntry {
    std::string displayName;
    std::string fullPath;
    std::string extension;
};

class HostWorkshopMaps : public BakkesMod::Plugin::BakkesModPlugin,
                          public BakkesMod::Plugin::PluginWindow
{
public:
    void onLoad()   override;
    void onUnload() override;

    void        Render()       override;
    std::string GetMenuName()  override { return "hostworkshopmaps"; }
    std::string GetMenuTitle() override { return "Host Workshop Maps"; }
    void        SetImGuiContext(uintptr_t ctx) override;
    bool        ShouldBlockInput()  override { return isWindowOpen_; }
    bool        IsActiveOverlay()   override { return isWindowOpen_; }
    void        OnOpen()  override { isWindowOpen_ = true;  if (autoScanOnOpen_) ScanMaps(); }
    void        OnClose() override { isWindowOpen_ = false; }

private:
    bool isWindowOpen_ = false;

    std::vector<MapEntry> mapList_;
    int                   selectedIndex_ = -1;
    char                  filterBuf_[256] = {};
    std::string           filterText_;
    char                  dirBuf_[512] = {};

    std::string mapsDirectory_;
    bool        autoScanOnOpen_ = true;

    bool        pendingLANTransport_ = false;
    std::string pendingMapPath_;
    int         transportCountdown_  = 0;

    std::string statusMsg_;

    void ScanMaps();
    void LoadMapPath(const std::string& path);
    void TeleportLANPlayers(const std::string& mapPath);
    void OnTick(std::string eventName);
    void SetStatus(const std::string& msg);

    static std::string SanitizePath(const std::string& raw);
    static std::string MapNameFromPath(const std::string& path);
    std::vector<MapEntry> FilteredMaps() const;
};
