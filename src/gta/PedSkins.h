#pragma once
// GTA pedestrians drawn as Minecraft villagers, police as pillagers, gang members as vindicators.

#include "ModCommon.h"

class CEntity;
class CPed;
class CPlayerPed;
class CVehicle;

namespace mc {

void PedSkinsUpdate(float dt, CPlayerPed* player); // script phase: hides the GTA models, advances animation
void PedSkinsAfterProcess(CPlayerPed* player);     // after the world moved: skeletons of the hidden peds
void PedSkinsRender(float light);            // peds on foot
void RenderVehicleOccupants(CVehicle* veh);  // drivers / passengers, before their vehicle is drawn
void PedSkinsForget();  // the game deleted all peds (new game / load)

// Switches a ped's GTA model on / off.
void SetPedDrawn(CPed* ped, bool drawn);
// Seated in a vehicle: where the hip is and how small a model must be so its head stays under the roof.
// headAboveHipPx = model pixels from the hip to the top of the head.
float SeatedFit(CPed* ped, const CVector& up, float headAboveHipPx, CVector* hip);

// Ray against standing peds (simple boxes, works for hidden peds too).
struct PedHit {
    CPed* ped = nullptr;
    float dist = 0.0f;
    CVector point;
};
// `seated`: people in cars and on bikes count too (around their seat)
PedHit RaycastPeds(const CVector& origin, const CVector& dir, float maxDist, const CEntity* ignore, bool includePlayer,
                   bool seated = false);

// Right click on a villager: trade emeralds. Returns true if the click was used.
bool VillagerInteract(CPed* ped);

// Throws a ped through the air (velocity in m/s). Shared by the magic stick, wind charges and the fishing rod.
void LaunchPed(CPed* ped, const CVector& velocity);
// Peds, vehicles and loose props.
void LaunchEntity(CEntity* e, const CVector& velocity);
void UpdateLaunchedPeds(float dt);

} // namespace mc
