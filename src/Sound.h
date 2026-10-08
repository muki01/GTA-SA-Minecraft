#pragma once

#include "GameTables.h"
#include "ModCommon.h"

namespace mc {

// Minecraft sounds through BASS (bass.dll ships with CLEO in the game folder).
void PlaySfx(SoundEvent ev, const CVector* pos = nullptr, float volume = 1.0f, float pitch = 1.0f);
void SetLoopSfx(SoundEvent ev, bool on, float volume = 1.0f); // one looping slot (elytra wind)
void PauseAllSfx(bool pause);
void ShutdownSfx();

} // namespace mc
