#pragma once
// Experience: orbs that fly to the player, the green bar and the level number.

#include "ModCommon.h"

class CPlayerPed;

namespace mc {

void SpawnXp(const CVector& at, int amount);
void XpUpdate(float dt, CPlayerPed* ped);
void XpRender(float light);
void XpClear();
void AddXp(int points);
int XpForBlock(int block); // ores give experience when mined
int XpToNextLevel(int level);

} // namespace mc
