#pragma once
// The GTA side of the animals (src/core/Mobs.h has the animals themselves).

#include "ModCommon.h"

class CPlayerPed;

namespace mc {

void MobsUpdate(float dt, CPlayerPed* player); // herds appear, cars hit animals, the animals live a frame
void MobsRender(float light);
bool GtaAnimalSpawnGround(const CVector& from, CVector& out);

} // namespace mc
