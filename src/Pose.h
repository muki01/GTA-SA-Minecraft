#pragma once
// A tiny affine transform stack entry that mirrors Minecraft's PoseStack maths, so model and
// item transforms can be ported one to one (post-multiplied: every call affects what is drawn next).

#include <cmath>

#include "ModCommon.h"

namespace mc {

constexpr float kPi = 3.14159265358979f;
inline float Rad(float deg) { return deg * (kPi / 180.0f); }

struct Pose {
    // world = o + X*x + Y*y + Z*z
    CVector X{ 1, 0, 0 }, Y{ 0, 1, 0 }, Z{ 0, 0, 1 }, o{ 0, 0, 0 };

    CVector P(float x, float y, float z) const { return o + X * x + Y * y + Z * z; }
    CVector D(float x, float y, float z) const { return X * x + Y * y + Z * z; }

    void Translate(float x, float y, float z) { o = P(x, y, z); }
    void Scale(float x, float y, float z) {
        X = X * x;
        Y = Y * y;
        Z = Z * z;
    }
    void Scale(float s) { Scale(s, s, s); }
    void RotX(float a) {
        float c = std::cos(a), s = std::sin(a);
        CVector y = Y * c + Z * s, z = Z * c - Y * s;
        Y = y;
        Z = z;
    }
    void RotY(float a) {
        float c = std::cos(a), s = std::sin(a);
        CVector x = X * c - Z * s, z = X * s + Z * c;
        X = x;
        Z = z;
    }
    void RotZ(float a) {
        float c = std::cos(a), s = std::sin(a);
        CVector x = X * c + Y * s, y = Y * c - X * s;
        X = x;
        Y = y;
    }
    // Quaternionf.rotationZYX(z, y, x) and rotationXYZ(x, y, z)
    void RotZYX(float z, float y, float x) {
        if (z != 0.0f) RotZ(z);
        if (y != 0.0f) RotY(y);
        if (x != 0.0f) RotX(x);
    }
    void RotXYZ(float x, float y, float z) {
        if (x != 0.0f) RotX(x);
        if (y != 0.0f) RotY(y);
        if (z != 0.0f) RotZ(z);
    }
};

// Minecraft model space (x = model's left, y = down, z = back; units: blocks) for a character
// standing at `feet` with unit axes right / up / forward. `scale` = metres per block.
inline Pose EntityPose(const CVector& feet, const CVector& right, const CVector& up, const CVector& forward, float scale) {
    Pose p;
    p.X = right * -scale;
    p.Y = up * -scale;
    p.Z = forward * -scale;
    p.o = feet + up * (1.501f * scale);
    return p;
}

// Minecraft world axes (x east, y up, z south) placed at a GTA position.
inline Pose WorldPose(const CVector& at) {
    Pose p;
    p.X = CVector(1, 0, 0);
    p.Y = CVector(0, 0, 1);
    p.Z = CVector(0, -1, 0);
    p.o = at;
    return p;
}

// Minecraft view space (x right, y up, z towards the viewer).
inline Pose ViewPose(const CVector& eye, const CVector& right, const CVector& up, const CVector& forward) {
    Pose p;
    p.X = right;
    p.Y = up;
    p.Z = forward * -1.0f;
    p.o = eye;
    return p;
}

} // namespace mc
