#include "Config.h"

#include "Lang.h"
#include "ModCommon.h"

namespace mc {

Config gConfig;

static const char* kDefaultIni =
    "; GTA SA Minecraft settings\r\n"
    "; key codes: https://learn.microsoft.com/windows/win32/inputdev/virtual-key-codes\r\n"
    "[Keys]\r\n"
    "; F6: Minecraft mod on / off\r\n"
    "ToggleMode=0x75\r\n"
    "; E: inventory\r\n"
    "Inventory=0x45\r\n"
    "; Q: drop the held item\r\n"
    "Drop=0x51\r\n"
    "; F7: survival / creative\r\n"
    "GameMode=0x76\r\n"
    "; F5: camera (first person, third person back / front)\r\n"
    "Camera=0x74\r\n"
    "; F8: CJ <-> Steve\r\n"
    "Steve=0x77\r\n"
    "; F: swap main hand and off hand (enters a car when one is near)\r\n"
    "SwapHands=0x46\r\n"
    "; F9: GTA radar on / off\r\n"
    "Radar=0x78\r\n"
    "\r\n"
    "[Settings]\r\n"
    "; language of everything Minecraft says: en = English, tr = Turkish (also in the menu: Language...)\r\n"
    "Language=en\r\n"
    "; 0 = automatic, 1-6 = GUI size\r\n"
    "GuiScale=0\r\n"
    "; how far blocks are drawn (metres)\r\n"
    "RenderDistance=160\r\n"
    "; block collision radius (metres)\r\n"
    "CollisionRadius=48\r\n"
    "MouseSensitivity=1.0\r\n"
    "; 1 = the inventory is kept when you die\r\n"
    "KeepInventory=1\r\n"
    "; 1 = the Minecraft mod is on when the game starts\r\n"
    "StartEnabled=1\r\n"
    "; GTA radar at the bottom left (F9 in the game)\r\n"
    "ShowRadar=1\r\n"
    "; 0 = survival, 1 = creative\r\n"
    "StartGameMode=0\r\n"
    "; blocks can be collected from the GTA map (ground, rock, trees)\r\n"
    "MineGtaWorld=1\r\n"
    "; GTA explosions break blocks\r\n"
    "ExplosionsBreakBlocks=1\r\n"
    "; 1 = start as Steve\r\n"
    "StartAsSteve=0\r\n"
    "; field of view (Minecraft's FOV slider, 30-110). 0 = GTA's own\r\n"
    "FOV=70\r\n"
    "; 1 = Minecraft keys (Space jump, Ctrl sprint, Shift sneak). 0 = GTA keys\r\n"
    "MinecraftControls=1\r\n"
    "; view bobbing while walking (first person)\r\n"
    "ViewBobbing=1\r\n"
    "; 1 = pedestrians look like villagers, police like pillagers\r\n"
    "PedSkins=1\r\n"
    "; 1 = cows, pigs, sheep and chickens spawn\r\n"
    "Animals=1\r\n"
    "; most animals around at the same time\r\n"
    "MaxAnimals=14\r\n"
    "; 1 = creepers come out at night\r\n"
    "Monsters=1\r\n"
    "; most creepers around at the same time\r\n"
    "MaxMonsters=4\r\n"
    "; 1 = police and gangs shoot arrows instead of guns (the helicopter too)\r\n"
    "NpcArrows=1\r\n"
    "; 1 = Minecraft physics (walking, jumping, falling, swimming). 0 = GTA physics\r\n"
    "MinecraftPhysics=1\r\n"
    "; 1 = dug holes show in the GTA ground. Set 0 if the picture breaks\r\n"
    "GroundHoles=1\r\n"
    "; 1 = GTA buildings, roofs and rocks can be broken into, block by block\r\n"
    "BreakBuildings=1\r\n"
    "; 1 = title screen, world list and pause menu look like Minecraft's. 0 = GTA's own menu\r\n"
    "MinecraftMenu=1\r\n";

// settings added by newer versions are appended to an existing ini so they can be found and changed
static void EnsureKey(const char* key, const char* value, const std::string& path, const char* section = "Settings") {
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), path.c_str());
    if (!buf[0])
        WritePrivateProfileStringA(section, key, value, path.c_str());
}

static int ReadInt(const char* section, const char* key, int def, const std::string& path) {
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), path.c_str());
    if (!buf[0])
        return def;
    return (int)strtol(buf, nullptr, 0);
}

static float ReadFloat(const char* section, const char* key, float def, const std::string& path) {
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), path.c_str());
    if (!buf[0])
        return def;
    return (float)atof(buf);
}

