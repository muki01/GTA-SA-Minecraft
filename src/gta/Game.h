#pragma once

#include "ModCommon.h"
#include "Entities.h"
#include "GameState.h"
#include "Survival.h"
#include "World.h"

class CPlayerPed;
class CPed;

namespace mc {

// What exists only because the game is GTA San Andreas. (The game being played - mode, screen, view, hands - is the
// core's gGame in GameState.h.)
struct GtaState {
    bool enabled = true;       // Minecraft mode on/off (F6)
    bool steve = false;        // CJ replaced by Steve
    bool swimming = false;     // GTA's swim task is running
    bool underwater = false;   // ... with the head under GTA's water
    bool hidPlayer = false;    // we made CJ invisible
    bool hudVisible = false;   // the Minecraft HUD is drawn
    bool inWorld = false;      // player exists and not in a cutscene/menu
    float directDamage = 0.0f; // health taken by poison / drowning this frame: armour does not block it
};
extern GtaState gGta;

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

} // namespace mc
