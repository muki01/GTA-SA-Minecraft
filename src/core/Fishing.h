#pragma once
// Minecraft's fishing rod: the bobber is cast, floats, a fish comes and bites, and reeling in brings what hangs on the
// hook: a catch, an animal, or one of the host's people, vehicles and loose things (found through Host::HookTrace).
// The host draws the bobber and the line (it reads gBobber).

#include "Core.h"
#include "Host.h"

namespace mc {

enum BobState { BOB_FLYING, BOB_FLOATING, BOB_STUCK, BOB_HOOKED_MOB, BOB_HOOKED_BEING, BOB_HOOKED_VEHICLE, BOB_HOOKED_OBJECT };

struct Bobber {
    bool active = false;
    int state = BOB_FLYING;
    Vec3 pos, vel;
    float waterZ = 0.0f;
    float wait = 0.0f;     // until a fish notices the bait
    float approach = 0.0f; // the fish is swimming towards the bobber
    float approachAngle = 0.0f;
    float nibble = 0.0f;   // time left to reel the fish in
    float dip = 0.0f;
    float age = 0.0f;
    uint32_t mobId = 0;    // the animal on the hook
    HostHit hooked;        // or one of the host's things
};
extern Bobber gBobber;

bool FishingUse(); // the use button with the rod: cast, or reel in
void FishingTick(float dt);
bool FishingIsCast();
void FishingClear();

} // namespace mc
