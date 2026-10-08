#include "Terrain.h"

#include "CCamera.h"
#include "CColPoint.h"
#include "CEntity.h"
#include "CGame.h"
#include "CWaterLevel.h"
#include "CWorld.h"
#include "eSurfaceType.h"

#include <array>
#include <unordered_set>

#include "Carve.h"
#include "Draw3D.h"
#include "GtaWorld.h"
#include "Items.h"
#include "Soup.h"
#include "Textures.h"
#include "World.h"

namespace mc {

namespace {
enum TerrainMat : uint8_t {
    TM_GRASS = 0, TM_DIRT, TM_SAND, TM_RED_SAND, TM_GRAVEL, TM_CLAY, TM_MUD, TM_ROCK, TM_ROAD, TM_PAVEMENT,
    TM_TERRACOTTA, TM_CONCRETE, TM_COUNT
};

struct Column {
    float gz = 0.0f;     // GTA surface
    uint8_t mat = TM_DIRT;
    bool opened = false;
    int16_t top = 0;     // highest natural cell
    int16_t zMin = 0;    // lowest filled cell
    uint16_t topBlock = 0; // surface block read from the GTA texture (bricks, planks...), 0 = the material's own
};

constexpr int kStartDepth = 20;  // cells filled when a column is converted
constexpr int kMaxDepth = 63;    // bedrock

std::unordered_map<Int3, Column, Int3Hash> gColumns;
std::vector<Int3> gOpened;
uint32_t gVersion = 1;
// closed columns whose top block was mined from below: their GTA ground stays as a thin layer over the hole
std::unordered_set<Int3, Int3Hash> gUndermined;
// the GTA ground's height along the edges of open columns (key: x, y, side), from its visible triangles
std::unordered_map<Int3, std::array<float, 9>, Int3Hash> gEdges;
std::unordered_map<Int3, float, Int3Hash> gCorners;

Int3 Key(int x, int y) { return { x, y, 0 }; }

Column* Find(int x, int y) {
    auto it = gColumns.find(Key(x, y));
    return it == gColumns.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------- noise
uint32_t Hash3(int x, int y, int z, uint32_t salt) {
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u ^ salt * 2654435761u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

float Rand3(int x, int y, int z, uint32_t salt) { return (Hash3(x, y, z, salt) & 0xFFFFFF) / 16777216.0f; }

float Noise3(float x, float y, float z, uint32_t salt) {
    const int x0 = FloorI(x), y0 = FloorI(y), z0 = FloorI(z);
    const float fx = x - x0, fy = y - y0, fz = z - z0;
    auto s = [](float t) { return t * t * (3.0f - 2.0f * t); };
    const float sx = s(fx), sy = s(fy), sz = s(fz);
    float v[2][2][2];
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k)
                v[i][j][k] = Rand3(x0 + i, y0 + j, z0 + k, salt);
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    float x00 = lerp(v[0][0][0], v[1][0][0], sx), x10 = lerp(v[0][1][0], v[1][1][0], sx);
    float x01 = lerp(v[0][0][1], v[1][0][1], sx), x11 = lerp(v[0][1][1], v[1][1][1], sx);
    return lerp(lerp(x00, x10, sy), lerp(x01, x11, sy), sz);
}

// ---------------------------------------------------------------- what is under the GTA map
int TerrainMatFor(const GtaMaterial& m) {
    switch (m.block) {
    case ID_GRASS_BLOCK: return TM_GRASS;
    case ID_DIRT: return TM_DIRT;
    case ID_SAND: return TM_SAND;
    case ID_RED_SAND: return TM_RED_SAND;
    case ID_GRAVEL: return TM_GRAVEL;
    case ID_CLAY: return TM_CLAY;
    case ID_MUD: return TM_MUD;
    case ID_TERRACOTTA: return TM_TERRACOTTA;
    case ID_BLACKSTONE: return TM_ROAD;
    case ID_STONE_BRICKS: return TM_PAVEMENT;
    case ID_LIGHT_GRAY_CONCRETE: return TM_CONCRETE;
    case ID_STONE: return TM_ROCK; // rock, and the "default" surface much of the map uses
    default:
        return -1;
    }
}

struct Ore {
    uint16_t ore, deep;
    int minD, maxD;
    float region, cell;
    uint32_t salt;
};
const Ore kOres[] = {
    { ID_DIAMOND_ORE, ID_DEEPSLATE_DIAMOND_ORE, 46, 62, 0.010f, 0.40f, 11 },
    { ID_EMERALD_ORE, ID_DEEPSLATE_EMERALD_ORE, 20, 62, 0.004f, 0.30f, 12 },
    { ID_GOLD_ORE, ID_DEEPSLATE_GOLD_ORE, 24, 62, 0.014f, 0.40f, 13 },
    { ID_REDSTONE_ORE, ID_DEEPSLATE_REDSTONE_ORE, 40, 62, 0.030f, 0.45f, 14 },
    { ID_LAPIS_ORE, ID_DEEPSLATE_LAPIS_ORE, 24, 62, 0.012f, 0.40f, 15 },
    { ID_IRON_ORE, ID_DEEPSLATE_IRON_ORE, 5, 62, 0.045f, 0.40f, 16 },
    { ID_COPPER_ORE, ID_DEEPSLATE_COPPER_ORE, 5, 45, 0.035f, 0.40f, 17 },
    { ID_COAL_ORE, ID_DEEPSLATE_COAL_ORE, 3, 60, 0.055f, 0.45f, 18 },
};

uint16_t NaturalBlock(int mat, int d, int x, int y, int z) {
    if (d >= kMaxDepth || (d >= 59 && Rand3(x, y, z, 1) < (d - 58) / 5.0f))
        return ID_BEDROCK;
    switch (mat) {
    case TM_GRASS:
        if (d == 0) return ID_GRASS_BLOCK;
        if (d <= 3) return ID_DIRT;
        break;
    case TM_DIRT:
        if (d <= 3) return ID_DIRT;
        break;
    case TM_SAND:
        if (d <= 3) return ID_SAND;
        if (d <= 6) return ID_SANDSTONE;
        break;
    case TM_RED_SAND:
        if (d <= 2) return ID_RED_SAND;
        if (d <= 8) return ID_TERRACOTTA;
        break;
    case TM_GRAVEL:
        if (d <= 2) return ID_GRAVEL;
        if (d <= 3) return ID_DIRT;
        break;
    case TM_CLAY:
        if (d <= 1) return ID_CLAY;
        if (d <= 2) return ID_GRAVEL;
        if (d <= 3) return ID_DIRT;
        break;
    case TM_MUD:
        if (d <= 2) return ID_MUD;
        if (d <= 3) return ID_DIRT;
        break;
    case TM_ROAD:
        if (d == 0) return ID_BLACKSTONE;
        if (d == 1) return ID_GRAVEL;
        if (d <= 3) return ID_DIRT;
        break;
    case TM_PAVEMENT:
        if (d == 0) return ID_STONE_BRICKS;
        if (d == 1) return ID_GRAVEL;
        if (d <= 3) return ID_DIRT;
        break;
    case TM_TERRACOTTA:
        if (d <= 6) return ID_TERRACOTTA;
        break;
    case TM_CONCRETE:
        if (d == 0) return ID_LIGHT_GRAY_CONCRETE;
        if (d == 1) return ID_GRAVEL;
        if (d <= 3) return ID_DIRT;
        break;
    default:
        break;
    }
    const bool deep = d >= 36 + (int)(Rand3(x, y, 0, 2) * 4.0f);
    // ore veins: 3x3x3 regions that host a vein, and cells inside them
    const int rx = FloorDiv(x, 3), ry = FloorDiv(y, 3), rz = FloorDiv(z, 3);
    for (const Ore& o : kOres) {
        if (d < o.minD || d > o.maxD)
            continue;
        if (Rand3(rx, ry, rz, o.salt) < o.region && Rand3(x, y, z, o.salt + 100) < o.cell)
            return deep ? o.deep : o.ore;
    }
    // blobs of other stones, dirt and gravel
    if (Noise3(x / 5.0f, y / 5.0f, z / 4.0f, 3) > 0.74f) {
        switch (Hash3(FloorDiv(x, 8), FloorDiv(y, 8), FloorDiv(z, 8), 4) % 4) {
        case 0: return ID_GRANITE;
        case 1: return ID_DIORITE;
        case 2: return ID_ANDESITE;
        default: return deep ? ID_TUFF : ID_DIRT;
        }
    }
    if (d < 40 && Noise3(x / 4.0f, y / 4.0f, z / 3.0f, 5) > 0.80f)
        return ID_GRAVEL;
    return deep ? ID_DEEPSLATE : ID_STONE;
}

// the block `d` cells under the surface of a column
uint16_t ColBlock(const Column& c, int d, int x, int y, int z) {
    return d == 0 && c.topBlock ? c.topBlock : NaturalBlock(c.mat, d, x, y, z);
}

// GTA ground at a column centre (or `refZ` / `refMat` when there is none nearby)
void SampleGround(int x, int y, float refZ, int refMat, float* gz, int* mat) {
    *gz = refZ;
    *mat = refMat;
    // (from a little above the dug spot only: roofs and plants on the ground are not the ground)
    CVector from(x + 0.5f, y + 0.5f, refZ + 2.5f);
    const CVector to(x + 0.5f, y + 0.5f, refZ - 8.0f);
    for (int i = 0; i < 6; ++i) {
        CColPoint cp;
        CEntity* e = nullptr;
        if (!CWorld::ProcessLineOfSight(from, to, cp, e, true, false, false, false, false, false, false, false))
            return;
        const int surface = HitSurface(cp);
        if (cp.m_vecNormal.z > 0.3f && (!e || !IsPlantModel(e->m_nModelIndex)) && TerrainNaturalSurface(surface)) {
            *gz = cp.m_vecPoint.z;
            const int m = TerrainMatFor(MaterialForSurface(surface));
            if (m >= 0)
                *mat = m;
            return;
        }
        from = cp.m_vecPoint - CVector(0, 0, 0.05f);
    }
}

void Fill(int x, int y, const Column& c, int zFrom, int zTo) {
    for (int z = zFrom; z >= zTo; --z) {
        if (gWorld.GetBlock(x, y, z) != ID_AIR)
            continue; // something the player built down there
        gWorld.SetRaw(x, y, z, MakeVox(ColBlock(c, c.top - z, x, y, z), META_NATURAL));
    }
}

void Convert(int x, int y, float refZ, int refMat, uint16_t refTop) {
    float gz;
    int mat;
    SampleGround(x, y, refZ, refMat, &gz, &mat);
    Column c;
    c.gz = gz;
    c.mat = (uint8_t)mat;
    c.topBlock = mat == refMat ? refTop : 0; // the same kind of ground around the dig has the same surface
    c.top = (int16_t)((int)std::ceil(gz) - 2);
    c.zMin = (int16_t)(c.top - kStartDepth);
    Fill(x, y, c, c.top, c.zMin);
    gColumns[Key(x, y)] = c;
    gWorld.dirty = true;
}

void EnsureAround(int cx, int cy, float refZ, int refMat, int r, uint16_t refTop = 0) {
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (!Find(cx + dx, cy + dy))
                Convert(cx + dx, cy + dy, refZ, refMat, refTop);
}

void EnsureDepth(int cx, int cy, int z) {
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            Column* c = Find(cx + dx, cy + dy);
            if (!c)
                continue;
            const int floor = c->top - kMaxDepth;
            if (z - 6 >= c->zMin || c->zMin <= floor)
                continue;
            int to = std::max(floor, std::min<int>(c->zMin - 16, z - 10));
            Fill(cx + dx, cy + dy, *c, c->zMin - 1, to);
            c->zMin = (int16_t)to;
        }
}

void RebuildOpenedList() {
    ++gVersion;
    gOpened.clear();
    for (auto& kv : gColumns)
        if (kv.second.opened)
            gOpened.push_back(kv.first);
}

float CornerZ(int x, int y, float ref) {
    auto it = gCorners.find(Key(x, y));
    if (it != gCorners.end())
        return it->second;
    float z = ref;
    CColPoint cp;
    CEntity* e = nullptr;
    if (CWorld::ProcessVerticalLine(CVector((float)x, (float)y, ref + 3.0f), ref - 4.0f, cp, e, true, false, false, false,
                                    false, false, nullptr) &&
        std::fabs(cp.m_vecPoint.z - ref) < 3.0f)
        z = cp.m_vecPoint.z;
    gCorners[Key(x, y)] = z;
    return z;
}

// blocks dug out of natural ground keep the walls and the depth around them
void OnNaturalRemoved(int x, int y, int z, int block) {
    Column* c = Find(x, y);
    if (!c || z > c->top) {
        CarveNaturalRemoved(x, y, z, block); // inside a building or a cliff
        return;
    }
    const float gz = c->gz;
    const int mat = c->mat;
    const uint16_t top = c->topBlock;
    if (!c->opened && z == c->top)
        gUndermined.insert(Key(x, y)); // the GTA ground over it stays: a thin layer, mined on its own
    EnsureAround(x, y, gz, mat, 2, top); // (may rehash gColumns)
    EnsureDepth(x, y, z);
}

struct HookInstaller {
    HookInstaller() { gNaturalRemovedHook = &OnNaturalRemoved; }
} gHookInstaller;
} // namespace

// ================================================================ queries
bool TerrainIsConverted(int x, int y) { return Find(x, y) != nullptr; }

bool TerrainIsOpened(int x, int y) {
    const Column* c = Find(x, y);
    return c && c->opened;
}

float TerrainSurface(int x, int y) {
    const Column* c = Find(x, y);
    return c ? c->gz : -1e9f;
}

bool TerrainCapAt(int x, int y, int z, float* top) {
    const Column* c = Find(x, y);
    if (!c || c->opened || z != c->top + 1)
        return false;
    if (top)
        *top = c->gz;
    return true;
}

int TerrainCapBlock(int x, int y) {
    const Column* c = Find(x, y);
    return c ? ColBlock(*c, 0, x, y, c->top) : ID_DIRT;
}

bool TerrainIgnoreHit(const CVector& p, const CEntity* e) {
    if (e && e->m_nType != ENTITY_TYPE_BUILDING)
        return false;
    if (CarvedAt(p))
        return true; // a hole broken into a building
    if (gOpened.empty())
        return false;
    const Column* c = Find(FloorI(p.x), FloorI(p.y));
    return c && c->opened && std::fabs(p.z - c->gz) < 0.8f;
}

bool TerrainNear(const CVector& p, float radius) {
    if (gOpened.empty())
        return false;
    const int r = (int)std::ceil(radius);
    const int cx = FloorI(p.x), cy = FloorI(p.y);
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            const Column* c = Find(cx + dx, cy + dy);
            if (c && c->opened && p.z < c->gz + 3.0f)
                return true;
        }
    return false;
}

