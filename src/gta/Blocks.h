#pragma once
// The GTA side of the block logic. The rules themselves (flowing water and lava, fire, falling sand and gravel,
// plants that need ground, growing saplings) are the core's: src/core/BlockRules.h, included here.

#include "ModCommon.h"
#include "BlockRules.h"

namespace mc {

void BlocksUpdate(float dt); // script phase: the core's block tick, then what lava and fire do in GTA's world
void BlocksClear();

// the GTA map as the blocks see it
bool GtaSolidCell(const Int3& c);                      // the GTA ground fills the cell (more than half of it)
void GtaSolidCellForget(const Int3& c);                // ...ask again next time
bool GtaWallBetween(const Int3& a, const Int3& b);     // a GTA wall between two neighbouring cells
bool GtaSupports(const Int3& c);                       // the GTA map (or a car / prop) carries a block in the cell
bool GtaFlammableUnder(const Int3& c);                 // GTA bushes and dry grass under the cell
// soil of the GTA map (grass, dirt; sand too when asked) under a point; *gz: its height
bool GtaSoilBelow(float x, float y, float fromZ, bool sandToo, float* gz);

} // namespace mc
