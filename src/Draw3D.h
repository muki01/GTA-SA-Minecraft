#pragma once
// Small RenderWare Im3D batching helper shared by all 3D drawing code.

#include "ModCommon.h"
#include "RenderWare.h"

namespace mc {
namespace d3 {

inline RwUInt32 Argb(int r, int g, int b, int a = 255) {
    return ((RwUInt32)(a & 255) << 24) | ((RwUInt32)(r & 255) << 16) | ((RwUInt32)(g & 255) << 8) | (RwUInt32)(b & 255);
}
inline RwUInt32 Gray(float v, int a = 255) {
    int c = (int)Clamp(v * 255.0f, 0.0f, 255.0f);
    return Argb(c, c, c, a);
}

// Set the texture used by the following quads (flushes when it changes).
void SetRaster(RwRaster* raster);
// Append one quad (p0..p3 in order) with UVs.
void Quad(const CVector& p0, const CVector& p1, const CVector& p2, const CVector& p3, float u0, float v0, float u1,
          float v1, RwUInt32 col);
// Quad with explicit per-corner UVs
void QuadUV(const CVector* p, const float* u, const float* v, RwUInt32 col);
// Raw access for meshes that are already built
RwIm3DVertex* AllocQuad();
void Flush();

// Common render-state setups (saved/restored by the caller with StateGuard)
void StateOpaque();      // z test+write, alpha test
void StateTranslucent(); // z test, no z write, blending
struct StateGuard {
    void *zw, *zt, *va, *src, *dst, *cull, *fog, *flt, *ras, *atf, *atr;
    StateGuard();
    ~StateGuard();
};

// Builds an oriented box (pixels -> world) and emits its 6 faces with skin-style UVs.
struct BoxUV { float u, v; float w, h, d; }; // texture origin and box size in texels
struct Frame {
    CVector o, r, u, f; // origin and unit axes (right, up, forward), already scaled
    CVector P(float x, float y, float z) const { return o + r * x + u * y + f * z; }
};
void SkinBox(const Frame& fr, float x0, float y0, float z0, float x1, float y1, float z1, const BoxUV& uv, float texW,
             float texH, float light, int alpha = 255, bool mirror = false, float tr = 1.0f, float tg = 1.0f,
             float tb = 1.0f);

} // namespace d3
} // namespace mc
