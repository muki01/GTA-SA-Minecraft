#include "Inventory.h"

namespace mc {

PlayerInventory gInv;

int PlayerInventory::Add(ItemStack s) {
    if (s.Empty())
        return 0;
    int maxStack = MaxStack(s.id);
    auto tryMerge = [&](ItemStack& slot) {
        if (s.count == 0 || slot.Empty() || !slot.SameItem(s) || slot.count >= maxStack)
            return;
        int move = std::min<int>(s.count, maxStack - slot.count);
        slot.count += (uint8_t)move;
        s.count -= (uint8_t)move;
    };
    tryMerge(slots[selected]);
    for (int i = 0; i < INV_SIZE && s.count; ++i)
        tryMerge(slots[i]);
    for (int i = 0; i < INV_SIZE && s.count; ++i) {
        if (slots[i].Empty()) {
            int move = std::min<int>(s.count, maxStack);
            slots[i] = s;
            slots[i].count = (uint8_t)move;
            s.count -= (uint8_t)move;
        }
    }
    return s.count;
}

int PlayerInventory::CountOf(uint16_t id) const {
    int n = 0;
    for (auto& s : slots)
        if (s.id == id)
            n += s.count;
    if (offhand.id == id)
        n += offhand.count;
    return n;
}

bool PlayerInventory::Remove(uint16_t id, int count) {
    if (CountOf(id) < count)
        return false;
    auto take = [&](ItemStack& s) {
        if (count <= 0 || s.id != id)
            return;
        int t = std::min<int>(count, s.count);
        s.count -= (uint8_t)t;
        count -= t;
        if (s.count == 0)
            s.Clear();
    };
    take(offhand);
    take(slots[selected]);
    for (auto& s : slots)
        take(s);
    return true;
}

void PlayerInventory::Clear() {
    for (auto& s : slots)
        s.Clear();
    for (auto& s : armor)
        s.Clear();
    offhand.Clear();
    for (auto& s : craft)
        s.Clear();
    for (auto& s : craft3)
        s.Clear();
    cursor.Clear();
}

bool PlayerInventory::HasElytra() const {
    return armor[ARMOR_CHEST].id == ID_ELYTRA && armor[ARMOR_CHEST].damage + 1 < Item(ID_ELYTRA).durability;
}

// ---------------------------------------------------------------- recipes
static bool InSet(int16_t set, uint16_t id) {
    if (set < 0 || set >= kNumIngredientSets)
        return false;
    const uint16_t* items = &kIngredientItems[kIngredientSets[set][0]];
    int n = kIngredientSets[set][1];
    for (int i = 0; i < n; ++i)
        if (items[i] == id)
            return true;
    return false;
}

static bool Bounds(const ItemStack* grid, int w, int h, int& x0, int& y0, int& x1, int& y1) {
    x0 = w; y0 = h; x1 = -1; y1 = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (!grid[y * w + x].Empty()) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
    return x1 >= 0;
}

static bool MatchShaped(const ShapedRecipe& r, const ItemStack* grid, int gw, int x0, int y0, bool mirror) {
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) {
            int px = mirror ? r.w - 1 - x : x;
            int16_t ing = r.cells[y * r.w + px];
            const ItemStack& cell = grid[(y0 + y) * gw + (x0 + x)];
            if (ing < 0) {
                if (!cell.Empty())
                    return false;
            } else if (cell.Empty() || !InSet(ing, cell.id)) {
                return false;
            }
        }
    return true;
}

static bool AssignShapeless(const ShapelessRecipe& r, const uint16_t* items, int n, int idx, uint16_t usedMask) {
    if (idx == n)
        return true;
    for (int k = 0; k < r.n; ++k) {
        if (usedMask & (1 << k))
            continue;
        if (InSet(r.ings[k], items[idx]) && AssignShapeless(r, items, n, idx + 1, usedMask | (1 << k)))
            return true;
    }
    return false;
}