bool TerrainCameraUnderground(const CVector& cam) {
    const Column* c = Find(FloorI(cam.x), FloorI(cam.y));
    return c && cam.z < c->gz - 0.05f;
}

float TerrainDepthShade(int x, int y, int z) {
    const Column* c = Find(x, y);
    if (!c)
        return 1.0f;
    float d = (float)(c->top - z);
    return Clamp(1.0f - std::max(0.0f, d - 1.0f) * 0.03f, 0.35f, 1.0f);
}

bool RaycastCaps(const CVector& o, const CVector& d, float maxDist, Int3* cell, int* face, float* dist) {
    if (gColumns.empty())
        return false;
    int x = FloorI(o.x), y = FloorI(o.y), z = FloorI(o.z);
    int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
    auto inv = [](float v) { return std::fabs(v) < 1e-6f ? 1e30f : 1.0f / std::fabs(v); };
    float tdx = inv(d.x), tdy = inv(d.y), tdz = inv(d.z);
    float tmx = (d.x > 0 ? (x + 1 - o.x) : (o.x - x)) * tdx;
    float tmy = (d.y > 0 ? (y + 1 - o.y) : (o.y - y)) * tdy;
    float tmz = (d.z > 0 ? (z + 1 - o.z) : (o.z - z)) * tdz;
    float t = 0.0f;
    int f = -1;
    for (int i = 0; i < 256 && t <= maxDist; ++i) {
        float top;
        if (f >= 0 && f != FACE_TOP && TerrainCapAt(x, y, z, &top) && o.z + d.z * t <= top + 0.02f) {
            *cell = { x, y, z };
            *face = f;
            *dist = t;
            return true;
        }
        if (tmx < tmy && tmx < tmz) {
            x += sx;
            t = tmx;
            tmx += tdx;
            f = sx > 0 ? FACE_WEST : FACE_EAST;
        } else if (tmy < tmz) {
            y += sy;
            t = tmy;
            tmy += tdy;
            f = sy > 0 ? FACE_SOUTH : FACE_NORTH;
        } else {
            z += sz;
            t = tmz;
            tmz += tdz;
            f = sz > 0 ? FACE_BOTTOM : FACE_TOP;
        }
    }
    return false;
}

