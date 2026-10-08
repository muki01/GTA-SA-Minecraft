#include "Render3D.h"

#include <d3d9.h>

#include "CCamera.h"
#include "CClock.h"
#include "CGame.h"
#include "CMirrors.h"
#include "common.h"
#include "ePedBones.h"

#include "BlockRules.h"
#include "Carve.h"
#include "GtaCombat.h"
#include "Config.h"
#include "Draw3D.h"
#include "Fishing.h"
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

// corner offsets per face: bottom-left, bottom-right, top-right, top-left (seen from outside)
static const int kFaceCorners[6][4][3] = {
    { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } }, // EAST  +X
    { { 0, 1, 0 }, { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 1 } }, // WEST  -X
    { { 1, 1, 0 }, { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 } }, // NORTH +Y
    { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 } }, // SOUTH -Y
    { { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } }, // TOP   +Z
    { { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 } }, // BOTTOM -Z
};
static const float kFaceShade[6] = { 0.6f, 0.6f, 0.8f, 0.8f, 1.0f, 0.5f };
static const float kAoLevel[4] = { 0.42f, 0.6f, 0.8f, 1.0f };

// ================================================================ meshing
namespace {
constexpr int P = CS + 2;
struct Padded {
    Voxel b[P * P * P];
    int Idx(int x, int y, int z) const { return (x + 1) + P * ((y + 1) + P * (z + 1)); }
    int At(int x, int y, int z) const { return VoxBlock(b[Idx(x, y, z)]); }
    Voxel V(int x, int y, int z) const { return b[Idx(x, y, z)]; }
};

inline bool Occludes(int block) { return IsOpaqueBlock(block); }

// glass-like blocks hide the face between two blocks of the same kind
inline bool MergesWithSame(int block) {
    const BlockDef& d = Block(block);
    return d.render == RENDER_TRANSLUCENT || (d.render == RENDER_CUTOUT && d.sound == SG_GLASS);
}

void FillPadded(const Chunk& c, Padded& pad) {
    memset(pad.b, 0, sizeof(pad.b));
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const Chunk* n = (dx == 0 && dy == 0 && dz == 0) ? &c : gWorld.FindChunk({ c.pos.x + dx, c.pos.y + dy, c.pos.z + dz });
                if (!n)
                    continue;
                int x0 = dx < 0 ? CS - 1 : 0, x1 = dx > 0 ? 0 : CS - 1;
                int y0 = dy < 0 ? CS - 1 : 0, y1 = dy > 0 ? 0 : CS - 1;
                int z0 = dz < 0 ? CS - 1 : 0, z1 = dz > 0 ? 0 : CS - 1;
                for (int z = z0; z <= z1; ++z)
                    for (int y = y0; y <= y1; ++y)
                        for (int x = x0; x <= x1; ++x)
                            pad.b[pad.Idx(x + dx * CS, y + dy * CS, z + dz * CS)] = n->Get(x, y, z);
            }
}
// fire_floor: four sheets from (0,0,8.8) to (16,22.4,8.8) and turned copies, leaning 22.5 degrees towards the
// middle with "rescale" (Minecraft model space, converted to ours: x, z -> y, y -> z)
struct FirePlane {
    float p[4][3];
    float u[4], v[4];
};

const FirePlane& FireFloorPlane(int i) {
    static FirePlane planes[4];
    static bool init = false;
    if (!init) {
        init = true;
        const float s = 1.0f / std::cos(22.5f * 3.14159265f / 180.0f);
        auto make = [&](FirePlane& fp, bool zPlane, float c, float deg) {
            const float a = deg * 3.14159265f / 180.0f, ca = std::cos(a), sa = std::sin(a);
            const float hs[4] = { 0, 16, 16, 0 }, ys[4] = { 0, 0, 22.4f, 22.4f };
            for (int k = 0; k < 4; ++k) {
                float mx = zPlane ? hs[k] : c, my = ys[k], mz = zPlane ? c : hs[k];
                float rx = mx - 8.0f, ry = my - 8.0f, rz = mz - 8.0f;
                if (zPlane) {
                    const float ny = ry * ca - rz * sa, nz = ry * sa + rz * ca;
                    ry = ny * s;
                    rz = nz * s;
                } else {
                    const float nx = rx * ca - ry * sa, ny = rx * sa + ry * ca;
                    rx = nx * s;
                    ry = ny * s;
                }
                fp.p[k][0] = (rx + 8.0f) / 16.0f;
                fp.p[k][1] = (rz + 8.0f) / 16.0f;
                fp.p[k][2] = (ry + 8.0f) / 16.0f;
                fp.u[k] = hs[k] / 16.0f;
                fp.v[k] = 1.0f - ys[k] / 22.4f;
            }
        };
        make(planes[0], true, 8.8f, -22.5f);
        make(planes[1], true, 7.2f, 22.5f);
        make(planes[2], false, 8.8f, -22.5f);
        make(planes[3], false, 7.2f, 22.5f);
    }
    return planes[i & 3];
}
} // namespace

