# Contributing to GTA SA Minecraft

Thanks for your interest in the project. Bug reports, ideas, screenshots and pull requests are all welcome.

## Reporting bugs

Open a [bug report](https://github.com/muki01/GTA-SA-Minecraft/issues/new?template=bug_report.yml) and include:
- the mod version (shown at the bottom left of the title screen);
- what you did, what happened, and what you expected;
- `MinecraftSA/MinecraftSA.log` from your game folder, taken right after the problem and before you start the game again;
- a screenshot, if the problem is visual.

For questions and ideas, use [Discussions](https://github.com/muki01/GTA-SA-Minecraft/discussions) or a [feature request](https://github.com/muki01/GTA-SA-Minecraft/issues/new?template=feature_request.yml).

## How the code is organised

```
src/core/   Minecraft itself: blocks, items, crafting, physics, mobs, combat, HUD layout, block meshes...
src/gta/    the GTA San Andreas side: engine hooks, rendering, collision, the GTA map turned into blocks
tools/      gen_assets.py: Minecraft resources -> texture atlases, sounds and generated C++ tables
tests/      offline tests of the core (mc_tests)
```

**`src/core` decides *what* happens; `src/gta` knows *how* to show it in San Andreas.**

- The core never includes plugin-sdk or anything GTA. It is built as its own library (`mc_core`) and tested without the game.
- The core talks to the game through one interface, `Host` (`src/core/Host.h`), implemented by `GtaHost` (`src/gta/GtaHost.cpp`).

## Building

See [Building from source](README.md#building-from-source) in the README. In short:

```bash
git clone --recursive https://github.com/muki01/GTA-SA-Minecraft.git
cd GTA-SA-Minecraft
python tools/gen_assets.py            # needs the unpacked Minecraft 26.3 resources
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release --target mc_tests MinecraftSA
build/Release/mc_tests.exe            # must print ALL CHECKS PASSED
```

The generated C++ tables in `src/core/generated/` are committed. Without the Minecraft resources you can still build and test, as long as you don't need to regenerate them.

## Rules for changes

- **Tests pass.** Run `mc_tests` before every pull request, and add checks for new core rules in `tests/test_main.cpp`. CI builds and tests every push and pull request.
- **Keep the split.** Game rules go into `src/core`; only engine-specific work goes into `src/gta`. Remove code that a change makes unnecessary.
- **Every text the player sees is bilingual.**
  - Use `LangStr(STR_...)` for texts that exist in Minecraft's language files; add the key to `MENU_STRINGS` in `tools/gen_assets.py`.
  - Use `Tr("English", "Türkçe")` for the mod's own texts.
- **IDs are saved in worlds and must never move.** New blocks and items are appended by the generator (`tools/ids_frozen.json`). New sound events and entity textures go at the end of their lists.
- **Never commit Minecraft's files.** No `minecraft-assets-*` folder and no generated `assets/` folder (both are in `.gitignore`).
- **Style:** follow `.editorconfig`: 4 spaces, UTF-8, and the naming and comment style of the code around your change.

## Pull requests

1. Fork the repository and create a branch from `main`.
2. Make your change, with tests where the core is involved.
3. Describe what changed and how you tested it in game. Fill in the pull request template.
4. One topic per pull request keeps reviews quick.

By contributing you agree that your contributions are licensed under the [MIT License](LICENSE), and you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).