// ================================================================ digging
bool TerrainNaturalSurface(int surface) {
    const GtaMaterial m = MaterialForSurface(surface);
    switch (m.block) {
    case ID_GRASS_BLOCK: case ID_DIRT: case ID_MUD: case ID_SAND: case ID_RED_SAND: case ID_TERRACOTTA: case ID_CLAY:
    case ID_GRAVEL: case ID_BLACKSTONE: case ID_STONE_BRICKS:
        return true;
    case ID_STONE:
        return m.kind == GM_ROCK && surface != SURFACE_DEFAULT;
    default:
        return false;
    }
}

bool TerrainDiggable(const CColPoint& cp, CEntity* ent, const GtaMaterial& m) {
    (void)m; // the model's name does not matter here, only what the ground is made of
    if (CGame::currArea != 0)
        return false; // interiors stay as they are
    if (ent && ent->m_nType != ENTITY_TYPE_BUILDING)
        return false;
    if (cp.m_vecNormal.z < 0.55f)
        return false; // walls and cliffs: only the ground
    if (ent && IsPlantModel(ent->m_nModelIndex))
        return false;
    if (TerrainMatFor(MaterialForSurface(HitSurface(cp))) < 0)
        return false;
    // a bridge, a deck or a roof is no ground: there is open space (or water) under it
    const CVector from = cp.m_vecPoint - CVector(0, 0, 0.3f);
    CColPoint below;
    CEntity* be = nullptr;
    if (CWorld::ProcessLineOfSight(from, from - CVector(0, 0, 80.0f), below, be, true, false, false, false, false, false,
                                   false, false) &&
        from.z - below.m_vecPoint.z > 1.2f)
        return false;
    float water;
    if (CWaterLevel::GetWaterLevelNoWaves(from.x, from.y, from.z, &water) && water < from.z && water > from.z - 80.0f)
        return false;
    return true;
}

