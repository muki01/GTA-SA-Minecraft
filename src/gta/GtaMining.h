#pragma once
// Mining GTA San Andreas itself: what of the GTA world is under the crosshair (ground, walls, cars, props), and
// what happens when it is broken. The Minecraft side of mining is the core's (src/core/Interact.h); GtaHost.cpp
// hands these answers to it.

#include "ModCommon.h"
#include "Interact.h"

namespace mc {

// Host::Pick: the GTA thing along the ray, if it is nearer than the block hit
bool GtaPick(const PickRay& ray, const VoxelHit& blocks, Target& out);
// Host::BreakTarget: the GTA thing in gTarget was mined (a hole opens, the car or prop goes, drops appear)
void GtaBreakTarget();
// Host::CellBlocked: the player, another person or a vehicle is in the cell
bool GtaCellBlocked(const Int3& c);

void UpdateBrokenVehicles(); // every frame: mined vehicles go once everyone is out
void GtaMiningClear();       // new session

} // namespace mc
