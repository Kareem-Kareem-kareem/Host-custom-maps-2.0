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

class HostWorkshopMaps : public BakkesMod::Plugin::BakkesModPlugin
{
public:
    void onLoad()   override;
    void onUnload() override;

private:
    std::vector<MapEntry> mapList_;
    int selectedIndex_ = 0;
    std::string statusMsg_;
    std::string mapsDirectory_;

    bool pendingLANTransport_ = false;
    std::string pendingMapPath_;
    int transportCountdown_ = 0;

    void ScanMaps();
    void LoadMapPath(const std::string& path);
    void TeleportLANPlayers(const std::string& path);
    void SetStatus(const std::string& msg);

    static std::string SanitizePath(const std::string& raw);
    static std::string MapNameFromPath(const std::string& path);
};