int TerrainOpenColumn(int x, int y, bool removeTop) {
    Column* c = Find(x, y);
    if (!c)
        return ID_AIR;
    const int top = c->top;
    const uint16_t surface = ColBlock(*c, 0, x, y, c->top);
    const bool was = c->opened;
    c->opened = true;
    gUndermined.erase(Key(x, y));
    if (!was) {
        gOpened.push_back(Key(x, y));
        gWorld.dirty = true;
        ++gVersion;
    }
    if (!removeTop)
        return ID_AIR;
    Voxel v = gWorld.Get(x, y, top);
    if (VoxBlock(v) != ID_AIR && (VoxMeta(v) & META_NATURAL)) {
        gWorld.Set(x, y, top, MakeVox(ID_AIR));
        return VoxBlock(v);
    }
    return surface;
}

int TerrainDig(const CColPoint& cp, CEntity* ent, const GtaMaterial& m) {
    (void)ent;
    (void)m;
    const int x = FloorI(cp.m_vecPoint.x), y = FloorI(cp.m_vecPoint.y);
    int mat = TerrainMatFor(MaterialForSurface(HitSurface(cp)));
    if (mat < 0)
        mat = TM_DIRT;
    // a paved, tiled or planked surface: what its texture showed is the top block
    uint16_t top = 0;
    if (m.kind == GM_BLOCK && IsSolidBlock(m.block) && Block(m.block).shape != SHAPE_COLUMN && Block(m.block).sound != SG_GRASS &&
        m.block != NaturalBlock(mat, 0, x, y, 0))
        top = m.block;
    EnsureAround(x, y, cp.m_vecPoint.z, mat, 2, top);
    static int logged = 0;
    if (logged < 10) {
        ++logged;
        Log("Dig: opened the ground at %d,%d (z %.1f, material %d)", x, y, cp.m_vecPoint.z, mat);
    }
    return TerrainOpenColumn(x, y, true);
}