void BuildChunkMesh(Chunk& c) {
    c.meshDirty = false;
    if (!c.mesh)
        c.mesh = std::make_unique<ChunkMesh>();
    ChunkMesh& m = *c.mesh;
    m.verts.clear();
    m.shade.clear();
    m.emissive.clear();
    m.anim.clear();
    m.nverts.clear();
    m.nshade.clear();
    m.tverts.clear();
    m.tshade.clear();
    m.tanim.clear();

    static Padded pad;
    FillPadded(c, pad);
    const float bx = (float)(c.pos.x * CS), by = (float)(c.pos.y * CS), bz = (float)(c.pos.z * CS);

    auto pushVertex = [](std::vector<RwIm3DVertex>& vv, float x, float y, float z, float u, float v, const Int3& n) {
        RwIm3DVertex vert;
        vert.objVertex.x = x;
        vert.objVertex.y = y;
        vert.objVertex.z = z;
        vert.objNormal.x = (float)n.x;
        vert.objNormal.y = (float)n.y;
        vert.objNormal.z = (float)n.z;
        vert.color = 0xFFFFFFFF;
        vert.u = u;
        vert.v = v;
        vv.push_back(vert);
    };

    for (int z = 0; z < CS; ++z)
        for (int y = 0; y < CS; ++y)
            for (int x = 0; x < CS; ++x) {
                Voxel vox = c.Get(x, y, z);
                int block = VoxBlock(vox);
                if (block == ID_AIR)
                    continue;
                const BlockDef& def = Block(block);
                const int meta = VoxMeta(vox);
                const float wx = bx + x, wy = by + y, wz = bz + z;

                // ---------------- plants: two crossed quads
                if (def.shape == SHAPE_CROSS) {
                    TileUV uv = AtlasTileUV(def.tex[FACE_SOUTH]);
                    const float k = 0.15f; // MC: 0.5 +- 0.45*sqrt2/2
                    const float q[2][4] = { { k, k, 1 - k, 1 - k }, { 1 - k, k, k, 1 - k } };
                    for (auto& d : q) {
                        const Int3 n{ 0, 0, 1 };
                        pushVertex(m.verts, wx + d[0], wy + d[1], wz, uv.u0, uv.v1, n);
                        pushVertex(m.verts, wx + d[2], wy + d[3], wz, uv.u1, uv.v1, n);
                        pushVertex(m.verts, wx + d[2], wy + d[3], wz + 1, uv.u1, uv.v0, n);
                        pushVertex(m.verts, wx + d[0], wy + d[1], wz + 1, uv.u0, uv.v0, n);
                        for (int i = 0; i < 4; ++i) {
                            m.shade.push_back(230);
                            m.emissive.push_back(0);
                            m.anim.push_back(0);
                        }
                    }
                    continue;
                }

                // ---------------- fluids (LiquidBlockRenderer)
                if (def.shape == SHAPE_FLUID) {
                    const bool water = block == ID_WATER;
                    // getHeight: 1 under the same fluid, its own level, 0 for air, -1 for solid blocks
                    auto H = [&](int px, int py, int pz) -> float {
                        const Voxel nv = pad.V(px, py, pz);
                        const int nb = VoxBlock(nv);
                        if (nb == block)
                            return pad.At(px, py, pz + 1) == block ? 1.0f : FluidOwnHeight(nv);
                        return IsSolidBlock(nb) ? -1.0f : 0.0f;
                    };
                    const bool above = pad.At(x, y, z + 1) == block;
                    const float h0 = above ? 1.0f : FluidOwnHeight(vox);
                    // calculateAverageHeight for the corner towards (sx, sy)
                    auto corner = [&](int sx, int sy) -> float {
                        if (above)
                            return 1.0f;
                        const float hA = H(x + sx, y, z), hB = H(x, y + sy, z);
                        if (hA >= 1.0f || hB >= 1.0f)
                            return 1.0f;
                        float sum = 0.0f, wgt = 0.0f;
                        auto add = [&](float h) {
                            if (h >= 0.8f) {
                                sum += h * 10.0f;
                                wgt += 10.0f;
                            } else if (h >= 0.0f) {
                                sum += h;
                                wgt += 1.0f;
                            }
                        };
                        if (hA > 0.0f || hB > 0.0f) {
                            const float hD = H(x + sx, y + sy, z);
                            if (hD >= 1.0f)
                                return 1.0f;
                            add(hD);
                        }
                        add(h0);
                        add(hA);
                        add(hB);
                        return wgt > 0.0f ? sum / wgt : h0;
                    };
                    // corner heights indexed [cx][cy]
                    const float ch[2][2] = { { corner(-1, -1), corner(-1, 1) }, { corner(1, -1), corner(1, 1) } };
                    const uint8_t stillAnim = water ? ANIM_WATER_STILL : ANIM_LAVA_STILL;
                    const uint8_t flowAnim = water ? ANIM_WATER_FLOW : ANIM_LAVA_FLOW;
                    auto& vv = water ? m.tverts : m.verts;
                    auto& ss = water ? m.tshade : m.shade;
                    auto& aa = water ? m.tanim : m.anim;
                    // sprite rectangle of an animation's first frame
                    auto sprite = [](uint8_t anim, float* u0, float* v0, float* size) {
                        const AnimDef& a = kAnims[anim];
                        *u0 = (a.tile % ATLAS_TILES_PER_ROW) * 16.0f / ATLAS_SIZE;
                        *v0 = (a.tile / ATLAS_TILES_PER_ROW) * 16.0f / ATLAS_SIZE;
                        *size = (float)a.px / ATLAS_SIZE;
                    };
                    auto quad = [&](const float (*p)[3], const float* us, const float* vs, float shade, uint8_t anim,
                                    const Int3& n) {
                        for (int i = 0; i < 4; ++i) {
                            pushVertex(vv, wx + p[i][0], wy + p[i][1], wz + p[i][2], us[i], vs[i], n);
                            ss.push_back((uint8_t)Clamp(shade * 255.0f, 0.0f, 255.0f));
                            aa.push_back(anim);
                            if (!water)
                                m.emissive.push_back(1);
                        }
                    };
                    // top: still texture, or the flowing one turned along the flow
                    if (!above) {
                        const float p[4][3] = { { 0, 0, ch[0][0] }, { 1, 0, ch[1][0] }, { 1, 1, ch[1][1] }, { 0, 1, ch[0][1] } };
                        const Vec3 flow =
                            FluidFlowT(block, vox, [&](int dx, int dy, int dz) { return pad.V(x + dx, y + dy, z + dz); });
                        float su, sv, sz;
                        float us[4], vs[4];
                        if (flow.x == 0.0f && flow.y == 0.0f) {
                            sprite(stillAnim, &su, &sv, &sz);
                            const float cu[4] = { 0, 1, 1, 0 }, cv[4] = { 1, 1, 0, 0 };
                            for (int i = 0; i < 4; ++i) {
                                us[i] = su + cu[i] * sz;
                                vs[i] = sv + cv[i] * sz;
                            }
                            quad(p, us, vs, 1.0f, stillAnim, FACE_DIR[FACE_TOP]);
                        } else {
                            sprite(flowAnim, &su, &sv, &sz);
                            // texture v runs with the flow, u across it; the middle half of the 32 px frame
                            const float ax = flow.y, ay = -flow.x;
                            for (int i = 0; i < 4; ++i) {
                                const float rx = p[i][0] - 0.5f, ry = p[i][1] - 0.5f;
                                us[i] = su + (0.5f + (rx * ax + ry * ay) * 0.5f) * sz;
                                vs[i] = sv + (0.5f + (rx * flow.x + ry * flow.y) * 0.5f) * sz;
                            }
                            quad(p, us, vs, 1.0f, flowAnim, FACE_DIR[FACE_TOP]);
                        }
                    }
                    // sides: the top left quarter of the flowing texture, cut at the fluid's height
                    for (int f = 0; f < 4; ++f) {
                        const Int3& d = FACE_DIR[f];
                        const int nb = pad.At(x + d.x, y + d.y, z + d.z);
                        if (nb == block || Occludes(nb))
                            continue;
                        float p[4][3], us[4], vs[4];
                        float su, sv, sz;
                        sprite(flowAnim, &su, &sv, &sz);
                        for (int k = 0; k < 4; ++k) {
                            const int* cc = kFaceCorners[f][k];
                            const float h = cc[2] ? ch[cc[0]][cc[1]] : 0.0f;
                            p[k][0] = (float)cc[0];
                            p[k][1] = (float)cc[1];
                            p[k][2] = h;
                            us[k] = su + (k == 1 || k == 2 ? 0.5f : 0.0f) * sz;
                            vs[k] = sv + (cc[2] ? (1.0f - h) * 0.5f : 0.5f) * sz;
                        }
                        quad(p, us, vs, kFaceShade[f], flowAnim, d);
                    }
                    const int below = pad.At(x, y, z - 1);
                    if (below != block && !Occludes(below)) {
                        const float p[4][3] = { { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 } };
                        float su, sv, sz;
                        sprite(stillAnim, &su, &sv, &sz);
                        const float us[4] = { su, su + sz, su + sz, su }, vs[4] = { sv + sz, sv + sz, sv, sv };
                        quad(p, us, vs, 0.5f, stillAnim, FACE_DIR[FACE_BOTTOM]);
                    }
                    continue;
                }

                // ---------------- fire: the four leaning flames of fire_floor, or flames on burning walls
                if (def.shape == SHAPE_FIRE) {
                    const uint8_t anim = (((int)wx * 31 + (int)wy * 17 + (int)wz * 7) & 1) ? ANIM_FIRE_1 : ANIM_FIRE_0;
                    TileUV uv = AtlasTileUV(kAnims[anim].tile);
                    auto plane = [&](const float (*p)[3], const float* cu, const float* cv) {
                        const Int3 n{ 0, 0, 1 };
                        for (int i = 0; i < 4; ++i) {
                            pushVertex(m.verts, wx + p[i][0], wy + p[i][1], wz + p[i][2], uv.u0 + cu[i] * (uv.u1 - uv.u0),
                                       uv.v0 + cv[i] * (uv.v1 - uv.v0), n);
                            m.shade.push_back(255);
                            m.emissive.push_back(1);
                            m.anim.push_back(anim);
                        }
                    };
                    bool sides = false;
                    const int below = pad.At(x, y, z - 1);
                    if (!IsSolidBlock(below) && FireIgnite(below) == 0)
                        for (int f = 0; f < 4; ++f) {
                            const Int3& d = FACE_DIR[f];
                            const int nb = pad.At(x + d.x, y + d.y, z);
                            if (!IsSolidBlock(nb) || FireIgnite(nb) == 0)
                                continue;
                            sides = true;
                            // fire_side: a sheet of flame just in front of the burning wall
                            float p[4][3];
                            for (int k = 0; k < 4; ++k) {
                                const int* cc = kFaceCorners[f][k];
                                p[k][0] = d.x != 0 ? (d.x > 0 ? 0.99f : 0.01f) : (float)cc[0];
                                p[k][1] = d.y != 0 ? (d.y > 0 ? 0.99f : 0.01f) : (float)cc[1];
                                p[k][2] = cc[2] ? 1.4f : 0.0f;
                            }
                            const float cu[4] = { 0, 1, 1, 0 }, cv[4] = { 1, 1, 0, 0 };
                            plane(p, cu, cv);
                        }
                    if (!sides) {
                        for (int i = 0; i < 4; ++i) {
                            const FirePlane& fp = FireFloorPlane(i);
                            plane(fp.p, fp.u, fp.v);
                        }
                    }
                    continue;
                }

                // ---------------- cubes
                const bool translucent = def.render == RENDER_TRANSLUCENT;
                // the dug ground's blocks are only seen through the holes; those inside buildings and cliffs are
                // drawn like any other (the GTA model around them is cut open where they show)
                const bool natural = (meta & META_NATURAL) != 0 && TerrainOwnsCell((int)wx, (int)wy, (int)wz);
                const float depthShade = natural ? TerrainDepthShade((int)wx, (int)wy, (int)wz) : 1.0f;
                for (int f = 0; f < 6; ++f) {
                    const Int3& d = FACE_DIR[f];
                    int nb = pad.At(x + d.x, y + d.y, z + d.z);
                    if (nb != ID_AIR) {
                        if (Occludes(nb))
                            continue;
                        if (nb == block && MergesWithSame(block))
                            continue;
                    }
                    TileUV uv = AtlasTileUV(BlockFaceTile(block, f, meta));
                    // texture coords per corner (BL, BR, TR, TL), optionally turned 90 degrees
                    float us[4] = { uv.u0, uv.u1, uv.u1, uv.u0 };
                    float vs[4] = { uv.v1, uv.v1, uv.v0, uv.v0 };
                    if (BlockFaceRotated(block, f, meta)) {
                        const float ru[4] = { uv.u1, uv.u1, uv.u0, uv.u0 };
                        const float rv[4] = { uv.v1, uv.v0, uv.v0, uv.v1 };
                        memcpy(us, ru, sizeof(us));
                        memcpy(vs, rv, sizeof(vs));
                    }

                    int ao[4];
                    for (int k = 0; k < 4; ++k) {
                        if (def.emissive || translucent) {
                            ao[k] = 3;
                            continue;
                        }
                        const int* cc = kFaceCorners[f][k];
                        int lx = x + d.x, ly = y + d.y, lz = z + d.z;
                        int axes[2], n = 0;
                        for (int a = 0; a < 3; ++a) {
                            int comp = a == 0 ? d.x : (a == 1 ? d.y : d.z);
                            if (comp == 0)
                                axes[n++] = a;
                        }
                        int da[3] = { 0, 0, 0 }, db[3] = { 0, 0, 0 };
                        da[axes[0]] = cc[axes[0]] ? 1 : -1;
                        db[axes[1]] = cc[axes[1]] ? 1 : -1;
                        int s0 = Occludes(pad.At(lx + da[0], ly + da[1], lz + da[2]));
                        int s1 = Occludes(pad.At(lx + db[0], ly + db[1], lz + db[2]));
                        int s2 = Occludes(pad.At(lx + da[0] + db[0], ly + da[1] + db[1], lz + da[2] + db[2]));
                        ao[k] = (s0 && s1) ? 0 : 3 - (s0 + s1 + s2);
                    }
                    int order[4] = { 0, 1, 2, 3 };
                    if (ao[0] + ao[2] < ao[1] + ao[3]) {
                        order[0] = 1; order[1] = 2; order[2] = 3; order[3] = 0;
                    }
                    auto& verts = natural ? m.nverts : (translucent ? m.tverts : m.verts);
                    auto& shade = natural ? m.nshade : (translucent ? m.tshade : m.shade);
                    for (int oi = 0; oi < 4; ++oi) {
                        int k = order[oi];
                        const int* cc = kFaceCorners[f][k];
                        pushVertex(verts, wx + cc[0], wy + cc[1], wz + cc[2], us[k], vs[k], d);
                        float s = def.emissive ? 1.0f : kFaceShade[f] * kAoLevel[ao[k]] * depthShade;
                        shade.push_back((uint8_t)Clamp(s * 255.0f, 0.0f, 255.0f));
                        if (natural)
                            continue;
                        if (translucent) {
                            m.tanim.push_back(0);
                        } else {
                            m.emissive.push_back(def.emissive ? 1 : 0);
                            m.anim.push_back(0);
                        }
                    }
                }
            }
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
