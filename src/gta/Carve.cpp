#include "Carve.h"

#include <deque>
#include <unordered_map>
#include <unordered_set>

#include "CCamera.h"
#include "CColPoint.h"
#include "CEntity.h"
#include "CGame.h"
#include "CWorld.h"
#include "common.h"

#include "Config.h"
#include "Draw3D.h"
#include "GtaTex.h"
#include "GtaWorld.h"
#include "Items.h"
#include "Soup.h"
#include "Terrain.h"
#include "Textures.h"
#include "World.h"

namespace mc {

namespace {
// GTA surfaces that cross a carved cell but reach a little outside it: the cut is stretched over them along the
// axis they face most, so no edge of them is left standing in the hole.
struct Slab {
    uint8_t axis = 0; // 0 x, 1 y, 2 z
    float lo = 0, hi = 0;
};
// The cross-section of the GTA model where a hole is cut into it ("capping", as CAD programs show cut solids): the
// parts of the hole's boundary that lie inside the model are closed with the block's texture. Only seen from the hole.
struct CapRect {
    uint8_t fa;       // the boundary face (FACE_* order: the solid lies beyond it on that side)
    float w;          // where its plane is on axis fa / 2
    float a0, a1;     // its extent on axis (fa / 2 + 1) % 3
    float b0, b1;     // and on axis (fa / 2 + 2) % 3
};
struct Carved {
    uint16_t block = 0; // what the building is made of here (the blocks around are filled with it)
    uint8_t n = 0;      // slabs in use
    Slab slabs[3];
    bool capsDirty = true;
    std::vector<CapRect> caps;
};
// A cell next to a hole, as far as walking and aiming go: what of it is solid (between the GTA model's faces). 4 x 4
// columns along one axis, each solid from lo to hi: thin and thick, flat and upright pieces, steps for slopes - as
// thick as the wall, the deck or the road really is. (What is seen of it are the hole's caps.)
constexpr int kSub = 4, kCols = kSub * kSub;
struct Skin {
    uint8_t axis = 2;
    float lo[kCols] = {}, hi[kCols] = {}; // lo >= hi: nothing
    uint16_t surfLo = 0, surfHi = 0;      // bit k: that end lies on a GTA face (shown only where the face is cut)
    uint16_t block = 0;
    bool Has(int k) const { return hi[k] > lo[k] + 1e-4f; }
};

std::unordered_map<Int3, Carved, Int3Hash> gCarved;
std::unordered_map<Int3, Skin, Int3Hash> gSkins;
uint32_t gVersion = 1;
struct PendingFill {
    Int3 cell;
    uint16_t block;
};
std::deque<PendingFill> gPendingFill; // cells opened by explosions whose surroundings are still to be worked out

constexpr Int3 kAxis[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
constexpr float kSlabMargin = 0.12f;
constexpr float kShell = 0.15f; // a GTA face with nothing solid behind it (roads, bridge decks): a layer this thick
constexpr float kReach = 0.06f; // a line reaches this far past the cell: faces right on its border count
constexpr float kNear = 10.0f;  // the GTA models within this distance of a cell are looked at

CVector Centre(const Int3& c) { return CVector(c.x + 0.5f, c.y + 0.5f, c.z + 0.5f); }
float Comp(const CVector& v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }
void SetComp(CVector& v, int axis, float x) {
    if (axis == 0)
        v.x = x;
    else if (axis == 1)
        v.y = x;
    else
        v.z = x;
}
int CellComp(const Int3& c, int axis) { return axis == 0 ? c.x : (axis == 1 ? c.y : c.z); }
Int3 Step(const Int3& c, int axis, int s) { return { c.x + kAxis[axis].x * s, c.y + kAxis[axis].y * s, c.z + kAxis[axis].z * s }; }
bool IsCarved(const Int3& c) { return gCarved.count(c) != 0; }

void PrepareAround(const CVector& p, float extra = 0.0f) {
    const float r = kNear + extra;
    SoupPrepare(p - CVector(r, r, r), p + CVector(r, r, r));
}

// The cell, and the cell stretched over the GTA faces that lie on or just past its border (one box per axis). A
// hair wider towards carved neighbours (no seam between two holes), a millimetre elsewhere: GTA faces lying right on
// the border (window frames, trims) go with the hole, and the GTA surface ends right where the cap begins.
int CellBoxes(const Int3& c, const Carved& cv, CutBox* out) {
    CutBox cell;
    for (int a = 0; a < 3; ++a) {
        const float base = (float)CellComp(c, a);
        cell.lo[a] = base - (IsCarved(Step(c, a, -1)) ? 0.004f : 0.001f);
        cell.hi[a] = base + 1.0f + (IsCarved(Step(c, a, 1)) ? 0.004f : 0.001f);
    }
    out[0] = cell;
    int n = 1;
    for (int axis = 0; axis < 3; ++axis) {
        float lo = cell.lo[axis], hi = cell.hi[axis];
        for (int i = 0; i < cv.n; ++i)
            if (cv.slabs[i].axis == axis) {
                lo = std::min(lo, cv.slabs[i].lo);
                hi = std::max(hi, cv.slabs[i].hi);
            }
        if (lo < cell.lo[axis] - 1e-4f || hi > cell.hi[axis] + 1e-4f) {
            CutBox b = cell;
            b.lo[axis] = lo;
            b.hi[axis] = hi;
            out[n++] = b;
        }
    }
    return n;
}

bool Overlap(const CutBox& b, const float lo[3], const float hi[3]) {
    return b.lo[0] <= hi[0] && b.hi[0] >= lo[0] && b.lo[1] <= hi[1] && b.hi[1] >= lo[1] && b.lo[2] <= hi[2] &&
           b.hi[2] >= lo[2];
}

bool InBox(const CutBox& b, const CVector& p) {
    return p.x >= b.lo[0] && p.x <= b.hi[0] && p.y >= b.lo[1] && p.y <= b.hi[1] && p.z >= b.lo[2] && p.z <= b.hi[2];
}

int DominantAxis(const CVector& n) {
    int axis = 0;
    if (std::fabs(n.y) > std::fabs(Comp(n, axis)))
        axis = 1;
    if (std::fabs(n.z) > std::fabs(Comp(n, axis)))
        axis = 2;
    return axis;
}

// The GTA face through `p` with normal `n` (along its main axis, where it runs across cell c): when it lies on the
// cell's border or just past it, the cut must reach over it. False: the face is well inside (the cell's cut takes
// it) or too far out (it belongs to the neighbour).
bool SlabFor(const Int3& c, const CVector& p, const CVector& n, Slab& s) {
    s.axis = (uint8_t)DominantAxis(n);
    const int a1 = (s.axis + 1) % 3, a2 = (s.axis + 2) % 3;
    const float na = Comp(n, s.axis), n1 = Comp(n, a1), n2 = Comp(n, a2);
    const float base = (float)CellComp(c, s.axis);
    float lo = 1e9f, hi = -1e9f;
    for (int k = 0; k < 4; ++k) {
        const float u = (float)CellComp(c, a1) + ((k & 1) ? 1.0f : 0.0f), v = (float)CellComp(c, a2) + ((k & 2) ? 1.0f : 0.0f);
        const float w = Comp(p, s.axis) - (n1 * (u - Comp(p, a1)) + n2 * (v - Comp(p, a2))) / na;
        lo = std::min(lo, w);
        hi = std::max(hi, w);
    }
    if (lo > base + 0.005f && hi < base + 0.995f)
        return false;
    s.lo = std::max(lo - 0.005f, base - kReach - 0.01f);
    s.hi = std::min(hi + 0.005f, base + 1.0f + kReach + 0.01f);
    return s.hi > s.lo;
}

void AddSlab(Carved& cv, const Slab& s) {
    ++gVersion;
    for (int i = 0; i < cv.n; ++i)
        if (cv.slabs[i].axis == s.axis && s.lo <= cv.slabs[i].hi && s.hi >= cv.slabs[i].lo) {
            cv.slabs[i].lo = std::min(cv.slabs[i].lo, s.lo);
            cv.slabs[i].hi = std::max(cv.slabs[i].hi, s.hi);
            return;
        }
    if (cv.n < 3)
        cv.slabs[cv.n++] = s;
}

const float kProbe[5][2] = { { 0, 0 }, { -0.3f, -0.3f }, { 0.3f, -0.3f }, { -0.3f, 0.3f }, { 0.3f, 0.3f } };
// a probe line through the cell along `axis`: offsets on the two other axes
CVector ProbePoint(const Int3& c, int axis, int k) {
    CVector p = Centre(c);
    const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
    SetComp(p, a1, Comp(p, a1) + kProbe[k][0]);
    SetComp(p, a2, Comp(p, a2) + kProbe[k][1]);
    return p;
}

// The GTA faces that run through cell c (and a little past it): their slabs, and what they look like
void SurfacesIn(const Int3& c, Carved& cv, uint16_t* look = nullptr) {
    static std::vector<SoupFace> f;
    for (int axis = 0; axis < 3; ++axis) {
        const float c0 = (float)CellComp(c, axis);
        for (int k = 0; k < 5; ++k) {
            const CVector p = ProbePoint(c, axis, k);
            SoupLine(axis, p, f);
            for (const SoupFace& x : f) {
                if (x.at < c0 - kReach || x.at > c0 + 1.0f + kReach)
                    continue;
                CVector at = p;
                SetComp(at, axis, x.at);
                Slab sl;
                if (SlabFor(c, at, x.n, sl))
                    AddSlab(cv, sl);
                if (look && !*look && x.block && IsSolidBlock(x.block))
                    *look = x.block;
            }
        }
    }
}

enum CellKind { CELL_AIR, CELL_FULL, CELL_PART };

// What is solid in cell q: nothing, all of it, or the columns of a rim piece (as thick as the GTA model there).
CellKind Classify(const Int3& q, Skin& s, uint16_t* look = nullptr) {
    static std::vector<SoupFace> f;
    // which way do the GTA faces in this cell run? along the axis most lines cross them on
    int counts[3] = { 0, 0, 0 };
    for (int axis = 0; axis < 3; ++axis) {
        const float c0 = (float)CellComp(q, axis);
        for (int k = 0; k < 5; ++k) {
            SoupLine(axis, ProbePoint(q, axis, k), f);
            for (const SoupFace& x : f)
                if (x.at >= c0 - kReach && x.at <= c0 + 1.0f + kReach) {
                    ++counts[axis];
                    if (look && !*look && x.block && IsSolidBlock(x.block))
                        *look = x.block;
                    break;
                }
        }
    }
    if (!counts[0] && !counts[1] && !counts[2])
        return SoupSolidAt(Centre(q)) ? CELL_FULL : CELL_AIR;
    int axis = 2;
    for (int a = 0; a < 3; ++a)
        if (counts[a] > counts[axis])
            axis = a;
    s.axis = (uint8_t)axis;
    s.surfLo = s.surfHi = 0;
    const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
    const float c0 = (float)CellComp(q, axis), c1 = c0 + 1.0f, l0 = c0 - kReach, l1 = c1 + kReach;
    bool any = false, full = true;
    for (int k = 0; k < kCols; ++k) {
        const int i = k % kSub, j = k / kSub;
        CVector p = Centre(q);
        SetComp(p, a1, CellComp(q, a1) + (i + 0.5f) / kSub);
        SetComp(p, a2, CellComp(q, a2) + (j + 0.5f) / kSub);
        SoupLine(axis, p, f);
        // the faces in reach split the line into pieces; the solid ones (looked at from all three sides) count
        float cuts[16];
        int8_t cutFacing[16];
        int n = 0;
        cuts[n] = l0;
        cutFacing[n++] = 0;
        for (const SoupFace& x : f)
            if (x.at > l0 && x.at < l1 && n < 15) {
                cuts[n] = x.at;
                cutFacing[n++] = x.facing;
            }
        cuts[n] = l1;
        cutFacing[n] = 0;
        float bestLo = 0.0f, bestHi = 0.0f;
        bool bestSurfLo = false, bestSurfHi = false;
        float runLo = 0.0f;
        bool inRun = false, runSurfLo = false;
        auto close = [&](float end, bool surf) {
            if (end - runLo > bestHi - bestLo) {
                bestLo = runLo;
                bestHi = end;
                bestSurfLo = runSurfLo;
                bestSurfHi = surf;
            }
            inRun = false;
        };
        for (int g = 0; g < n; ++g) {
            const float a = cuts[g], b = cuts[g + 1];
            if (b - a < 1e-4f)
                continue;
            SetComp(p, axis, (a + b) * 0.5f);
            const bool solid = SoupSolidAt(p);
            if (solid && !inRun) {
                inRun = true;
                runLo = a;
                runSurfLo = cutFacing[g] != 0;
            } else if (!solid && inRun) {
                close(a, cutFacing[g] != 0);
            }
        }
        if (inRun)
            close(l1, false);
        if (bestHi - bestLo < 0.02f) {
            // nothing solid: a face with open space on both sides (a road, a deck) is a thin layer behind it
            for (int g = 1; g < n; ++g)
                if (cuts[g] >= c0 - 0.001f && cuts[g] <= c1 + 0.001f) {
                    if (cutFacing[g] > 0) {
                        bestLo = cuts[g] - kShell;
                        bestHi = cuts[g];
                        bestSurfLo = false;
                        bestSurfHi = true;
                    } else {
                        bestLo = cuts[g];
                        bestHi = cuts[g] + kShell;
                        bestSurfLo = true;
                        bestSurfHi = false;
                    }
                    break;
                }
        }
        // solid that goes on past the line ends at the cell's border; a face just outside it is kept
        if (bestHi - bestLo >= 0.02f) {
            if (bestLo <= l0 + 0.001f)
                bestLo = c0;
            if (bestHi >= l1 - 0.001f)
                bestHi = c1;
            bestLo = std::max(bestLo, l0);
            bestHi = std::min(bestHi, l1);
        }
        if (bestHi - bestLo < 0.02f) {
            s.lo[k] = s.hi[k] = 0.0f;
            full = false;
            continue;
        }
        any = true;
        s.lo[k] = bestLo;
        s.hi[k] = bestHi;
        if (bestSurfLo)
            s.surfLo |= (uint16_t)(1 << k);
        if (bestSurfHi)
            s.surfHi |= (uint16_t)(1 << k);
        if (std::fabs(bestLo - c0) > 0.004f || std::fabs(bestHi - c1) > 0.004f)
            full = false;
    }
    if (!any)
        return CELL_AIR;
    return full ? CELL_FULL : CELL_PART;
}

bool SolidVoxel(const Int3& c) { return gWorld.IsSolid(c.x, c.y, c.z); }

// The inside of a GTA building as plain building blocks: wood stays wood, glass glass, bricks and stone stay; paint,
// plaster and odd stones become the plain stone, concrete or sandstone block nearest in colour.
uint16_t PlainBlock(uint16_t b) {
    if (!IsSolidBlock(b))
        return ID_STONE;
    if (b == ID_GRASS_BLOCK)
        return ID_DIRT; // under the grass it is earth
    const char* key = Block(b).key;
    if (strstr(key, "planks") || strstr(key, "_log") || strstr(key, "glass") || b == ID_BRICKS || b == ID_STONE_BRICKS ||
        b == ID_STONE || b == ID_COBBLESTONE || b == ID_SMOOTH_STONE || b == ID_SANDSTONE || b == ID_WHITE_CONCRETE ||
        b == ID_LIGHT_GRAY_CONCRETE || b == ID_GRAY_CONCRETE || b == ID_BLACK_CONCRETE || b == ID_IRON_BLOCK ||
        b == ID_DIRT || b == ID_GRASS_BLOCK || b == ID_SAND)
        return b;
    static const uint16_t kPlain[] = { ID_STONE, ID_COBBLESTONE, ID_STONE_BRICKS, ID_BRICKS, ID_SMOOTH_STONE, ID_SANDSTONE,
                                       ID_WHITE_CONCRETE, ID_LIGHT_GRAY_CONCRETE, ID_GRAY_CONCRETE, ID_BLACK_CONCRETE,
                                       ID_OAK_PLANKS, ID_SPRUCE_PLANKS, ID_DARK_OAK_PLANKS };
    const uint32_t col = kBlockColor[b];
    return NearestBlock(kPlain, (int)(sizeof(kPlain) / sizeof(kPlain[0])), (col >> 16) & 255, (col >> 8) & 255, col & 255);
}

// what appears in a cell next to a hole: a rim piece where the GTA model goes on, a block where it is solid
CellKind FillCell(const Int3& q, uint16_t block) {
    if (gCarved.count(q) || gWorld.GetBlock(q.x, q.y, q.z) != ID_AIR)
        return CELL_FULL; // (nothing to do: a hole, or a block is there)
    Skin s;
    s.block = block;
    const CellKind kind = Classify(q, s);
    gSkins.erase(q);
    if (kind == CELL_FULL) {
        for (int k = 0; k < kCols; ++k) {
            s.lo[k] = (float)CellComp(q, s.axis);
            s.hi[k] = s.lo[k] + 1.0f;
        }
        s.surfLo = s.surfHi = 0;
    }
    if (kind != CELL_AIR)
        gSkins[q] = s;
    return kind;
}

// The cells around a new hole: what of them can be stood on and broken next
void FillAround(const Int3& c, uint16_t block) {
    PrepareAround(Centre(c));
    for (const Int3& d : FACE_DIR)
        FillCell(c + d, block);
}

// the cross-section of the GTA model on the hole's boundary (see CapRect)
void ComputeCaps(const Int3& c, Carved& cv) {
    cv.caps.clear();
    cv.capsDirty = false;
    static std::vector<SoupFace> f;
    CutBox boxes[4];
    const int nb = CellBoxes(c, cv, boxes);
    for (int bi = 0; bi < nb; ++bi) {
        const CutBox& b = boxes[bi];
        // a stretched box: only where it reaches past the cell's own (the rest is the cell's)
        int S = -1;
        for (int a = 0; a < 3 && bi > 0; ++a)
            if (b.lo[a] != boxes[0].lo[a] || b.hi[a] != boxes[0].hi[a])
                S = a;
        for (int fa = 0; fa < 6; ++fa) {
            const int A = fa / 2, p1 = (A + 1) % 3, p2 = (A + 2) % 3;
            const bool pos = (fa % 2) == 0;
            if (bi == 0) {
                // onto another hole or a block: nothing to close
                const Int3 n = Step(c, A, pos ? 1 : -1);
                if (IsCarved(n) || SolidVoxel(n))
                    continue;
            }
            const float w = pos ? b.hi[A] : b.lo[A];
            if (S >= 0 && A == S && w == (pos ? boxes[0].hi[A] : boxes[0].lo[A]))
                continue; // the cell's own face
            const size_t first = cv.caps.size();
            const float beyond = w + (pos ? 0.004f : -0.004f);
            if (b.hi[p1] - b.lo[p1] < 1e-3f || b.hi[p2] - b.lo[p2] < 1e-3f)
                continue;
            // scan along the plane axis the GTA faces cross (exact there), in rows along the other
            int counts[2] = { 0, 0 };
            for (int t = 0; t < 2; ++t) {
                const int sa = t == 0 ? p1 : p2, ra = t == 0 ? p2 : p1;
                for (int k = 0; k < 2; ++k) {
                    CVector q;
                    SetComp(q, A, beyond);
                    SetComp(q, ra, b.lo[ra] + (b.hi[ra] - b.lo[ra]) * (k ? 0.75f : 0.25f));
                    SetComp(q, sa, b.lo[sa]);
                    SoupLine(sa, q, f);
                    for (const SoupFace& x : f)
                        if (x.at > b.lo[sa] && x.at < b.hi[sa])
                            ++counts[t];
                }
            }
            const int sa = counts[1] > counts[0] ? p2 : p1, ra = sa == p1 ? p2 : p1;
            const float u0 = b.lo[sa], u1 = b.hi[sa], r0 = b.lo[ra], r1 = b.hi[ra];
            const int rows = std::clamp((int)std::ceil((r1 - r0) * 8.0f - 0.01f), 1, 8);
            for (int r = 0; r < rows; ++r) {
                const float v0 = r0 + (r1 - r0) * r / rows, v1 = r0 + (r1 - r0) * (r + 1) / rows;
                CVector q;
                SetComp(q, A, beyond);
                SetComp(q, ra, (v0 + v1) * 0.5f);
                SetComp(q, sa, u0);
                SoupLine(sa, q, f);
                float cuts[18];
                int n = 0;
                cuts[n++] = u0;
                for (const SoupFace& x : f)
                    if (x.at > u0 + 1e-4f && x.at < u1 - 1e-4f && n < 17)
                        cuts[n++] = x.at;
                cuts[n] = u1;
                float runLo = 0.0f;
                bool inRun = false;
                auto emit = [&](float e0, float e1) {
                    if (e1 - e0 < 1e-3f || (u1 - u0 > 0.2f && e1 - e0 < 0.03f))
                        return;
                    CapRect cr;
                    cr.fa = (uint8_t)fa;
                    cr.w = w;
                    if (sa == p1) {
                        cr.a0 = e0; cr.a1 = e1; cr.b0 = v0; cr.b1 = v1;
                    } else {
                        cr.a0 = v0; cr.a1 = v1; cr.b0 = e0; cr.b1 = e1;
                    }
                    // the same piece as in the row before: one taller rectangle
                    if (cv.caps.size() > first) {
                        CapRect& last = cv.caps.back();
                        if (last.fa == cr.fa && last.w == cr.w) {
                            if (sa == p1 && last.a0 == cr.a0 && last.a1 == cr.a1 && std::fabs(last.b1 - cr.b0) < 1e-4f) {
                                last.b1 = cr.b1;
                                return;
                            }
                            if (sa == p2 && last.b0 == cr.b0 && last.b1 == cr.b1 && std::fabs(last.a1 - cr.a0) < 1e-4f) {
                                last.a1 = cr.a1;
                                return;
                            }
                        }
                    }
                    cv.caps.push_back(cr);
                };
                for (int g = 0; g < n; ++g) {
                    if (cuts[g + 1] - cuts[g] < 1e-4f)
                        continue;
                    SetComp(q, sa, (cuts[g] + cuts[g + 1]) * 0.5f);
                    const bool solid = SoupSolidAt(q);
                    if (solid && !inRun) {
                        inRun = true;
                        runLo = cuts[g];
                    } else if (!solid && inRun) {
                        emit(runLo, cuts[g]);
                        inRun = false;
                    }
                }
                if (inRun)
                    emit(runLo, u1);
            }
            if (S >= 0 && A != S) {
                // keep the stretched box's side only where it lies past the cell's box
                const float c0 = boxes[0].lo[S], c1 = boxes[0].hi[S];
                std::vector<CapRect> kept;
                for (size_t i = first; i < cv.caps.size(); ++i) {
                    const CapRect cr = cv.caps[i];
                    const bool onA = S == p1;
                    const float s0 = onA ? cr.a0 : cr.b0, s1 = onA ? cr.a1 : cr.b1;
                    const float pieces[2][2] = { { s0, std::min(s1, c0) }, { std::max(s0, c1), s1 } };
                    for (const auto& pc : pieces) {
                        if (pc[1] - pc[0] < 1e-4f)
                            continue;
                        CapRect k = cr;
                        (onA ? k.a0 : k.b0) = pc[0];
                        (onA ? k.a1 : k.b1) = pc[1];
                        kept.push_back(k);
                    }
                }
                cv.caps.resize(first);
                cv.caps.insert(cv.caps.end(), kept.begin(), kept.end());
            }
        }
    }
}

// a neighbouring hole's caps that now lie in the new hole `c` go (nothing else of them changes)
void PruneCaps(Carved& cv) {
    bool partly = false;
    auto gone = [&](const CapRect& cr) {
        const int A = cr.fa / 2, p1 = (A + 1) % 3, p2 = (A + 2) % 3;
        // a cap stays while the solid right behind it does: looked at in its middle and near its four corners
        const float da = (cr.a1 - cr.a0) * 0.45f, db = (cr.b1 - cr.b0) * 0.45f;
        const float ma = (cr.a0 + cr.a1) * 0.5f, mb = (cr.b0 + cr.b1) * 0.5f;
        const float pts[5][2] = { { ma, mb }, { ma - da, mb - db }, { ma + da, mb - db }, { ma - da, mb + db }, { ma + da, mb + db } };
        int carved = 0;
        for (const auto& pt : pts) {
            CVector v;
            SetComp(v, A, cr.w + ((cr.fa % 2) == 0 ? 0.004f : -0.004f));
            SetComp(v, p1, pt[0]);
            SetComp(v, p2, pt[1]);
            if (CarvedAt(v))
                ++carved;
        }
        if (carved > 0 && carved < 5)
            partly = true;
        return carved == 5;
    };
    cv.caps.erase(std::remove_if(cv.caps.begin(), cv.caps.end(), gone), cv.caps.end());
    if (partly)
        cv.capsDirty = true; // only part of a cap went: worked out again (soon, while drawing)
}

// Makes cell c part of the hole. Its surfaces (found before, while they still count) are cut with it.
Carved& Mark(const Int3& c, uint16_t block, const Carved& found) {
    ++gVersion;
    Carved& cv = gCarved[c];
    cv.block = block;
    cv.capsDirty = true;
    for (int i = 0; i < found.n; ++i)
        AddSlab(cv, found.slabs[i]);
    // the caps of the holes around lose what now lies in this one (with its stretched boxes)
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy && !dz)
                    continue;
                auto it = gCarved.find({ c.x + dx, c.y + dy, c.z + dz });
                if (it != gCarved.end() && !it->second.capsDirty)
                    PruneCaps(it->second);
            }
    gSkins.erase(c);
    const Voxel v = gWorld.Get(c.x, c.y, c.z);
    if (VoxBlock(v) != ID_AIR && (VoxMeta(v) & META_NATURAL) && IsSolidBlock(VoxBlock(v))) {
        auto hook = gNaturalRemovedHook; // (this is the hole being made: no second time round)
        gNaturalRemovedHook = nullptr;
        gWorld.SetRaw(c.x, c.y, c.z, MakeVox(ID_AIR));
        gNaturalRemovedHook = hook;
    }
    gWorld.dirty = true;
    return cv;
}

void Open(const Int3& c, uint16_t block, const Carved& found) {
    Mark(c, block, found);
    FillAround(c, block);
    auto it = gCarved.find(c);
    if (it != gCarved.end())
        ComputeCaps(c, it->second);
    for (const Int3& d : FACE_DIR) {
        auto n = gCarved.find(c + d);
        if (n != gCarved.end() && !n->second.capsDirty)
            continue;
        if (n != gCarved.end())
            ComputeCaps(n->first, n->second);
    }
}

// the box of one column of a rim piece
bool ColumnBox(const Int3& c, const Skin& s, int k, float b[6]) {
    if (!s.Has(k))
        return false;
    const int a = s.axis, a1 = (a + 1) % 3, a2 = (a + 2) % 3;
    const int i = k % kSub, j = k / kSub;
    b[a] = s.lo[k];
    b[3 + a] = s.hi[k];
    b[a1] = CellComp(c, a1) + (float)i / kSub;
    b[3 + a1] = b[a1] + 1.0f / kSub;
    b[a2] = CellComp(c, a2) + (float)j / kSub;
    b[3 + a2] = b[a2] + 1.0f / kSub;
    return true;
}

// the solid range of column k of the rim piece in cell `c`, if it is one along the same axis
bool NeighbourRange(const Int3& c, int axis, int k, float* lo, float* hi) {
    auto it = gSkins.find(c);
    if (it == gSkins.end() || it->second.axis != axis)
        return false;
    *lo = it->second.lo[k];
    *hi = it->second.hi[k];
    return true;
}
} // namespace