void TerrainExplode(const CVector& at, float radius) {
    if (CGame::currArea != 0 || radius <= 0.0f)
        return;
    CColPoint cp;
    CEntity* e = nullptr;
    if (!CWorld::ProcessVerticalLine(at + CVector(0, 0, 1.0f), at.z - radius - 1.5f, cp, e, true, false, false, false, false,
                                     false, nullptr))
        return;
    const int cx = FloorI(cp.m_vecPoint.x), cy = FloorI(cp.m_vecPoint.y);
    const GtaMaterial m = MaterialForSurface(HitSurface(cp));
    if (!Find(cx, cy) && !TerrainDiggable(cp, e, m))
        return;
    int mat = TerrainMatFor(m);
    if (mat < 0)
        mat = TM_DIRT;
    const int r = (int)std::ceil(radius);
    EnsureAround(cx, cy, cp.m_vecPoint.z, mat, r + 2);
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            Column* c = Find(cx + dx, cy + dy);
            if (!c || c->opened)
                continue;
            CVector capCentre(cx + dx + 0.5f, cy + dy + 0.5f, c->top + 1.5f);
            if ((capCentre - at).Magnitude() <= radius * 0.9f)
                TerrainOpenColumn(cx + dx, cy + dy, false);
        }
}

// ================================================================ rendering
bool TerrainAnyOpening(const CVector& cam, float radius) {
    for (const Int3& k : gOpened) {
        float dx = k.x + 0.5f - cam.x, dy = k.y + 0.5f - cam.y;
        if (dx * dx + dy * dy < radius * radius)
            return true;
    }
    return false;
}

