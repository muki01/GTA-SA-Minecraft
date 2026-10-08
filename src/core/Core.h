#pragma once
// The Minecraft side of the mod ("core"): game rules and data that know nothing about the game they run in.
// Nothing in src/core may include GTA or plugin-sdk headers - the core is built as its own library without them
// (CMake target mc_core), so a slip does not compile. What the core needs from the game it runs in ("the host":
// GTA SA, the offline tests, some day another game) is declared here and defined there.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
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

// A point or direction in the world (metres, z up). Any vector of the host with x, y and z (GTA's CVector) turns
// into one by itself and back, so the host's code can use the core's vectors as if they were its own.
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    template <class V, class = std::enable_if_t<std::is_floating_point_v<std::decay_t<decltype(std::declval<const V&>().x)>> &&
                                                std::is_floating_point_v<std::decay_t<decltype(std::declval<const V&>().z)>>>>
    Vec3(const V& v) : x((float)v.x), y((float)v.y), z((float)v.z) {}
    // ...and back: wherever the host wants its own vector, this one will do
    template <class V, class = std::enable_if_t<std::is_class_v<V> && !std::is_aggregate_v<V> && !std::is_same_v<V, Vec3> &&
                                                std::is_floating_point_v<std::decay_t<decltype(std::declval<const V&>().x)>> &&
                                                std::is_floating_point_v<std::decay_t<decltype(std::declval<const V&>().z)>> &&
                                                std::is_constructible_v<V, float, float, float>>>
    operator V() const { return V(x, y, z); }
    Vec3 operator-() const { return { -x, -y, -z }; }
    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    float Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    float Length() const { return std::sqrt(x * x + y * y + z * z); }
};

inline Vec3 Cross(const Vec3& a, const Vec3& b) { return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }

constexpr float kPi = 3.14159265358979f;

inline int FloorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
inline int FloorMod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }
inline int FloorI(float f) { return (int)std::floor(f); }
inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Smoothstep(float a, float b, float x) {
    float t = Clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float Rand01(); // a random number, 0 <= r < 1

// Six face directions. Order is used everywhere (meshing, textures, raycast normals).
enum Face : int { FACE_EAST = 0, FACE_WEST, FACE_NORTH, FACE_SOUTH, FACE_TOP, FACE_BOTTOM };
constexpr Int3 FACE_DIR[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

// ---- provided by the host
void Log(const char* fmt, ...); // a line for the log file

} // namespace mc
