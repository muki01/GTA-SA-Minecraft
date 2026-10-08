#pragma once
// What the player does with keys and buttons, the Minecraft way: the list of actions, the key each one has by
// default, and whether it is being used right now. Reading the real keyboard, mouse or pad is the host's job: it
// fills gControls once a frame, before the game logic runs. The game logic only ever asks for actions.

#include "Core.h"

namespace mc {

// the keys the default bindings are made of (the host knows what each one is on its own keyboard)
enum Key : uint8_t {
    KEY_NONE = 0,
    KEY_MOUSE_LEFT, KEY_MOUSE_RIGHT, KEY_MOUSE_MIDDLE,
    KEY_SPACE, KEY_ESCAPE, KEY_ENTER,
    KEY_SHIFT, KEY_LSHIFT, // either Shift key / the left one
    KEY_CTRL, KEY_LCTRL,   // either Ctrl key / the left one
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_COUNT
};

enum Action : uint8_t {
    ACT_ATTACK,      // attack / destroy
    ACT_USE,         // use the item in the hand / place a block
    ACT_PICK_BLOCK,  // creative: take the block looked at into the hand
    ACT_JUMP,        // also swim up and fly up; twice in a row: creative flight
    ACT_SNEAK,       // also fly down and get off an animal
    ACT_SPRINT,
    ACT_INVENTORY,   // open / close
    ACT_DROP,        // throw one item out of the hand
    ACT_DROP_STACK,  // held together with ACT_DROP: the whole stack
    ACT_SWAP_HANDS,  // main hand <-> off hand
    ACT_HOTBAR_1, ACT_HOTBAR_2, ACT_HOTBAR_3, ACT_HOTBAR_4, ACT_HOTBAR_5, ACT_HOTBAR_6, ACT_HOTBAR_7, ACT_HOTBAR_8,
    ACT_HOTBAR_9,
    ACT_PERSPECTIVE, // first person / third person from behind / from the front
    ACT_GAME_MODE,   // survival <-> creative
    ACT_BACK,        // closes the screen that is open
    ACT_COUNT
};

struct ActionDef {
    const char* name; // as in Minecraft's controls screen
    Key key;          // Minecraft's default
};
extern const ActionDef kActions[ACT_COUNT];

struct Controls {
    bool down[ACT_COUNT] = {};     // held right now
    bool pressed[ACT_COUNT] = {};  // went down this frame
    bool scrollUp = false;         // mouse wheel this frame (the hotbar turns with it)
    bool scrollDown = false;

    // the host: once a frame for every action
    void Set(Action a, bool isDown, bool wasPressed) {
        down[a] = isDown;
        pressed[a] = wasPressed;
    }
};
extern Controls gControls;

inline bool ActionDown(Action a) { return gControls.down[a]; }
inline bool ActionPressed(Action a) { return gControls.pressed[a]; }

} // namespace mc
