#pragma once

namespace mc {

struct Config {
    // keys (virtual key codes)
    int keyToggleMode = 0x75;  // F6
    int keyInventory = 'E';
    int keyDrop = 'Q';
    int keyGameMode = 0x76;    // F7
    int keyCamera = 0x74;      // F5
    int keySteve = 0x77;       // F8
    int keySwap = 'F';         // swap main hand and off hand (enters cars when one is near)
    int keyRadar = 0x78;       // F9: GTA's radar on / off

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
