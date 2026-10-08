#pragma once
// Minecraft's HUD and screens as a picture to draw: the hotbar, experience, hearts, armour, food and air, the held
// item's name, the status effects, the crosshair, the tint inside water or lava, the open inventory screen, the death
// screen and the messages. Each is a piece at a place in GUI pixels from an anchor of the screen; sprites are parts of
// the GUI texture (GuiRect, generated/Assets.h). The host only paints the pieces, at its own GUI scale.

#include <string>

#include "Core.h"
#include "World.h"

namespace mc {

enum HudPieceKind {
    HP_SPRITE,  // src of the GUI texture (or of the menu texture)
    HP_STACK,   // an item stack in a slot: icon, count, durability bar
    HP_ICON,    // an item's icon
    HP_TEXT,
    HP_FILL,    // a rectangle of colour (w = h = 0: the whole screen)
    HP_GRADIENT, // the whole screen, from color at the top to color2 at the bottom, faded in by `scale`
    HP_TOOLTIP, // a tooltip box with `text`
    HP_PLAYER,  // the player's doll in the inventory (centre x, top y)
};
enum HudAnchor {
    AT_TOP_LEFT,
    AT_TOP,       // the middle of the top edge
    AT_TOP_RIGHT,
    AT_CENTRE,
    AT_QUARTER,   // the middle, a quarter of the way down
    AT_BOTTOM,    // the middle of the bottom edge
    AT_WINDOW,    // the open screen's window, top left corner
    AT_CURSOR,    // the mouse pointer
};
enum HudTexture { HT_GUI, HT_MENU };

struct HudPiece {
    int kind = HP_SPRITE;
    int anchor = AT_TOP_LEFT;
    float x = 0.0f, y = 0.0f;     // GUI pixels from the anchor
    float w = 0.0f, h = 0.0f;     // HP_FILL: size in GUI pixels
    GuiRect src{ 0, 0, 0, 0 };    // HP_SPRITE
    int texture = HT_GUI;         // HP_SPRITE
    uint32_t color = 0xFFFFFFFF;  // tint, text or fill colour (ARGB); HP_GRADIENT: the top
    uint32_t color2 = 0xFFFFFFFF; // HP_GRADIENT: the bottom; HP_TEXT: the colour of text2
    float scale = 1.0f;           // HP_TEXT, HP_PLAYER: times the GUI scale; HP_GRADIENT: how far it faded in
    const ItemStack* stack = nullptr; // HP_STACK
    uint16_t item = 0;                // HP_ICON
    std::string text, text2;          // HP_TEXT (text2 follows text one pixel later), HP_TOOLTIP
    int align = 0;                    // HP_TEXT: 0 starts at x, 1 centred on x (text and text2 together), 2 ends at x
    bool shadow = true;               // HP_TEXT
    bool outline = false;             // HP_TEXT: a black outline (the experience level)
    bool invert = false;              // HP_SPRITE: inverts what is behind it (the crosshair)
};

// what the host knows this frame
struct HudFacts {
    bool shown = true;       // the HUD is on: the mod runs and the world is on screen
    bool hasPlayer = true;
    float health = 1.0f;     // the host's health, 0..1
    float hostArmour = 0.0f; // the host's own body armour, 0..1
    Vec3 camera;             // (inside our water or lava the screen is tinted)
    float mouseX = 0.0f, mouseY = 0.0f; // the mouse in GUI pixels from the open window's corner
    const char* playerName = "Steve";
};

// What to draw this frame, in the order to draw it. While a screen is open the host has built its slots (BuildSlots)
// to know where the window goes.
void BuildHud(const HudFacts& f, std::vector<HudPiece>& out);

} // namespace mc
