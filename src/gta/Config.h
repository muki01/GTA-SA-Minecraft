#pragma once

namespace mc {

struct Config {
    // keys (Windows virtual key codes)
    int keyToggleMode = 0x75;  // F6: Minecraft mode on / off
    int keySteve = 0x77;       // F8: Steve / CJ
    int keyRadar = 0x78;       // F9: GTA's radar on / off
    // Minecraft's own actions: 0 = Minecraft's default key (src/core/Controls.cpp), anything else replaces it
    int keyInventory = 0;      // E
    int keyDrop = 0;           // Q
    int keyGameMode = 0;       // F7
    int keyCamera = 0;         // F5
    int keySwap = 0;           // F: swap main hand and off hand (enters cars when one is near)

    // settings
    int guiScale = 0;          // 0 = auto (like Minecraft)
    float renderDistance = 160.0f;
    float collisionRadius = 48.0f;
    float mouseSensitivity = 1.0f;
    bool keepInventory = true;
    bool startEnabled = true;
    bool showRadar = true;
    int startGameMode = 0;     // 0 survival, 1 creative
    bool mineGtaWorld = true;  // allow collecting blocks from the GTA map
    bool explosionsBreakBlocks = true;
    bool startAsSteve = false;
    float fov = 70.0f;         // vertical field of view like Minecraft's slider (0 = leave GTA's)
    bool minecraftControls = true; // Space jump, Ctrl sprint, Shift sneak
    bool viewBobbing = true;
    bool pedSkins = true;      // pedestrians -> villagers, police -> pillagers
    bool animals = true;
    int maxAnimals = 14;
    bool monsters = true;
    int maxMonsters = 4;
    bool npcArrows = true;     // police / gang guns and the helicopter shoot arrows
    bool minecraftPhysics = true; // our own walking / jumping / falling instead of GTA's
    bool minecraftMenu = true;  // title screen, world list and pause menu drawn the Minecraft way
    bool breakBuildings = true; // GTA buildings can be broken into, block by block
    bool groundHoles = true;   // dug holes are drawn into the GTA ground (needs a stencil buffer)
};

extern Config gConfig;

void LoadConfig();
// remembers a setting that was changed in the game
void SaveConfigValue(const char* section, const char* key, const char* value);

} // namespace mc
