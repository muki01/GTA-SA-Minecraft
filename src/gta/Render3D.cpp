#include "Render3D.h"

#include <d3d9.h>

#include "CCamera.h"
#include "CClock.h"
#include "CGame.h"
#include "CMirrors.h"
#include "common.h"
#include "ePedBones.h"

#include "BlockMesh.h"
#include "BlockRules.h"
#include "Carve.h"
#include "GtaCombat.h"
#include "Config.h"
#include "Draw3D.h"
#include "GtaFishing.h"
#include "GameState.h"
#include "Items.h"
#include "McModel.h"
#include "GtaMobs.h"
#include "PedSkins.h"
#include "Player3D.h"
#include "Terrain.h"
#include "Xp.h"
#include "safetyhook.hpp"
#include "Textures.h"

namespace mc {

// the world's chunks carry their mesh (see World.h): made and freed here, where it is known what a mesh is
Chunk::Chunk() = default;
Chunk::~Chunk() = default;

BlockTargetVisual gTargetVisual;

namespace {
struct ShadowReq {
    CVector pos;
    float radius, strength;
};
std::vector<ShadowReq> gShadows;
} // namespace

void AddShadow(const CVector& ground, float radius, float strength) {
    if (strength > 0.02f && gShadows.size() < 256)
        gShadows.push_back({ ground, radius, strength });
}

// ================================================================ meshing
namespace {
void ToRw(const std::vector<MeshVertex>& from, std::vector<RwIm3DVertex>& to) {
    to.resize(from.size());
    for (size_t i = 0; i < from.size(); ++i) {
        const MeshVertex& s = from[i];
        RwIm3DVertex& vert = to[i];
        vert.objVertex.x = s.x;
        vert.objVertex.y = s.y;
        vert.objVertex.z = s.z;
        vert.objNormal.x = (float)s.n.x;
        vert.objNormal.y = (float)s.n.y;
        vert.objNormal.z = (float)s.n.z;
        vert.color = 0xFFFFFFFF;
        vert.u = s.u;
        vert.v = s.v;
    }
}
} // namespace

// the core's mesh of the chunk, in RenderWare's vertices
static void BuildChunkMesh(Chunk& c) {
    c.meshDirty = false;
    if (!c.mesh)
        c.mesh = std::make_unique<ChunkMesh>();
    ChunkMesh& m = *c.mesh;
    static BlockMesh bm;
    MeshChunk(c, bm);
    ToRw(bm.verts, m.verts);
    ToRw(bm.nverts, m.nverts);
    ToRw(bm.tverts, m.tverts);
    m.shade = bm.shade;
    m.emissive = bm.emissive;
    m.anim = bm.anim;
    m.nshade = bm.nshade;
    m.tshade = bm.tshade;
    m.tanim = bm.tanim;
}

float DaylightFactor() {
    if (CGame::currArea != 0)
        return 1.0f;
    float t = CClock::ms_nGameClockHours + CClock::ms_nGameClockMinutes / 60.0f;
    float sun = Smoothstep(5.0f, 7.0f, t) * (1.0f - Smoothstep(19.5f, 21.5f, t));
    return 0.28f + 0.72f * sun;
}

// ================================================================ item helpers
void EmitItemCube(const CVector& c, float half, const CVector& r, const CVector& u, const CVector& f, int block,
                  float light) {
    for (int face = 0; face < 6; ++face) {
        TileUV uv = AtlasTileUV(BlockFaceTile(block, face, Block(block).shape == SHAPE_FACING ? 2 : 0));
        float us[4] = { uv.u0, uv.u1, uv.u1, uv.u0 };
        float vs[4] = { uv.v1, uv.v1, uv.v0, uv.v0 };
        float s = Block(block).emissive ? 1.0f : light * kFaceShade[face];
        CVector p[4];
        for (int k = 0; k < 4; ++k) {
            const int* cc = kFaceCorners[face][k];
            p[k] = c + r * ((cc[0] * 2 - 1) * half) + f * ((cc[1] * 2 - 1) * half) + u * ((cc[2] * 2 - 1) * half);
        }
        d3::QuadUV(p, us, vs, d3::Gray(s));
    }
}

void EmitItemSprite(const CVector& c, const CVector& au, const CVector& av, float half, uint16_t tile, float light) {
    TileUV uv = AtlasTileUV(tile);
    // texture v runs downwards, av points up
    d3::Quad(c - au * half + av * half, c + au * half + av * half, c + au * half - av * half, c - au * half - av * half,
             uv.u0, uv.v0, uv.u1, uv.v1, d3::Gray(light));
}

// ================================================================ passes
namespace {
struct Vis {
    Chunk* c;
    float d2;
};
std::vector<Vis> gVisible;

void CollectChunks() {
    CVector cam = TheCamera.GetPosition();
    float maxDist = gConfig.renderDistance;
    gVisible.clear();
    for (auto& kv : gWorld.chunks) {
        Chunk* c = kv.second.get();
        if (c->nonAir == 0)
            continue;
        CVector center(c->pos.x * CS + CS * 0.5f, c->pos.y * CS + CS * 0.5f, c->pos.z * CS + CS * 0.5f);
        CVector d = center - cam;
        float d2 = d.x * d.x + d.y * d.y + d.z * d.z;
        if (d2 > (maxDist + 14.0f) * (maxDist + 14.0f))
            continue;
        if (d2 > 30.0f * 30.0f && !TheCamera.IsSphereVisible(center, CS * 0.8661f + 0.5f))
            continue;
        gVisible.push_back({ c, d2 });
    }
    std::sort(gVisible.begin(), gVisible.end(), [](const Vis& a, const Vis& b) { return a.d2 < b.d2; });
    int budget = 6;
    for (auto& v : gVisible)
        if ((v.c->meshDirty || !v.c->mesh) && budget-- > 0)
            BuildChunkMesh(*v.c);
}

void EmitMesh(const std::vector<RwIm3DVertex>& verts, const std::vector<uint8_t>& shade, const uint8_t* emissive,
              const uint8_t* anim, float light) {
    const float nightBlue = Clamp(light * 1.08f + 0.04f, 0.0f, 1.0f);
    const int ticks = (int)(gGame.age * 20.0f);
    float shift[ANIM_COUNT] = {};
    for (int a = 1; a < ANIM_COUNT; ++a)
        shift[a] = (float)((ticks / kAnims[a].ticks) % kAnims[a].frames) * kAnims[a].px / ATLAS_SIZE;
    for (size_t i = 0; i + 3 < verts.size(); i += 4) {
        RwIm3DVertex* q = d3::AllocQuad();
        memcpy(q, &verts[i], sizeof(RwIm3DVertex) * 4);
        for (int k = 0; k < 4; ++k) {
            float s = shade[i + k];
            int r, g, b;
            if (emissive && emissive[i + k]) {
                r = g = b = (int)s;
            } else {
                r = g = (int)(s * light);
                b = (int)(s * nightBlue);
            }
            q[k].color = d3::Argb(r, g, b, 255);
            if (anim && anim[i + k] && anim[i + k] < ANIM_COUNT)
                q[k].u += shift[anim[i + k]];
        }
    }
}

bool HasNatural(const ChunkMesh* m) { return m && !m->nverts.empty(); }

void RenderOpaqueChunks(float light, bool natural) {
    d3::SetRaster(gAtlasTex.Raster());
    for (auto& v : gVisible) {
        ChunkMesh* m = v.c->mesh.get();
        if (!m)
            continue;
        if (!m->verts.empty())
            EmitMesh(m->verts, m->shade, m->emissive.data(), m->anim.data(), light);
        if (natural && !m->nverts.empty())
            EmitMesh(m->nverts, m->nshade, nullptr, nullptr, light);
    }
    d3::Flush();
}

void RenderTranslucentChunks(float light, bool holeChunksOnly) {
    d3::SetRaster(gAtlasTex.Raster());
    for (auto it = gVisible.rbegin(); it != gVisible.rend(); ++it) { // far to near
        ChunkMesh* m = it->c->mesh.get();
        if (!m || m->tverts.empty() || (holeChunksOnly && !HasNatural(m)))
            continue;
        EmitMesh(m->tverts, m->tshade, nullptr, m->tanim.data(), light);
    }
    d3::Flush();
}

// ---------------------------------------------------------------- holes in the GTA ground
// The stencil marks where the opening of a dug column is visible; there the depth buffer is reset so
// that the blocks under the GTA surface can be drawn.
IDirect3DDevice9* Device() { return reinterpret_cast<IDirect3DDevice9*>(GetD3DDevice()); }

int gStencilState = -1; // -1 unknown, 0 none, 1 usable

bool StencilUsable() {
    if (gStencilState >= 0)
        return gStencilState == 1;
    gStencilState = 0;
    IDirect3DSurface9* ds = nullptr;
    if (Device() && SUCCEEDED(Device()->GetDepthStencilSurface(&ds)) && ds) {
        D3DSURFACE_DESC d;
        if (SUCCEEDED(ds->GetDesc(&d)))
            gStencilState = (d.Format == D3DFMT_D24S8 || d.Format == D3DFMT_D24FS8 || d.Format == D3DFMT_D24X4S4 ||
                             d.Format == D3DFMT_D15S1) ? 1 : 0;
        Log("Depth buffer format %d: holes in the ground %s", (int)d.Format, gStencilState ? "on" : "off (no stencil)");
        ds->Release();
    }
    return gStencilState == 1;
}

// RenderWare keeps its own copy of the device states and only sends changes when it draws, so they are
// always set through it (RwD3D9SetRenderState): setting them on the device directly leaves RenderWare with a
// wrong idea of the device and breaks GTA's own drawing afterwards.
struct DeviceStates {
    static constexpr int N = 11;
    D3DRENDERSTATETYPE type[N] = { D3DRS_STENCILENABLE, D3DRS_STENCILFUNC, D3DRS_STENCILREF, D3DRS_STENCILMASK,
                                   D3DRS_STENCILWRITEMASK, D3DRS_STENCILPASS, D3DRS_STENCILFAIL, D3DRS_STENCILZFAIL,
                                   D3DRS_ZENABLE, D3DRS_ZFUNC, D3DRS_ZWRITEENABLE };
    RwUInt32 value[N] = {};
    void Save() {
        for (int i = 0; i < N; ++i)
            RwD3D9GetRenderState(type[i], &value[i]);
    }
    void Restore() {
        for (int i = 0; i < N; ++i)
            RwD3D9SetRenderState(type[i], value[i]);
    }
};

// stencil bit: where the dug-open ground's space can be seen
constexpr DWORD kStencilGround = 1;

void Stencil(DWORD func, DWORD pass, DWORD ref = 1, DWORD mask = 0xFF) {
    RwD3D9SetRenderState(D3DRS_STENCILENABLE, TRUE);
    RwD3D9SetRenderState(D3DRS_STENCILFUNC, func);
    RwD3D9SetRenderState(D3DRS_STENCILREF, ref);
    RwD3D9SetRenderState(D3DRS_STENCILMASK, mask);
    RwD3D9SetRenderState(D3DRS_STENCILWRITEMASK, mask);
    RwD3D9SetRenderState(D3DRS_STENCILPASS, pass);
    RwD3D9SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
    RwD3D9SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
}

// The GTA ground of the dug-open columns is cut out of its model (GeoCut), so the depth buffer already shows what
// lies behind. The blocks of the dug ground are only drawn where the open columns' space can be seen: under the
// GTA ground they would show through its slopes everywhere else.
// Returns true when the stencil was used (the caller must clean up).
bool RenderHoles(float light, const CVector& cam, DeviceStates& saved) {
    const float radius = gConfig.renderDistance;
    if (!gConfig.groundHoles || !TerrainAnyOpening(cam, radius) || !StencilUsable())
        return false;
    IDirect3DDevice9* dev = Device();
    d3::Flush();
    saved.Save();
    dev->Clear(0, nullptr, D3DCLEAR_STENCIL, 0, 1.0f, 0);

    // 1. mark where the open columns' space is seen (nothing of it is drawn: only the stencil counts)
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDZERO);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
    RwD3D9SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    d3::SetRaster(nullptr);
    Stencil(D3DCMP_ALWAYS, D3DSTENCILOP_REPLACE, kStencilGround, kStencilGround);
    TerrainEmitHoleVolume(cam, radius + 8.0f);
    d3::Flush();

