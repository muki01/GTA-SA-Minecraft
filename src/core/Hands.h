#pragma once
// What the player's hands do with Minecraft's buttons, once a frame: the hotbar, the inventory key, swapping hands,
// dropping, picking a block, the off hand, the bow and the food, using (containers, people of the host's, animals, the
// held item, placing blocks), fighting and mining. The host turns its own buttons off around it (HandsResult.blockPad).

#include "Core.h"

namespace mc {

// what the host knows this frame
struct HandsFacts {
    bool alive = true;
    bool inVehicle = false;
    bool vehicleGuns = false; // the attack button fires the guns of the vehicle he sits in
    bool sneaking = false;    // (a right click on a container then uses the held item instead of opening it)
    bool swapKeyFree = true;  // the swap-hands key is not the host's right now
};

struct HandsResult {
    bool blockPad = false;          // a screen is open (or was just opened): the host keeps its buttons off
    bool aimed = false;             // gTarget was brought up to date
    int finished = 0;               // something was just eaten or drunk up
    bool hadFireResistance = false; // (whether he had fire resistance before it)
};

HandsResult HandsTick(float dt, const HandsFacts& f);
// keys that change how the game is played and seen: game mode, camera (only while no screen is open)
void GameKeysTick();

} // namespace mc
