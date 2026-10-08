#pragma once
// The fishing rod: cast the bobber, wait for a bite, reel in.

#include "ModCommon.h"

class CPlayerPed;

namespace mc {

bool FishingUse(CPlayerPed* ped); // right click with the rod: cast or reel in
void FishingUpdate(float dt, CPlayerPed* ped);
void FishingRender(float light);
bool FishingIsCast();
void FishingClear();

} // namespace mc
