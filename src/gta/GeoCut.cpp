#include "GeoCut.h"

#include <unordered_map>
#include <vector>

#include "CBuilding.h"
#include "CCamera.h"
#include "CEntity.h"
#include "CModelInfo.h"
#include "CPools.h"
#include "CWorld.h"
#include "RenderWare.h"
#include "safetyhook.hpp"

#include "Carve.h"
#include "Config.h"
#include "Terrain.h"
#include "ModCommon.h"

namespace mc {

namespace {
// ---------------------------------------------------------------- GTA's pieces
// CCustomBuildingDNPipeline keeps night colours next to every building mesh (a geometry plugin)
struct ExtraVertColour {
    RwRGBA* night;
    RwRGBA* day;
    float balance;
};
ExtraVertColour* VertColours(RpGeometry* g) {
    const int32_t off = *reinterpret_cast<const int32_t*>(0x8D12BC);
    return off > 0 ? reinterpret_cast<ExtraVertColour*>(reinterpret_cast<uint8_t*>(g) + off) : nullptr;
}
// CMemoryMgr::Malloc / Free: the plugin frees the colours with the game's Free
void* GameMalloc(uint32_t size) { return reinterpret_cast<void*(__cdecl*)(uint32_t)>(0x72F420)(size); }
void GameFree(void* p) { reinterpret_cast<void(__cdecl*)(void*)>(0x72F430)(p); }
// CCustomBuildingDNPipeline::PreRenderUpdate: GTA only updates the model's own mesh between day and night
void DayNightUpdate(RpAtomic* a) { reinterpret_cast<void(__cdecl*)(RpAtomic*, bool)>(0x5D7200)(a, false); }

SafetyHookInline gPreRenderHook;

// ---------------------------------------------------------------- cutting a mesh
constexpr int kMaxUV = 2;
// a vertex while it is cut: everything in it is interpolated the same way
struct CV {
    float w[3];               // world position: where the cut is made
    float p[3], n[3];         // model space
    float c[4], nc[4], dc[4]; // pre-light, night and day colours
    float uv[kMaxUV * 2];
};
constexpr int kFloats = sizeof(CV) / sizeof(float);

CV Lerp(const CV& a, const CV& b, float t) {
    CV r;
    const float* fa = reinterpret_cast<const float*>(&a);
    const float* fb = reinterpret_cast<const float*>(&b);
    float* fr = reinterpret_cast<float*>(&r);
    for (int i = 0; i < kFloats; ++i)
        fr[i] = fa[i] + (fb[i] - fa[i]) * t;
    return r;
}

constexpr int kPolyMax = 40;
struct Poly {
    int n = 0;
    CV v[kPolyMax];
};

// the part of `in` beyond the plane (sign * (w[axis] - value) > 0) goes to `out`, the rest to `rest`
bool Split(const Poly& in, int axis, float value, float sign, Poly& out, Poly& rest) {
    out.n = rest.n = 0;
    float d[kPolyMax];
    bool above = false, below = false;
    for (int i = 0; i < in.n; ++i) {
        d[i] = sign * (in.v[i].w[axis] - value);
        if (std::fabs(d[i]) < 1e-5f)
            d[i] = 0.0f;
        above |= d[i] > 0.0f;
        below |= d[i] < 0.0f;
    }
    if (!below) { // all of it on the plane or beyond
        out = in;
        return true;
    }
    if (!above) {
        rest = in;
        return true;
    }
    for (int i = 0; i < in.n; ++i) {
        const int j = (i + 1) % in.n;
        if (out.n + 2 > kPolyMax || rest.n + 2 > kPolyMax)
            return false;
        const CV& a = in.v[i];
        if (d[i] >= 0.0f)
            out.v[out.n++] = a;
        if (d[i] <= 0.0f)
            rest.v[rest.n++] = a;
        if ((d[i] > 0.0f && d[j] < 0.0f) || (d[i] < 0.0f && d[j] > 0.0f)) {
            CV m = Lerp(a, in.v[j], d[i] / (d[i] - d[j]));
            m.w[axis] = value;
            out.v[out.n++] = m;
            rest.v[rest.n++] = m;
        }
    }
    return true;
}

// The pieces of `p` that lie outside box `b` are appended to `out`. Returns false when `p` does not reach into
// the box (then nothing is appended); `ok` turns false if the polygon got too complicated.
bool Subtract(const Poly& p, const CutBox& b, std::vector<Poly>& out, bool& ok) {
    Poly cur = p, o, r;
    bool split = false;
    for (int plane = 0; plane < 6; ++plane) {
        const int axis = plane >> 1;
        const bool high = (plane & 1) != 0;
        if (!Split(cur, axis, high ? b.hi[axis] : b.lo[axis], high ? 1.0f : -1.0f, o, r)) {
            ok = false;
            return false;
        }
        if (r.n < 3) { // all of what is left lies outside this side of the box
            if (!split)
                return false;
            out.push_back(o);
            return true;
        }
        if (o.n >= 3) {
            out.push_back(o);
            split = true;
        }
        cur = r;
    }
    return true; // what is left lies in the box: cut away
}

bool BoxTouches(const CutBox& b, const float lo[3], const float hi[3]) {
    return b.lo[0] < hi[0] && b.hi[0] > lo[0] && b.lo[1] < hi[1] && b.hi[1] > lo[1] && b.lo[2] < hi[2] && b.hi[2] > lo[2];
}

uint8_t Byte(float v) { return (uint8_t)Clamp(v + 0.5f, 0.0f, 255.0f); }

// the boxes by 1 m square (a ground model can have thousands of dug columns)
struct BoxGrid {
    std::unordered_map<int64_t, std::vector<int>> cells;
    std::vector<uint32_t> stamp;
    uint32_t now = 0;
    static int64_t Key(int x, int y) { return ((int64_t)x << 32) ^ (uint32_t)y; }
    void Build(const std::vector<CutBox>& boxes) {
        cells.clear();
        stamp.assign(boxes.size(), 0);
        now = 0;
        for (int i = 0; i < (int)boxes.size(); ++i)
            for (int y = FloorI(boxes[i].lo[1]); y <= FloorI(boxes[i].hi[1]); ++y)
                for (int x = FloorI(boxes[i].lo[0]); x <= FloorI(boxes[i].hi[0]); ++x)
                    cells[Key(x, y)].push_back(i);
    }
    // boxes whose squares meet lo..hi (false: too large an area, test them all)
    bool Gather(const float lo[3], const float hi[3], std::vector<int>& out) {
        const int x0 = FloorI(lo[0]), x1 = FloorI(hi[0]), y0 = FloorI(lo[1]), y1 = FloorI(hi[1]);
        if ((int64_t)(x1 - x0 + 1) * (y1 - y0 + 1) > 256)
            return false;
        ++now;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                auto it = cells.find(Key(x, y));
                if (it == cells.end())
                    continue;
                for (int i : it->second)
                    if (stamp[i] != now) {
                        stamp[i] = now;
                        out.push_back(i);
                    }
            }
        return true;
    }
};

// A copy of `src` without what lies in the boxes; nullptr when nothing of it is in them (or it cannot be cut).
RpGeometry* BuildCut(RpGeometry* src, const RwMatrix* m, const std::vector<CutBox>& boxes, int* removed) {
    if (!src || (src->flags & rpGEOMETRYNATIVE) || src->numMorphTargets != 1 || !src->morphTarget ||
        !src->morphTarget->verts || !src->triangles || src->numVertices <= 0 || src->numTriangles <= 0 ||
        src->numTexCoordSets > kMaxUV)
        return nullptr;
    const int nv = src->numVertices, nm = src->matList.numMaterials;
    for (int i = 0; i < nm; ++i)
        if (!src->matList.materials[i])
            return nullptr;
    for (int t = 0; t < src->numTriangles; ++t)
        if (src->triangles[t].matIndex >= nm)
            return nullptr;
    const RwV3d* sv = src->morphTarget->verts;
    const RwV3d* sn = (src->flags & rpGEOMETRYNORMALS) ? src->morphTarget->normals : nullptr;
    const RwRGBA* sc = (src->flags & rpGEOMETRYPRELIT) ? src->preLitLum : nullptr;
    ExtraVertColour* sx = VertColours(src);
    const bool dayNight = sx && sx->night && sx->day;
    const int nuv = src->numTexCoordSets;

    std::vector<CVector> world(nv);
    for (int i = 0; i < nv; ++i) {
        const RwV3d& v = sv[i];
        world[i] = CVector(m->pos.x + m->right.x * v.x + m->up.x * v.y + m->at.x * v.z,
                           m->pos.y + m->right.y * v.x + m->up.y * v.y + m->at.y * v.z,
                           m->pos.z + m->right.z * v.x + m->up.z * v.y + m->at.z * v.z);
    }
    auto load = [&](int i) {
        CV v{};
        v.w[0] = world[i].x; v.w[1] = world[i].y; v.w[2] = world[i].z;
        v.p[0] = sv[i].x; v.p[1] = sv[i].y; v.p[2] = sv[i].z;
        if (sn) {
            v.n[0] = sn[i].x; v.n[1] = sn[i].y; v.n[2] = sn[i].z;
        }
        if (sc) {
            v.c[0] = sc[i].red; v.c[1] = sc[i].green; v.c[2] = sc[i].blue; v.c[3] = sc[i].alpha;
        }
        if (dayNight) {
            const RwRGBA& a = sx->night[i];
            const RwRGBA& b = sx->day[i];
            v.nc[0] = a.red; v.nc[1] = a.green; v.nc[2] = a.blue; v.nc[3] = a.alpha;
            v.dc[0] = b.red; v.dc[1] = b.green; v.dc[2] = b.blue; v.dc[3] = b.alpha;
        }
        for (int s = 0; s < nuv; ++s)
            if (src->texCoords[s]) {
                v.uv[s * 2] = src->texCoords[s][i].u;
                v.uv[s * 2 + 1] = src->texCoords[s][i].v;
            }
        return v;
    };

    struct NewTri {
        int v[3];
        uint16_t mat;
    };
    std::vector<int> keep; // triangles that stay as they are
    keep.reserve(src->numTriangles);
    std::vector<CV> verts; // vertices of the cut pieces
    std::vector<NewTri> tris;
    std::vector<int> hits, nearby;
    std::vector<Poly> polys, next;
    BoxGrid grid;
    grid.Build(boxes);
    int gone = 0;
    for (int t = 0; t < src->numTriangles; ++t) {
        const RpTriangle& tri = src->triangles[t];
        const int i0 = tri.vertIndex[0], i1 = tri.vertIndex[1], i2 = tri.vertIndex[2];
        if (i0 >= nv || i1 >= nv || i2 >= nv) {
            keep.push_back(t);
            continue;
        }
        const CVector& a = world[i0];
        const CVector& b = world[i1];
        const CVector& c = world[i2];
        const float lo[3] = { std::min({ a.x, b.x, c.x }), std::min({ a.y, b.y, c.y }), std::min({ a.z, b.z, c.z }) };
        const float hi[3] = { std::max({ a.x, b.x, c.x }), std::max({ a.y, b.y, c.y }), std::max({ a.z, b.z, c.z }) };
        hits.clear();
        nearby.clear();
        if (!grid.Gather(lo, hi, nearby)) {
            nearby.resize(boxes.size());
            for (int k = 0; k < (int)boxes.size(); ++k)
                nearby[k] = k;
        }
        for (int k : nearby)
            if (boxes[k].lo[0] <= hi[0] && boxes[k].hi[0] >= lo[0] && boxes[k].lo[1] <= hi[1] &&
                boxes[k].hi[1] >= lo[1] && boxes[k].lo[2] <= hi[2] && boxes[k].hi[2] >= lo[2])
                hits.push_back(k);
        if (hits.empty()) {
            keep.push_back(t);
            continue;
        }
        polys.clear();
        polys.emplace_back();
        polys.back().n = 3;
        polys.back().v[0] = load(i0);
        polys.back().v[1] = load(i1);
        polys.back().v[2] = load(i2);
        bool changed = false, ok = true;
        for (int k : hits) {
            next.clear();
            for (const Poly& p : polys) {
                if (Subtract(p, boxes[k], next, ok))
                    changed = true;
                else if (ok)
                    next.push_back(p);
                else
                    break;
            }
            if (!ok)
                break;
            polys.swap(next);
            if (polys.empty())
                break;
        }
        if (!ok || !changed) {
            keep.push_back(t);
            continue;
        }
        if (polys.empty())
            ++gone;
        for (const Poly& p : polys) {
            const int base = nv + (int)verts.size();
            for (int k = 0; k < p.n; ++k)
                verts.push_back(p.v[k]);
            for (int k = 1; k + 1 < p.n; ++k) // convex: a fan keeps the winding
                tris.push_back({ { base, base + k, base + k + 1 }, tri.matIndex });
        }
    }
    if (keep.size() == (size_t)src->numTriangles)
        return nullptr; // nothing of it is in the holes
    const int dv = nv + (int)verts.size(), dt = (int)(keep.size() + tris.size());
    if (dv > 65535 || dt <= 0) {
        Log("GeoCut: mesh too big to cut (%d vertices)", dv);
        return nullptr;
    }

    const RwUInt32 format = (src->flags & rpGEOMETRYFLAGSMASK & ~(RwUInt32)rpGEOMETRYTRISTRIP) | rpGEOMETRYTEXCOORDSETS(nuv);
    RpGeometry* dst = RpGeometryCreate(dv, dt, format);
    if (!dst)
        return nullptr;
    RpMorphTarget* mt = &dst->morphTarget[0];
    if (!mt->verts || !dst->triangles) {
        RpGeometryDestroy(dst);
        return nullptr;
    }
    // vertices: the old ones keep their numbers, the pieces' come after them
    memcpy(mt->verts, sv, sizeof(RwV3d) * nv);
    for (size_t k = 0; k < verts.size(); ++k) {
        RwV3d& p = mt->verts[nv + k];
        p.x = verts[k].p[0]; p.y = verts[k].p[1]; p.z = verts[k].p[2];
    }
    if (sn && mt->normals) {
        memcpy(mt->normals, sn, sizeof(RwV3d) * nv);
        for (size_t k = 0; k < verts.size(); ++k) {
            const float* n = verts[k].n;
            const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            const float s = len > 1e-6f ? 1.0f / len : 0.0f;
            RwV3d& d = mt->normals[nv + k];
            d.x = n[0] * s; d.y = n[1] * s; d.z = n[2] * s;
        }
    }
    if (sc && dst->preLitLum) {
        memcpy(dst->preLitLum, sc, sizeof(RwRGBA) * nv);
        for (size_t k = 0; k < verts.size(); ++k) {
            RwRGBA& d = dst->preLitLum[nv + k];
            d.red = Byte(verts[k].c[0]); d.green = Byte(verts[k].c[1]); d.blue = Byte(verts[k].c[2]);
            d.alpha = Byte(verts[k].c[3]);
        }
    }
    for (int s = 0; s < nuv; ++s) {
        if (!src->texCoords[s] || !dst->texCoords[s])
            continue;
        memcpy(dst->texCoords[s], src->texCoords[s], sizeof(RwTexCoords) * nv);
        for (size_t k = 0; k < verts.size(); ++k) {
            dst->texCoords[s][nv + k].u = verts[k].uv[s * 2];
            dst->texCoords[s][nv + k].v = verts[k].uv[s * 2 + 1];
        }
    }
    if (dayNight) {
        // the building pipeline blends these two into the pre-light colours: they must be there
        ExtraVertColour* dx = VertColours(dst);
        RwRGBA* night = static_cast<RwRGBA*>(GameMalloc(sizeof(RwRGBA) * dv));
        RwRGBA* day = static_cast<RwRGBA*>(GameMalloc(sizeof(RwRGBA) * dv));
        if (!dx || !night || !day) {
            if (night)
                GameFree(night);
            if (day)
                GameFree(day);
            RpGeometryDestroy(dst);
            return nullptr;
        }
        memcpy(night, sx->night, sizeof(RwRGBA) * nv);
        memcpy(day, sx->day, sizeof(RwRGBA) * nv);
        for (size_t k = 0; k < verts.size(); ++k) {
            const CV& v = verts[k];
            night[nv + k] = { Byte(v.nc[0]), Byte(v.nc[1]), Byte(v.nc[2]), Byte(v.nc[3]) };
            day[nv + k] = { Byte(v.dc[0]), Byte(v.dc[1]), Byte(v.dc[2]), Byte(v.dc[3]) };
        }
        dx->night = night;
        dx->day = day;
        dx->balance = sx->balance;
    }
    // triangles and their materials (the material list is built as they are set)
    std::vector<int> matMap(nm, -1);
    auto setMaterial = [&](RpTriangle& d, int srcMat) {
        if (matMap[srcMat] < 0) {
            RpGeometryTriangleSetMaterial(dst, &d, src->matList.materials[srcMat]);
            matMap[srcMat] = d.matIndex;
        } else {
            d.matIndex = (RwUInt16)matMap[srcMat];
        }
    };
    int ti = 0;
    for (int t : keep) {
        RpTriangle& d = dst->triangles[ti++];
        const RpTriangle& s = src->triangles[t];
        d.vertIndex[0] = s.vertIndex[0];
        d.vertIndex[1] = s.vertIndex[1];
        d.vertIndex[2] = s.vertIndex[2];
        setMaterial(d, s.matIndex);
    }
    for (const NewTri& nt : tris) {
        RpTriangle& d = dst->triangles[ti++];
        d.vertIndex[0] = (RwUInt16)nt.v[0];
        d.vertIndex[1] = (RwUInt16)nt.v[1];
        d.vertIndex[2] = (RwUInt16)nt.v[2];
        setMaterial(d, nt.mat);
    }
    RwSphere sphere;
    RpMorphTargetCalcBoundingSphere(mt, &sphere);
    mt->boundingSphere = sphere;
    RpGeometryUnlock(dst);
    RpD3D9GeometrySetUsageFlags(dst, RpD3D9GeometryGetUsageFlags(src));
    if (removed)
        *removed = src->numTriangles - (int)keep.size();
    (void)gone;
    return dst;
}

// ---------------------------------------------------------------- the buildings that have holes
struct Cut {
    CEntity* ent = nullptr;
    int16_t model = -1;
    RpAtomic* atomic = nullptr; // the building's atomic as we left it
    RpGeometry* geo = nullptr;  // what it showed then: our copy, or GTA's own mesh when nothing needed cutting
    bool ours = false;
    uint32_t sig = 0;           // the boxes it was cut with
    bool bounds = false;
    float lo[3] = {}, hi[3] = {}; // world bounds of its mesh
    bool seen = false;
};
std::unordered_map<CEntity*, Cut> gCuts;
uint32_t gScannedVersion = 0;
DWORD gLastScan = 0;
std::vector<CutBox> gBoxes;
int gLogged = 0;

bool StillThere(const Cut& c) {
    auto* pool = CPools::ms_pBuildingPool;
    if (!pool || !pool->m_pObjects)
        return false;
    const uintptr_t base = reinterpret_cast<uintptr_t>(pool->m_pObjects), at = reinterpret_cast<uintptr_t>(c.ent);
    if (at < base || (at - base) % sizeof(CBuilding) != 0)
        return false;
    const int idx = (int)((at - base) / sizeof(CBuilding));
    if (idx >= pool->m_nSize || pool->IsFreeSlotAtIndex(idx))
        return false;
    return c.ent->m_nModelIndex == c.model;
}

RpAtomic* EntityAtomic(CEntity* e) {
    RwObject* o = e->m_pRwObject;
    return o && o->type == rpATOMIC ? reinterpret_cast<RpAtomic*>(o) : nullptr;
}

RpGeometry* ModelGeometry(CEntity* e) {
    if (e->m_nModelIndex < 0)
        return nullptr;
    CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[e->m_nModelIndex];
    if (!mi || !mi->m_pRwObject || mi->m_pRwObject->type != rpATOMIC)
        return nullptr;
    return reinterpret_cast<RpAtomic*>(mi->m_pRwObject)->geometry;
}

// GTA's own mesh of this building (not our copy)
RpGeometry* SourceOf(const Cut& c, RpAtomic* a) {
    if (c.ours && a == c.atomic && a->geometry == c.geo)
        return ModelGeometry(c.ent);
    return a->geometry;
}

const RwMatrix* AtomicLtm(RpAtomic* a) {
    RwFrame* f = RpAtomicGetFrame(a);
    return f ? RwFrameGetLTM(f) : nullptr;
}

bool ComputeBounds(Cut& c, RpAtomic* a) {
    RpGeometry* g = SourceOf(c, a);
    const RwMatrix* m = AtomicLtm(a);
    if (!g || !m || !g->morphTarget || !g->morphTarget->verts || g->numVertices <= 0)
        return false;
    for (int k = 0; k < 3; ++k) {
        c.lo[k] = 1e9f;
        c.hi[k] = -1e9f;
    }
    for (int i = 0; i < g->numVertices; ++i) {
        const RwV3d& v = g->morphTarget->verts[i];
        const float w[3] = { m->pos.x + m->right.x * v.x + m->up.x * v.y + m->at.x * v.z,
                             m->pos.y + m->right.y * v.x + m->up.y * v.y + m->at.y * v.z,
                             m->pos.z + m->right.z * v.x + m->up.z * v.y + m->at.z * v.z };
        for (int k = 0; k < 3; ++k) {
            c.lo[k] = std::min(c.lo[k], w[k] - 0.05f);
            c.hi[k] = std::max(c.hi[k], w[k] + 0.05f);
        }
    }
    c.bounds = true;
    return true;
}

uint32_t Signature(const std::vector<CutBox>& boxes) {
    uint32_t sig = boxes.empty() ? 0u : 0x9E3779B9u;
    for (const CutBox& b : boxes) { // order does not matter
        uint32_t h = 2166136261u;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&b);
        for (size_t i = 0; i < sizeof(CutBox); ++i)
            h = (h ^ p[i]) * 16777619u;
        sig += h;
    }
    return sig;
}