namespace {
bool IsOpen(int x, int y) {
    const Column* c = Find(x, y);
    return c && c->opened;
}

// the space of an open column: from deep down to a little over its GTA surface. A hair wider towards open
// neighbours (no seam), a hair narrower towards closed ones (the GTA ground overlaps the skirts: no slit).
CutBox ColumnBox(const Int3& k, const Column& c, float below) {
    float zlo = c.gz, zhi = c.gz;
    const int cx[4] = { 0, 1, 1, 0 }, cy[4] = { 0, 0, 1, 1 };
    for (int i = 0; i < 4; ++i) {
        const float z = CornerZ(k.x + cx[i], k.y + cy[i], c.gz);
        zlo = std::min(zlo, z);
        zhi = std::max(zhi, z);
    }
    CutBox b;
    b.lo[0] = k.x + (IsOpen(k.x - 1, k.y) ? -0.004f : 0.003f);
    b.hi[0] = k.x + 1.0f + (IsOpen(k.x + 1, k.y) ? 0.004f : -0.003f);
    b.lo[1] = k.y + (IsOpen(k.x, k.y - 1) ? -0.004f : 0.003f);
    b.hi[1] = k.y + 1.0f + (IsOpen(k.x, k.y + 1) ? 0.004f : -0.003f);
    b.lo[2] = zlo - below;
    b.hi[2] = zhi + 0.6f;
    return b;
}
} // namespace

uint32_t TerrainVersion() { return gVersion; }

bool TerrainOwnsCell(int x, int y, int z) {
    const Column* c = Find(x, y);
    return c && z <= c->top + 1;
}

void TerrainBoxesIn(const float lo[3], const float hi[3], std::vector<CutBox>& out) {
    for (const Int3& k : gOpened) {
        if (k.x > hi[0] + 0.1f || k.x + 1 < lo[0] - 0.1f || k.y > hi[1] + 0.1f || k.y + 1 < lo[1] - 0.1f)
            continue;
        const Column* c = Find(k.x, k.y);
        if (!c)
            continue;
        const CutBox b = ColumnBox(k, *c, 0.8f);
        if (b.lo[2] <= hi[2] && b.hi[2] >= lo[2])
            out.push_back(b);
    }
}

void TerrainAreas(const CVector& cam, float radius, std::vector<CutBox>& out) {
    std::unordered_map<Int3, CutBox, Int3Hash> areas;
    for (const Int3& k : gOpened) {
        const Column* c = Find(k.x, k.y);
        if (!c)
            continue;
        const float dx = k.x + 0.5f - cam.x, dy = k.y + 0.5f - cam.y;
        if (dx * dx + dy * dy > radius * radius)
            continue;
        const Int3 key{ FloorDiv(k.x, 16), FloorDiv(k.y, 16), 0 };
        auto it = areas.find(key);
        if (it == areas.end()) {
            CutBox b;
            b.lo[0] = (float)k.x; b.lo[1] = (float)k.y; b.lo[2] = c->gz;
            b.hi[0] = k.x + 1.0f; b.hi[1] = k.y + 1.0f; b.hi[2] = c->gz;
            areas.emplace(key, b);
        } else {
            CutBox& b = it->second;
            b.lo[0] = std::min(b.lo[0], (float)k.x); b.lo[1] = std::min(b.lo[1], (float)k.y);
            b.lo[2] = std::min(b.lo[2], c->gz);
            b.hi[0] = std::max(b.hi[0], k.x + 1.0f); b.hi[1] = std::max(b.hi[1], k.y + 1.0f);
            b.hi[2] = std::max(b.hi[2], c->gz);
        }
    }
    for (auto& kv : areas) {
        CutBox b = kv.second;
        b.lo[0] -= 0.5f; b.lo[1] -= 0.5f; b.lo[2] -= 3.0f;
        b.hi[0] += 0.5f; b.hi[1] += 0.5f; b.hi[2] += 3.0f;
        out.push_back(b);
    }
}

void TerrainEmitHoleVolume(const CVector& cam, float radius) {
    static const int kFace[6][4][3] = {
        { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } }, { { 0, 1, 0 }, { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 1 } },
        { { 1, 1, 0 }, { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 } }, { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 } },
        { { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } }, { { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 } },
    };
    const RwUInt32 col = d3::Argb(0, 0, 0, 0);
    for (const Int3& k : gOpened) {
        const Column* c = Find(k.x, k.y);
        if (!c)
            continue;
        const float dx = k.x + 0.5f - cam.x, dy = k.y + 0.5f - cam.y;
        if (dx * dx + dy * dy > radius * radius)
            continue;
        const CutBox b = ColumnBox(k, *c, (float)kMaxDepth + 4.0f);
        const CVector mid((b.lo[0] + b.hi[0]) * 0.5f, (b.lo[1] + b.hi[1]) * 0.5f, (b.lo[2] + b.hi[2]) * 0.5f);
        if (!TheCamera.IsSphereVisible(mid, (b.hi[2] - b.lo[2]) * 0.5f + 1.0f))
            continue;
        for (int f = 0; f < 6; ++f) {
            // between two open columns nothing needs marking: an outer face is in front of it
            if (f < 4 && IsOpen(k.x + FACE_DIR[f].x, k.y + FACE_DIR[f].y))
                continue;
            CVector p[4];
            for (int i = 0; i < 4; ++i)
                p[i] = CVector(kFace[f][i][0] ? b.hi[0] : b.lo[0], kFace[f][i][1] ? b.hi[1] : b.lo[1],
                               kFace[f][i][2] ? b.hi[2] : b.lo[2]);
            d3::Quad(p[0], p[1], p[2], p[3], 0.0f, 0.0f, 0.0f, 0.0f, col);
        }
    }
}