static bool CarvedCell(int x, int y, int z) { return !gCarved.empty() && gCarved.count({ x, y, z }) != 0; }

bool CarvedAt(const CVector& p) {
    if (gCarved.empty())
        return false;
    const Int3 c{ FloorI(p.x), FloorI(p.y), FloorI(p.z) };
    if (gCarved.count(c))
        return true;
    // the cut of a neighbour reaches over (its slabs, and the hair it is wider)
    const float f[3] = { p.x - c.x, p.y - c.y, p.z - c.z };
    CutBox boxes[4];
    for (int a = 0; a < 3; ++a)
        for (int s = -1; s <= 1; s += 2) {
            if ((s < 0 && f[a] > 0.45f) || (s > 0 && f[a] < 0.55f))
                continue; // too far from that side for any cut to reach
            auto it = gCarved.find(Step(c, a, s));
            if (it == gCarved.end())
                continue;
            const int n = CellBoxes(it->first, it->second, boxes);
            for (int i = 0; i < n; ++i)
                if (InBox(boxes[i], p))
                    return true;
        }
    return false;
}

bool CarveAny() { return !gCarved.empty(); }

bool CarveInside(const CVector& cam) { return CarvedCell(FloorI(cam.x), FloorI(cam.y), FloorI(cam.z)); }

