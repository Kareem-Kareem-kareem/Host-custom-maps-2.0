#pragma once
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include <vector>
#include <string>
#include <algorithm>

struct MapEntry
{
    std::string displayName;
    std::string fullPath;
};

class HostWorkshopMaps : public BakkesMod::Plugin::BakkesModPlugin
{
public:
    void onLoad() override;
    void onUnload() override;

private:
    void ScanMaps();
    void LoadMap(int index, bool isLAN);
    void SetStatus(const std::string& msg);
    static std::string MapNameFromPath(const std::string& path);
    void RenderUI();

    std::vector<MapEntry> mapList_;
    std::string mapsDirectory_;
    std::string statusMsg_;
    bool uiOpen_ = false;
    int selectedMapIndex_ = -1;
    char directoryBuffer_[512] = "";
};