// the holes in this building: broken into it, and dug into the ground it is part of
void BoxesFor(const Cut& c, std::vector<CutBox>& out) {
    out.clear();
    CarveBoxesIn(c.lo, c.hi, out);
    TerrainBoxesIn(c.lo, c.hi, out);
}

uint32_t HoleVersion() { return CarveVersion() * 2654435761u + TerrainVersion(); }

// gives the building the mesh it should have now
void Apply(Cut& c, RpAtomic* a) {
    if (!c.bounds && !ComputeBounds(c, a))
        return;
    BoxesFor(c, gBoxes);
    c.sig = Signature(gBoxes);
    RpGeometry* src = SourceOf(c, a);
    RpGeometry* cur = a->geometry;
    if (!src)
        return;
    int removed = 0;
    const RwMatrix* m = AtomicLtm(a);
    RpGeometry* cut = gBoxes.empty() || !m ? nullptr : BuildCut(src, m, gBoxes, &removed);
    if (cut) {
        RpAtomicSetGeometry(a, cut, rpATOMICSAMEBOUNDINGSPHERE);
        RpGeometryDestroy(cut); // the atomic holds it now
        c.geo = cut;
        c.ours = true;
        if (gLogged < 12) {
            ++gLogged;
            Log("GeoCut: model %d has %d hole box(es), %d of %d triangles cut", c.model, (int)gBoxes.size(), removed,
                src->numTriangles);
        }
    } else {
        if (cur != src)
            RpAtomicSetGeometry(a, src, rpATOMICSAMEBOUNDINGSPHERE); // our old copy goes away with this
        c.geo = src;
        c.ours = false;
    }
    c.atomic = a;
}

