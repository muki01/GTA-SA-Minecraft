#pragma once
// The GTA side of fighting (src/core/Combat.h has the blows, the shots and the items).

#include "ModCommon.h"

#include "Host.h"

class CPlayerPed;

namespace mc {

// ---- asked by the core (Host, see GtaHost.cpp)
float GtaAimDistance(const CVector& origin, const CVector& dir, float reach, float nothing);
HostHit GtaBlowTrace(const CVector& origin, const CVector& dir, float reach);
HostHit GtaShotTrace(const CVector& from, const CVector& dir, float len, const ShotOwner& by);
void GtaHurtBeing(int being, float halfHearts, int how, const ShotOwner* by);
void GtaPushBeing(int being, const CVector& velocity);
void GtaHurtVehicle(int vehicle, float halfHearts);
void GtaGust(const CVector& centre, float radius, float side, float up, bool playerToo);
void GtaIgnite(const CVector& at, float seconds, int spread);
bool GtaPlaceVehicle(int kind, const CVector& at, float headingDeg);
void GtaBoostVehicle(float seconds);
bool GtaItemAttack(int special);
bool GtaItemUse(int special);

void UpdateCarBoost(float dt, CPlayerPed* ped);
void UpdatePedLoot();
void RenderProjectiles(float light);
void RenderLightning();
void GtaCombatClear();
// Police and gang guns shoot arrows, the police helicopter too; punches reach the player (engine hooks).
void InstallCombatHooks();

} // namespace mc
