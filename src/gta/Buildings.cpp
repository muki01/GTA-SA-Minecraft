#include "Buildings.h"

#include <unordered_map>

#include "CBuilding.h"
#include "CColModel.h"
#include "CCollisionData.h"
#include "CEntity.h"
#include "CPlayerPed.h"
#include "CPools.h"
#include "RenderWare.h"
#include "common.h"

#include "Game.h"
#include "GtaTex.h"
#include "GtaWorld.h"
#include "Items.h"
#include "World.h"

namespace mc {

namespace {
// version 0.7 turned whole buildings into blocks and took the models out of the world. These are the
// buildings it converted: their blocks are taken away again as soon as the model is in memory.
struct Record {
    int32_t model = -1;
    float x = 0, y = 0, z = 0;
    int32_t lodModel = -1;
    float lx = 0, ly = 0, lz = 0;
};
std::vector<Record> gRecords;
std::unordered_map<int, uint8_t> gModelClass; // model id -> 1 structure, 2 landscape
float gScanTimer = 0.0f;

// ground the landscape is made of
bool GroundSurface(int surface) {
    const GtaMaterial m = MaterialForSurface(surface);
    switch (m.block) {
    case ID_GRASS_BLOCK: case ID_DIRT: case ID_MUD: case ID_SAND: case ID_RED_SAND: case ID_TERRACOTTA: case ID_CLAY:
    case ID_GRAVEL: case ID_BLACKSTONE: case ID_STONE_BRICKS:
        return true;
    case ID_STONE:
        return m.kind == GM_ROCK && surface != 0; // natural rock (surface 0 is the "default" most walls use)
    default:
        return false;
    }
}

// ---- triangle / box overlap (Akenine-Moller), box centre c, half size h
bool AxisTest(float a, float b, float fa, float fb, float p0a, float p0b, float p1a, float p1b, float ha, float hb) {
    const float p0 = a * p0a + b * p0b, p1 = a * p1a + b * p1b;
    const float mn = std::min(p0, p1), mx = std::max(p0, p1);
    const float rad = fa * ha + fb * hb;
    return !(mn > rad || mx < -rad);
}

bool TriBoxOverlap(const float c[3], float h, const CVector& A, const CVector& B, const CVector& C) {
    const float v0[3] = { A.x - c[0], A.y - c[1], A.z - c[2] };
    const float v1[3] = { B.x - c[0], B.y - c[1], B.z - c[2] };
    const float v2[3] = { C.x - c[0], C.y - c[1], C.z - c[2] };
    for (int i = 0; i < 3; ++i) {
        const float mn = std::min(v0[i], std::min(v1[i], v2[i])), mx = std::max(v0[i], std::max(v1[i], v2[i]));
        if (mn > h || mx < -h)
            return false;
    }
    const float e[3][3] = { { v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2] },
                            { v2[0] - v1[0], v2[1] - v1[1], v2[2] - v1[2] },
                            { v0[0] - v2[0], v0[1] - v2[1], v0[2] - v2[2] } };
    const float* v[3] = { v0, v1, v2 };
    for (int i = 0; i < 3; ++i) {
        const float* p = v[i];
        const float* q = v[(i + 2) % 3];
        const float ex = e[i][0], ey = e[i][1], ez = e[i][2];
        const float fx = std::fabs(ex), fy = std::fabs(ey), fz = std::fabs(ez);
        if (!AxisTest(-ez, ey, fz, fy, p[1], p[2], q[1], q[2], h, h))
            return false;
        if (!AxisTest(ez, -ex, fz, fx, p[0], p[2], q[0], q[2], h, h))
            return false;
        if (!AxisTest(-ey, ex, fy, fx, p[0], p[1], q[0], q[1], h, h))
            return false;
    }
    const float n[3] = { e[0][1] * e[1][2] - e[0][2] * e[1][1], e[0][2] * e[1][0] - e[0][0] * e[1][2],
                         e[0][0] * e[1][1] - e[0][1] * e[1][0] };
    const float d = -(n[0] * v0[0] + n[1] * v0[1] + n[2] * v0[2]);
    const float r = h * (std::fabs(n[0]) + std::fabs(n[1]) + std::fabs(n[2]));
    return std::fabs(d) <= r;
}

bool IsGlass(uint16_t b) { return b == ID_GLASS || Block(b).render == RENDER_TRANSLUCENT; }

struct Cell {
    uint16_t block;
    float weight;
};

// the blocks version 0.7 made of this building (the same way it made them)
bool BuildingCells(CEntity* e, std::unordered_map<Int3, Cell, Int3Hash>& cells) {
    bool tooBig = false;
    const bool ok = ForEachTriangle(e, [&](const CVector& a, const CVector& b, const CVector& c, RwTexture* tex, uint32_t color) {
        if (tooBig)
            return;
        uint16_t block = 0;
        if (tex) {
            const TexInfo& ti = TextureInfo(tex);
            if (ti.skip)
                return;
            block = ti.block;
        }
        if (!block) {
            if (((color >> 24) & 255) < 100)
                return;
            block = BlockForColor((color >> 16) & 255, (color >> 8) & 255, color & 255);
        }
        const float area = CVector::Cross(b - a, c - a).Magnitude() * 0.5f;
        if (area < 1e-4f)
            return;
        const float weight = std::min(area, 2.0f) * (IsGlass(block) ? 1.5f : 1.0f);
        const int x0 = FloorI(std::min(a.x, std::min(b.x, c.x))), x1 = FloorI(std::max(a.x, std::max(b.x, c.x)));
        const int y0 = FloorI(std::min(a.y, std::min(b.y, c.y))), y1 = FloorI(std::max(a.y, std::max(b.y, c.y)));
        const int z0 = FloorI(std::min(a.z, std::min(b.z, c.z))), z1 = FloorI(std::max(a.z, std::max(b.z, c.z)));
        if ((double)(x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > 3.0e6)
            return;
        for (int z = z0; z <= z1; ++z)
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const float centre[3] = { x + 0.5f, y + 0.5f, z + 0.5f };
                    if (!TriBoxOverlap(centre, 0.5f, a, b, c))
                        continue;
                    Cell& cell = cells[{ x, y, z }];
                    if (weight > cell.weight) {
                        cell.weight = weight;
                        cell.block = block;
                    }
                }
        if (cells.size() > 400000)
            tooBig = true;
    });
    return ok && !cells.empty();
}
} // namespace

