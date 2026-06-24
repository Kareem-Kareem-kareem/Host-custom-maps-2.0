#pragma once

#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include "bakkesmod/plugin/PluginSettingsWindow.h"

#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

struct MapEntry {
    std::string displayName;
    std::string fullPath;
    std::string extension;
};

class HostWorkshopMaps 
    : public BakkesMod::Plugin::BakkesModPlugin
    , public BakkesMod::Plugin::PluginSettingsWindow
{
public:
    void onLoad() override;
    void onUnload() override;

    // ── PluginSettingsWindow ────────────────────────────────────────
    void RenderSettings() override;
    std::string GetPluginName() override { return "Host Workshop Maps"; }
    void SetImGuiContext(uintptr_t ctx) override;

private:
    // ── State flags ─────────────────────────────────────────────────
    bool isLoaded_ = false;
    bool imguiInitialized_ = false;
    
    // ── Map list ────────────────────────────────────────────────────
    std::vector<MapEntry> mapList_;
    int selectedIndex_ = -1;
    char filterBuf_[256] = {};
    std::string filterText_;

    // ── Directory input buffer ──────────────────────────────────────
    char dirBuf_[512] = {};

    // ── CVar-backed settings ────────────────────────────────────────
    std::string mapsDirectory_;
    bool autoScanOnOpen_ = true;

    // ── Status message ──────────────────────────────────────────────
    std::string statusMsg_;

    // ── Helpers ─────────────────────────────────────────────────────
    void ScanMaps();
    void LoadMapPath(const std::string& path);
    void SetStatus(const std::string& msg);
    
    static std::string DefaultWorkshopPath();
    static std::string SanitizePath(const std::string& raw);
    static std::string MapNameFromPath(const std::string& path);
    
    std::vector<MapEntry> FilteredMaps() const;
    bool IsGameWrapperValid() const;
    bool IsCVarManagerValid() const;
};
