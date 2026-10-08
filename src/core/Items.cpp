#include <cstring>
#include "Items.h"

namespace mc {

const BlockDef& Block(int id) {
    if (id < 0 || id >= NUM_BLOCKS)
        return kBlockDefs[0];
    return kBlockDefs[id];
}

static int BlockFuel(int id) {
    const BlockDef& b = Block(id);
    if (id == ID_COAL_BLOCK)
        return 16000;
    if (b.sound == SG_WOOD)
        return 300;
    return 0;
}

const ItemDef& Item(int id) {
    static ItemDef blockItems[NUM_BLOCKS];
    static bool init = false;
    if (!init) {
        init = true;
        for (int b = 0; b < NUM_BLOCKS; ++b) {
            ItemDef& d = blockItems[b];
            d = {};
            d.key = kBlockDefs[b].key;
            d.name = kBlockDefs[b].name;
            d.tile = kBlockDefs[b].tex[FACE_SOUTH];
            d.maxStack = 64;
            d.speed = 1.0f;
            d.damage = 1.0f;
            d.attackSpeed = 4.0f;
            d.fuel = (uint16_t)BlockFuel(b);
            d.category = kBlockDefs[b].category;
        }
    }
    if (IsBlockItem(id))
        return blockItems[id];
    if (id >= FIRST_ITEM && id < ITEM_END)
        return kItemDefs[id - FIRST_ITEM];
    return blockItems[0];
}

const char* ItemName(int id) { return Item(id).name; }

namespace {
struct BlockFlags {
    bool sapling[NUM_BLOCKS] = {};
    bool gravity[NUM_BLOCKS] = {};
    uint8_t ignite[NUM_BLOCKS] = {}, burn[NUM_BLOCKS] = {};
    BlockFlags() {
        auto has = [](const std::string& k, const char* w) { return k.find(w) != std::string::npos; };
        auto ends = [](const std::string& k, const char* w) {
            const size_t n = strlen(w);
            return k.size() >= n && k.compare(k.size() - n, n, w) == 0;
        };
        for (int b = 1; b < NUM_BLOCKS; ++b) {
            const std::string k = kBlockDefs[b].key;
            sapling[b] = ends(k, "_sapling");
            gravity[b] = k == "sand" || k == "red_sand" || k == "gravel" || has(k, "concrete_powder");
            // FireBlock.bootStrap (the nether woods do not burn)
            const bool netherWood = has(k, "crimson") || has(k, "warped");
            int ig = 0, bu = 0;
            if (netherWood)
                ;
            else if (ends(k, "_planks") || ends(k, "_slab") && (has(k, "oak") || has(k, "spruce") || has(k, "birch") ||
                                                                  has(k, "jungle") || has(k, "acacia") || has(k, "mangrove") ||
                                                                  has(k, "cherry") || has(k, "bamboo")))
                ig = 5, bu = 20;
            else if (ends(k, "_log") || ends(k, "_wood") || has(k, "bamboo_block"))
                ig = 5, bu = 5;
            else if (ends(k, "_leaves"))
                ig = 30, bu = 60;
            else if (ends(k, "_wool"))
                ig = 30, bu = 60;
            else if (ends(k, "_carpet") || k == "hay_block" || k == "dried_kelp_block" || k == "target")
                ig = 60, bu = 20;
            else if (k == "bookshelf" || k == "chiseled_bookshelf" || k == "lectern" || k == "beehive" || k == "bee_nest")
                ig = 30, bu = 20;
            else if (k == "tnt")
                ig = 15, bu = 100;
            else if (k == "coal_block")
                ig = 5, bu = 5;
            else if (kBlockDefs[b].shape == SHAPE_CROSS)
                ig = 60, bu = 100;
            ignite[b] = (uint8_t)ig;
            burn[b] = (uint8_t)bu;
        }
    }
};
const BlockFlags& Flags() {
    static BlockFlags f;
    return f;
}
} // namespace

bool IsSaplingBlock(int id) { return id > 0 && id < NUM_BLOCKS && Flags().sapling[id]; }
bool IsGravityBlock(int id) { return id > 0 && id < NUM_BLOCKS && Flags().gravity[id]; }
int FireIgnite(int id) { return id > 0 && id < NUM_BLOCKS ? Flags().ignite[id] : 0; }
int FireBurn(int id) { return id > 0 && id < NUM_BLOCKS ? Flags().burn[id] : 0; }

int MaxStack(int id) { return IsValidItem(id) ? Item(id).maxStack : 64; }

int FuelTicks(int id) { return IsValidItem(id) ? Item(id).fuel : 0; }

uint16_t CookResult(int kind, int id, int* timeOut) {
    for (int i = 0; i < kNumCooking; ++i) {
        const CookingRecipe& r = kCooking[i];
        if (r.kind != kind)
            continue;
        const uint16_t* set = &kIngredientItems[kIngredientSets[r.ing][0]];
        int n = kIngredientSets[r.ing][1];
        for (int k = 0; k < n; ++k)
            if (set[k] == id) {
                // blast furnaces and smokers work twice as fast as a furnace
                if (timeOut)
                    *timeOut = (kind != COOK_SMELTING && r.time >= 200) ? r.time / 2 : r.time;
                return r.result;
            }
    }
    return 0;
}

const std::vector<uint16_t>& CreativeItems(int category) {
    static std::vector<uint16_t> lists[CAT_COUNT];
    static bool init = false;
    if (!init) {
        init = true;
        for (int n = 0; n < kCreativeOrderCount; ++n) {
            const uint16_t id = kCreativeOrder[n];
            if (id < NUM_BLOCKS) {
                if (Block(id).shape != SHAPE_FLUID && Block(id).shape != SHAPE_FIRE && Block(id).shape != SHAPE_BED_HEAD)
                    lists[std::min<int>(Block(id).category, CAT_COUNT - 1)].push_back(id);
            } else if (IsValidItem(id)) {
                lists[std::min<int>(Item(id).category, CAT_COUNT - 1)].push_back(id);
            }
        }
    }
    return lists[std::clamp(category, 0, CAT_COUNT - 1)];
}

// compass order used for facing rotation
static const int kCompassFace[4] = { FACE_NORTH, FACE_EAST, FACE_SOUTH, FACE_WEST };

static int CompassOf(int face) {
    switch (face) {
    case FACE_NORTH: return 0;
    case FACE_EAST: return 1;
    case FACE_SOUTH: return 2;
    case FACE_WEST: return 3;
    default: return -1;
    }
}

uint16_t BlockFaceTile(int block, int face, int meta) {
    const BlockDef& b = Block(block);
    switch (b.shape) {
    case SHAPE_COLUMN: {
        int axis = meta & META_DIR_MASK;
        uint16_t end = b.tex[FACE_TOP], side = b.tex[FACE_EAST];
        if (axis == 1)
            return (face == FACE_EAST || face == FACE_WEST) ? end : side;
        if (axis == 2)
            return (face == FACE_NORTH || face == FACE_SOUTH) ? end : side;
        return b.tex[face];
    }
    case SHAPE_FACING: {
        int c = CompassOf(face);
        if (c < 0)
            return b.tex[face];
        int f = meta & META_DIR_MASK;
        int src = (c - f + 4) & 3;
        if (src == 0 && (meta & META_LIT) && b.texFrontLit != 0xFFFF)
            return b.texFrontLit;
        return b.tex[kCompassFace[src]];
    }
    case SHAPE_BED:
    case SHAPE_BED_HEAD: {
        // the tiles are for a bed whose head is north; meta: the side its head is on
        const int c = CompassOf(face);
        if (c < 0)
            return b.tex[face];
        return b.tex[kCompassFace[(c - CompassOf(meta & 3) + 4) & 3]];
    }
    default:
        return b.tex[face];
    }
}

bool BlockFaceRotated(int block, int face, int meta) {
    if (Block(block).shape != SHAPE_COLUMN)
        return false;
    int axis = meta & META_DIR_MASK;
    if (axis == 1)
        return face == FACE_NORTH || face == FACE_SOUTH || face == FACE_TOP || face == FACE_BOTTOM;
    if (axis == 2)
        return face == FACE_EAST || face == FACE_WEST;
    return false;
}

} // namespace mc
