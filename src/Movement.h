#pragma once

#include "ModCommon.h"

class CPlayerPed;

namespace mc {

// Minecraft movement: jumping, sprinting, sneaking, swimming keys, creative flight (double space),
// elytra gliding with firework boosts and riding animals. Runs in the script phase, before GTA's physics.
// after CGame::Process: puts the player back where our physics says (GTA only animates)
void MovementAfterProcess(CPlayerPed* ped);
void InstallMovementHooks(); // hook after CWorld::Process
void UpdateMovement(float dt, CPlayerPed* ped);
// Pad changes that must come after the mod blocked GTA's action buttons (diving while swimming).
void LateMovementInput(CPlayerPed* ped);
void StopFlying(CPlayerPed* ped);
bool FireworkBoost(); // true if a boost was started (player is gliding)
bool PlayerOnGround(CPlayerPed* ped);
bool MovementControllerActive(); // the Minecraft physics controller moves the player (velocity in gGame.flyVel)
// how fast the player (or the car he sits in) moves, m/s
CVector PlayerVelocity(CPlayerPed* ped);
// Throws the player (wind charge); no fall damage for that flight.
void LaunchPlayer(CPlayerPed* ped, const CVector& velocity);
void StopRiding(CPlayerPed* ped);

} // namespace mc
