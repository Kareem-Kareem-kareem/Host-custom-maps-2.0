#include "HostworkshopMaps.h"
#include "imgui.h"          // ← ADD THIS LINE
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/GameEvent/ServerWrapper.h"
#include "imgui.h"

#include <algorithm>
#include <sstream>

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Host Workshop Maps", "2.0", PLUGINTYPE_FREEPLAY)

// ═══════════════════════════════════════════════════════════════════
// SAFE UTILITY FUNCTIONS
// ═══════════════════════════════════════════════════════════════════

std::string HostWorkshopMaps::DefaultWorkshopPath() {
    try {
        const char* appdata = std::getenv("APPDATA");
        if (appdata) {
            fs::path bm = fs::path(appdata) / "bakkesmod" / "bakkesmod" / "data" / "workshop";
            if (fs::exists(bm)) {
                return bm.string();
            }
        }

        const char* pf = std::getenv("ProgramFiles(x86)");
        if (!pf) pf = std::getenv("ProgramFiles");
        if (pf) {
            fs::path steam = fs::path(pf) / "Steam" / "steamapps" / "workshop" / "content" / "252950";
            if (fs::exists(steam)) {
                return steam.string();
            }
        }

        const char* up = std::getenv("USERPROFILE");
        if (up) {
            fs::path docs = fs::path(up) / "Documents" / "rocketleague" / "workshop";
            return docs.string();
        }
    }
    catch (...) {
        // Fallback if any filesystem operation fails
    }
    
    return "C:/Program Files (x86)/Steam/steamapps/workshop/content/252950";
}

std::string HostWorkshopMaps::SanitizePath(const std::string& raw) {
    std::string out = raw;
    for (char& c : out) {
        if (c == '\\') c = '/';
    }
    while (!out.empty() && (out.back() == '/' || out.back() == '\\')) {
        out.pop_back();
    }
    return out;
}

std::string HostWorkshopMaps::MapNameFromPath(const std::string& path) {
    try {
        return fs::path(path).stem().string();
    }
    catch (...) {
        return "Unknown";
    }
}

bool HostWorkshopMaps::IsGameWrapperValid() const {
    return gameWrapper != nullptr;
}

bool HostWorkshopMaps::IsCVarManagerValid() const {
    return cvarManager != nullptr;
}

std::vector<MapEntry> HostWorkshopMaps::FilteredMaps() const {
    if (filterText_.empty()) {
        return mapList_;
    }

    std::string lf = filterText_;
    std::transform(lf.begin(), lf.end(), lf.begin(), 
        [](unsigned char c) { return std::tolower(c); });

    std::vector<MapEntry> out;
    for (const auto& m : mapList_) {
        std::string ln = m.displayName;
        std::transform(ln.begin(), ln.end(), ln.begin(),
            [](unsigned char c) { return std::tolower(c); });
        
        if (ln.find(lf) != std::string::npos) {
            out.push_back(m);
        }
    }
    return out;
}

void HostWorkshopMaps::SetStatus(const std::string& msg) {
    statusMsg_ = msg;
    if (IsCVarManagerValid()) {
        cvarManager->log("HostWorkshopMaps: " + msg);
    }
}

// ═══════════════════════════════════════════════════════════════════
// PLUGIN LIFECYCLE
// ═══════════════════════════════════════════════════════════════════