// back to GTA's own mesh (when the building is still there with our copy on it)
void Restore(Cut& c) {
    if (!c.ours || !StillThere(c))
        return;
    RpAtomic* a = EntityAtomic(c.ent);
    if (!a || a != c.atomic || a->geometry != c.geo)
        return;
    if (RpGeometry* g = ModelGeometry(c.ent))
        RpAtomicSetGeometry(a, g, rpATOMICSAMEBOUNDINGSPHERE);
    c.ours = false;
    c.geo = nullptr;
}

void Scan() {
    gScannedVersion = HoleVersion();
    gLastScan = GetTickCount();
    for (auto& kv : gCuts)
        kv.second.seen = false;
    static std::vector<CutBox> areas;
    const CVector cam = TheCamera.GetPosition();
    areas.clear();
    CarveAreas(cam, gConfig.renderDistance, areas);
    TerrainAreas(cam, gConfig.renderDistance, areas);
    static CEntity* found[256];
    for (const CutBox& area : areas) {
        short n = 0;
        CWorld::FindObjectsIntersectingCube(CVector(area.lo[0], area.lo[1], area.lo[2]),
                                            CVector(area.hi[0], area.hi[1], area.hi[2]), &n, 256, found, true, false,
                                            false, false, false);
        for (int i = 0; i < std::min<int>(n, 256); ++i) {
            CEntity* e = found[i];
            if (!e || e->m_nType != ENTITY_TYPE_BUILDING)
                continue;
            Cut& c = gCuts[e];
            if (!c.ent) {
                c.ent = e;
                c.model = e->m_nModelIndex;
            }
            c.seen = true;
        }
    }
    for (auto it = gCuts.begin(); it != gCuts.end();) {
        Cut& c = it->second;
        if (!c.seen || !StillThere(c)) {
            Restore(c);
            it = gCuts.erase(it);
            continue;
        }
        RpAtomic* a = EntityAtomic(c.ent);
        if (!a) { // not streamed in: it gets its holes when it comes
            c.atomic = nullptr;
            c.geo = nullptr;
            c.ours = false;
        } else if (a != c.atomic || a->geometry != c.geo || !c.bounds) {
            Apply(c, a);
        } else {
            BoxesFor(c, gBoxes);
            if (Signature(gBoxes) != c.sig)
                Apply(c, a);
        }
        ++it;
    }
}