int CarveBreak(const CColPoint& cp, CEntity* ent, const CVector& dir, int block) {
    (void)ent;
    block = PlainBlock((uint16_t)block);
    CVector n = cp.m_vecNormal;
    if (n.x * dir.x + n.y * dir.y + n.z * dir.z > 0.0f)
        n = n * -1.0f; // broken from behind: towards the player all the same
    const CVector in = cp.m_vecPoint - n * 0.05f;
    const Int3 c{ FloorI(in.x), FloorI(in.y), FloorI(in.z) };
    PrepareAround(Centre(c));
    Carved found;
    SurfacesIn(c, found);
    Slab hitSlab;
    if (SlabFor(c, cp.m_vecPoint, cp.m_vecNormal, hitSlab))
        AddSlab(found, hitSlab); // the face that was hit goes, wherever on the border it lies
    Open(c, (uint16_t)block, found);
    static int logged = 0;
    if (logged < 10) {
        ++logged;
        Log("Carve: opened cell %d,%d,%d (%s, %d surfaces)", c.x, c.y, c.z, Block(block).key, found.n);
    }
    return block;
}

void CarveNaturalRemoved(int x, int y, int z, int block) {
    const Int3 c{ x, y, z };
    if (!IsSolidBlock(block))
        block = ID_STONE;
    PrepareAround(Centre(c));
    Carved found;
    SurfacesIn(c, found); // GTA faces that cross this cell disappear with the block
    Open(c, (uint16_t)block, found);
}

