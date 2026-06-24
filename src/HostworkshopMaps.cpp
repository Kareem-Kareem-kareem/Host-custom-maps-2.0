#include "HostworkshopMaps.h"
#include "bakkesmod/wrappers/gamewrapper.h"
#include "bakkesmod/wrappers/gameevent/serverwrapper.h"
#include "bakkesmod/wrappers/engine/unrealstringwrapper.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Host Workshop Maps", "1.6", PLUGINTYPE_FREEPLAY)

std::string HostWorkshopMaps::DefaultWorkshopPath()
{
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        std::string bm = std::string(appdata) + "/bakkesmod/bakkesmod/data/workshop";
        if (fs::exists(bm)) return bm;
    }
    const char* pf = std::getenv("ProgramFiles(x86)");
    if (!pf) pf = std::getenv("ProgramFiles");
    if (pf) {
        std::string steam = std::string(pf) + "/Steam/steamapps/workshop/content/252950";
        if (fs::exists(steam)) return steam;
    }
    const char* up = std::getenv("USERPROFILE");
    if (up) return std::string(up) + "/Documents/rocketleague/workshop";
    return "C:/Program Files (x86)/Steam/steamapps/workshop/content/252950";
}

std::string HostWorkshopMaps::SanitizePath(const std::string& raw)
{
    std::string out = raw;
    for (char& c : out) if (c == '\\') c = '/';
    while (!out.empty() && (out.back() == '/' || out.back() == '\\')) out.pop_back();
    return out;
}

std::string HostWorkshopMaps::MapNameFromPath(const std::string& path)
{
    return fs::path(path).stem().string();
}

std::vector<MapEntry> HostWorkshopMaps::FilteredMaps() const
{
    if (filterText_.empty()) return mapList_;
    std::string lf = filterText_;
    std::transform(lf.begin(), lf.end(), lf.begin(), [](unsigned char c){ return std::tolower(c); });
    std::vector<MapEntry> out;
    for (auto& m : mapList_) {
        std::string ln = m.displayName;
        std::transform(ln.begin(), ln.end(), ln.begin(), [](unsigned char c){ return std::tolower(c); });
        if (ln.find(lf) != std::string::npos) out.push_back(m);
    }
    return out;
}

void HostWorkshopMaps::SetStatus(const std::string& msg)
{
    statusMsg_ = msg;
    if (cvarManager) cvarManager->log("HostWorkshopMaps: " + msg);
}

void HostWorkshopMaps::onLoad()
{
    if (!cvarManager || !gameWrapper) return;

    cvarManager->log("HostWorkshopMaps v1.6 loading...");

    std::string defPath = DefaultWorkshopPath();

    cvarManager->registerCvar("hwm_maps_directory", defPath, "Maps folder", true)
        .addOnValueChanged([this](std::string, CVarWrapper cv) {
            mapsDirectory_ = SanitizePath(cv.getStringValue());
            strncpy_s(dirBuf_, mapsDirectory_.c_str(), sizeof(dirBuf_) - 1);
            ScanMaps();
        });

    mapsDirectory_ = SanitizePath(cvarManager->getCvar("hwm_maps_directory").getStringValue());
    strncpy_s(dirBuf_, mapsDirectory_.c_str(), sizeof(dirBuf_) - 1);

    cvarManager->registerNotifier("hwm_scan", [this](std::vector<std::string>) { ScanMaps(); }, "Scan", PERMISSION_ALL);
    cvarManager->registerNotifier("hwm_toggle", [this](std::vector<std::string>) {
        if (cvarManager) cvarManager->executeCommand("togglemenu hostworkshopmaps");
    }, "Toggle GUI", PERMISSION_ALL);

    gameWrapper->SetTimeout([this](GameWrapper*) {
        ScanMaps();
        SetStatus("Ready - " + std::to_string(mapList_.size()) + " maps");
    }, 2.0f);

    cvarManager->log("HostWorkshopMaps loaded successfully");
}

void HostWorkshopMaps::onUnload() {}

void HostWorkshopMaps::ScanMaps()
{
    mapList_.clear();
    selectedIndex_ = -1;

    if (mapsDirectory_.empty()) {
        SetStatus("No directory set");
        return;
    }

    fs::path dir(mapsDirectory_);
    if (!fs::exists(dir)) {
        SetStatus("Directory not found");
        return;
    }

    try {
        for (auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
            if (ext == ".upk" || ext == ".udk") {
                MapEntry me;
                me.extension = ext.substr(1);
                me.fullPath = SanitizePath(entry.path().string());
                me.displayName = MapNameFromPath(me.fullPath);
                mapList_.push_back(me);
            }
        }
    } catch (...) {
        SetStatus("Scan error");
        return;
    }

    std::sort(mapList_.begin(), mapList_.end(), [](const MapEntry& a, const MapEntry& b){ return a.displayName < b.displayName; });
    SetStatus(std::to_string(mapList_.size()) + " maps found");
}

void HostWorkshopMaps::LoadMap(const MapEntry& entry) { LoadMapPath(entry.fullPath); }

void HostWorkshopMaps::LoadMapPath(const std::string& path)
{
    if (path.empty() || !fs::exists(fs::path(path))) {
        SetStatus("Map not found");
        return;
    }

    SetStatus("Loading: " + MapNameFromPath(path));

    if (cvarManager) {
        cvarManager->executeCommand("load_workshop \"" + path + "\"", false);
    }
}

void HostWorkshopMaps::SetImGuiContext(uintptr_t ctx)
{
    ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

void HostWorkshopMaps::Render()
{
    if (!isWindowOpen_) return;

    ImGui::SetNextWindowSize(ImVec2(620, 540), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Host Workshop Maps", &isWindowOpen_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 130);
    if (ImGui::InputText("##dir", dirBuf_, sizeof(dirBuf_), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (cvarManager) cvarManager->getCvar("hwm_maps_directory").setValue(std::string(dirBuf_));
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Apply", ImVec2(60,0)) && cvarManager)
        cvarManager->getCvar("hwm_maps_directory").setValue(std::string(dirBuf_));
    ImGui::SameLine();
    if (ImGui::Button("Scan", ImVec2(55,0)))
        ScanMaps();

    ImGui::Spacing();

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputTextWithHint("##filter", "Search maps...", filterBuf_, sizeof(filterBuf_)))
        filterText_ = filterBuf_;

    ImGui::Spacing();

    auto filtered = FilteredMaps();
    float listHeight = ImGui::GetContentRegionAvail().y - 100;
    ImGui::BeginChild("##maplist", ImVec2(0, listHeight), true);

    if (filtered.empty()) {
        ImGui::TextDisabled("No maps found. Set directory and click Scan.");
    } else {
        for (int i = 0; i < (int)filtered.size(); ++i) {
            auto& m = filtered[i];
            bool sel = (selectedIndex_ == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f,0.6f,1.0f,0.4f));

            std::string label = m.displayName + "  ##" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                selectedIndex_ = i;
                if (ImGui::IsMouseDoubleClicked(0))
                    LoadMap(m);
            }
            if (sel) ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    bool canLoad = (selectedIndex_ >= 0 && selectedIndex_ < (int)filtered.size());
    if (!canLoad) ImGui::BeginDisabled();
    if (ImGui::Button("Load Selected Map", ImVec2(180, 0)) && canLoad)
        LoadMap(filtered[selectedIndex_]);
    if (!canLoad) ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("%d map(s)", (int)filtered.size());

    if (!statusMsg_.empty())
        ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "%s", statusMsg_.c_str());

    ImGui::End();
}