namespace {
// the visible GTA ground along an open column's edge towards its neighbour, at 9 points (corner to corner)
void EdgeHeights(const Int3& k, int side, const Column& c, float h[9], int& budget) {
    static const int kEdge[4][4] = { { 1, 0, 1, 1 }, { 0, 1, 0, 0 }, { 1, 1, 0, 1 }, { 0, 0, 1, 0 } };
    static const int kDir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    const int* e = kEdge[side];
    const float ha = CornerZ(k.x + e[0], k.y + e[1], c.gz), hb = CornerZ(k.x + e[2], k.y + e[3], c.gz);
    const Int3 key{ k.x, k.y, side };
    auto it = gEdges.find(key);
    if (it != gEdges.end()) {
        for (int i = 0; i < 9; ++i)
            h[i] = it->second[i];
        return;
    }
    for (int i = 0; i < 9; ++i)
        h[i] = ha + (hb - ha) * (i / 8.0f);
    if (budget <= 0)
        return;
    --budget;
    const CVector mid(k.x + 0.5f + kDir[side][0] * 0.5f, k.y + 0.5f + kDir[side][1] * 0.5f, c.gz);
    SoupPrepare(mid - CVector(4, 4, 4), mid + CVector(4, 4, 4));
    static std::vector<SoupFace> f;
    std::array<float, 9> out;
    for (int i = 0; i < 9; ++i) {
        const float t = i / 8.0f;
        // a hair inside the neighbour (whose ground is still there)
        const CVector p(k.x + e[0] + (e[2] - e[0]) * t + kDir[side][0] * 0.02f,
                        k.y + e[1] + (e[3] - e[1]) * t + kDir[side][1] * 0.02f, c.gz);
        SoupLine(2, p, f);
        float best = -1e9f;
        for (const SoupFace& x : f)
            if (x.facing > 0 && x.at > c.gz - 3.0f && x.at < c.gz + 3.0f)
                best = std::max(best, x.at);
        out[i] = best > -1e8f ? best : h[i];
        h[i] = out[i];
    }
    gEdges[key] = out;
}
} // namespace

void TerrainEmitCapBottoms(const CVector& cam, float radius, float light) {
    for (auto it = gUndermined.begin(); it != gUndermined.end();) {
        const Column* c = Find(it->x, it->y);
        if (!c || c->opened) {
            it = gUndermined.erase(it);
            continue;
        }
        const Int3 k = *it++;
        const float dx = k.x + 0.5f - cam.x, dy = k.y + 0.5f - cam.y;
        if (dx * dx + dy * dy > radius * radius || gWorld.GetBlock(k.x, k.y, c->top) != ID_AIR)
            continue;
        // the underside of the GTA ground's layer, seen from the dug-out space below
        const float z = (float)(c->top + 1);
        const TileUV uv = AtlasTileUV(BlockFaceTile(ColBlock(*c, 0, k.x, k.y, c->top), FACE_BOTTOM, 0));
        const CVector q[4] = { CVector((float)k.x, (float)k.y + 1, z), CVector((float)k.x + 1, (float)k.y + 1, z),
                               CVector((float)k.x + 1, (float)k.y, z), CVector((float)k.x, (float)k.y, z) };
        const float us[4] = { uv.u0, uv.u1, uv.u1, uv.u0 }, vs[4] = { uv.v1, uv.v1, uv.v0, uv.v0 };
        d3::QuadUV(q, us, vs, d3::Gray(light * 0.5f));
    }
}

