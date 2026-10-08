#pragma once
// Minecraft animals living in the GTA world (cow, pig, sheep, chicken).

#include "ModCommon.h"

class CPlayerPed;

namespace mc {

struct MobHit {
    int index = -1;
    float dist = 0.0f;
    CVector point;
};

void MobsUpdate(float dt, CPlayerPed* player);
void MobsRender(float light);
void MobsClear();
int MobCount();
// kind: MOB_* from McModel.h. Returns the index or -1.
int SpawnMob(int kind, const CVector& feet, bool persistent, bool baby = false);
MobHit MobsRaycast(const CVector& origin, const CVector& dir, float maxDist);
// halfHearts of damage coming from `from`; knock = knockback strength (1 = a normal hit)
void MobHurt(int index, float halfHearts, const CVector& from, float knock);
void MobPush(int index, const CVector& velocity); // m/s added to the animal
// explosions and shock waves: damage and push fall off with the distance
void MobsRadial(const CVector& at, float radius, float halfHearts, float push);
// right click with the held item (shears, bucket, food); true if the click was used
bool MobInteract(int index, CPlayerPed* player);
CVector MobCentre(int index);
// stable ids (indices change when animals despawn)
uint32_t MobIdAt(int index);
int MobIndexById(uint32_t id);
// The player rides this animal: it walks with `velocity` (m/s). Returns false when it is gone.
bool MobRide(uint32_t id, const CVector& velocity, float dt, CVector* seat, float* yaw);

} // namespace mc