    // 2. the ground's blocks there
    Stencil(D3DCMP_EQUAL, D3DSTENCILOP_KEEP, kStencilGround, kStencilGround);
    d3::StateOpaque();
    d3::SetRaster(gAtlasTex.Raster());
    for (auto& v : gVisible) {
        ChunkMesh* m = v.c->mesh.get();
        if (HasNatural(m))
            EmitMesh(m->nverts, m->nshade, nullptr, nullptr, light);
    }
    TerrainEmitSkirts(cam, radius, light);
    TerrainEmitCapBottoms(cam, radius, light);
    d3::Flush();
    RwD3D9SetRenderState(D3DRS_STENCILENABLE, FALSE);
    return true;
}

void RenderDropsAndParticles(float light) {
    CVector cam = TheCamera.GetPosition();
    const CMatrix& cm = TheCamera.m_mCameraMatrix;
    CVector camRight = cm.right * -1.0f; // GTA keeps the camera's left vector in "right"
    CVector camUp = cm.at;
    d3::SetRaster(gAtlasTex.Raster());
    for (auto& d : gDrops) {
        CVector diff = d.pos - cam;
        if (diff.x * diff.x + diff.y * diff.y + diff.z * diff.z > 80.0f * 80.0f)
            continue;
        // ItemEntityRenderer: bob, spin, then the model's "ground" transform
        const uint16_t id = d.stack.id;
        if (!IsValidItem(id))
            continue;
        const float groundScale = kDisplays[kItemDisplay[id]].ground.scale[1];
        Pose p = WorldPose(d.pos);
        p.Translate(0.0f, std::sin(d.age * 2.0f + d.spin) * 0.1f + 0.1f + 0.25f * groundScale, 0.0f);
        p.RotY(d.age + d.spin);
        int copies = d.stack.count > 48 ? 5 : d.stack.count > 32 ? 4 : d.stack.count > 16 ? 3 : d.stack.count > 1 ? 2 : 1;
        const bool flat = !IsBlockItem(id);
        if (flat)
            p.Translate(0.0f, 0.0f, -0.09375f * groundScale * (copies - 1) * 0.5f);
        static const float kJitter[5][3] = { { 0, 0, 0 }, { 0.11f, 0.07f, -0.09f }, { -0.1f, 0.12f, 0.08f },
                                             { 0.06f, -0.04f, 0.13f }, { -0.12f, 0.03f, -0.11f } };
        for (int i = 0; i < copies; ++i) {
            Pose q = p;
            if (flat)
                q.Translate(0.0f, 0.0f, 0.09375f * groundScale * i);
            else
                q.Translate(kJitter[i][0], kJitter[i][1], kJitter[i][2]);
            ApplyDisplay(q, id, CTX_GROUND);
            DrawItemModel(q, id, light);
        }
        AddShadow(CVector(d.pos.x, d.pos.y, d.pos.z + 0.01f), 0.15f, 0.6f);
    }
    d3::SetRaster(gAtlasTex.Raster());
    for (auto& t : gPrimedTnt) {
        // TntRenderer: swells just before the blast and blinks white
        float half = 0.49f;
        if (t.fuse < 0.5f) {
            float g = Clamp(1.0f - t.fuse / 0.5f, 0.0f, 1.0f);
            g *= g;
            half *= 1.0f + g * g * 0.3f;
        }
        const CVector c = t.pos + CVector(0, 0, 0.5f);
        EmitItemCube(c, half, CVector(1, 0, 0), CVector(0, 0, 1), CVector(0, 1, 0), ID_TNT, light);
        if (((int)(t.fuse * 20.0f) / 5) % 2 == 0) {
            d3::SetRaster(gEntityTex.Raster());
            const float h = half + 0.004f;
            const float u = (ENT_WHITE.x + 4.0f) / ENT_TEX_W, v = (ENT_WHITE.y + 4.0f) / ENT_TEX_H;
            const RwUInt32 white = d3::Argb(255, 255, 255, 150);
            for (int face = 0; face < 6; ++face) {
                CVector q[4];
                for (int k = 0; k < 4; ++k) {
                    const int* cc = kFaceCorners[face][k];
                    q[k] = c + CVector((cc[0] * 2 - 1) * h, (cc[1] * 2 - 1) * h, (cc[2] * 2 - 1) * h);
                }
                d3::Quad(q[0], q[1], q[2], q[3], u, v, u, v, white);
            }
            d3::SetRaster(gAtlasTex.Raster());
        }
        AddShadow(CVector(t.pos.x, t.pos.y, t.pos.z + 0.01f), 0.5f, t.onGround ? 1.0f : 0.5f);
    }
    static const uint16_t kSpark[8] = { TILE_P_SPARK_0, TILE_P_SPARK_1, TILE_P_SPARK_2, TILE_P_SPARK_3,
                                        TILE_P_SPARK_4, TILE_P_SPARK_5, TILE_P_SPARK_6, TILE_P_SPARK_7 };
    static const uint16_t kSmoke[8] = { TILE_P_GENERIC_0, TILE_P_GENERIC_1, TILE_P_GENERIC_2, TILE_P_GENERIC_3,
                                        TILE_P_GENERIC_4, TILE_P_GENERIC_5, TILE_P_GENERIC_6, TILE_P_GENERIC_7 };
    for (auto& p : gParticles) {
        uint16_t tile = p.tile;
        if (p.anim) {
            int frame = std::clamp((int)((1.0f - p.life / p.maxLife) * 8.0f), 0, 7);
            tile = p.anim == 1 ? kSpark[7 - frame] : kSmoke[7 - frame];
        }
        TileUV uv = AtlasTileUV(tile);
        float tu = uv.u1 - uv.u0, tv = uv.v1 - uv.v0;
        float u0 = uv.u0 + tu * p.u, v0 = uv.v0 + tv * p.v;
        float u1 = u0 + tu * p.sub, v1 = v0 + tv * p.sub;
        CVector r = camRight * p.size, up = camUp * p.size;
        RwUInt32 col = p.color;
        if (!p.glow) {
            int a = (col >> 24) & 255, rr = (int)(((col >> 16) & 255) * light), gg = (int)(((col >> 8) & 255) * light),
                bb = (int)((col & 255) * light);
            col = d3::Argb(rr, gg, bb, a);
        }
        d3::Quad(p.pos - r + up, p.pos + r + up, p.pos + r - up, p.pos - r - up, u0, v0, u1, v1, col);
    }
    d3::Flush();
}

void RenderTarget() {
    if (!gTargetVisual.show)
        return;
    const Int3& p = gTargetVisual.pos;
    const float e = 0.004f;
    float x0 = p.x - e, y0 = p.y - e, z0 = p.z - e, x1 = p.x + 1 + e, y1 = p.y + 1 + e, z1 = p.z + 1 + e;
    if (gTargetVisual.progress > 0.0f) {
        int stage = (int)Clamp(gTargetVisual.progress * 10.0f, 0.0f, 9.0f);
        TileUV uv = AtlasTileUV(TILE_DESTROY_0 + stage);
        float us[4] = { uv.u0, uv.u1, uv.u1, uv.u0 };
        float vs[4] = { uv.v1, uv.v1, uv.v0, uv.v0 };
        // Minecraft multiplies the cracks onto the block
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDDESTCOLOR);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDSRCCOLOR);
        d3::SetRaster(gAtlasTex.Raster());
        for (int f = 0; f < 6; ++f) {
            CVector c[4];
            for (int k = 0; k < 4; ++k) {
                const int* cc = kFaceCorners[f][k];
                c[k] = CVector(cc[0] ? x1 : x0, cc[1] ? y1 : y0, cc[2] ? z1 : z0);
            }
            d3::QuadUV(c, us, vs, d3::Argb(255, 255, 255, 255));
        }
        d3::Flush();
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    }
    RwIm3DVertex v[8];
    for (int i = 0; i < 8; ++i) {
        v[i].objVertex.x = (i & 1) ? x1 : x0;
        v[i].objVertex.y = (i & 2) ? y1 : y0;
        v[i].objVertex.z = (i & 4) ? z1 : z0;
        v[i].objNormal.x = v[i].objNormal.y = 0.0f;
        v[i].objNormal.z = 1.0f;
        v[i].color = d3::Argb(0, 0, 0, 160);
        v[i].u = v[i].v = 0.0f;
    }
    static RwImVertexIndex lines[24] = { 0, 1, 2, 3, 4, 5, 6, 7, 0, 2, 1, 3, 4, 6, 5, 7, 0, 4, 1, 5, 2, 6, 3, 7 };
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nullptr);
    if (RwIm3DTransform(v, 8, nullptr, rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA)) {
        RwIm3DRenderIndexedPrimitive(rwPRIMTYPELINELIST, lines, 24);
        RwIm3DEnd();
    }
}

