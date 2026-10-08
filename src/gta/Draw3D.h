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

} // namespace d3
} // namespace mc
