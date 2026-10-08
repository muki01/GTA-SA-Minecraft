#pragma once
// Beds, as in Minecraft: the use button on one makes it the respawn point and, at night (or in a thunderstorm) with no
// monsters near, puts the player to sleep. Five seconds later the night is over: the morning comes, the sky clears.
// Dying, he comes back at his bed, as long as it still stands.

#include "Core.h"

namespace mc {

struct SleepState {
    bool asleep = false;
    float timer = 0.0f;    // seconds in bed
    float wake = 0.0f;     // the dark fading away after getting up
    float fadeFrom = 0.0f; // (how dark it was)
    Int3 bed;              // the foot of the bed he lies in
    int meta = 0;          // (its side)
    bool spawnSet = false;
    Int3 spawn;            // the foot of the bed he comes back at
};
extern SleepState gSleep;

constexpr float kFallAsleep = 5.0f; // 100 ticks
bool CanSleepAt(float hours);       // the clock (0..24) says night

bool UseBed(const Int3& cell); // false: no bed there
// every frame; leave: he gets up (a key, or he cannot lie there any more). true while he lies in bed
bool SleepTick(float dt, bool leave);
void WakeUp();
bool SleepSpot(Vec3* feet);                 // where he lies (standing there, his feet would be here)
bool SleepPose(Vec3* feet, Vec3* headDir);  // how his model lies: on its back, the head on the pillow
float SleepFade();                          // the dark over the screen, 0..1
void BedRespawn();                          // he came back after dying: to his bed

} // namespace mc
