#include "Soup.h"

#include <algorithm>
#include <unordered_map>

#include "CColPoint.h"
#include "CEntity.h"
#include "CWorld.h"
#include "RenderWare.h"

#include "GtaTex.h"
#include "GtaWorld.h"
#include "Terrain.h"

namespace mc {

namespace {
struct Tri {
    CVector a, b, c, n; // n as the winding says (see gWinding)
    uint16_t block;
};
std::vector<Tri> gTris;
// per axis: the triangles by the 1 m square their shadow on the other two axes covers
std::unordered_map<int64_t, std::vector<int>> gBins[3];
std::vector<int> gBig[3]; // huge triangles (big ground pieces): tested by every line
struct Loaded {
    int16_t model;
    float x, y;
};
std::unordered_map<CEntity*, Loaded> gLoaded;
struct TexLite {
    uint16_t block;
    bool skip;
};
std::unordered_map<RwTexture*, TexLite> gTexCache;
// Which way the triangles of GTA's models face: decided by comparing them with the collision's normals.
int gWinding = 0; // 0: not known yet (taken as +1)
int gVotes = 0;
constexpr int kBinLimit = 2048;

float C(const CVector& v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }
void SetC(CVector& v, int a, float x) {
    if (a == 0)
        v.x = x;
    else if (a == 1)
        v.y = x;
    else
        v.z = x;
}
int64_t Key(int u, int v) { return ((int64_t)u << 32) ^ (uint32_t)v; }
float Sign() { return gWinding < 0 ? -1.0f : 1.0f; }

void AddTri(const CVector& a, const CVector& b, const CVector& c, uint16_t block) {
    CVector n = CVector::Cross(b - a, c - a);
    const float len = n.Magnitude();
    if (len < 1e-6f)
        return;
    n = n * (1.0f / len);
    const int idx = (int)gTris.size();
    gTris.push_back({ a, b, c, n, block });
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(C(n, axis)) < 1e-4f)
            continue; // edge-on: a line along this axis never crosses it
        const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
        const int u0 = FloorI(std::min({ C(a, a1), C(b, a1), C(c, a1) })), u1 = FloorI(std::max({ C(a, a1), C(b, a1), C(c, a1) }));
        const int v0 = FloorI(std::min({ C(a, a2), C(b, a2), C(c, a2) })), v1 = FloorI(std::max({ C(a, a2), C(b, a2), C(c, a2) }));
        if ((int64_t)(u1 - u0 + 1) * (v1 - v0 + 1) > kBinLimit) {
            gBig[axis].push_back(idx);
            continue;
        }
        for (int v = v0; v <= v1; ++v)
            for (int u = u0; u <= u1; ++u)
                gBins[axis][Key(u, v)].push_back(idx);
    }
}

// Shadows, graffiti, tyre marks and the like are painted onto walls: not walls themselves. (By the name only: the
// transparency read from a texture is not reliable enough - whole walls would go missing.)
bool Decal(const RwTexture* tex) {
    std::string n(tex->name, strnlen(tex->name, sizeof(tex->name)));
    for (auto& ch : n)
        ch = (char)tolower((unsigned char)ch);
    static const char* const kDecal[] = { "shad", "decal", "graf", "tag_", "skid", "blood", "smoke", "cloud", "corona",
                                          "flare", "particle", "reflect", nullptr };
    for (const char* const* w = kDecal; *w; ++w)
        if (n.find(*w) != std::string::npos)
            return true;
    return false;
}

void LoadEntity(CEntity* e) {
    ForEachTriangle(e, [](const CVector& a, const CVector& b, const CVector& c, RwTexture* tex, uint32_t) {
        uint16_t block = 0;
        if (tex) {
            auto it = gTexCache.find(tex);
            if (it == gTexCache.end())
                it = gTexCache.emplace(tex, TexLite{ TextureInfo(tex).block, Decal(tex) }).first;
            if (it->second.skip)
                return;
            block = it->second.block;
        }
        AddTri(a, b, c, block);
    });
}

// raw: without the winding sign
void LineRaw(int axis, const CVector& p, std::vector<SoupFace>& out, bool filter) {
    out.clear();
    const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
    const float u = C(p, a1), v = C(p, a2);
    auto test = [&](int idx) {
        const Tri& t = gTris[idx];
        const float ax = C(t.a, a1), ay = C(t.a, a2), bx = C(t.b, a1), by = C(t.b, a2), cx = C(t.c, a1), cy = C(t.c, a2);
        const float d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        if (std::fabs(d) < 1e-9f)
            return;
        const float l1 = ((by - cy) * (u - cx) + (cx - bx) * (v - cy)) / d;
        const float l2 = ((cy - ay) * (u - cx) + (ax - cx) * (v - cy)) / d;
        const float l3 = 1.0f - l1 - l2;
        constexpr float e = -1e-5f;
        if (l1 < e || l2 < e || l3 < e)
            return;
        const float at = l1 * C(t.a, axis) + l2 * C(t.b, axis) + l3 * C(t.c, axis);
        if (filter) {
            CVector pt = p;
            SetC(pt, axis, at);
            if (TerrainIgnoreHit(pt, nullptr))
                return; // cut away (a hole broken in, or dug-open ground)
        }
        const float na = C(t.n, axis);
        out.push_back({ at, (int8_t)(na > 0.0f ? 1 : -1), t.block, t.n });
    };
    auto it = gBins[axis].find(Key(FloorI(u), FloorI(v)));
    if (it != gBins[axis].end())
        for (int idx : it->second)
            test(idx);
    for (int idx : gBig[axis])
        test(idx);
    std::sort(out.begin(), out.end(), [](const SoupFace& x, const SoupFace& y) { return x.at < y.at; });
    // the same face found twice (on the edge between two triangles)
    size_t w = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        if (w > 0 && std::fabs(out[i].at - out[w - 1].at) < 1e-4f && out[i].facing == out[w - 1].facing)
            continue;
        out[w++] = out[i];
    }
    out.resize(w);
}

