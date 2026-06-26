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

    void ScanMaps();
    void LoadMapPath(const std::string& path);

private:
    void SetStatus(const std::string& msg);
    std::string AutoDetectMapsPath();

    static std::string SanitizePath(const std::string& raw);
    static std::string MapNameFromPath(const std::string& path);

    std::vector<MapEntry> mapList_;
    std::string mapsDirectory_;
};