void TerrainEmitSkirts(const CVector& cam, float radius, float light) {
    struct Side {
        int dx, dy;
        int ax, ay, bx, by; // corner offsets of the shared edge
        float shade;
    };
    static const Side kSides[4] = {
        { 1, 0, 1, 0, 1, 1, 0.6f },  { -1, 0, 0, 1, 0, 0, 0.6f },
        { 0, 1, 1, 1, 0, 1, 0.8f },  { 0, -1, 0, 0, 1, 0, 0.8f },
    };
    int budget = 6; // edges measured per frame (the rest use the corners until then)
    for (const Int3& k : gOpened) {
        const Column* c = Find(k.x, k.y);
        if (!c)
            continue;
        float dx = k.x + 0.5f - cam.x, dy = k.y + 0.5f - cam.y;
        if (dx * dx + dy * dy > radius * radius)
            continue;
        for (const Side& s : kSides) {
            const Column* n = Find(k.x + s.dx, k.y + s.dy);
            if (!n || n->opened)
                continue;
            const float bottom = (float)(n->top + 1);
            const int side = (int)(&s - kSides);
            float h[9];
            EdgeHeights(k, side, *c, h, budget);
            const int block = gWorld.GetBlock(k.x + s.dx, k.y + s.dy, n->top) != ID_AIR
                                ? gWorld.GetBlock(k.x + s.dx, k.y + s.dy, n->top)
                                : ColBlock(*n, 0, k.x + s.dx, k.y + s.dy, n->top);
            const TileUV uv = AtlasTileUV(BlockFaceTile(block, FACE_NORTH, 0));
            const float tv = uv.v1 - uv.v0, tu = uv.u1 - uv.u0;
            const CVector a((float)(k.x + s.ax), (float)(k.y + s.ay), bottom), b((float)(k.x + s.bx), (float)(k.y + s.by), bottom);
            for (int i = 0; i < 8; ++i) {
                const float t0 = i / 8.0f, t1 = (i + 1) / 8.0f;
                const float h0 = std::max(h[i], bottom), h1 = std::max(h[i + 1], bottom);
                if (h0 <= bottom + 0.005f && h1 <= bottom + 0.005f)
                    continue;
                const CVector p0 = a + (b - a) * t0, p1 = a + (b - a) * t1;
                const CVector q[4] = { p0, p1, CVector(p1.x, p1.y, h1), CVector(p0.x, p0.y, h0) };
                const float us[4] = { uv.u0 + tu * t0, uv.u0 + tu * t1, uv.u0 + tu * t1, uv.u0 + tu * t0 };
                const float vs[4] = { uv.v0 + tv * Clamp(h0 - bottom, 0.0f, 1.0f), uv.v0 + tv * Clamp(h1 - bottom, 0.0f, 1.0f),
                                      uv.v0, uv.v0 };
                d3::QuadUV(q, us, vs, d3::Gray(light * s.shade));
            }
        }
    }
}

// ================================================================ bookkeeping
void TerrainClear() {
    gColumns.clear();
    gOpened.clear();
    gCorners.clear();
    gUndermined.clear();
    gEdges.clear();
    ++gVersion;
}

void TerrainWrite(FILE* f) {
    uint32_t n = (uint32_t)gColumns.size() | 0x80000000u; // high bit: columns with a surface block
    fwrite(&n, 4, 1, f);
    for (auto& kv : gColumns) {
        const Column& c = kv.second;
        int32_t xy[2] = { kv.first.x, kv.first.y };
        fwrite(xy, sizeof(xy), 1, f);
        fwrite(&c.gz, 4, 1, f);
        uint8_t b[2] = { c.mat, (uint8_t)(c.opened ? 1 : 0) };
        fwrite(b, 2, 1, f);
        int16_t z[2] = { c.top, c.zMin };
        fwrite(z, sizeof(z), 1, f);
        fwrite(&c.topBlock, 2, 1, f);
    }
}

bool TerrainRead(FILE* f) {
    TerrainClear();
    uint32_t n = 0;
    if (fread(&n, 4, 1, f) != 1)
        return false;
    const bool withTop = (n & 0x80000000u) != 0;
    n &= 0x7FFFFFFFu;
    if (n > 2000000)
        return false;
    for (uint32_t i = 0; i < n; ++i) {
        int32_t xy[2];
        Column c;
        uint8_t b[2];
        int16_t z[2];
        if (fread(xy, sizeof(xy), 1, f) != 1 || fread(&c.gz, 4, 1, f) != 1 || fread(b, 2, 1, f) != 1 ||
            fread(z, sizeof(z), 1, f) != 1)
            return false;
        c.mat = b[0] < TM_COUNT ? b[0] : (uint8_t)TM_DIRT;
        c.opened = b[1] != 0;
        c.top = z[0];
        c.zMin = z[1];
        if (withTop && (fread(&c.topBlock, 2, 1, f) != 1 || !IsSolidBlock(c.topBlock)))
            c.topBlock = 0;
        gColumns[Key(xy[0], xy[1])] = c;
    }
    RebuildOpenedList();
    return true;
}

} // namespace mc