// compares the triangles with the collision where both are hit: which side of a triangle is its front
void Vote(const CVector& centre) {
    static std::vector<SoupFace> f;
    static const CVector kFrom[5] = { { 0, 0, 0 }, { 3, 2, 1 }, { -2, 3, -1 }, { 2, -3, 2 }, { -3, -2, 0.5f } };
    for (int o = 0; o < 5 && gWinding == 0; ++o)
    for (int axis = 0; axis < 3 && gWinding == 0; ++axis)
        for (int s = -1; s <= 1 && gWinding == 0; s += 2) {
            const CVector from = centre + kFrom[o];
            CVector to = from;
            SetC(to, axis, C(from, axis) + s * 12.0f);
            CColPoint cp;
            CEntity* e = nullptr;
            if (!CWorld::ProcessLineOfSight(from, to, cp, e, true, false, false, false, false, false, false, false))
                continue;
            if (!e || IsPlantModel(e->m_nModelIndex) || std::fabs(C(cp.m_vecNormal, axis)) < 0.7f)
                continue;
            LineRaw(axis, cp.m_vecPoint, f, false);
            const float hit = C(cp.m_vecPoint, axis);
            const SoupFace* best = nullptr;
            for (const SoupFace& x : f)
                if (std::fabs(x.at - hit) < 0.15f && (!best || std::fabs(x.at - hit) < std::fabs(best->at - hit)))
                    best = &x;
            if (!best)
                continue;
            gVotes += (best->facing > 0) == (C(cp.m_vecNormal, axis) > 0.0f) ? 1 : -1;
            if (std::abs(gVotes) >= 8) {
                gWinding = gVotes > 0 ? 1 : -1;
                Log("Soup: GTA triangles face %s (votes %d, %d triangles loaded)", gWinding > 0 ? "as wound" : "against their winding",
                    gVotes, (int)gTris.size());
                return;
            }
        }
}
} // namespace

void SoupClear() {
    gTris.clear();
    for (auto& b : gBins)
        b.clear();
    for (auto& b : gBig)
        b.clear();
    gLoaded.clear();
    gTexCache.clear();
}

void SoupPrepare(const CVector& lo, const CVector& hi) {
    if (gTris.size() > 600000)
        SoupClear();
    static CEntity* found[512];
    short n = 0;
    CWorld::FindObjectsIntersectingCube(lo, hi, &n, 512, found, true, false, false, false, false);
    for (int i = 0; i < std::min<int>(n, 512); ++i) {
        CEntity* e = found[i];
        if (!e || e->m_nType != ENTITY_TYPE_BUILDING || !e->m_pRwObject || IsPlantModel(e->m_nModelIndex))
            continue;
        const CVector pos = e->GetPosition();
        auto it = gLoaded.find(e);
        if (it != gLoaded.end() && it->second.model == e->m_nModelIndex && it->second.x == pos.x && it->second.y == pos.y)
            continue;
        if (it != gLoaded.end()) {
            // a different building in that pool slot: start over (its triangles cannot be taken out)
            SoupClear();
            SoupPrepare(lo, hi);
            return;
        }
        LoadEntity(e);
        gLoaded[e] = { e->m_nModelIndex, pos.x, pos.y };
    }
    if (gWinding == 0)
        Vote((lo + hi) * 0.5f);
}

void SoupLine(int axis, const CVector& p, std::vector<SoupFace>& out) {
    LineRaw(axis, p, out, false);
    if (gWinding < 0)
        for (SoupFace& f : out) {
            f.facing = (int8_t)-f.facing;
            f.n = f.n * -1.0f;
        }
}

bool SoupSolidAt(const CVector& p) {
    if (CarvedAt(p))
        return false;
    // Every axis direction votes: the first face it meets seen from behind (we are inside it) or from the front.
    // Nothing above is the sky, nothing below is the earth under the world. Faces on both sides of one axis seen
    // from behind (a wall, a deck, a roof and a floor) settle it at once.
    static std::vector<SoupFace> f;
    int in = 0, out = 0;
    for (int axis = 2; axis >= 0; --axis) {
        SoupLine(axis, p, f);
        const float x = C(p, axis);
        int next = -1, prev = -1;
        for (int i = 0; i < (int)f.size(); ++i) {
            if (f[i].at > x) {
                next = i;
                break;
            }
            prev = i;
        }
        const bool backNext = next >= 0 && f[next].facing > 0;
        const bool backPrev = prev >= 0 && f[prev].facing < 0;
        if (backNext && backPrev)
            return true;
        if (next >= 0)
            (backNext ? in : out)++;
        else if (axis == 2)
            ++out; // the sky
        if (prev >= 0) {
            // the ground right under another ground layer (a road on the earth): still the ground
            const bool layer = axis == 2 && backNext && x - f[prev].at < 0.25f;
            (backPrev || layer ? in : out)++;
        } else if (axis == 2) {
            ++in; // nothing below at all: the earth
        }
    }
    return in > out;
}

} // namespace mc