void HostWorkshopMaps::onLoad() {
    if (!IsCVarManagerValid()) {
        return; // Cannot proceed without cvarManager
    }

    cvarManager->log("HostWorkshopMaps v2.0 loading...");

    try {
        // Register CVars with safe defaults
        std::string defPath = DefaultWorkshopPath();
        
        auto dirCvar = cvarManager->registerCvar(
            "hwm_maps_directory", 
            defPath, 
            "Workshop maps folder path",
            true
        );
        
        dirCvar.addOnValueChanged([this](std::string, CVarWrapper cv) {
            if (!IsCVarManagerValid()) return;
            
            std::string newPath = SanitizePath(cv.getStringValue());
            mapsDirectory_ = newPath;
            
            // Safe string copy
            size_t copyLen = std::min(newPath.length(), sizeof(dirBuf_) - 1);
            memcpy(dirBuf_, newPath.c_str(), copyLen);
            dirBuf_[copyLen] = '\0';
            
            ScanMaps();
        });

        // Get initial value safely
        if (IsCVarManagerValid()) {
            mapsDirectory_ = SanitizePath(
                cvarManager->getCvar("hwm_maps_directory").getStringValue()
            );
            size_t copyLen = std::min(mapsDirectory_.length(), sizeof(dirBuf_) - 1);
            memcpy(dirBuf_, mapsDirectory_.c_str(), copyLen);
            dirBuf_[copyLen] = '\0';
        }

        // Register notifiers
        cvarManager->registerNotifier(
            "hwm_scan",
            [this](std::vector<std::string>) {
                if (isLoaded_) ScanMaps();
            },
            "Scan for workshop maps",
            PERMISSION_ALL
        );

        cvarManager->registerNotifier(
            "hwm_list",
            [this](std::vector<std::string>) {
                if (!IsCVarManagerValid() || !isLoaded_) return;
                
                std::stringstream ss;
                ss << "Found " << mapList_.size() << " maps:\n";
                for (size_t i = 0; i < mapList_.size(); ++i) {
                    ss << i << ": " << mapList_[i].displayName << "\n";
                }
                cvarManager->log(ss.str());
            },
            "List all maps in console",
            PERMISSION_ALL
        );

        isLoaded_ = true;

        // Delayed scan to ensure game is ready
        if (IsGameWrapperValid()) {
            gameWrapper->SetTimeout([this](GameWrapper* gw) {
                if (isLoaded_ && gw) {
                    ScanMaps();
                    SetStatus("Ready - " + std::to_string(mapList_.size()) + " maps found");
                }
            }, 2.5f);
        }

        cvarManager->log("HostWorkshopMaps loaded successfully");
    }
    catch (const std::exception& e) {
        if (IsCVarManagerValid()) {
            cvarManager->log("HostWorkshopMaps ERROR in onLoad: " + std::string(e.what()));
        }
        isLoaded_ = false;
    }
    catch (...) {
        if (IsCVarManagerValid()) {
            cvarManager->log("HostWorkshopMaps ERROR: Unknown exception in onLoad");
        }
        isLoaded_ = false;
    }
}

void HostWorkshopMaps::onUnload() {
    isLoaded_ = false;
    imguiInitialized_ = false;
    mapList_.clear();
}

// ═══════════════════════════════════════════════════════════════════
// MAP SCANNING
// ═══════════════════════════════════════════════════════════════════

void HostWorkshopMaps::ScanMaps() {
    if (!isLoaded_) return;

    mapList_.clear();
    selectedIndex_ = -1;

    if (mapsDirectory_.empty()) {
        SetStatus("No directory set");
        return;
    }

    try {
        fs::path dir(mapsDirectory_);
        
        if (!fs::exists(dir)) {
            SetStatus("Directory does not exist: " + mapsDirectory_);
            return;
        }

        if (!fs::is_directory(dir)) {
            SetStatus("Path is not a directory: " + mapsDirectory_);
            return;
        }

        for (auto& entry : fs::recursive_directory_iterator(
            dir, 
            fs::directory_options::skip_permission_denied
        )) {
            try {
                if (!entry.is_regular_file()) continue;

                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return std::tolower(c); });

                if (ext == ".upk" || ext == ".udk") {
                    MapEntry me;
                    me.extension = ext.substr(1);
                    me.fullPath = SanitizePath(entry.path().string());
                    me.displayName = MapNameFromPath(me.fullPath);
                    mapList_.push_back(me);
                }
            }
            catch (...) {
                // Skip files that cause errors
                continue;
            }
        }

        std::sort(mapList_.begin(), mapList_.end(),
            [](const MapEntry& a, const MapEntry& b) {
                return a.displayName < b.displayName;
            });

        SetStatus("Scan complete - " + std::to_string(mapList_.size()) + " maps found");
    }
    catch (const fs::filesystem_error& e) {
        SetStatus("Filesystem error: " + std::string(e.what()));
    }
    catch (const std::exception& e) {
        SetStatus("Scan error: " + std::string(e.what()));
    }
    catch (...) {
        SetStatus("Unknown error during scan");
    }
}

