#pragma once

#include "ModCommon.h"

namespace mc {

// Third-person Steve model (replaces CJ when Steve mode is on) and the elytra.
void RenderPlayerModel(float light);
// Steve sitting in a vehicle: drawn from the vehicle render hook so the glass ends up in front of him.
void RenderPlayerInVehicle(float light);
// First-person arm / held item, drawn on top of the world (2D phase).
void RenderFirstPersonHand();
// Called once per frame from the script phase: the core's animation clocks (PlayerAnimTick).
void UpdatePlayerAnimation(float dt);

} // namespace mc