int CarveSkinBoxes(int x, int y, int z, float boxes[][6], int max) {
    if (gSkins.empty())
        return 0;
    auto it = gSkins.find({ x, y, z });
    if (it == gSkins.end())
        return 0;
    const Skin& s = it->second;
    const Int3& c = it->first;
    int n = 0;
    // neighbouring columns of a row with the same range become one box
    for (int j = 0; j < kSub; ++j)
        for (int i = 0; i < kSub && n < max;) {
            const int k = i + j * kSub;
            if (!s.Has(k)) {
                ++i;
                continue;
            }
            int e = i + 1;
            while (e < kSub && s.Has(e + j * kSub) && std::fabs(s.lo[e + j * kSub] - s.lo[k]) < 1e-4f &&
                   std::fabs(s.hi[e + j * kSub] - s.hi[k]) < 1e-4f)
                ++e;
            float b[6], last[6];
            ColumnBox(c, s, k, b);
            ColumnBox(c, s, e - 1 + j * kSub, last);
            for (int a = 0; a < 3; ++a) {
                boxes[n][a] = std::min(b[a], last[a]);
                boxes[n][3 + a] = std::max(b[3 + a], last[3 + a]);
            }
            ++n;
            i = e;
        }
    return n;
}

int CarveSkinBlock(int x, int y, int z) {
    auto it = gSkins.find({ x, y, z });
    return it == gSkins.end() ? ID_AIR : it->second.block;
}