void __cdecl HookPreRender() {
    gPreRenderHook.ccall<void>();
    GeoCutUpdate();
}
} // namespace

void GeoCutUpdate() {
    if (HoleVersion() != gScannedVersion || GetTickCount() - gLastScan > 500) {
        Scan();
        return;
    }
    // every frame: buildings that streamed in again, and the day / night colours of our copies
    for (auto it = gCuts.begin(); it != gCuts.end();) {
        Cut& c = it->second;
        if (!StillThere(c)) {
            it = gCuts.erase(it);
            continue;
        }
        RpAtomic* a = EntityAtomic(c.ent);
        if (!a) { // streamed out: its new atomic gets the holes again
            c.atomic = nullptr;
            c.geo = nullptr;
            c.ours = false;
        } else if (a != c.atomic || a->geometry != c.geo) {
            Apply(c, a);
        }
        if (a && c.ours && a->geometry == c.geo) {
            const ExtraVertColour* x = VertColours(c.geo);
            if (x && x->night && x->day)
                DayNightUpdate(a);
        }
        ++it;
    }
}

void InstallGeoCutHooks() {
    static bool done = false;
    if (done)
        return;
    done = true;
    gPreRenderHook = safetyhook::create_inline(reinterpret_cast<void*>(0x553910), reinterpret_cast<void*>(&HookPreRender));
    Log("Hooks: building holes %s", gPreRenderHook ? "ok" : "FAILED");
}

} // namespace mc
