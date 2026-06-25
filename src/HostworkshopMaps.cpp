#include "HostworkshopMaps.h"
#include <windows.h>

BAKKESMOD_PLUGIN(HostWorkshopMaps, "Workshop Maps", "2.0", PLUGINTYPE_FREEPLAY)

std::string HostWorkshopMaps::GetMenuName()
{
    return "Workshop Maps";
}

std::string HostWorkshopMaps::GetMenuTitle()
{
    return "Workshop Maps";
}

void HostWorkshopMaps::SetImGuiContext(uintptr_t ctx)
{
}

bool HostWorkshopMaps::ShouldBlockInput()
{
    return false;
}

bool HostWorkshopMaps::IsActiveOverlay()
{
    return false;
}

void HostWorkshopMaps::OnOpen()
{
}

void HostWorkshopMaps::OnClose()
{
}

void HostWorkshopMaps::Render()
{
}

void HostWorkshopMaps::onLoad()
{
    cvarManager->log("HWM: Loading");
}

void HostWorkshopMaps::onUnload()
{
    cvarManager->log("HWM: Unloading");
}