bool CarveRaycastSkins(const CVector& o, const CVector& d, float maxDist, Int3* cell, int* face, float* dist) {
    if (gSkins.empty())
        return false;
    bool found = false;
    float best = maxDist;
    const float oo[3] = { o.x, o.y, o.z }, dd[3] = { d.x, d.y, d.z };
    for (auto& kv : gSkins) {
        const CVector m = Centre(kv.first);
        if ((m - o).Magnitude() > maxDist + 1.0f)
            continue;
        for (int k = 0; k < kCols; ++k) {
            float b[6];
            if (!ColumnBox(kv.first, kv.second, k, b))
                continue;
            float t0 = 0.0f, t1 = best;
            int hitFace = -1;
            bool ok = true;
            for (int a = 0; a < 3 && ok; ++a) {
                if (std::fabs(dd[a]) < 1e-6f) {
                    ok = oo[a] >= b[a] && oo[a] <= b[3 + a];
                    continue;
                }
                float ta = (b[a] - oo[a]) / dd[a], tb = (b[3 + a] - oo[a]) / dd[a];
                int fa = a * 2 + 1; // entering through the low face: the face that looks to -axis
                if (ta > tb) {
                    std::swap(ta, tb);
                    fa = a * 2;
                }
                if (ta > t0) {
                    t0 = ta;
                    hitFace = fa;
                }
                t1 = std::min(t1, tb);
                ok = t0 <= t1;
            }
            if (!ok || hitFace < 0 || t0 >= best)
                continue;
            best = t0;
            *cell = kv.first;
            *face = hitFace; // FACE_EAST 0 (+x), WEST 1, NORTH 2 (+y), SOUTH 3, TOP 4, BOTTOM 5
            *dist = t0;
            found = true;
        }
    }
    return found;
}

