// Offline checks of the pure game logic (recipes, cooking, tables). Not part of the mod.
#include <cstdio>
#include <initializer_list>

#include "Inventory.h"
#include "Items.h"

using namespace mc;

static int gFail = 0;
#define CHECK(cond, ...) \
    do { \
        if (!(cond)) { \
            gFail++; \
            printf("FAIL: "); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

static ItemStack Craft(int w, int h, std::initializer_list<uint16_t> cells) {
    ItemStack grid[9];
    int i = 0;
    for (uint16_t c : cells) {
        if (c) {
            grid[i].id = c;
            grid[i].count = 1;
        }
        ++i;
    }
    return MatchRecipe(grid, w, h);
}

static void Expect(const char* what, const ItemStack& r, uint16_t id, int count) {
    CHECK(r.id == id && r.count == count, "%s -> got %s x%d, want %s x%d", what, r.id ? Item(r.id).key : "nothing",
          r.count, Item(id).key, count);
}

int main() {
    printf("blocks %d, items %d, shaped %d, shapeless %d, cooking %d\n", NUM_BLOCKS - 1, ITEM_END - FIRST_ITEM, kNumShaped,
           kNumShapeless, kNumCooking);
    const uint16_t P = ID_OAK_PLANKS, S = ID_STICK, C = ID_COBBLESTONE, I = ID_IRON_INGOT, G = ID_GUNPOWDER;
    Expect("log->planks", Craft(2, 2, { ID_OAK_LOG, 0, 0, 0 }), ID_OAK_PLANKS, 4);
    Expect("birch log->planks", Craft(3, 3, { 0, 0, 0, 0, ID_BIRCH_LOG, 0, 0, 0, 0 }), ID_BIRCH_PLANKS, 4);
    Expect("sticks", Craft(2, 2, { P, 0, P, 0 }), ID_STICK, 4);
    Expect("sticks (right column)", Craft(2, 2, { 0, P, 0, P }), ID_STICK, 4);
    Expect("crafting table", Craft(2, 2, { P, P, P, P }), ID_CRAFTING_TABLE, 1);
    Expect("wooden pickaxe", Craft(3, 3, { P, P, P, 0, S, 0, 0, S, 0 }), ID_WOODEN_PICKAXE, 1);
    Expect("stone pickaxe", Craft(3, 3, { C, C, C, 0, S, 0, 0, S, 0 }), ID_STONE_PICKAXE, 1);
    Expect("stone axe", Craft(3, 3, { C, C, 0, C, S, 0, 0, S, 0 }), ID_STONE_AXE, 1);
    Expect("stone axe mirrored", Craft(3, 3, { 0, C, C, 0, S, C, 0, S, 0 }), ID_STONE_AXE, 1);
    Expect("iron sword", Craft(3, 3, { 0, I, 0, 0, I, 0, 0, S, 0 }), ID_IRON_SWORD, 1);
    Expect("diamond sword", Craft(3, 3, { ID_DIAMOND, 0, 0, ID_DIAMOND, 0, 0, S, 0, 0 }), ID_DIAMOND_SWORD, 1);
    Expect("furnace", Craft(3, 3, { C, C, C, C, 0, C, C, C, C }), ID_FURNACE, 1);
    Expect("chest", Craft(3, 3, { P, P, P, P, 0, P, P, P, P }), ID_CHEST, 1);
    Expect("bow", Craft(3, 3, { 0, S, ID_STRING, S, 0, ID_STRING, 0, S, ID_STRING }), ID_BOW, 1);
    Expect("arrow", Craft(3, 3, { ID_FLINT, 0, 0, S, 0, 0, ID_FEATHER, 0, 0 }), ID_ARROW, 4);
    Expect("tnt", Craft(3, 3, { G, ID_SAND, G, ID_SAND, G, ID_SAND, G, ID_SAND, G }), ID_TNT, 1);
    Expect("flint and steel", Craft(2, 2, { I, ID_FLINT, 0, 0 }), ID_FLINT_AND_STEEL, 1);
    Expect("firework rocket", Craft(2, 2, { G, ID_PAPER, 0, 0 }), ID_FIREWORK_ROCKET, 3);
    Expect("iron block", Craft(3, 3, { I, I, I, I, I, I, I, I, I }), ID_IRON_BLOCK, 1);
    Expect("iron block -> ingots", Craft(2, 2, { ID_IRON_BLOCK, 0, 0, 0 }), ID_IRON_INGOT, 9);
    Expect("bread", Craft(3, 3, { ID_WHEAT, ID_WHEAT, ID_WHEAT, 0, 0, 0, 0, 0, 0 }), ID_BREAD, 1);
    Expect("garbage", Craft(2, 2, { ID_DIRT, ID_STONE, 0, 0 }), 0, 0);
    Expect("empty", Craft(2, 2, { 0, 0, 0, 0 }), 0, 0);

    int t = 0;
    CHECK(CookResult(COOK_SMELTING, ID_RAW_IRON, &t) == ID_IRON_INGOT && t == 200, "smelt raw iron (t=%d)", t);
    CHECK(CookResult(COOK_BLASTING, ID_RAW_IRON, &t) == ID_IRON_INGOT && t == 100, "blast raw iron (t=%d)", t);
    CHECK(CookResult(COOK_SMELTING, ID_COBBLESTONE) == ID_STONE, "smelt cobblestone");
    CHECK(CookResult(COOK_SMELTING, ID_SAND) == ID_GLASS, "smelt sand");
    CHECK(CookResult(COOK_SMELTING, ID_OAK_LOG) == ID_CHARCOAL, "smelt log");
    CHECK(CookResult(COOK_SMOKING, ID_BEEF) == ID_COOKED_BEEF, "smoke beef");
    CHECK(CookResult(COOK_SMELTING, ID_DIRT) == 0, "dirt must not smelt");

    CHECK(FuelTicks(ID_COAL) == 1600, "coal fuel");
    CHECK(FuelTicks(ID_OAK_PLANKS) == 300, "planks fuel %d", FuelTicks(ID_OAK_PLANKS));
    CHECK(FuelTicks(ID_STONE) == 0, "stone fuel");
    CHECK(MaxStack(ID_DIAMOND_SWORD) == 1 && MaxStack(ID_ENDER_PEARL) == 16 && MaxStack(ID_STONE) == 64, "stack sizes");
    CHECK(Item(ID_DIAMOND_SWORD).tool == TOOL_SWORD && Item(ID_DIAMOND_SWORD).damage == 7.0f, "diamond sword stats");
    CHECK(Item(ID_ELYTRA).armorSlot == 2 && Item(ID_ELYTRA).special == SP_ELYTRA, "elytra");
    CHECK(Item(ID_BOW).special == SP_BOW && Item(ID_ARROW).special == SP_ARROW, "bow/arrow");
    CHECK(Item(ID_FLINT_AND_STEEL).special == SP_IGNITER && Item(ID_FIREWORK_ROCKET).special == SP_FIREWORK,
          "igniter/firework");
    CHECK(Block(ID_STONE).requiresTool && Block(ID_STONE).tool == TOOL_PICKAXE, "stone needs pickaxe");
    CHECK(!Block(ID_DIRT).requiresTool && Block(ID_DIRT).tool == TOOL_SHOVEL, "dirt");
    CHECK(Block(ID_DIAMOND_ORE).tier == TIER_IRON, "diamond ore tier %d", Block(ID_DIAMOND_ORE).tier);
    CHECK(Block(ID_OAK_LOG).shape == SHAPE_COLUMN && Block(ID_FURNACE).shape == SHAPE_FACING, "shapes");
    CHECK(Block(ID_GLASS).render == RENDER_CUTOUT && Block(ID_WHITE_STAINED_GLASS).render == RENDER_TRANSLUCENT &&
              Block(ID_STONE).render == RENDER_OPAQUE,
          "render types");
    // facing: the front tile follows the facing
    for (int f = 0; f < 4; ++f) {
        static const int faces[4] = { FACE_NORTH, FACE_EAST, FACE_SOUTH, FACE_WEST };
        int fronts = 0;
        for (int k = 0; k < 4; ++k)
            if (BlockFaceTile(ID_FURNACE, faces[k], f) == Block(ID_FURNACE).tex[FACE_NORTH])
                fronts += (k == f) ? 1 : 10;
        CHECK(fronts == 1, "furnace facing %d front count %d", f, fronts);
    }
    CHECK(BlockFaceTile(ID_FURNACE, FACE_NORTH, META_LIT) == Block(ID_FURNACE).texFrontLit, "lit furnace front");
    CHECK(BlockFaceTile(ID_OAK_LOG, FACE_EAST, 1) == Block(ID_OAK_LOG).tex[FACE_TOP], "log axis X end");
    CHECK(BlockFaceTile(ID_OAK_LOG, FACE_TOP, 1) == Block(ID_OAK_LOG).tex[FACE_EAST], "log axis X side on top");

    size_t total = 0;
    for (int c = 0; c < CAT_COUNT; ++c) {
        printf("creative tab %d: %d items\n", c, (int)CreativeItems(c).size());
        total += CreativeItems(c).size();
    }
    size_t fluids = 0; // water and lava only come from buckets
    for (int b = 1; b < NUM_BLOCKS; ++b)
        if (IsFluidBlock(b) || IsFireBlock(b))
            ++fluids;
    CHECK(total == (size_t)(NUM_BLOCKS - 1 - fluids + ITEM_END - FIRST_ITEM), "creative tabs cover everything (%d vs %d)",
          (int)total, (int)(NUM_BLOCKS - 1 - fluids + ITEM_END - FIRST_ITEM));

    // inventory behaviour
    PlayerInventory inv;
    ItemStack s;
    s.id = ID_STONE;
    s.count = 64;
    for (int i = 0; i < 36; ++i)
        CHECK(inv.Add(s) == 0, "add stack %d", i);
    CHECK(inv.Add(s) == 64, "full inventory must reject");
    CHECK(inv.CountOf(ID_STONE) == 36 * 64, "count");
    CHECK(inv.Remove(ID_STONE, 100) && inv.CountOf(ID_STONE) == 36 * 64 - 100, "remove");

    printf(gFail ? "\n%d CHECK(S) FAILED\n" : "\nALL CHECKS PASSED\n", gFail);
    return gFail;
}
