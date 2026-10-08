#pragma once

#include "ModCommon.h"
#include "World.h"

namespace mc {

constexpr int INV_SIZE = 36; // 0..8 hotbar, 9..35 main
enum ArmorSlot { ARMOR_HEAD = 0, ARMOR_CHEST, ARMOR_LEGS, ARMOR_FEET };

struct PlayerInventory {
    ItemStack slots[INV_SIZE];
    ItemStack armor[4];
    ItemStack offhand;
    ItemStack craft[4];  // 2x2 grid in the inventory screen
    ItemStack craft3[9]; // crafting table grid (emptied when closed)
    ItemStack cursor;    // stack carried by the mouse
    int selected = 0;

    ItemStack& Held() { return slots[selected]; }
    int Add(ItemStack s); // returns how many did NOT fit
    int CountOf(uint16_t id) const;
    bool Remove(uint16_t id, int count); // false if not enough
    void Clear();
    bool HasElytra() const;
};

extern PlayerInventory gInv;

ItemStack MatchRecipe(const ItemStack* grid, int w, int h);
void ConsumeIngredients(ItemStack* grid, int count);

// furnace / smoker / blast furnace
int CookKindForBlock(int block);
void TickFurnaces(float dt);
bool FurnaceCanSmelt(const FurnaceState& f, int kind);

} // namespace mc
