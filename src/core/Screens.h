#pragma once
// Minecraft's inventory screens: which slots each one has and where (GUI pixels from the window's top left corner),
// the creative tabs, and what clicking does (picking up, splitting, shift-moving, crafting, armour, the creative
// palette, the trash slot, throwing out what the cursor holds outside the window). The host draws them and turns its
// mouse into GUI pixels.

#include "Core.h"
#include "World.h"

namespace mc {

enum SlotKind { SK_NORMAL, SK_RESULT2, SK_RESULT3, SK_FUEL, SK_OUTPUT, SK_PALETTE, SK_ARMOR, SK_OFFHAND, SK_TRASH };
enum SlotGroup { G_HOTBAR, G_MAIN, G_CONTAINER, G_GRID, G_FURNACE_IN, G_FURNACE_FUEL, G_ARMOR };

struct UiSlot {
    ItemStack* st;
    int gx, gy; // GUI pixels from the window's top left corner
    int kind;
    int group;
    int index;
};
extern std::vector<UiSlot> gSlots; // the open screen's (BuildSlots)
extern int gWinW, gWinH;           // the open screen's window, GUI pixels

// creative tabs (CAT_COUNT item tabs + the survival inventory)
struct TabPos {
    bool top;
    int col; // 0..5 from the left; 6 = the right end
};
extern const TabPos kTabPos[];
extern const uint16_t kTabIcons[];
extern const char* const kTabNames[];

void BuildSlots(); // for gGame.screen
UiSlot* SlotAt(float gx, float gy);
void TabBox(int tab, float& x, float& y, float& w, float& h); // GUI pixels from the window's corner
int TabAt(float gx, float gy);                                // -1: none
bool OverWindow(float gx, float gy);
bool CreativeInventoryTab();
int PaletteRows();
FurnaceState* OpenFurnace();
const char* ScreenTitle();
// a click of the left / right button (shift held or not) at a point of the open screen (GUI pixels)
void ScreenClick(float gx, float gy, bool left, bool right, bool shift);
void ScreenScroll(int rows); // the mouse wheel: the creative palette

} // namespace mc
