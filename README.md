<div align="center">

# GTA SA Minecraft: Minecraft Mod for GTA San Andreas

**Play Minecraft and GTA San Andreas together, in one world.**<br>
Mine and build blocks in Los Santos, craft, survive, fight Creepers and the Warden, and blow holes into the map with TNT. Then jump into a car and drive off with a hotbar full of blocks.

![Game: GTA San Andreas](https://img.shields.io/badge/game-GTA%20San%20Andreas%201.0%20US-orange)
![Minecraft crossover](https://img.shields.io/badge/crossover-Minecraft-62B47A)
![Mod type: ASI plugin](https://img.shields.io/badge/mod-ASI%20plugin-blue)
![Language: C++](https://img.shields.io/badge/language-C%2B%2B-00599C?logo=cplusplus)
![Platform: Windows](https://img.shields.io/badge/platform-Windows-0078D6?logo=windows)
![Status: in development](https://img.shields.io/badge/status-in%20development-yellow)

<img src="docs/images/gta-sa-minecraft-mod-screenshot.png" alt="GTA San Andreas Minecraft mod: Steve in diamond armour with a bow on a Los Santos street, next to a Minecraft wooden house and tree, GTA cars, villagers and the Minecraft hotbar, hearts and hunger bar" width="100%">

</div>

---

## Contents

- [What is GTA SA Minecraft?](#what-is-gta-sa-minecraft)
- [Features](#features)
- [Requirements](#requirements)
- [Building and installing](#building-and-installing)
- [Controls](#controls)
- [Settings](#settings)
- [Project structure](#project-structure)
- [FAQ](#faq)
- [Credits](#credits)
- [Disclaimer](#disclaimer)
- [Türkçe](#türkçe)

## What is GTA SA Minecraft?

**GTA SA Minecraft** is a total crossover mod that brings **Minecraft into Grand Theft Auto: San Andreas**. It is not a texture pack and not a map. It is an **ASI plugin** written in C++ that runs Minecraft's game rules inside the GTA SA engine:

- Minecraft blocks, items, crafting, survival and creative modes.
- Minecraft mobs, mining, sounds and the Minecraft HUD.
- All of it on top of the real GTA world: its streets, cars, people, police and missions.

Break the GTA map itself: roads give blackstone, pavements give stone bricks and grass gives dirt. Dig holes into the ground that cars and pedestrians fall into, or break into buildings and cliffs. Build a house on Grove Street, fight a Creeper in Ganton, or fly over Los Santos with an elytra and fireworks.

The mod uses Minecraft's original textures, sounds, models, recipes and loot tables, read from your own copy of the game's resources. Nothing from Minecraft is included in this repository.

## Features

### The GTA world becomes a Minecraft world
- **Mine the GTA map.** Every surface gives what it is made of: asphalt, pavement, concrete, rock, grass, sand, mud, glass, metal, trees and bushes.
- **Dig into the ground.** The GTA terrain turns into Minecraft blocks under the surface: dirt, stone and ores, down to bedrock.
- **Break into buildings and cliffs.** The GTA model is cut open where you break it, and blocks lie behind.
- **Cars, pedestrians and street objects can be mined too.** Break a car with a pickaxe for iron, glass and redstone.
- **GTA vehicles and people fall into your holes**, and stand on the blocks you place.
- **TNT blows craters** into the GTA map and its buildings.

### Minecraft gameplay
- **680+ blocks and 390+ items** with their original textures and sounds.
- **Shaped blocks:** stairs, slabs, fences, walls, glass panes, iron bars and beds.
- **Nearly 900 recipes:** crafting table, 2×2 inventory crafting, furnace, smoker and blast furnace.
- **Survival:** health, hunger, saturation, XP and levels, armour, fall damage, drowning and burning.
- **Creative mode:** flying, pick block, and creative tabs laid out and sorted as in Minecraft.
- **Beds:** sleep through the night and set your respawn point.
- **Day and night** follow the GTA clock. Monsters come out in the dark.
- **Minecraft physics** for walking, sprinting, sneaking, jumping and swimming. Water and lava flow; sand and gravel fall; fire spreads.
- **First- and third-person camera,** with the Minecraft player model (Steve) and armour shown on him.
- **Minecraft-style menus:** title screen, world list and pause menu.

### Mobs and people
- **Animals:** cows, pigs, sheep and chickens. Breed them, shear sheep, milk cows, saddle and ride pigs.
- **Creeper:** it hisses, swells and explodes.
- **Warden:** blind, hears your footsteps, smells you. It has a sonic boom that goes through walls.
- **GTA pedestrians look like villagers** and trade with emeralds.
- **Police and gangs fight with bows and arrows.**

### Combat and items
- **Weapons:** swords with attack charge, critical hits and knockback; bow, crossbow, and a trident that calls lightning in the rain.
- **Throwables and charges:** fire charges, wind charges, snowballs, eggs and ender pearls.
- **Elytra and fireworks,** fishing rod, spyglass, totem of undying, buckets, bone meal, spawn eggs.
- **Mixed with GTA:**
  - A firework strapped to your car works as a rocket boost.
  - Milk clears your wanted level.
  - Body armour shows as golden absorption hearts.
  - The wanted stars sit under the radar.

### Saving
- The Minecraft world is saved **together with your GTA save game**, one world per save slot.

## Requirements

| What | Version |
|------|---------|
| GTA San Andreas (PC) | **1.0 US** executable (`gta_sa.exe`) |
| ASI loader | e.g. Silent's ASI Loader or Ultimate ASI Loader |
| Windows | 10 or 11 |
| Minecraft resources | the unpacked Minecraft 26.3 assets and data (textures, sounds, models, lang, recipes) |

To build from source you also need:
- **Visual Studio 2022** with the C++ desktop workload.
- **CMake 3.21+**.
- **Python 3** with `pillow` and `numpy`.
- **Git**.

## Building and installing

Minecraft's textures and sounds belong to Mojang and cannot be shipped here, so you build the mod yourself. The build turns your Minecraft resources into the mod's texture atlases and sounds.

```bash
# 1. get the code, with the plugin-sdk submodule
git clone --recursive https://github.com/muki01/GTA-SA-Minecraft.git
cd GTA-SA-Minecraft

# 2. put the unpacked Minecraft 26.3 resources next to the code, as
#    minecraft-assets-26.3/assets/minecraft/...  and  minecraft-assets-26.3/data/minecraft/...

# 3. generate the textures, sounds and game tables
pip install pillow numpy
python tools/gen_assets.py

# 4. build the ASI plugin (32-bit, like the game)
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release --target MinecraftSA
```

**Install:**
1. Copy `build/Release/MinecraftSA.asi` into your GTA San Andreas folder.
2. Copy `assets/*.png` into `<GTA folder>/MinecraftSA/`.
3. Copy `assets/sounds/*.ogg` into `<GTA folder>/MinecraftSA/sounds/`.

`build_and_install.bat` does all of this at once. Set the `GTA` path at its top first.

**Offline tests:**

```bash
cmake --build build --config Release --target mc_tests
build/Release/mc_tests.exe
```

## Controls

| Key | On foot |
|-----|---------|
| `W` `A` `S` `D` | walk |
| `Space` | jump (double tap in creative: fly) |
| `Left Ctrl` | sprint |
| `Left Shift` | sneak |
| `Left mouse` | attack / mine (hold) |
| `Right mouse` | place block, use item, open chest or crafting table, trade |
| `Middle mouse` | pick block (creative) |
| `1`–`9`, mouse wheel | hotbar |
| `E` | inventory (creative: all items in tabs) |
| `Q` | drop item (`Ctrl+Q`: whole stack) |
| `F` | swap hands |
| `F5` | camera: first person / third person back / third person front |
| `F6` | turn the Minecraft mod on / off |
| `F7` | survival / creative |
| `F8` | CJ / Steve |
| `F9` | GTA radar on / off |

In a car you drive as in GTA. The hotbar, the inventory and right-click items such as food, bow and fireworks still work.

## Settings

`MinecraftSA/MinecraftSA.ini` in the game folder:

| Setting | Meaning |
|---------|---------|
| `FOV=70` | field of view (0 = GTA's own) |
| `MinecraftControls=1` | Minecraft keys for jump, sprint and sneak |
| `MinecraftPhysics=1` | Minecraft movement physics (0 = GTA's) |
| `ViewBobbing=1` | view bobbing while walking |
| `PedSkins=1` | pedestrians look like villagers |
| `NpcArrows=1` | police and gangs use bows |
| `Animals=1`, `MaxAnimals=14` | animal spawning |
| `Monsters=1`, `MaxMonsters` | monster spawning at night |
| `GroundHoles=1` | dug holes are visible in the GTA ground |
| `BreakBuildings=1` | buildings and cliffs can be broken into |
| `MinecraftMenu=1` | Minecraft-style menus |
| `ShowRadar=1` | GTA radar |

## Project structure

```
src/core/   Minecraft itself: blocks, items, crafting, physics, mobs, combat, HUD layout, block meshes.
            It knows nothing about GTA and is built and tested on its own.
src/gta/    The GTA San Andreas side: engine hooks, rendering, collision, the GTA map turned into blocks.
tools/      gen_assets.py: turns Minecraft's resources into atlases, sounds and generated C++ tables.
tests/      offline tests of the core (mc_tests).
third_party/plugin-sdk   DK22Pac's plugin-sdk (git submodule).
```

The core decides **what** happens and the GTA layer knows **how** to show it in San Andreas. This keeps the Minecraft rules testable without the game.

## FAQ

**Does it work with the Definitive Edition, the Steam version or SA-MP?**
No. It needs the PC version 1.0 US executable, which is what most GTA SA mods require.

**Is Minecraft included?**
No. You need the Minecraft resources yourself (see [Building and installing](#building-and-installing)). No Mojang or Rockstar files are in this repository.

**Which language is the game text in?**
In-game names and messages are currently Turkish: Minecraft's own Turkish names for blocks and items.

**Is there a download (release)?**
Not yet. The mod is in active development; build it from source for now.

## Credits

- [plugin-sdk](https://github.com/DK22Pac/plugin-sdk) by DK22Pac and contributors: the GTA SA modding SDK (includes [safetyhook](https://github.com/cursey/safetyhook)).
- [gta-reversed](https://github.com/gta-reversed/gta-reversed): used as a reference for the GTA SA engine.
- Minecraft is by Mojang Studios; Grand Theft Auto: San Andreas is by Rockstar Games.

## Disclaimer

This is a fan-made, non-commercial mod. It is not affiliated with, endorsed by or connected to Mojang Studios, Microsoft, Rockstar Games or Take-Two Interactive. "Minecraft" and "Grand Theft Auto" are trademarks of their respective owners. You need legal copies of both games.

## Türkçe

**GTA SA Minecraft**, GTA San Andreas ile Minecraft'ı tek bir dünyada birleştiren bir moddur (ASI eklentisi).

- GTA haritasını kırıp Minecraft blokları topla: Los Santos'ta ev kur, üretim yap, hayatta kal.
- Creeper ve Warden ile savaş, TNT ile haritada çukur aç.
- Arabana binip hotbar'ındaki bloklarla şehirde dolaş.

Oyun içi yazılar Türkçedir. Kurulum için yukarıdaki *Building and installing* bölümüne bak.

**Anahtar kelimeler:** GTA San Andreas Minecraft modu, GTA SA Minecraft, Minecraft GTA mod, GTA SA mod, Minecraft mod.

---

<div align="center">

If you like the project, please ⭐ **star the repository**. It helps more players find it.

</div>
