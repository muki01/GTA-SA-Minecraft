#include "BlockMesh.h"

#include <cmath>
#include <cstring>

#include "BlockRules.h"
#include "Host.h"
#include "Items.h"

namespace mc {

const int kFaceCorners[6][4][3] = {
    { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } }, // EAST  +X
    { { 0, 1, 0 }, { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 1 } }, // WEST  -X
    { { 1, 1, 0 }, { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 } }, // NORTH +Y
    { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 } }, // SOUTH -Y
    { { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } }, // TOP   +Z
    { { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 } }, // BOTTOM -Z
};
const float kFaceShade[6] = { 0.6f, 0.6f, 0.8f, 0.8f, 1.0f, 0.5f };
namespace {
const float kAoLevel[4] = { 0.42f, 0.6f, 0.8f, 1.0f };
} // namespace

TileUV AtlasTileUV(int tile) {
    const float ts = 16.0f / ATLAS_SIZE;
    const float inset = 0.02f / ATLAS_SIZE;
    int tx = tile % ATLAS_TILES_PER_ROW, ty = tile / ATLAS_TILES_PER_ROW;
    return { tx * ts + inset, ty * ts + inset, (tx + 1) * ts - inset, (ty + 1) * ts - inset };
}

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

void BlockMesh::Clear() {
    verts.clear();
    shade.clear();
    emissive.clear();
    anim.clear();
    nverts.clear();
    nshade.clear();
    tverts.clear();
    tshade.clear();
    tanim.clear();
}

void MeshChunk(const Chunk& c, BlockMesh& m) {
    m.Clear();

    static Padded pad;
    FillPadded(c, pad);
    const float bx = (float)(c.pos.x * CS), by = (float)(c.pos.y * CS), bz = (float)(c.pos.z * CS);

    auto pushVertex = [](std::vector<MeshVertex>& vv, float x, float y, float z, float u, float v, const Int3& n) {
        vv.push_back({ x, y, z, u, v, n });
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
                // the host's dug ground is only seen through its holes; what it owns elsewhere is drawn like any
                // other block
                float depthShade = 1.0f;
                const bool natural = (meta & META_NATURAL) != 0 && TheHost().OwnGround(Int3{ (int)wx, (int)wy, (int)wz }, &depthShade);
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

} // namespace mc
