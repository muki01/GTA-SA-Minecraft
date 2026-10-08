#pragma once
// Common header of the GTA SA side of the mod (src/gta): plugin-sdk plus the core's own basics (src/core/Core.h).
// The core never includes this file.

#include "plugin.h"

#include "Core.h"

namespace mc {

std::string ModPath(const char* file); // <game dir>/MinecraftSA/<file>
std::string GameDir();

// Seconds of game time elapsed this frame (respects GTA pause / slow motion).
float FrameDelta();

// A core vector as GTA's. (Both turn into each other by themselves, see Vec3; this says it out loud where the
// compiler cannot guess which of the two is wanted.)
inline CVector ToGta(const Vec3& v) { return CVector(v.x, v.y, v.z); }
// sums of one of each are GTA vectors
inline CVector operator+(const Vec3& a, const CVector& b) { return CVector(a.x + b.x, a.y + b.y, a.z + b.z); }
inline CVector operator+(const CVector& a, const Vec3& b) { return CVector(a.x + b.x, a.y + b.y, a.z + b.z); }
inline CVector operator-(const Vec3& a, const CVector& b) { return CVector(a.x - b.x, a.y - b.y, a.z - b.z); }
inline CVector operator-(const CVector& a, const Vec3& b) { return CVector(a.x - b.x, a.y - b.y, a.z - b.z); }

} // namespace mc