void RenderShadows() {
    if (gShadows.empty())
        return;
    d3::SetRaster(gEntityTex.Raster());
    const float u0 = (ENT_SHADOW.x + 0.5f) / ENT_TEX_W, v0 = (ENT_SHADOW.y + 0.5f) / ENT_TEX_H;
    const float u1 = (ENT_SHADOW.x + ENT_SHADOW.w - 0.5f) / ENT_TEX_W, v1 = (ENT_SHADOW.y + ENT_SHADOW.h - 0.5f) / ENT_TEX_H;
    for (auto& sh : gShadows) {
        const float r = sh.radius;
        const CVector c(sh.pos.x, sh.pos.y, sh.pos.z + 0.03f);
        d3::Quad(c + CVector(-r, r, 0), c + CVector(r, r, 0), c + CVector(r, -r, 0), c + CVector(-r, -r, 0), u0, v0, u1, v1,
                 d3::Argb(0, 0, 0, (int)(Clamp(sh.strength, 0.0f, 1.0f) * 105.0f)));
    }
    d3::Flush();
    gShadows.clear();
}
} // namespace

namespace {
SafetyHookInline gFadingHook;

// CRenderer::RenderFadingInEntities: vehicles (and everything see-through) come after the world's solid parts. The
// mod's world is drawn just before them, so that car windows show it behind them (glass writes depth).
void __cdecl HookRenderFading() {
    if (!CMirrors::bRenderingReflection)
        Render3D();
    gFadingHook.ccall<void>();
}
} // namespace