bool IsStructureModel(CEntity* e) {
    if (!e || e->m_nType != ENTITY_TYPE_BUILDING)
        return false;
    const int model = e->m_nModelIndex;
    auto it = gModelClass.find(model);
    if (it != gModelClass.end())
        return it->second == 1;
    CColModel* col = e->GetColModel();
    if (!col || !col->m_pColData)
        return true; // collision not streamed in yet: ask again later
    CCollisionData* d = col->m_pColData;
    int ground = 0, total = 0;
    for (int i = 0; i < d->m_nNumTriangles; ++i) {
        ++total;
        if (GroundSurface(d->m_pTriangles[i].m_nMaterial))
            ++ground;
    }
    for (int i = 0; i < d->m_nNumBoxes; ++i) {
        total += 2;
        if (GroundSurface(d->m_pBoxes[i].m_nMaterial))
            ground += 2;
    }
    // mostly earth, grass, sand or road: a piece of the landscape
    const bool structure = total > 0 && ground * 2 < total;
    gModelClass[model] = structure ? 1 : 2;
    return structure;
}

void BuildingsUpdate(float dt) {
    if (gRecords.empty())
        return;
    gScanTimer -= dt;
    if (gScanTimer > 0.0f)
        return;
    gScanTimer = 0.5f;
    auto* pool = CPools::ms_pBuildingPool;
    CPlayerPed* player = FindPlayerPed();
    if (!pool || !player)
        return;
    const CVector pp = player->GetPosition();
    // one building per round: give back the nearest one that is loaded
    int best = -1;
    float bestD = 1e18f;
    for (size_t i = 0; i < gRecords.size(); ++i) {
        const Record& r = gRecords[i];
        const float dx = r.x - pp.x, dy = r.y - pp.y, d2 = dx * dx + dy * dy;
        if (d2 < 400.0f * 400.0f && d2 < bestD) {
            bestD = d2;
            best = (int)i;
        }
    }
    if (best < 0)
        return;
    std::vector<int> nearby;
    for (size_t i = 0; i < gRecords.size(); ++i) {
        const float dx = gRecords[i].x - pp.x, dy = gRecords[i].y - pp.y;
        if (dx * dx + dy * dy < 400.0f * 400.0f)
            nearby.push_back((int)i);
    }
    for (int i = 0; i < pool->m_nSize; ++i) {
        CBuilding* b = pool->GetAt(i);
        if (!b || !b->m_pRwObject)
            continue;
        const CVector p = b->GetPosition();
        for (int ri : nearby) {
            const Record& r = gRecords[ri];
            if (b->m_nModelIndex != r.model || std::fabs(p.x - r.x) > 0.6f || std::fabs(p.y - r.y) > 0.6f ||
                std::fabs(p.z - r.z) > 0.6f)
                continue;
            std::unordered_map<Int3, Cell, Int3Hash> cells;
            int removed = 0;
            if (BuildingCells(b, cells))
                for (auto& kv : cells) {
                    const Int3& c = kv.first;
                    // only what the conversion put there; what the player built or changed stays
                    if (gWorld.Get(c.x, c.y, c.z) == MakeVox(kv.second.block)) {
                        gWorld.SetRaw(c.x, c.y, c.z, MakeVox(ID_AIR));
                        ++removed;
                    }
                }
            Log("Buildings: model %d '%s' is a GTA building again (%d blocks taken away)", (int)r.model,
                GtaModelName(r.model), removed);
            gRecords.erase(gRecords.begin() + ri);
            gWorld.dirty = true;
            return; // one per round
        }
    }
}

void BuildingsClear() {
    gRecords.clear();
    gModelClass.clear();
}

void BuildingsWrite(FILE* f) {
    const uint32_t n = (uint32_t)gRecords.size();
    fwrite(&n, 4, 1, f);
    if (n)
        fwrite(gRecords.data(), sizeof(Record), n, f);
}

bool BuildingsRead(FILE* f) {
    gRecords.clear();
    uint32_t n = 0;
    if (fread(&n, 4, 1, f) != 1 || n > 200000)
        return false;
    gRecords.resize(n);
    if (n && fread(gRecords.data(), sizeof(Record), n, f) != n) {
        gRecords.clear();
        return false;
    }
    if (n)
        Log("Buildings: %u buildings from version 0.7 will be turned back into GTA buildings", n);
    return true;
}

} // namespace mc
