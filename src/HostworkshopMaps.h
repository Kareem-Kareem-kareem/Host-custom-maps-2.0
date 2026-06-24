#pragma once
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  Map entry: one .upk / .udk file found on disk
// ─────────────────────────────────────────────────────────────────────────────
struct MapEntry {
    std::string displayName;   // filename without extension
    std::string fullPath;      // absolute path with forward slashes
    std::string extension;     // "upk" or "udk"
};

// ─────────────────────────────────────────────────────────────────────────────
//  HostWorkshopMaps
//  - Scans a configurable folder (default: Steam Workshop maps dir) for
//    .upk / .udk files
//  - Loads any chosen map as freeplay / training (works offline & LAN)
//  - When the host loads a map while a LAN game is active it teleports all
//    connected players to the new map automatically
// ─────────────────────────────────────────────────────────────────────────────
class HostWorkshopMaps : public BakkesMod::Plugin::BakkesModPlugin,
                          public BakkesMod::Plugin::PluginWindow
{
public:
    // ── BakkesMod lifecycle ───────────────────────────────────────────────
    void onLoad()   override;
    void onUnload() override;

    // ── PluginWindow (ImGui F2 panel) ────────────────────────────────────
    void        Render()       override;
    std::string GetMenuName()  override { return "hostworkshopmaps"; }
    std::string GetMenuTitle() override { return "Host Workshop Maps"; }
    void        SetImGuiContext(uintptr_t ctx) override;
    bool        ShouldBlockInput()  override { return false; }
    bool        IsActiveOverlay()   override { return false; }
    void        OnOpen()  override {}
    void        OnClose() override {}

private:
    // ── Map list ─────────────────────────────────────────────────────────
    std::vector<MapEntry> mapList_;
    int                   selectedIndex_  = -1;
    std::string           filterText_;          // ImGui search box buffer
    char                  filterBuf_[256]  = {};

    // ── CVar-backed settings ─────────────────────────────────────────────
    std::string mapsDirectory_;  // from cvar hwm_maps_directory
    bool        autoScanOnOpen_; // from cvar hwm_auto_scan

    // ── LAN state ────────────────────────────────────────────────────────
    bool isHostingLAN_        = false;
    bool pendingLANTransport_ = false;
    std::string pendingMapPath_;
    int         transportCountdown_ = 0; // ticks before we issue the travel

    // ── Helpers ──────────────────────────────────────────────────────────
    void ScanMaps();
    void LoadMap(const MapEntry& entry);
    void LoadMapPath(const std::string& path);
    void TeleportLANPlayers(const std::string& mapPath);
    void OnTick(std::string eventName);

    static std::string DefaultWorkshopPath();
    static std::string SanitizePath(const std::string& raw);
    static std::string MapNameFromPath(const std::string& path);

    std::vector<MapEntry> FilteredMaps() const;
};
