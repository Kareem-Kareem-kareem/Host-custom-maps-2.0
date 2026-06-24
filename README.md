# Workshop Map Loader — BakkesMod Plugin

A BakkesMod plugin for Rocket League that lets you browse and load any `.upk` or `.udk` Workshop map directly from inside the game. When you are the LAN host, loading a new map automatically teleports all connected players to it.

---

## Features

| Feature | Details |
|---|---|
| **Map scanning** | Recursively scans a configurable folder for `.upk` and `.udk` files |
| **In-game GUI** | Full ImGui panel under **F2 → Plugins → Workshop Map Loader** |
| **Search filter** | Type to filter the map list by name |
| **Freeplay / Training load** | Works offline, no server needed |
| **LAN host teleport** | While hosting a LAN match, loading a map issues a `ServerTravel` that moves every connected client to the new map |
| **Console commands** | Full console API (see below) |

---

## Installation

1. Build the plugin (see **Build** below) or download a pre-built release.
2. Copy `WorkshopMapLoader.dll` → `%APPDATA%\bakkesmod\bakkesmod\plugins\`
3. Copy `WorkshopMapLoader.set` → `%APPDATA%\bakkesmod\bakkesmod\plugins\settings\`
4. In BakkesMod open **F2 → Plugins → Plugin Manager** and enable **Workshop Map Loader**.

---

## Build

### Prerequisites

- Windows 10/11
- CMake ≥ 3.15
- Visual Studio 2019/2022 with **Desktop development with C++**
- [BakkesModSDK](https://github.com/bakkesmodorg/BakkesModSDK) cloned somewhere

### Steps

```powershell
git clone https://github.com/YourUser/WorkshopMapLoader
cd WorkshopMapLoader

cmake -B build -S . -DBAKKESMOD_SDK="C:\path\to\BakkesModSDK"
cmake --build build --config Release
```

The DLL (and .set file if `%APPDATA%` is set and `CI` is not set) is copied automatically into your BakkesMod plugins folder after a successful build.

---

## Console Commands

| Command | Description |
|---|---|
| `wml_scan` | Re-scan the maps directory |
| `wml_list` | Print all found maps to the BM console |
| `wml_load_index <n>` | Load map by index (0-based) |
| `wml_load_path <path>` | Load map by full absolute path |
| `wml_maps_directory <path>` | Change the scan directory |

---

## How LAN teleport works

1. You start a LAN match (**Play → LAN**) and are the host.
2. Other players join via local network.
3. You open the plugin panel, pick a new map, and click **Load Selected Map**.
4. The plugin detects you have authority (`server.HasAuthority()`) and that remote players are connected.
5. After a ~1 second delay (to ensure Unreal is ready), it issues `servertravel "<map path>"`.
6. Unreal Engine 3's built-in `ServerTravel` migrates every connected client to the new level — no rejoin needed.

---

## Map folder defaults

The plugin tries these locations in order:

1. `%APPDATA%\bakkesmod\bakkesmod\data\workshop`
2. `%ProgramFiles(x86)%\Steam\steamapps\workshop\content\252950`
3. `%USERPROFILE%\Documents\rocketleague\workshop`

You can override with `wml_maps_directory <path>` or via the Settings section in the F2 panel.

---

## Supported map formats

| Extension | Notes |
|---|---|
| `.upk` | Unreal Package — most Steam Workshop maps |
| `.udk` | Unreal Development Kit package — older community maps |
