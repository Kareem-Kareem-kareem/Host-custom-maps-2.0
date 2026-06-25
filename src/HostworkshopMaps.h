#pragma once

#include <vector>
#include <string>
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"

// NOTE: We deliberately do NOT use <filesystem>.
// std::filesystem::recursive_directory_iterator is a known crash source in
// BakkesMod x86 plugins (it throws access-violation exceptions on protected
// Workshop/Steam folders that C++ try-catch cannot intercept).
// We use the Win32 FindFirstFileA API instead, which never throws.

class HostWorkshopMaps : public BakkesMod::Plugin::BakkesModPlugin, public BakkesMod::Plugin::PluginWindow
{
public:
    virtual void onLoad() override;
    virtual void onUnload() override;

    // PluginWindow (pure virtuals) - stub bodies, we render nothing to avoid ImGui crashes
    virtual void Render() override;
    virtual std::string GetMenuName() override;
    virtual std::string GetMenuTitle() override;
    virtual void SetImGuiContext(uintptr_t ctx) override;
    virtual bool ShouldBlockInput() override;
    virtual bool IsActiveOverlay() override;
    virtual void OnOpen() override;
    virtual void OnClose() override;

private:
    // Win32-based directory scan (recursive, depth-limited, never throws)
    void scanDirectoryRecursive(const std::string& dir, int depth);
    void scanMapsDirectory();

    std::string getDefaultPath();
    bool pathExists(const std::string& path);

    void loadMapByIndex(int index);
    void loadMapByPath(const std::string& path);
    bool isLanHost();

    static std::string toLower(const std::string& s);

    std::vector<std::string> mapFiles;
    std::vector<std::string> mapNames;
};
