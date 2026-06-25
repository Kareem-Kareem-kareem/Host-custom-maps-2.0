#pragma once

#include <vector>
#include <string>
#include <windows.h>
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"

// FEATURE SET 1: We will add ImGui UI and Map Loading functionality.
// This is the first step of adding features "2 by 2" to ensure stability.

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
    // Base Feature: CVars and notifiers
    void registerCommands();

    // Base Feature: Directory scanning (basic)
    void scanMapsDirectory();
    std::string getDefaultPath();
    std::vector<std::string> mapFiles;
    std::vector<std::string> mapNames;

    // Feature 1: Map Loading
    void loadMap(const std::string& path);
    
    // Feature 2: Basic ImGui State
    bool isWindowOpen = false;
    int selectedMapIndex = -1;
};

