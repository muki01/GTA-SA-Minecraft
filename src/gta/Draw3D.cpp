#include "Draw3D.h"

#include "McModel.h"
#include "Textures.h"

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


namespace {
// the core's models (McModel.h) are drawn through here
struct Im3DSink : ModelSink {
    void Texture(int tex) override { SetRaster(tex == MT_ATLAS ? gAtlasTex.Raster() : gEntityTex.Raster()); }
    void Quad(const Vec3* p, const float* u, const float* v, uint32_t color) override {
        const CVector q[4] = { ToGta(p[0]), ToGta(p[1]), ToGta(p[2]), ToGta(p[3]) };
        QuadUV(q, u, v, color);
    }
    bool Opaque(int tile, int px, int py) override { return AtlasPixelOpaque(tile, px, py); }
} gSink;
struct SinkRegistration {
    SinkRegistration() { SetModelSink(&gSink); }
} gSinkRegistration;
} // namespace

} // namespace d3
} // namespace mc