// ═══════════════════════════════════════════════════════════════════
// MAP LOADING
// ═══════════════════════════════════════════════════════════════════

void HostWorkshopMaps::LoadMapPath(const std::string& path) {
    if (!isLoaded_ || !IsGameWrapperValid() || !IsCVarManagerValid()) {
        SetStatus("Cannot load map - game not ready");
        return;
    }

    if (path.empty()) {
        SetStatus("Invalid map path");
        return;
    }

    try {
        if (!fs::exists(path)) {
            SetStatus("Map file not found: " + path);
            return;
        }

        std::string cmd = "load_workshop \"" + path + "\"";
        cvarManager->executeCommand(cmd, false);
        
        SetStatus("Loading: " + MapNameFromPath(path));
    }
    catch (const std::exception& e) {
        SetStatus("Error loading map: " + std::string(e.what()));
    }
    catch (...) {
        SetStatus("Unknown error loading map");
    }
}

// ═══════════════════════════════════════════════════════════════════
// IMGUI RENDERING
// ═══════════════════════════════════════════════════════════════════

void HostWorkshopMaps::SetImGuiContext(uintptr_t ctx) {
    if (ctx == 0) return;
    
    try {
        ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
        imguiInitialized_ = true;
    }
    catch (...) {
        imguiInitialized_ = false;
    }
}

void HostWorkshopMaps::RenderSettings() {
    if (!isLoaded_ || !imguiInitialized_) return;

    try {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Host Workshop Maps v2.0");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Directory input
        ImGui::Text("Maps Directory:");
        ImGui::PushItemWidth(-80);
        if (ImGui::InputText("##dir", dirBuf_, sizeof(dirBuf_), 
            ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (IsCVarManagerValid()) {
                cvarManager->getCvar("hwm_maps_directory").setValue(std::string(dirBuf_));
            }
        }
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        if (ImGui::Button("Apply", ImVec2(70, 0))) {
            if (IsCVarManagerValid()) {
                cvarManager->getCvar("hwm_maps_directory").setValue(std::string(dirBuf_));
            }
        }

        ImGui::Spacing();
        
        if (ImGui::Button("Scan Maps", ImVec2(120, 0))) {
            ScanMaps();
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(%d maps found)", (int)mapList_.size());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Filter
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##filter", "Search maps...", 
            filterBuf_, sizeof(filterBuf_))) {
            filterText_ = filterBuf_;
        }

        ImGui::Spacing();

        // Map list
        auto filtered = FilteredMaps();
        float listHeight = ImGui::GetContentRegionAvail().y - 80;
        
        ImGui::BeginChild("##maplist", ImVec2(0, listHeight), true);
        
        if (filtered.empty()) {
            ImGui::TextDisabled("No maps found. Check directory and click 'Scan Maps'.");
        } else {
            for (int i = 0; i < (int)filtered.size(); ++i) {
                bool isSelected = (i == selectedIndex_);
                std::string label = filtered[i].displayName + " [" + 
                                  filtered[i].extension + "]";
                
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    selectedIndex_ = i;
                }
                
                if (isSelected && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", filtered[i].fullPath.c_str());
                }
            }
        }
        
        ImGui::EndChild();

        ImGui::Spacing();

        // Load button
        bool canLoad = (selectedIndex_ >= 0 && selectedIndex_ < (int)filtered.size());
        
        if (!canLoad) ImGui::BeginDisabled();
        
        if (ImGui::Button("Load Selected Map", ImVec2(150, 0)) && canLoad) {
            LoadMapPath(filtered[selectedIndex_].fullPath);
        }
        
        if (!canLoad) ImGui::EndDisabled();

        ImGui::SameLine();
        if (!statusMsg_.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%s", statusMsg_.c_str());
        }
    }
    catch (const std::exception& e) {
        if (IsCVarManagerValid()) {
            cvarManager->log("ImGui render error: " + std::string(e.what()));
        }
    }
    catch (...) {
        // Silently fail to prevent crashes
    }
}
