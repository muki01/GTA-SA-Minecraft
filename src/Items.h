#pragma once

#include "GameTables.h"
#include "ModCommon.h"

namespace mc {

const BlockDef& Block(int id);
const ItemDef& Item(int id);
inline bool IsBlockItem(int id) { return id > 0 && id < NUM_BLOCKS; }
inline bool IsValidItem(int id) { return IsBlockItem(id) || (id >= FIRST_ITEM && id < ITEM_END); }
// solid = something you collide with (plants and fluids are not)
inline bool IsSolidBlock(int id) { return id > 0 && id < NUM_BLOCKS && Block(id).shape < SHAPE_CROSS; }
inline bool IsFluidBlock(int id) { return id == ID_WATER || id == ID_LAVA; }
inline bool IsFireBlock(int id) { return id == ID_FIRE; }
inline bool IsPlantBlock(int id) { return id > 0 && id < NUM_BLOCKS && Block(id).shape == SHAPE_CROSS; }
bool IsSaplingBlock(int id);
bool IsGravityBlock(int id);
// FireBlock odds: how easily fire starts on the block (0 = never) and how fast it burns away
int FireIgnite(int id);
int FireBurn(int id);
// random ticks: saplings grow, lava sets things on fire, fire spreads and dies
inline bool IsRandomTicking(int id) { return IsSaplingBlock(id) || id == ID_LAVA || id == ID_FIRE; } // sand, gravel, concrete powder
inline bool IsOpaqueBlock(int id) { return id > 0 && id < NUM_BLOCKS && Block(id).render == RENDER_OPAQUE; }
const char* ItemName(int id);
int MaxStack(int id);
int FuelTicks(int id);

// cooking: returns result item (0 = none) and the time in ticks
uint16_t CookResult(int kind, int id, int* timeOut = nullptr);

// creative inventory contents per tab
const std::vector<uint16_t>& CreativeItems(int category);

// Block tile for a face given the block's metadata (facing / axis / lit).
uint16_t BlockFaceTile(int block, int face, int meta);
// horizontal logs: the side texture is turned 90 degrees on some faces
bool BlockFaceRotated(int block, int face, int meta);

// metadata helpers
constexpr int META_DIR_MASK = 0x3; // facing (0 N, 1 E, 2 S, 3 W) or axis (0 vertical, 1 X, 2 Y)
constexpr int META_LIT = 0x4;
constexpr int META_NATURAL = 0x8;       // solid blocks: ground that came from the GTA map (drawn only through holes)
constexpr int META_FLUID_LEVEL = 0x7;   // fluids: 0 = source .. 7 = thinnest
constexpr int META_FLUID_FALLING = 0x8;

} // namespace mc
