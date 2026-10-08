#pragma once

#include "ModCommon.h"
#include "World.h"

class CPlayerPed;
class CPed;

namespace mc {

enum Screen { SCREEN_NONE = 0, SCREEN_INVENTORY, SCREEN_CRAFTING, SCREEN_FURNACE, SCREEN_CHEST, SCREEN_CREATIVE };
enum GameMode { MODE_SURVIVAL = 0, MODE_CREATIVE = 1 };
enum CameraMode { CAM_THIRD_BACK = 0, CAM_THIRD_FRONT, CAM_FIRST };

struct GameState {
    bool enabled = true;      // Minecraft mode on/off (F6)
    int gameMode = MODE_SURVIVAL;
    int screen = SCREEN_NONE;
    Int3 openPos;             // block of the open container
    float cursorX = 0, cursorY = 0;
    int creativeTab = 0;      // CAT_* or CAT_COUNT for the survival inventory tab
    int creativeScroll = 0;

    // survival
    float food = 20.0f;
    float saturation = 5.0f;
    float exhaustion = 0.0f;
    float absorption = 0.0f;  // yellow hearts, in Minecraft health points (half hearts)
    float air = 300.0f;       // ticks of air left under water (300 = full, ten bubbles)
    float directDamage = 0.0f;
    int deathCause = STR_DEATH_GENERIC; // what hurt the player last (a STR_DEATH_* text for the death screen)
    float deathCauseTime = -100.0f;     // gGame.age when it did // health taken by poison / drowning this frame: armour does not block it
    int xpLevel = 0;
    float xpProgress = 0.0f;  // 0..1 towards the next level
    int xpTotal = 0;
    float foodTimer = 0.0f;
    float eatTimer = 0.0f;

    // view / body
    int cameraMode = CAM_THIRD_BACK;
    bool steve = false;       // CJ replaced by Steve
    CVector eyePos;           // player's eyes
    CVector lookDir;          // where the player looks (unit)
    CVector rayOrigin;        // origin of the block-targeting ray

    // movement
    bool flying = false;      // creative flight
    bool gliding = false;     // elytra
    bool jumping = false;     // Minecraft jump / fall in progress (we move the player ourselves)
    bool sprinting = false;
    bool sprintLatch = false; // Ctrl was tapped: keep sprinting until the player stops
    bool swimming = false;    // GTA's swim task is running
    bool underwater = false;
    uint32_t ridingMob = 0;   // id of the animal we sit on (0 = none)
    bool inVehicle = false;
    CVector flyVel;           // m/s while flying / gliding / jumping
    float boostTime = 0.0f;   // firework boost left (s)

    // animation
    float swing = -1.0f;      // arm swing progress 0..1, <0 idle
    float bowDraw = -1.0f;    // seconds the bow has been drawn, <0 not drawing
    float crossbowCharge = -1.0f; // seconds the crossbow has been loading, <0 not loading
    int crossbowSlot = -1;    // hotbar slot of the loaded crossbow (-1 = none loaded)
    bool crossbowRocket = false; // the loaded crossbow holds a firework
    float tridentCharge = -1.0f;
    bool spyglass = false;    // looking through the spyglass
    float hurtTimer = 0.0f;   // red flash after taking damage
    float deathTime = -1.0f;  // seconds since the player died (< 0 alive)
    float fireproofTimer = 0.0f;
    float handHeight = 1.0f;  // 0..1, the hand drops while switching items / recharging an attack
    uint16_t handItem = 0;    // item currently shown in the hand
    float offSwing = -1.0f;   // off-hand swing progress
    float offHeight = 1.0f;   // off hand drops while its item changes
    uint16_t offItem = 0;     // item shown in the off hand
    bool offhandActive = false; // the off-hand item is being used right now (swapped into the hotbar slot)
    bool usingOffhand = false;  // eating / drawing with the off hand
    float attackTimer = 10.0f; // seconds since last attack
    float walkPhase = 0.0f, walkAmount = 0.0f; // limb swing (third person)
    float walkDist = 0.0f, bob = 0.0f;         // view bobbing (first person)
    float fovMod = 1.0f;      // sprint / flight widen the view a little
    float age = 0.0f;
    bool hidPlayer = false;   // we made CJ invisible

    // HUD helpers
    float selectedNameTimer = 0.0f;
    std::string message;
    float messageTimer = 0.0f;
    bool hudVisible = false;
    bool inWorld = false;     // player exists and not in a cutscene/menu
};

extern GameState gGame;

inline void NoteDamage(int cause) {
    gGame.deathCause = cause;
    gGame.deathCauseTime = gGame.age;
}

void ShowMessage(const std::string& text, float seconds = 2.5f);

// gameplay hooks
void GameInit();
void GameProcess();       // script phase, before the world moves
void GameAfterProcess();  // after CGame::Process: camera override, visibility
void GameOnNewSession();
void GameShutdown();
void SaveAll();

// worlds: every GTA save game has its own Minecraft world (blocks, inventory, experience)
void SetNextWorld(int id, bool fresh);  // the session that starts next plays world `id` (0 = a new world, 1..8 = save slot)
void WorldSavedToSlot(int slot);        // GTA saved the game into that slot (1..8): the world goes with it
void WorldSlotDeleted(int slot);
bool WorldFileExists(int id);


// shared helpers
void OpenScreen(int screen, const Int3& pos = {});
void CloseScreen();
void DropStackAtPlayer(const ItemStack& s, bool thrown);
void SpawnDrop(const CVector& pos, const ItemStack& s, const CVector& vel = CVector(0, 0, 0), float delay = 0.5f);
void SpawnDropItem(const CVector& pos, uint16_t id, int count);
void StartSwing();
void DamageHeldItem(int amount = 1);
float Rand01();
float AttackCharge();     // 0..1 attack strength (Minecraft's cooldown)
// blocks, TNT chain, knockback. own = we start GTA's explosion of type `gtaType` too.
void ExplodeAt(const CVector& at, float radius, bool own, int gtaType = 0);
void SpawnBlockDrops(int block, const CVector& at);
// highest solid surface (GTA map or blocks) below `from`; false if there is none within `maxDrop`
bool GroundBelow(const CVector& from, float maxDrop, float* zOut);
// pushes dropped items and primed TNT away from a blast
void PushLooseThings(const CVector& at, float radius, float speed);

} // namespace mc
