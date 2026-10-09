#pragma once
// Data structures filled by the generated tables (src/core/generated/GameData.cpp).

#include <cstdint>

#include "Lang.h"
#include "generated/GameData.h"

namespace mc {

enum Shape : uint8_t { SHAPE_CUBE = 0, SHAPE_COLUMN, SHAPE_FACING, SHAPE_CROSS, SHAPE_FLUID, SHAPE_FIRE, SHAPE_STAIRS, SHAPE_SLAB, SHAPE_PANE, SHAPE_FENCE, SHAPE_WALL, SHAPE_BED, SHAPE_BED_HEAD };
enum RenderType : uint8_t { RENDER_AIR = 0, RENDER_OPAQUE, RENDER_CUTOUT, RENDER_TRANSLUCENT };
enum ToolType : uint8_t { TOOL_NONE = 0, TOOL_PICKAXE, TOOL_AXE, TOOL_SHOVEL, TOOL_HOE, TOOL_SWORD };
enum ToolTier : uint8_t { TIER_HAND = 0, TIER_WOOD = 1, TIER_STONE = 2, TIER_IRON = 3, TIER_DIAMOND = 4 };
enum Special : uint8_t {
    SP_NONE = 0, SP_BOW, SP_ARROW, SP_IGNITER, SP_FIREWORK, SP_ELYTRA, SP_SNOWBALL, SP_EGG, SP_ENDER_PEARL,
    SP_WAND = 9, SP_EGG_COW = 10, SP_EGG_PIG, SP_EGG_SHEEP, SP_EGG_CHICKEN, SP_FISHING_ROD = 14, SP_SHEARS = 15,
    SP_BUCKET = 16, SP_FIRE_CHARGE = 17, SP_WIND_CHARGE = 18, SP_SPYGLASS = 19, SP_TOTEM = 20, SP_BOAT = 21,
    SP_MINECART = 22, SP_SADDLE = 23, SP_MILK = 24, SP_TRIDENT = 25, SP_WATER_BUCKET = 26, SP_LAVA_BUCKET = 27,
    SP_CROSSBOW = 28, SP_BONE_MEAL = 29, SP_EGG_CREEPER = 30, SP_EGG_WARDEN = 31
};
enum Category : uint8_t {
    CAT_BUILDING = 0, CAT_COLORED, CAT_NATURAL, CAT_FUNCTIONAL, CAT_TOOLS, CAT_COMBAT, CAT_FOOD, CAT_INGREDIENTS,
    CAT_REDSTONE, CAT_SPAWN_EGGS, CAT_COUNT
};
enum SoundGroup : uint8_t { SG_STONE = 0, SG_WOOD, SG_GRAVEL, SG_GRASS, SG_SAND, SG_GLASS, SG_WOOL, SG_METAL, SG_SNOW };
enum CookKind : uint8_t { COOK_SMELTING = 0, COOK_BLASTING, COOK_SMOKING };

struct BlockDrop {
    uint16_t item;
    uint8_t min, max;
    float chance;
    uint8_t group; // drops of the same non-zero group are alternatives: the first roll that succeeds wins
};

struct BlockDef {
    const char* key;
    const char* name[LANG_COUNT]; // English, Turkish
    uint16_t tex[6];       // per face (E, W, N, S, top, bottom) for the default orientation
    uint16_t texFrontLit;  // facing blocks: front tile when lit (0xFFFF = none)
    uint8_t shape;
    uint8_t render;
    float hardness;        // < 0 unbreakable
    uint8_t tool;
    uint8_t tier;
    bool requiresTool;
    uint8_t surface;       // GTA eSurfaceType
    uint8_t sound;         // SoundGroup
    uint8_t category;
    bool emissive;
    uint8_t numDrops;
    BlockDrop drops[3];
};

struct ItemDef {
    const char* key;
    const char* name[LANG_COUNT]; // English, Turkish
    uint16_t tile;
    uint8_t maxStack;
    uint8_t tool;
    uint8_t tier;
    float speed;
    uint16_t durability;
    uint8_t food;
    float saturation;
    uint16_t fuel;
    float damage;       // half hearts
    float attackSpeed;  // attacks per second
    uint8_t armorSlot;  // 0 none, 1 head, 2 chest, 3 legs, 4 feet
    uint8_t armorPoints;
    uint8_t special;
    uint8_t category;
    uint8_t glint;      // enchanted shimmer
};

// "display" transforms from the Minecraft item models (degrees / pixels / factors)
struct ItemTransform {
    float rot[3], trans[3], scale[3];
};
struct ItemDisplay {
    ItemTransform ground, firstPerson, thirdPerson;
};

struct ShapedRecipe {
    uint8_t w, h;
    int16_t cells[9]; // ingredient set index or -1
    uint16_t result;
    uint8_t count;
};

struct ShapelessRecipe {
    uint8_t n;
    int16_t ings[9];
    uint16_t result;
    uint8_t count;
};

struct CookingRecipe {
    uint8_t kind;
    int16_t ing;
    uint16_t result;
    uint16_t time;
};

struct SoundFile {
    const char* file;
    float volume;
    float pitch;
};

struct SoundEventDef {
    uint16_t first, count;
};

extern const BlockDef kBlockDefs[NUM_BLOCKS];
extern const ItemDef kItemDefs[];
extern const ItemDisplay kDisplays[];
extern const uint8_t kItemDisplay[]; // item id -> index into kDisplays
extern const uint16_t kIngredientSets[][2];
extern const uint16_t kCreativeOrder[]; // every block and item in the order of Minecraft's creative tabs
extern const int kCreativeOrderCount;
extern const uint16_t kIngredientItems[];
extern const ShapedRecipe kShaped[];
extern const ShapelessRecipe kShapeless[];
extern const CookingRecipe kCooking[];
extern const int kNumShaped, kNumShapeless, kNumCooking, kNumIngredientSets;
extern const SoundFile kSoundFiles[];
extern const SoundEventDef kSoundEvents[SND_COUNT];

} // namespace mc
