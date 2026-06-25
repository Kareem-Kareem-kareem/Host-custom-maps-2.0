#pragma once
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include <string>
#include <vector>
#include <Windows.h>

struct MapEntry {
    std::string displayName;
    std::string fullPath;
    std::string extension;
};

// No PluginWindow inheritance — we render via HookEvent on the DX present hook
class HostWorkshopMaps : public BakkesMod::Plugin::BakkesModPlugin
{
public:
    void onLoad()   override;
    void onUnload() override;

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
    void OnRender(CanvasWrapper canvas);
    void SetStatus(const std::string& msg);

    static std::string SanitizePath(const std::string& raw);
    static std::string MapNameFromPath(const std::string& path);
    std::vector<MapEntry> FilteredMaps() const;
};
