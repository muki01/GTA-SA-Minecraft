#pragma once

#include "plugin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "generated/Assets.h"

namespace mc {

struct Int3 {
    int x = 0, y = 0, z = 0;
    bool operator==(const Int3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Int3& o) const { return !(*this == o); }
    Int3 operator+(const Int3& o) const { return { x + o.x, y + o.y, z + o.z }; }
};

struct Int3Hash {
    size_t operator()(const Int3& p) const {
        uint64_t h = (uint64_t)(uint32_t)p.x * 73856093ull ^ (uint64_t)(uint32_t)p.y * 19349663ull ^
                     (uint64_t)(uint32_t)p.z * 83492791ull;
        return (size_t)(h ^ (h >> 29));
    }
};

inline int FloorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
inline int FloorMod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }
inline int FloorI(float f) { return (int)std::floor(f); }
inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Smoothstep(float a, float b, float x) {
    float t = Clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Six face directions. Order is used everywhere (meshing, textures, raycast normals).
enum Face : int { FACE_EAST = 0, FACE_WEST, FACE_NORTH, FACE_SOUTH, FACE_TOP, FACE_BOTTOM };
constexpr Int3 FACE_DIR[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

void Log(const char* fmt, ...);
std::string ModPath(const char* file); // <game dir>\MinecraftSA\<file>
std::string GameDir();

// Seconds of game time elapsed this frame (respects GTA pause / slow motion).
float FrameDelta();

} // namespace mc
