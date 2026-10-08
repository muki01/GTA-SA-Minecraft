#include "Controls.h"

namespace mc {

Controls gControls;

// Minecraft's own default keys. (Walking is the host's stick or keys; the game mode switch is F3 + F4 in
// Minecraft and has a key of its own here.)
const ActionDef kActions[ACT_COUNT] = {
    { "Attack/Destroy", KEY_MOUSE_LEFT },        // ACT_ATTACK
    { "Use Item/Place Block", KEY_MOUSE_RIGHT }, // ACT_USE
    { "Pick Block", KEY_MOUSE_MIDDLE },          // ACT_PICK_BLOCK
    { "Jump", KEY_SPACE },                       // ACT_JUMP
    { "Sneak", KEY_LSHIFT },                     // ACT_SNEAK
    { "Sprint", KEY_LCTRL },                     // ACT_SPRINT
    { "Open/Close Inventory", KEY_E },           // ACT_INVENTORY
    { "Drop Selected Item", KEY_Q },             // ACT_DROP
    { "Drop Whole Stack", KEY_CTRL },            // ACT_DROP_STACK
    { "Swap Item With Offhand", KEY_F },         // ACT_SWAP_HANDS
    { "Hotbar Slot 1", KEY_1 },                  // ACT_HOTBAR_1 ...
    { "Hotbar Slot 2", KEY_2 },
    { "Hotbar Slot 3", KEY_3 },
    { "Hotbar Slot 4", KEY_4 },
    { "Hotbar Slot 5", KEY_5 },
    { "Hotbar Slot 6", KEY_6 },
    { "Hotbar Slot 7", KEY_7 },
    { "Hotbar Slot 8", KEY_8 },
    { "Hotbar Slot 9", KEY_9 },
    { "Toggle Perspective", KEY_F5 },            // ACT_PERSPECTIVE
    { "Switch Game Mode", KEY_F7 },              // ACT_GAME_MODE
    { "Close Screen", KEY_ESCAPE },              // ACT_BACK
};

} // namespace mc