int CarveBreakSkin(const Int3& cell) {
    auto it = gSkins.find(cell);
    if (it == gSkins.end())
        return ID_AIR;
    const uint16_t block = it->second.block;
    gSkins.erase(it);
    PrepareAround(Centre(cell));
    Carved found;
    SurfacesIn(cell, found);
    Open(cell, block, found);
    return block;
}

std::vector<CarveOpened> CarveExplode(const CVector& at, float radius) {
    std::vector<CarveOpened> opened;
    if (!gConfig.breakBuildings || CGame::currArea != 0 || radius <= 0.0f)
        return opened;
    PrepareAround(at, radius);
    struct Hit {
        Int3 cell;
        Carved found;
        uint16_t block;
        bool surface;
    };
    std::vector<Hit> hits;
    uint16_t common = 0;
    const int r = (int)std::ceil(radius);
    const int cx = FloorI(at.x), cy = FloorI(at.y), cz = FloorI(at.z);
    for (int z = cz - r; z <= cz + r; ++z)
        for (int y = cy - r; y <= cy + r; ++y)
            for (int x = cx - r; x <= cx + r; ++x) {
                const Int3 c{ x, y, z };
                if ((Centre(c) - at).Magnitude() > radius * (0.8f + 0.2f * Rand01()))
                    continue;
                if (gCarved.count(c))
                    continue;
                // the dug ground is the terrain's (TerrainExplode)
                if (TerrainIsConverted(x, y) && z + 1.0f <= TerrainSurface(x, y) + 0.6f)
                    continue;
                Hit h{ c, Carved(), 0, false };
                const bool skin = gSkins.count(c) != 0;
                const Voxel v = gWorld.Get(x, y, z);
                const bool natural = VoxBlock(v) != ID_AIR && (VoxMeta(v) & META_NATURAL);
                if (!skin && !natural) {
                    if (VoxBlock(v) != ID_AIR)
                        continue; // a block the player placed: the block code blows it up
                    Skin s;
                    const CellKind kind = Classify(c, s, &h.block);
                    if (kind == CELL_AIR)
                        continue;
                }
                SurfacesIn(c, h.found, h.block ? nullptr : &h.block);
                h.surface = h.found.n > 0;
                if (skin && !h.block)
                    h.block = gSkins[c].block;
                if (natural && !h.block)
                    h.block = (uint16_t)VoxBlock(v);
                if (h.block)
                    common = h.block;
                hits.push_back(h);
            }
    if (hits.empty())
        return opened;
    for (Hit& h : hits) {
        if (!h.block || !IsSolidBlock(h.block))
            h.block = common && IsSolidBlock(common) ? common : (uint16_t)ID_STONE;
        h.block = PlainBlock(h.block);
        Mark(h.cell, h.block, h.found);
        opened.push_back({ h.cell, h.block, h.surface });
    }
    // the cells around the crater are worked out over the next frames
    for (const Hit& h : hits)
        gPendingFill.push_back({ h.cell, h.block });
    Log("Carve: explosion opened %d cells", (int)hits.size());
    return opened;
}

void CarveUpdate() {
    // a few cells per frame
    for (int budget = 3; budget > 0 && !gPendingFill.empty(); --budget) {
        const PendingFill p = gPendingFill.front();
        gPendingFill.pop_front();
        if (gCarved.count(p.cell))
            FillAround(p.cell, p.block);
    }
}