void LoadConfig() {
    std::string path = ModPath("MinecraftSA.ini");
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        FILE* f = fopen(path.c_str(), "wb");
        if (f) {
            fwrite(kDefaultIni, 1, strlen(kDefaultIni), f);
            fclose(f);
        }
    }
    Config& c = gConfig;
    c.keyToggleMode = ReadInt("Keys", "ToggleMode", c.keyToggleMode, path);
    c.keyInventory = ReadInt("Keys", "Inventory", c.keyInventory, path);
    c.keyDrop = ReadInt("Keys", "Drop", c.keyDrop, path);
    c.keyGameMode = ReadInt("Keys", "GameMode", c.keyGameMode, path);
    c.keyCamera = ReadInt("Keys", "Camera", c.keyCamera, path);
    c.keySteve = ReadInt("Keys", "Steve", c.keySteve, path);
    c.guiScale = ReadInt("Settings", "GuiScale", c.guiScale, path);
    c.renderDistance = Clamp(ReadFloat("Settings", "RenderDistance", c.renderDistance, path), 32.0f, 400.0f);
    c.collisionRadius = Clamp(ReadFloat("Settings", "CollisionRadius", c.collisionRadius, path), 16.0f, 96.0f);
    c.mouseSensitivity = Clamp(ReadFloat("Settings", "MouseSensitivity", c.mouseSensitivity, path), 0.1f, 10.0f);
    c.keepInventory = ReadInt("Settings", "KeepInventory", c.keepInventory, path) != 0;
    c.startEnabled = ReadInt("Settings", "StartEnabled", c.startEnabled, path) != 0;
    {
        // version 0.9: the radar is on (F9 switches it); older ini files had it off without a key for it
        char buf[16];
        GetPrivateProfileStringA("Keys", "Radar", "", buf, sizeof(buf), path.c_str());
        if (!buf[0]) {
            WritePrivateProfileStringA("Keys", "Radar", "0x78", path.c_str());
            WritePrivateProfileStringA("Settings", "ShowRadar", "1", path.c_str());
        }
    }
    c.keyRadar = ReadInt("Keys", "Radar", c.keyRadar, path);
    c.showRadar = ReadInt("Settings", "ShowRadar", c.showRadar, path) != 0;
    c.startGameMode = ReadInt("Settings", "StartGameMode", c.startGameMode, path);
    c.mineGtaWorld = ReadInt("Settings", "MineGtaWorld", c.mineGtaWorld, path) != 0;
    c.explosionsBreakBlocks = ReadInt("Settings", "ExplosionsBreakBlocks", c.explosionsBreakBlocks, path) != 0;
    c.startAsSteve = ReadInt("Settings", "StartAsSteve", c.startAsSteve, path) != 0;
    EnsureKey("Language", "en", path);
    {
        char lang[16];
        GetPrivateProfileStringA("Settings", "Language", "en", lang, sizeof(lang), path.c_str());
        gLanguage = LanguageFromCode(lang);
    }
    EnsureKey("FOV", "70", path);
    EnsureKey("MinecraftControls", "1", path);
    EnsureKey("ViewBobbing", "1", path);
    EnsureKey("PedSkins", "1", path);
    EnsureKey("Animals", "1", path);
    EnsureKey("MaxAnimals", "14", path);
    EnsureKey("Monsters", "1", path);
    EnsureKey("MaxMonsters", "4", path);
    EnsureKey("NpcArrows", "1", path);
    EnsureKey("MinecraftPhysics", "1", path);
    EnsureKey("GroundHoles", "1", path);
    EnsureKey("BreakBuildings", "1", path);
    EnsureKey("MinecraftMenu", "1", path);
    c.minecraftMenu = ReadInt("Settings", "MinecraftMenu", c.minecraftMenu, path) != 0;
    c.breakBuildings = ReadInt("Settings", "BreakBuildings", c.breakBuildings, path) != 0;
    c.groundHoles = ReadInt("Settings", "GroundHoles", c.groundHoles, path) != 0;
    EnsureKey("SwapHands", "0x46", path, "Keys");
    c.keySwap = ReadInt("Keys", "SwapHands", c.keySwap, path);
    c.minecraftPhysics = ReadInt("Settings", "MinecraftPhysics", c.minecraftPhysics, path) != 0;
    c.fov = ReadFloat("Settings", "FOV", c.fov, path);
    if (c.fov != 0.0f)
        c.fov = Clamp(c.fov, 30.0f, 110.0f);
    c.minecraftControls = ReadInt("Settings", "MinecraftControls", c.minecraftControls, path) != 0;
    c.viewBobbing = ReadInt("Settings", "ViewBobbing", c.viewBobbing, path) != 0;
    c.pedSkins = ReadInt("Settings", "PedSkins", c.pedSkins, path) != 0;
    c.animals = ReadInt("Settings", "Animals", c.animals, path) != 0;
    c.npcArrows = ReadInt("Settings", "NpcArrows", c.npcArrows, path) != 0;
    c.maxAnimals = std::clamp(ReadInt("Settings", "MaxAnimals", c.maxAnimals, path), 0, 40);
    c.monsters = ReadInt("Settings", "Monsters", c.monsters, path) != 0;
    c.maxMonsters = std::clamp(ReadInt("Settings", "MaxMonsters", c.maxMonsters, path), 0, 20);
    Log("Config loaded: guiScale=%d renderDist=%.0f colRadius=%.0f", c.guiScale, c.renderDistance, c.collisionRadius);
}

void SaveConfigValue(const char* section, const char* key, const char* value) {
    WritePrivateProfileStringA(section, key, value, ModPath("MinecraftSA.ini").c_str());
}

} // namespace mc
