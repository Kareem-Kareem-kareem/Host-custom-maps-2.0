#pragma once

#include <vector>
#include <string>
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"

class HostWorkshopMaps : public BakkesMod::Plugin::BakkesModPlugin, public BakkesMod::Plugin::PluginWindow
{
public:
    virtual void onLoad() override;
    virtual void onUnload() override;

    virtual void Render() override;
    virtual std::string GetMenuName() override;
    virtual std::string GetMenuTitle() override;
    virtual void SetImGuiContext(uintptr_t ctx) override;
    virtual bool ShouldBlockInput() override;
    virtual bool IsActiveOverlay() override;
    virtual void OnOpen() override;
    virtual void OnClose() override;

private:
    void scanDirectoryRecursive(const std::string& dir, int depth);
    void scanMapsDirectory();
    std::string getDefaultPath();
    void loadMapByIndex(int index);
    void loadMapByPath(const std::string& path);
    bool isLanHost();
    static std::string toLower(const std::string& s);

    std::vector<std::string> mapFiles;
    std::vector<std::string> mapNames;
};