void Render3DInit() {
    StencilUsable();
    if (!gFadingHook) {
        gFadingHook = safetyhook::create_inline(reinterpret_cast<void*>(0x5531E0), reinterpret_cast<void*>(&HookRenderFading));
        Log("Hooks: world drawing before vehicles %s", gFadingHook ? "ok" : "FAILED");
    }
}

bool Render3DHooked() { return (bool)gFadingHook; }

void Render3D() {
    if (!gAtlasTex.tex)
        return;
    float light = DaylightFactor();
    d3::StateGuard guard;
    DeviceStates devStates; // only touched when holes are drawn
    d3::StateOpaque();

    gShadows.clear();
    CollectChunks();
    const CVector cam = TheCamera.GetPosition();
    const bool underground = TerrainCameraUnderground(cam) || CarveInside(cam);
    RenderOpaqueChunks(light, underground);
    d3::SetRaster(gAtlasTex.Raster());
    // the rims of holes broken into buildings (their GTA triangles are cut out: depth is right everywhere)
    CarveEmitCaps(cam, gConfig.renderDistance + 40.0f, light);
    if (underground) {
        TerrainEmitSkirts(cam, gConfig.renderDistance, light);
        TerrainEmitCapBottoms(cam, gConfig.renderDistance, light);
    }
    d3::Flush();
    const bool holes = Device() && RenderHoles(light, cam, devStates);
    RenderDropsAndParticles(light);
    XpRender();
    RenderPlayerModel(light);
    MobsRender(light);
    PedSkinsRender(light);
    RenderProjectiles(light);
    FishingRender(light);
    RenderFallingBlocks(light);

    d3::StateTranslucent();
    RenderTranslucentChunks(light, false);
    if (holes) {
        d3::Flush();
        Device()->Clear(0, nullptr, D3DCLEAR_STENCIL, 0, 1.0f, 0);
    }
    RenderShadows();
    RenderLightning();
    RenderTarget();
    d3::Flush();
    if (holes)
        devStates.Restore();
}

} // namespace mc
