#pragma once
// The GTA side of the fishing rod (src/core/Fishing.h has the rod itself).

#include "ModCommon.h"

#include "Host.h"

namespace mc {

// asked by the core (Host, see GtaHost.cpp)
HostHit GtaHookTrace(const CVector& from, const CVector& dir, float len);
bool GtaHookPoint(const HostHit& hooked, CVector& at);
bool GtaFling(const HostHit& what, const CVector& velocity);

void FishingRender(float light);

} // namespace mc