uint32_t CarveVersion() { return gVersion; }

void CarveBoxesIn(const float lo[3], const float hi[3], std::vector<CutBox>& out) {
    CutBox boxes[4];
    for (auto& kv : gCarved) {
        const Int3& c = kv.first;
        // a cell's boxes stay within 1.5 m of it
        if (c.x > hi[0] + 1.5f || c.x + 1 < lo[0] - 1.5f || c.y > hi[1] + 1.5f || c.y + 1 < lo[1] - 1.5f ||
            c.z > hi[2] + 1.5f || c.z + 1 < lo[2] - 1.5f)
            continue;
        const int n = CellBoxes(c, kv.second, boxes);
        for (int i = 0; i < n; ++i)
            if (Overlap(boxes[i], lo, hi))
                out.push_back(boxes[i]);
    }
}

void CarveAreas(const CVector& cam, float radius, std::vector<CutBox>& out) {
    std::unordered_map<Int3, CutBox, Int3Hash> areas;
    for (auto& kv : gCarved) {
        const Int3& c = kv.first;
        const float dx = c.x + 0.5f - cam.x, dy = c.y + 0.5f - cam.y;
        if (dx * dx + dy * dy > radius * radius)
            continue;
        const Int3 key{ FloorDiv(c.x, 16), FloorDiv(c.y, 16), 0 };
        auto it = areas.find(key);
        if (it == areas.end()) {
            CutBox b;
            b.lo[0] = (float)c.x; b.lo[1] = (float)c.y; b.lo[2] = (float)c.z;
            b.hi[0] = c.x + 1.0f; b.hi[1] = c.y + 1.0f; b.hi[2] = c.z + 1.0f;
            areas.emplace(key, b);
        } else {
            CutBox& b = it->second;
            b.lo[0] = std::min(b.lo[0], (float)c.x); b.lo[1] = std::min(b.lo[1], (float)c.y);
            b.lo[2] = std::min(b.lo[2], (float)c.z);
            b.hi[0] = std::max(b.hi[0], c.x + 1.0f); b.hi[1] = std::max(b.hi[1], c.y + 1.0f);
            b.hi[2] = std::max(b.hi[2], c.z + 1.0f);
        }
    }
    for (auto& kv : areas) {
        CutBox b = kv.second;
        for (int a = 0; a < 3; ++a) { // the slabs reach a little further
            b.lo[a] -= 1.0f;
            b.hi[a] += 1.0f;
        }
        out.push_back(b);
    }
}

void CarveEmitCaps(const CVector& cam, float radius, float light) {
    static const float kShade[6] = { 0.6f, 0.6f, 0.8f, 0.8f, 1.0f, 0.5f };
    int budget = 4; // caps worked out per frame (holes loaded with a world, explosions)
    for (auto& kv : gCarved) {
        const Int3& c = kv.first;
        const float dx = c.x + 0.5f - cam.x, dy = c.y + 0.5f - cam.y;
        if (dx * dx + dy * dy > radius * radius)
            continue;
        if (!TheCamera.IsSphereVisible(Centre(c), 1.5f))
            continue;
        Carved& cv = kv.second;
        if (cv.capsDirty) {
            if (budget <= 0)
                continue;
            --budget;
            PrepareAround(Centre(c));
            ComputeCaps(c, cv);
        }
        for (const CapRect& cr : cv.caps) {
            // seen from the hole the cap faces back: a cap above is a block's bottom, one below its top
            const int A = cr.fa / 2, p1 = (A + 1) % 3, p2 = (A + 2) % 3;
            const bool pos = (cr.fa % 2) == 0;
            const int seen = A == 2 ? (pos ? FACE_BOTTOM : FACE_TOP) : (pos ? cr.fa + 1 : cr.fa - 1);
            const TileUV uv = AtlasTileUV(BlockFaceTile(cv.block, A == 2 ? seen : FACE_NORTH, 0));
            const float pts[4][2] = { { cr.a0, cr.b0 }, { cr.a1, cr.b0 }, { cr.a1, cr.b1 }, { cr.a0, cr.b1 } };
            // the texture: one block per cell, stuck to it (the boxes reach a hair past the cell); on walls the
            // picture stands upright (its v along z)
            const bool swap = A == 1; // (on a y wall the plane's first axis is z)
            const float base1 = (float)CellComp(c, p1), base2 = (float)CellComp(c, p2);
            CVector q[4];
            float us[4], vs[4];
            for (int k = 0; k < 4; ++k) {
                float v[3];
                v[A] = cr.w;
                v[p1] = pts[k][0];
                v[p2] = pts[k][1];
                q[k] = CVector(v[0], v[1], v[2]);
                const float f1 = Clamp(pts[k][0] - base1, 0.0f, 1.0f), f2 = Clamp(pts[k][1] - base2, 0.0f, 1.0f);
                const float fu = swap ? f2 : f1, fv = swap ? f1 : f2;
                us[k] = uv.u0 + (uv.u1 - uv.u0) * fu;
                vs[k] = uv.v1 - (uv.v1 - uv.v0) * fv;
            }
            d3::QuadUV(q, us, vs, d3::Gray(light * kShade[seen]));
        }
    }
}

void CarveClear() {
    gCarved.clear();
    gSkins.clear();
    gPendingFill.clear();
    SoupClear();
    ++gVersion;
}

void CarveWrite(FILE* f) {
    uint32_t n = (uint32_t)gCarved.size();
    fwrite(&n, 4, 1, f);
    for (auto& kv : gCarved) {
        const Carved& cv = kv.second;
        int32_t p[3] = { kv.first.x, kv.first.y, kv.first.z };
        uint16_t b[2] = { cv.block, cv.n };
        fwrite(p, sizeof(p), 1, f);
        fwrite(b, sizeof(b), 1, f);
        for (int i = 0; i < cv.n; ++i) {
            const uint32_t axis = cv.slabs[i].axis;
            fwrite(&axis, 4, 1, f);
            fwrite(&cv.slabs[i].lo, 4, 1, f);
            fwrite(&cv.slabs[i].hi, 4, 1, f);
        }
    }
    // rim pieces: the marker -1000000 - count says "columns" (0.11 on)
    int32_t ns = -1000000 - (int32_t)gSkins.size();
    fwrite(&ns, 4, 1, f);
    for (auto& kv : gSkins) {
        int32_t p[3] = { kv.first.x, kv.first.y, kv.first.z };
        const Skin& s = kv.second;
        fwrite(p, sizeof(p), 1, f);
        fwrite(&s.axis, 1, 1, f);
        fwrite(s.lo, sizeof(s.lo), 1, f);
        fwrite(s.hi, sizeof(s.hi), 1, f);
        fwrite(&s.surfLo, 2, 1, f);
        fwrite(&s.surfHi, 2, 1, f);
        fwrite(&s.block, 2, 1, f);
    }
}

