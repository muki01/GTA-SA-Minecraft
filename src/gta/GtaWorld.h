#pragma once

#include "ModCommon.h"

class CEntity;
class CColPoint;
class CVehicle;

namespace mc {

// Reads model names from the game's IDE files (needed to recognise trees, bushes, buildings...).
void LoadGtaModelNames();
const char* GtaModelName(int modelId);
// trees, bushes, grass and flowers: never solid ground or walls for digging and breaking
bool IsPlantModel(int modelId);

// What a piece of the GTA map "is" when it gets mined.
enum GtaMatKind : uint8_t {
    GM_NONE = 0,
    GM_BLOCK,   // drops whatever the Minecraft block drops
    GM_ROCK,    // stone: cobblestone and the occasional ore
    GM_GRASS,   // dirt, sometimes seeds
    GM_LEAVES,  // apples / sticks
    GM_GLASS,   // the glass itself
    GM_METAL,   // iron nuggets / ingots
    GM_LAMP,    // iron + glowstone
    GM_PAPER,   // cardboard boxes
    GM_TRASH,   // bin bags
    GM_SHOP,    // shop shelves: food
    GM_DESK,    // office desks: paper, books
    GM_TABLE,   // restaurant / bar tables: food and bowls
    GM_CROP,    // corn fields: wheat
    GM_GORE,
    GM_RAIL,
    GM_RUBBER,  // slime balls
    GM_WOOD_FENCE,
    GM_VEHICLE,
};

struct GtaMaterial {
    uint16_t block = 0;      // Minecraft block used for hardness, tool, sounds and particles
    uint8_t kind = GM_NONE;
    float hardness = -2.0f;  // > -1: replaces the block's hardness
};

// The surface (eSurfaceType) of what a line test hit. plugin-sdk's CColPoint puts m_nSurfaceTypeB one byte too
// far (0x24, which is the piece type and always 0 on the map); the game keeps it at 0x23.
inline int HitSurface(const CColPoint& cp) { return reinterpret_cast<const unsigned char*>(&cp)[0x23]; }

GtaMaterial MaterialFor(const CColPoint& cp, CEntity* entity);
// like MaterialFor, but walls and floors are judged by the texture the ray (origin, dir) hits
GtaMaterial MaterialForTarget(const CColPoint& cp, CEntity* ent, const CVector& origin, const CVector& dir);
GtaMaterial MaterialForSurface(int surface); // only the collision surface (the ground under named models)
GtaMaterial MaterialForVehicle(CVehicle* veh);
// The block a spot of the map looks like (animals use it to pick grassland). ID_AIR when unknown.
int VirtualBlockFor(const CColPoint& cp, CEntity* entity);
// Survival drops for a mined piece of the GTA world. `toolTier` = tier of the held tool.
void SpawnMaterialDrops(const GtaMaterial& m, const CVector& at, int toolTier, CVehicle* veh = nullptr);

} // namespace mc
