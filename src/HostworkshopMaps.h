#pragma once

#include <vector>
#include <string>
#include <windows.h>
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include <imgui.h>

class HostWorkshopMaps : public BakkesMod::Plugin::BakkesModPlugin, public BakkesMod::Plugin::PluginWindow {
public:
    virtual void onLoad() override;
    virtual void onUnload() override;
    
    // ImGui PluginWindow interface
    virtual void Render() override;
    virtual std::string GetMenuName() override;
    virtual std::string GetMenuTitle() override;
    virtual void SetImGuiContext(uintptr_t ctx) override;
    virtual bool ShouldBlockInput() override;
    virtual bool IsActiveOverlay() override;
    virtual void OnOpen() override;
    virtual void OnClose() override;

private:
    void registerCommands();
    void scanMapsDirectory();
    std::string getDefaultPath();
    std::vector<std::string> mapFiles;
    std::vector<std::string> mapNames;

    void loadMap(const std::string& path);
    std::string cleanMapName(const std::string& filename);

    bool isWindowOpen = false;
    int selectedMapIndex = -1;

    // Feature 5 & 6: Multiplayer Hooks
    void onJoinParty();
    void sendMapToParty(const std::string& mapName);
};
