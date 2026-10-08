#pragma once
// Sounds. Which sound plays when is decided by the rules here in the core; getting it out of the speakers is the
// host's job: it defines these functions (GTA SA: src/gta/Sound.cpp; the offline tests: a list of what played).

#include "Core.h"
#include "GameTables.h"

namespace mc {

// pos = nullptr: at the listener (the player's own sounds, the user interface)
void PlaySfx(SoundEvent ev, const Vec3* pos = nullptr, float volume = 1.0f, float pitch = 1.0f);
void SetLoopSfx(SoundEvent ev, bool on, float volume = 1.0f); // one looping slot (elytra wind)

} // namespace mc
