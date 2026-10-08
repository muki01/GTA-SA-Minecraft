#pragma once
// The state of the game being played, as Minecraft keeps it: game mode, the screen that is open, where the player
// looks, what the hands are doing. The host fills in what only it can know (where the eyes are, where they look)
// and reads the rest to show it.

#include "Core.h"
#include "World.h"

namespace mc {

enum Screen { SCREEN_NONE = 0, SCREEN_INVENTORY, SCREEN_CRAFTING, SCREEN_FURNACE, SCREEN_CHEST, SCREEN_CREATIVE };
enum GameMode { MODE_SURVIVAL = 0, MODE_CREATIVE = 1 };
enum CameraMode { CAM_THIRD_BACK = 0, CAM_THIRD_FRONT, CAM_FIRST };

struct GameState {
    int gameMode = MODE_SURVIVAL;
    int screen = SCREEN_NONE;
    Int3 openPos;             // block of the open container
    float cursorX = 0, cursorY = 0;
    int creativeTab = 0;      // CAT_* or CAT_COUNT for the survival inventory tab
    int creativeScroll = 0;

    // survival (hunger, air, effects and experience: gSurvival in Survival.h)
    int deathCause = STR_DEATH_GENERIC; // what hurt the player last (a STR_DEATH_* text for the death screen)
    float deathCauseTime = -100.0f;     // gGame.age when it did

    // view
    int cameraMode = CAM_THIRD_BACK;
    Vec3 eyePos;              // player's eyes
    Vec3 lookDir;             // where the player looks (unit)
    Vec3 rayOrigin;           // origin of the block-targeting ray

    // movement
    bool flying = false;      // creative flight
    bool gliding = false;     // elytra
    bool jumping = false;     // jump / fall in progress
    bool sprinting = false;
    bool sprintLatch = false; // the sprint key was tapped: keep sprinting until the player stops
    uint32_t ridingMob = 0;   // id of the animal we sit on (0 = none)
    Vec3 flyVel;              // m/s while flying / gliding / jumping
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
    float age = 0.0f;         // seconds played in this session

    // HUD helpers
    float viewW = 0.0f, viewH = 0.0f; // size of the picture in pixels (the host says)
    float selectedNameTimer = 0.0f;
    std::string message;      // the line above the hotbar
    float messageTimer = 0.0f;
};

extern GameState gGame;

inline void NoteDamage(int cause) {
    gGame.deathCause = cause;
    gGame.deathCauseTime = gGame.age;
}

void GameTimersTick(float dt);    // every frame: the message fades, the attack recharges
void ShowMessage(const std::string& text, float seconds = 2.5f);
void StartSwing();               // swings the arm that is in use
float AttackCharge();            // 0..1 attack strength (Minecraft's cooldown)
void DamageHeldItem(int amount = 1); // wears the tool in the hand; it breaks when worn out

// settings of the world, like Minecraft's game rules (the host sets them from its own settings)
struct GameRules {
    bool explosionsBreakBlocks = true;
    bool animals = true;   // herds appear around the player
    int maxAnimals = 14;   // wild ones at a time
    bool keepInventory = true; // the player keeps his things when he dies
};
extern GameRules gRules;

// ---- screens: inventory, crafting table, furnace, chest
void OpenScreen(int screen, const Int3& pos = {});
// what is left on the crafting grid or on the cursor goes back into the inventory, or on the ground
void CloseScreen();
// the host saw the player die: what Minecraft does then (his things fall out unless the rules keep them)
void PlayerDied(const Vec3& at);
// a fresh world: something to start with
void GiveStarterKit();
// puts a stack into the world in front of the player (thrown: flung forward)
void DropStackAtPlayer(const ItemStack& s, bool thrown);

} // namespace mc
