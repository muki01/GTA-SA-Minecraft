# Changelog

All notable changes to GTA SA Minecraft. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). The project is in active development (0.x); Turkish release notes for every version are in [OKUBENI.txt](OKUBENI.txt).

## [0.51] - 2026-10-09
### Added
- English and Turkish for everything the Minecraft side says: block and item names, menus, creative tabs, messages, effect names and the death screen. The texts come from Minecraft's own language files.
- A **Language...** page on the title screen and in the pause menu. The choice is kept in `MinecraftSA.ini` (`Language=en` / `tr`).
### Changed
- English is the default language.
- `MinecraftSA.ini` comments are in English.

## [0.50] - 2026-10-08
### Added
- Creative inventory as in Minecraft: **Redstone Blocks** and **Spawn Eggs** tabs, with the tabs where Minecraft has them.
### Changed
- Creative items are grouped and sorted in Minecraft's order: wood by wood type, stone families with their stairs, slabs and walls, colours in Minecraft's order, and tools and armour by material.
### Fixed
- Items in the wrong tab: buckets and maps, spears and horse armour, seeds, glass, and wool and concrete stairs.

## [0.49] - 2026-10-08
### Added
- **The Warden**, and its spawn egg.
  - It is blind: it hears your footsteps (not when you sneak) and smells you within 6 m.
  - When angry it roars, then hunts you: 15-heart melee blows, and from 3–15 m a sonic boom through walls that deals 5 hearts and knocks you back.
  - It has 250 hearts, drops a sculk catalyst, and digs back into the ground after a calm minute.

## [0.48] - 2026-10-08
### Added
- **Beds** in 16 colours, two blocks long. Using one sets your respawn point. At night or in a thunderstorm you sleep, and five seconds later it is 06:00 and the rain stops.
- You can't sleep with monsters nearby. When you die you respawn at your bed.

## [0.47] - 2026-10-08
### Added
- Glass panes (all colours), iron bars, fences and walls that connect to their neighbours. Fences and walls collide 1.5 blocks high.
- Stairs, slabs, fences and walls show their real shape in the hand and the inventory.

## [0.46]
### Added
- **Stairs** (93 kinds) and **slabs** (96 kinds), placed as in Minecraft (upside down, double slabs). Crafting recipes went from 441 to 690.

## [0.45]
### Added
- Creeper spawn egg.
### Changed
- Block and item IDs are frozen (`tools/ids_frozen.json`), so new blocks never break existing worlds.

## [0.44]
### Added
- A first-person fire overlay while you burn, with Minecraft's animated flames.

## [0.43]
### Fixed
- Cars and pedestrians that fall into holes now land on the blocks inside instead of falling out of the map.

## [0.42]
### Added
- GTA vehicles and pedestrians fall into dug holes and broken buildings, and collide with the blocks inside.

## [0.41]
### Added
- **The Creeper:** it spawns at night, hisses, swells and explodes (radius 3, breaks blocks). Set with `Monsters` / `MaxMonsters` in the ini.

## [0.40]
### Fixed
- Traffic stops for the player again.

## [0.39]
### Changed
- Burning works as in Minecraft:
  - Fire and lava hurt every half second.
  - You keep burning for 8 seconds after leaving fire, 15 after lava.
  - Water, the sea and rain put you out.
  - GTA's own fires follow the same rules.

## [0.38]
### Changed
- GTA body armour works as golden absorption hearts instead of armour points.

## [0.37]
### Added
- Police wanted stars under the radar.

## [0.36]
### Fixed
- Shift-click moves items to the same slots as in Minecraft.

## [0.14] – [0.35]
### Changed
- The code was split into a game-independent Minecraft core (`src/core`) and the GTA San Andreas layer (`src/gta`), one step at a time. This covered movement, survival, block rules, sounds, keys, game state, saving, animals, combat, fishing, villagers, the hands' click flow, inventory screens, the HUD, block meshing, models, animation and rendering rules. The core has offline tests (`mc_tests`).
### Added
- 0.21: your hands are free while driving: mine, attack, place blocks and use items from a car.
### Fixed
- 0.23: jump height no longer depends on the frame rate. Fire resistance really ends, and sprinting no longer drains hunger twice as fast.
- 0.24: villagers you run over drop their loot.
- 0.25: angry villagers' punches hit you.

## [0.13] - 2026-10-06
### Fixed
- Thin strips of leftover GTA geometry around broken cells.
- Block textures inside holes: one full block texture per cell, upright on walls.
### Changed
- Building interiors turn into plain blocks: wood stays wood, glass stays glass, bricks stay bricks.

## [0.12]
### Added
- Breaking into buildings closes the hole's walls with block textures, seen only from inside the hole.
### Fixed
- Aiming from a car in first person.
- People and blocks are now visible through car windows.

## [0.11]
### Changed
- Breaking measures the visible GTA model, so walls, bridges and roads are as thick as they look. The rims of holes are filled with quarter-block steps.
- Thrown items go where the crosshair points, with your own speed.

## [0.10]
### Changed
- Breaking into buildings measures the real thickness of the GTA model.
- Ground holes are cut out of the GTA model.
- TNT blows craters into buildings from the side.
- Fast swimming moved to `Ctrl`.

## [0.9]
### Changed
- Broken cells are cut out of the GTA model completely; tunnels go right through buildings.
- The Minecraft world is saved only together with a GTA save game, like GTA itself.
- Minecraft-style menus on GTA's menu background.
### Added
- The GTA radar, toggled with `F9`.

## [0.8]
### Added
- Buildings stay GTA buildings: only the cells you break open up, with blocks behind them.
- The ground type is read correctly.
- Minecraft's death screen, title screen and world list.
- Every GTA save slot is its own Minecraft world.

## [0.7]
### Added
- Breaking a surface gives a material that matches its texture.
- Cars break apart instead of exploding.
- Golden apples and effects, and air bubbles under water.

## [0.6]
### Added
- Minecraft water and lava with flow and animation, and fire blocks that spread.
- Lava sets blocks, bushes, people and cars on fire.
- The 3D trident model, and off-hand animations.

## [0.5]
### Added
- **Digging into the GTA ground:** dirt, stone and ores down to bedrock, plus TNT craters.
- Water and lava buckets, the off hand, saplings that grow into trees, and bone meal.
- Minecraft movement physics, falling sand and gravel, and XP.

## [0.4]
### Added
- Using items while driving; a firework turns your car into a rocket.
- Swimming, and more Steve animations.

## [0.1] – [0.3]
### Added
- The first versions:
  - Minecraft blocks and items in GTA San Andreas, with crafting and furnaces.
  - Survival and creative modes and the Minecraft HUD.
  - Mining the GTA map, Steve, animals, villagers and Minecraft weapons.

[0.51]: https://github.com/muki01/GTA-SA-Minecraft/releases/tag/v0.51
