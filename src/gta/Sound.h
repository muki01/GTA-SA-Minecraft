#pragma once
// How the Minecraft sounds get out of the speakers in GTA SA: through BASS (bass.dll ships with CLEO in the game
// folder). Which sound plays when is the core's business (src/core/Audio.h declares PlaySfx and SetLoopSfx; they
// are defined here).

#include <cstddef>

#include "Audio.h"
#include "ModCommon.h"

namespace mc {

// the same with a position in GTA's own vector type
inline void PlaySfx(SoundEvent ev, const CVector* pos, float volume = 1.0f, float pitch = 1.0f) {
    if (!pos) {
        PlaySfx(ev, (const Vec3*)nullptr, volume, pitch);
        return;
    }
    const Vec3 at = *pos;
    PlaySfx(ev, &at, volume, pitch);
}
inline void PlaySfx(SoundEvent ev, std::nullptr_t, float volume = 1.0f, float pitch = 1.0f) {
    PlaySfx(ev, (const Vec3*)nullptr, volume, pitch);
}

void PauseAllSfx(bool pause);
void ShutdownSfx();

} // namespace mc
