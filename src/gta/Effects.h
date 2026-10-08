#pragma once
// The GTA side of the status effects and the air supply: runs the core's rules (Survival.h) on the player's ped,
// finds out whether his head is under water and shows the bubbles.

#include "ModCommon.h"
#include "Survival.h"

class CPlayerPed;

namespace mc {

// regeneration, poison, hunger, fire resistance and drowning (script phase)
void EffectsUpdate(float dt, CPlayerPed* ped);
// ends every effect, on the ped too
void ClearEffects(CPlayerPed* ped);
// Fire resistance is GTA's fireproof flag on the ped: call after the core cleared the effects (milk, totem) with
// whether the player had fire resistance before.
void FireResistanceCleared(CPlayerPed* ped, bool hadIt);

} // namespace mc