ItemStack MatchRecipe(const ItemStack* grid, int w, int h) {
    int x0, y0, x1, y1;
    if (!Bounds(grid, w, h, x0, y0, x1, y1))
        return {};
    int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    for (int i = 0; i < kNumShaped; ++i) {
        const ShapedRecipe& r = kShaped[i];
        if (r.w != bw || r.h != bh)
            continue;
        if (MatchShaped(r, grid, w, x0, y0, false) || MatchShaped(r, grid, w, x0, y0, true)) {
            ItemStack s;
            s.id = r.result;
            s.count = r.count;
            return s;
        }
    }
    uint16_t items[9];
    int n = 0;
    for (int i = 0; i < w * h; ++i)
        if (!grid[i].Empty())
            items[n++] = grid[i].id;
    for (int i = 0; i < kNumShapeless; ++i) {
        const ShapelessRecipe& r = kShapeless[i];
        if (r.n != n)
            continue;
        if (AssignShapeless(r, items, n, 0, 0)) {
            ItemStack s;
            s.id = r.result;
            s.count = r.count;
            return s;
        }
    }
    return {};
}

void ConsumeIngredients(ItemStack* grid, int count) {
    for (int i = 0; i < count; ++i)
        if (!grid[i].Empty()) {
            uint16_t id = grid[i].id;
            grid[i].count--;
            if (grid[i].count == 0)
                grid[i].Clear();
            // buckets give back an empty bucket, like in Minecraft
            if ((id == ID_MILK_BUCKET || id == ID_WATER_BUCKET || id == ID_LAVA_BUCKET) && grid[i].Empty()) {
                grid[i].id = ID_BUCKET;
                grid[i].count = 1;
            }
        }
}

// ---------------------------------------------------------------- furnaces
int CookKindForBlock(int block) {
    if (block == ID_BLAST_FURNACE)
        return COOK_BLASTING;
    if (block == ID_SMOKER)
        return COOK_SMOKING;
    return COOK_SMELTING;
}

bool FurnaceCanSmelt(const FurnaceState& f, int kind) {
    if (f.input.Empty())
        return false;
    uint16_t res = CookResult(kind, f.input.id);
    if (!res)
        return false;
    if (f.output.Empty())
        return true;
    return f.output.id == res && f.output.count < MaxStack(res);
}

void TickFurnaces(float dt) {
    for (auto& kv : gWorld.furnaces) {
        FurnaceState& f = kv.second;
        Voxel vox = gWorld.Get(kv.first.x, kv.first.y, kv.first.z);
        int block = VoxBlock(vox);
        if (block != ID_FURNACE && block != ID_BLAST_FURNACE && block != ID_SMOKER)
            continue;
        int kind = CookKindForBlock(block);
        f.tickAccum += dt * 20.0f;
        bool wasLit = f.burnTime > 0;
        while (f.tickAccum >= 1.0f) {
            f.tickAccum -= 1.0f;
            if (f.burnTime > 0)
                f.burnTime--;
            bool can = FurnaceCanSmelt(f, kind);
            if (f.burnTime == 0 && can) {
                int ticks = FuelTicks(f.fuel.id);
                if (kind != COOK_SMELTING)
                    ticks /= 2;
                if (ticks > 0 && !f.fuel.Empty()) {
                    f.burnTime = f.burnTimeTotal = ticks;
                    uint16_t fuelId = f.fuel.id;
                    if (--f.fuel.count == 0) {
                        f.fuel.Clear();
                        if (fuelId == ID_LAVA_BUCKET) {
                            f.fuel.id = ID_BUCKET;
                            f.fuel.count = 1;
                        }
                    }
                }
            }
            if (f.burnTime > 0 && can) {
                int total = 200;
                CookResult(kind, f.input.id, &total);
                if (++f.cookTime >= total) {
                    f.cookTime = 0;
                    uint16_t res = CookResult(kind, f.input.id);
                    if (f.output.Empty()) {
                        f.output.id = res;
                        f.output.count = 1;
                        f.output.damage = 0;
                    } else {
                        f.output.count++;
                    }
                    if (--f.input.count == 0)
                        f.input.Clear();
                    gWorld.dirty = true;
                }
            } else if (f.cookTime > 0) {
                f.cookTime = std::max(0, f.cookTime - 2);
            }
        }
        bool lit = f.burnTime > 0;
        if (lit != wasLit) {
            int meta = VoxMeta(vox);
            meta = lit ? (meta | META_LIT) : (meta & ~META_LIT);
            gWorld.Set(kv.first.x, kv.first.y, kv.first.z, MakeVox(block, meta));
        }
    }
}

} // namespace mc
