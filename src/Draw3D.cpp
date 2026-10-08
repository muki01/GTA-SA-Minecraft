#include "Draw3D.h"

namespace mc {
namespace d3 {

namespace {
constexpr int kQuads = 512;
RwIm3DVertex gV[kQuads * 4];
RwImVertexIndex gIdx[kQuads * 6];
int gN = 0;
bool gIdxInit = false;
RwRaster* gRaster = nullptr;

void InitIdx() {
    if (gIdxInit)
        return;
    for (int q = 0; q < kQuads; ++q) {
        gIdx[q * 6 + 0] = (RwImVertexIndex)(q * 4 + 0);
        gIdx[q * 6 + 1] = (RwImVertexIndex)(q * 4 + 1);
        gIdx[q * 6 + 2] = (RwImVertexIndex)(q * 4 + 2);
        gIdx[q * 6 + 3] = (RwImVertexIndex)(q * 4 + 0);
        gIdx[q * 6 + 4] = (RwImVertexIndex)(q * 4 + 2);
        gIdx[q * 6 + 5] = (RwImVertexIndex)(q * 4 + 3);
    }
    gIdxInit = true;
}

inline void SetV(RwIm3DVertex& v, const CVector& p, float u, float vv, RwUInt32 col) {
    v.objVertex.x = p.x;
    v.objVertex.y = p.y;
    v.objVertex.z = p.z;
    v.objNormal.x = 0.0f;
    v.objNormal.y = 0.0f;
    v.objNormal.z = 1.0f;
    v.color = col;
    v.u = u;
    v.v = vv;
}
} // namespace

void Flush() {
    if (gN == 0)
        return;
    InitIdx();
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, gRaster);
    if (RwIm3DTransform(gV, gN, nullptr, rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA)) {
        RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, gIdx, gN / 4 * 6);
        RwIm3DEnd();
    }
    gN = 0;
}

void SetRaster(RwRaster* raster) {
    if (raster != gRaster) {
        Flush();
        gRaster = raster;
    }
}

RwIm3DVertex* AllocQuad() {
    if (gN + 4 > kQuads * 4)
        Flush();
    RwIm3DVertex* q = &gV[gN];
    gN += 4;
    return q;
}

void Quad(const CVector& p0, const CVector& p1, const CVector& p2, const CVector& p3, float u0, float v0, float u1,
          float v1, RwUInt32 col) {
    RwIm3DVertex* q = AllocQuad();
    SetV(q[0], p0, u0, v0, col);
    SetV(q[1], p1, u1, v0, col);
    SetV(q[2], p2, u1, v1, col);
    SetV(q[3], p3, u0, v1, col);
}

void QuadUV(const CVector* p, const float* u, const float* v, RwUInt32 col) {
    RwIm3DVertex* q = AllocQuad();
    for (int i = 0; i < 4; ++i)
        SetV(q[i], p[i], u[i], v[i], col);
}

void StateOpaque() {
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONGREATER);
    RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)100);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
}

void StateTranslucent() {
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONGREATER);
    RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)2);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
}

StateGuard::StateGuard() {
    RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &zw);
    RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &zt);
    RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &va);
    RwRenderStateGet(rwRENDERSTATESRCBLEND, &src);
    RwRenderStateGet(rwRENDERSTATEDESTBLEND, &dst);
    RwRenderStateGet(rwRENDERSTATECULLMODE, &cull);
    RwRenderStateGet(rwRENDERSTATEFOGENABLE, &fog);
    RwRenderStateGet(rwRENDERSTATETEXTUREFILTER, &flt);
    RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &ras);
    RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &atf);
    RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, &atr);
    gRaster = nullptr;
    gN = 0;
}

StateGuard::~StateGuard() {
    Flush();
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, zw);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, zt);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, va);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, src);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, dst);
    RwRenderStateSet(rwRENDERSTATECULLMODE, cull);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, fog);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, flt);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, ras);
    RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, atf);
    RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, atr);
    gRaster = nullptr;
}

void SkinBox(const Frame& fr, float x0, float y0, float z0, float x1, float y1, float z1, const BoxUV& b, float texW,
             float texH, float light, int alpha, bool mirror, float tr, float tg, float tb) {
    // texture rectangles (texels) for the six faces, Minecraft skin layout
    struct Face { float u, v, w, h; CVector c[4]; CVector n; };
    const float U = b.u, V = b.v, w = b.w, h = b.h, d = b.d;
    Face faces[6] = {
        { U + d, V + d, w, h, { fr.P(x1, y1, z1), fr.P(x0, y1, z1), fr.P(x0, y0, z1), fr.P(x1, y0, z1) }, fr.f },          // front
        { U + 2 * d + w, V + d, w, h, { fr.P(x0, y1, z0), fr.P(x1, y1, z0), fr.P(x1, y0, z0), fr.P(x0, y0, z0) }, fr.f * -1.0f }, // back
        { U, V + d, d, h, { fr.P(x1, y1, z0), fr.P(x1, y1, z1), fr.P(x1, y0, z1), fr.P(x1, y0, z0) }, fr.r },              // right
        { U + d + w, V + d, d, h, { fr.P(x0, y1, z1), fr.P(x0, y1, z0), fr.P(x0, y0, z0), fr.P(x0, y0, z1) }, fr.r * -1.0f }, // left
        { U + d, V, w, d, { fr.P(x1, y1, z0), fr.P(x0, y1, z0), fr.P(x0, y1, z1), fr.P(x1, y1, z1) }, fr.u },              // top
        { U + d + w, V, w, d, { fr.P(x1, y0, z1), fr.P(x0, y0, z1), fr.P(x0, y0, z0), fr.P(x1, y0, z0) }, fr.u * -1.0f },  // bottom
    };
    for (auto& fc : faces) {
        float u0 = fc.u / texW, v0 = fc.v / texH, u1 = (fc.u + fc.w) / texW, v1 = (fc.v + fc.h) / texH;
        if (mirror)
            std::swap(u0, u1);
        CVector n = fc.n;
        float len = n.Magnitude();
        float nz = len > 0 ? n.z / len : 0.0f;
        float shade = light * (0.72f + 0.28f * nz);
        float us[4] = { u0, u1, u1, u0 };
        float vs[4] = { v0, v0, v1, v1 };
        QuadUV(fc.c, us, vs, Argb((int)Clamp(shade * tr * 255.0f, 0, 255), (int)Clamp(shade * tg * 255.0f, 0, 255),
                                 (int)Clamp(shade * tb * 255.0f, 0, 255), alpha));
    }
}

} // namespace d3
} // namespace mc