bool CarveRead(FILE* f) {
    CarveClear();
    uint32_t n = 0;
    if (fread(&n, 4, 1, f) != 1 || n > 4000000)
        return false;
    for (uint32_t i = 0; i < n; ++i) {
        int32_t p[3];
        uint16_t b[2];
        if (fread(p, sizeof(p), 1, f) != 1 || fread(b, sizeof(b), 1, f) != 1 || b[1] > 3)
            return false;
        Carved c;
        c.block = PlainBlock(b[0]);
        for (int k = 0; k < b[1]; ++k) {
            uint32_t axis;
            Slab s;
            if (fread(&axis, 4, 1, f) != 1 || fread(&s.lo, 4, 1, f) != 1 || fread(&s.hi, 4, 1, f) != 1)
                return false;
            s.axis = (uint8_t)std::min<uint32_t>(axis, 2);
            c.slabs[c.n++] = s;
        }
        gCarved[{ p[0], p[1], p[2] }] = c;
    }
    int32_t ns = 0;
    if (fread(&ns, 4, 1, f) != 1)
        return false;
    // a whole box per cell (0.10) or one side of it (0.8 / 0.9): as columns along z
    auto boxSkin = [](const Int3& c, const float lo[3], const float hi[3], uint16_t block) {
        Skin s;
        s.axis = 2;
        for (int k = 0; k < kCols; ++k) {
            const int i = k % kSub, j = k / kSub;
            const float x = c.x + (i + 0.5f) / kSub, y = c.y + (j + 0.5f) / kSub;
            if (x >= lo[0] && x <= hi[0] && y >= lo[1] && y <= hi[1]) {
                s.lo[k] = lo[2];
                s.hi[k] = hi[2];
            }
        }
        s.block = IsSolidBlock(block) ? block : (uint16_t)ID_STONE;
        gSkins[c] = s;
    };
    if (ns <= -1000000) {
        const int32_t count = -1000000 - ns;
        if (count > 4000000)
            return false;
        for (int32_t i = 0; i < count; ++i) {
            int32_t p[3];
            Skin s;
            if (fread(p, sizeof(p), 1, f) != 1 || fread(&s.axis, 1, 1, f) != 1 || fread(s.lo, sizeof(s.lo), 1, f) != 1 ||
                fread(s.hi, sizeof(s.hi), 1, f) != 1 || fread(&s.surfLo, 2, 1, f) != 1 || fread(&s.surfHi, 2, 1, f) != 1 ||
                fread(&s.block, 2, 1, f) != 1)
                return false;
            s.axis = (uint8_t)std::min<int>(s.axis, 2);
            if (!IsSolidBlock(s.block))
                s.block = ID_STONE;
            gSkins[{ p[0], p[1], p[2] }] = s;
        }
    } else if (ns < 0) {
        const int32_t count = -ns - 1;
        if (count > 4000000)
            return false;
        for (int32_t i = 0; i < count; ++i) {
            int32_t p[3];
            float lo[3], hi[3];
            uint16_t block;
            if (fread(p, sizeof(p), 1, f) != 1 || fread(lo, sizeof(lo), 1, f) != 1 || fread(hi, sizeof(hi), 1, f) != 1 ||
                fread(&block, 2, 1, f) != 1)
                return false;
            boxSkin({ p[0], p[1], p[2] }, lo, hi, block);
        }
    } else {
        if (ns > 4000000)
            return false;
        for (int32_t i = 0; i < ns; ++i) {
            int32_t p[3];
            int8_t a[2];
            float v[3];
            uint16_t block;
            if (fread(p, sizeof(p), 1, f) != 1 || fread(a, 2, 1, f) != 1 || fread(v, sizeof(v), 1, f) != 1 ||
                fread(&block, 2, 1, f) != 1)
                return false;
            const int axis = std::clamp((int)a[0], 0, 2);
            const Int3 c{ p[0], p[1], p[2] };
            float lo[3], hi[3];
            for (int k = 0; k < 3; ++k) {
                lo[k] = (float)CellComp(c, k);
                hi[k] = lo[k] + 1.0f;
            }
            if (a[1] >= 0)
                hi[axis] = v[0];
            else
                lo[axis] = v[0];
            boxSkin(c, lo, hi, block);
        }
    }
    // Versions before 0.12 filled the inside of buildings with blocks (often in the wrong places) and worked out
    // the rims another way: those blocks go, the caps show what is solid now, and the rims are worked out again.
    std::vector<Int3> old;
    for (auto& kv : gWorld.chunks) {
        const Chunk& ch = *kv.second;
        for (int z = 0; z < CS; ++z)
            for (int y = 0; y < CS; ++y)
                for (int x = 0; x < CS; ++x) {
                    const Voxel v = ch.Get(x, y, z);
                    if (VoxBlock(v) == ID_AIR || !(VoxMeta(v) & META_NATURAL))
                        continue;
                    const Int3 w{ ch.pos.x * CS + x, ch.pos.y * CS + y, ch.pos.z * CS + z };
                    if (!TerrainOwnsCell(w.x, w.y, w.z))
                        old.push_back(w);
                }
    }
    // (taken away quietly: they were not mined, nothing opens where they were)
    auto hook = gNaturalRemovedHook;
    gNaturalRemovedHook = nullptr;
    for (const Int3& w : old)
        gWorld.SetRaw(w.x, w.y, w.z, MakeVox(ID_AIR));
    gNaturalRemovedHook = hook;
    const int purged = (int)old.size();
    gSkins.clear();
    for (auto& kv : gCarved)
        gPendingFill.push_back({ kv.first, kv.second.block });
    if (purged)
        Log("Carve: %d blocks of the old building fill removed", purged);
    ++gVersion;
    return true;
}

} // namespace mc
