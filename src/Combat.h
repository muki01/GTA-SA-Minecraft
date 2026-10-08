#pragma once

#include "ModCommon.h"

class CPlayerPed;

namespace mc {

// Melee: returns true if something was hit (the click is used up).
bool TryMeleeAttack(CPlayerPed* ped);
// Items used by holding the right mouse button: bow, crossbow, trident, spyglass.
// Returns true while the right button belongs to one of them.
bool UpdateChargedItems(float dt, CPlayerPed* ped, bool rmbDown, bool rmbPressed);
// Right click with a special item; returns true if the item did something.
bool UseSpecialItem(CPlayerPed* ped, bool targetValid, bool targetVoxel, const Int3& targetPos, const CVector& hitPoint,
                    const CVector& hitNormal);
void UpdateProjectiles(float dt, CPlayerPed* ped);
void RenderProjectiles(float light);
void RenderLightning();
void UpdatePedLoot();
void ClearProjectiles();
// Turns a TNT block into a primed one; without a velocity it does Minecraft's little hop.
void IgniteTnt(const Int3& p, float fuse, const CVector* velocity = nullptr);
void StrikeLightning(const CVector& at);
bool LightningFlashActive();
void UpdateCarBoost(float dt, CPlayerPed* ped);
// Police and gang guns shoot arrows, the police helicopter too (engine hooks).
void InstallCombatHooks();

} // namespace mc
