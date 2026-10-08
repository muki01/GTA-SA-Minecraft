#include <algorithm>
#include "Collision.h"

#include "CAtomicModelInfo.h"
#include "CColModel.h"
#include "CModelInfo.h"
#include "CObject.h"
#include "CPools.h"
#include "CStreaming.h"
#include "CWorld.h"
#include "common.h"
#include "extensions/ScriptCommands.h"
#include "safetyhook.hpp"

#include "Config.h"
#include "Shapes.h"
#include "Terrain.h"
#include "World.h"

namespace mc {

namespace {
constexpr int kMaxSlots = 160;
constexpr size_t kMaxBoxes = 4000;
constexpr int kMinFreeObjectSlots = 40;
constexpr uint8_t kBoxLighting = 0x5C; // day 12, night 5

struct Slot {
    int modelId = -1;
    CColModel* col = nullptr;      // allocated by us, never owned by the game
    CCollisionData* data = nullptr;
    std::vector<CColBox> boxes;
    CObject* obj = nullptr;
    int objHandle = -1;
    Int3 column{};
    bool used = false;
    uint32_t version = 0xFFFFFFFF;
};

std::vector<Slot> gSlots;
std::unordered_map<Int3, int, Int3Hash> gColumnToSlot;
int gNextProbe = -1;
bool gDisabled = false;
bool gStoreFull = false; // no more model ids: the slots we have are shared out to the nearest columns
int gFreeCheckTimer = 0;
int gFreeObjectSlots = 1000;
std::vector<const CColModel*> gOurColModels; // the block columns' collision models
} // namespace

static uint32_t AtomicStoreCount() { return *reinterpret_cast<uint32_t*>(0xAAE950); }

static int FindFreeModelId() {
    int count = CModelInfo::ms_modelInfoCount;
    if (count <= 0 || count > 100000)
        count = 20000;
    if (gNextProbe < 0)
        gNextProbe = count - 1;
    for (int id = gNextProbe; id > 18630; --id) {
        if (CModelInfo::ms_modelInfoPtrs[id])
            continue;
        CStreamingInfo& si = CStreaming::ms_aInfoForModel[id];
        if (si.m_nLoadState != 0 || si.m_nCdSize != 0)
            continue;
        gNextProbe = id - 1;
        return id;
    }
    return -1;
}

static bool InitSlotModel(Slot& s) {
    if (s.modelId >= 0)
        return true;
    if (gStoreFull)
        return false;
    // the game's store holds 14000 atomic models; leave a few for the game and other mods
    if (AtomicStoreCount() >= 13997) {
        Log("Collision: atomic model store is full (%u): %d collision slots are shared by distance", AtomicStoreCount(),
            (int)std::count_if(gSlots.begin(), gSlots.end(), [](const Slot& x) { return x.modelId >= 0; }));
        gStoreFull = true;
        return false;
    }
    int id = FindFreeModelId();
    if (id < 0) {
        Log("Collision: no free model id left: collision slots are shared by distance");
        gStoreFull = true;
        return false;
    }
    s.col = static_cast<CColModel*>(calloc(1, sizeof(CColModel)));
    s.data = static_cast<CCollisionData*>(calloc(1, sizeof(CCollisionData)));
    s.col->m_nColSlot = 0; // "generic" slot, never streamed out
    s.col->m_bHasCollisionVolumes = 1;
    s.col->m_bIsActive = 1;
    s.col->m_pColData = s.data;
    gOurColModels.push_back(s.col);

    CAtomicModelInfo* mi = CModelInfo::AddAtomicModel(id);
    // Draw distance 0 keeps CRenderer::ShouldModelBeStreamed() from ever requesting this model:
    // it has no IMG data, so a streaming request would break the loader.
    mi->m_fDrawDistance = 0.0f;
    mi->SetColModel(s.col, false); // false: the game must not delete our col model
    s.modelId = id;
    Log("Collision: model id %d registered", id);
    return true;
}

static void RebuildBoxes(Slot& s) {
    s.boxes.clear();
    CVector bmin(1e9f, 1e9f, 1e9f), bmax(-1e9f, -1e9f, -1e9f);

    auto it = gWorld.columnChunks.find({ s.column.x, s.column.y, 0 });
    if (it != gWorld.columnChunks.end() && !it->second.empty()) {
        int czMin = *std::min_element(it->second.begin(), it->second.end());
        int czMax = *std::max_element(it->second.begin(), it->second.end());
        int zBase = czMin * CS;
        int H = (czMax - czMin + 1) * CS;
        static std::vector<uint8_t> grid;
        grid.assign((size_t)CS * CS * H, 0);
        auto G = [&](int x, int y, int z) -> uint8_t& { return grid[(size_t)x + CS * ((size_t)y + CS * (size_t)z)]; };
        // ground made from the GTA map counts only in and next to the holes dug into it (elsewhere the GTA ground
        // lies over it): GTA's vehicles and people fall into the holes and land on it
        bool nearHole[CS * CS] = {};
        if (TerrainAnyHole())
            for (int y = 0; y < CS; ++y)
                for (int x = 0; x < CS; ++x) {
                    const int wx = s.column.x * CS + x, wy = s.column.y * CS + y;
                    for (int dy = -1; dy <= 1 && !nearHole[x + CS * y]; ++dy)
                        for (int dx = -1; dx <= 1 && !nearHole[x + CS * y]; ++dx)
                            nearHole[x + CS * y] = TerrainIsOpened(wx + dx, wy + dy);
                }

        for (int cz : it->second) {
            Chunk* c = gWorld.FindChunk({ s.column.x, s.column.y, cz });
            if (!c)
                continue;
            int oz = cz * CS - zBase;
            for (int z = 0; z < CS; ++z)
                for (int y = 0; y < CS; ++y)
                    for (int x = 0; x < CS; ++x) {
                        Voxel v = c->Get(x, y, z);
                        int b = VoxBlock(v);
                        if (IsSolidBlock(b) && !IsShapedBlock(b) && (!(VoxMeta(v) & META_NATURAL) || nearHole[x + CS * y]))
                            G(x, y, oz + z) = (uint8_t)(Block(b).surface + 1);
                    }
        }

        // stairs and slabs: their own boxes
        for (int cz : it->second) {
            Chunk* c = gWorld.FindChunk({ s.column.x, s.column.y, cz });
            if (!c)
                continue;
            for (int z = 0; z < CS; ++z)
                for (int y = 0; y < CS; ++y)
                    for (int x = 0; x < CS && s.boxes.size() < kMaxBoxes; ++x) {
                        const Voxel v = c->Get(x, y, z);
                        if (!IsShapedBlock(VoxBlock(v)))
                            continue;
                        ShapeBox sb[kMaxShapeBoxes];
                        const int cellX = s.column.x * CS + x, cellY = s.column.y * CS + y, cellZ = cz * CS + z;
                        const int n = BlockShapeBoxes(VoxBlock(v), VoxMeta(v), sb, ShapeConnectionsAt(cellX, cellY, cellZ), true);
                        for (int i = 0; i < n; ++i) {
                            CColBox box;
                            memset(&box, 0, sizeof(box));
                            const float wz = (float)(cz * CS + z);
                            box.m_vecMin = CVector(x + sb[i].x0, y + sb[i].y0, wz + sb[i].z0);
                            box.m_vecMax = CVector(x + sb[i].x1, y + sb[i].y1, wz + sb[i].z1);
                            box.m_nMaterial = Block(VoxBlock(v)).surface;
                            box.m_nLighting = kBoxLighting;
                            s.boxes.push_back(box);
                            bmin.x = std::min(bmin.x, box.m_vecMin.x);
                            bmin.y = std::min(bmin.y, box.m_vecMin.y);
                            bmin.z = std::min(bmin.z, box.m_vecMin.z);
                            bmax.x = std::max(bmax.x, box.m_vecMax.x);
                            bmax.y = std::max(bmax.y, box.m_vecMax.y);
                            bmax.z = std::max(bmax.z, box.m_vecMax.z);
                        }
                    }
        }

        // greedy merge of equal-surface cells into boxes
        for (int z = 0; z < H && s.boxes.size() < kMaxBoxes; ++z)
            for (int y = 0; y < CS; ++y)
                for (int x = 0; x < CS; ++x) {
                    uint8_t v = G(x, y, z);
                    if (!v)
                        continue;
                    int x2 = x;
                    while (x2 + 1 < CS && G(x2 + 1, y, z) == v)
                        x2++;
                    int y2 = y;
                    for (;;) {
                        if (y2 + 1 >= CS)
                            break;
                        bool ok = true;
                        for (int xx = x; xx <= x2 && ok; ++xx)
                            ok = G(xx, y2 + 1, z) == v;
                        if (!ok)
                            break;
                        y2++;
                    }
                    int z2 = z;
                    for (;;) {
                        if (z2 + 1 >= H)
                            break;
                        bool ok = true;
                        for (int yy = y; yy <= y2 && ok; ++yy)
                            for (int xx = x; xx <= x2 && ok; ++xx)
                                ok = G(xx, yy, z2 + 1) == v;
                        if (!ok)
                            break;
                        z2++;
                    }
                    for (int zz = z; zz <= z2; ++zz)
                        for (int yy = y; yy <= y2; ++yy)
                            for (int xx = x; xx <= x2; ++xx)
                                G(xx, yy, zz) = 0;

                    CColBox box;
                    memset(&box, 0, sizeof(box));
                    box.m_vecMin = CVector((float)x, (float)y, (float)(zBase + z));
                    box.m_vecMax = CVector((float)(x2 + 1), (float)(y2 + 1), (float)(zBase + z2 + 1));
                    box.m_nMaterial = (uint8_t)(v - 1);
                    box.m_nFlags = 0;
                    box.m_nLighting = kBoxLighting;
                    box.m_nLight = 0;
                    s.boxes.push_back(box);
                    bmin.x = std::min(bmin.x, box.m_vecMin.x);
                    bmin.y = std::min(bmin.y, box.m_vecMin.y);
                    bmin.z = std::min(bmin.z, box.m_vecMin.z);
                    bmax.x = std::max(bmax.x, box.m_vecMax.x);
                    bmax.y = std::max(bmax.y, box.m_vecMax.y);
                    bmax.z = std::max(bmax.z, box.m_vecMax.z);
                    if (s.boxes.size() >= kMaxBoxes)
                        break;
                }
    }

    memset(s.data, 0, sizeof(CCollisionData));
    s.data->m_nNumBoxes = (unsigned short)s.boxes.size();
    s.data->m_pBoxes = s.boxes.empty() ? nullptr : s.boxes.data();
    if (!s.boxes.empty())
        s.data->m_nFlags.bNotEmpty = 1;

    if (s.boxes.empty()) {
        bmin = CVector(0, 0, 0);
        bmax = CVector(1, 1, 1);
    }
    s.col->m_boundBox.m_vecMin = bmin;
    s.col->m_boundBox.m_vecMax = bmax;
    CVector center = (bmin + bmax) * 0.5f;
    CVector ext = bmax - bmin;
    s.col->m_boundSphere.m_vecCenter = center;
    s.col->m_boundSphere.m_fRadius = ext.Magnitude() * 0.5f + 0.1f;
}

static bool ObjectStillValid(Slot& s) {
    if (!s.obj)
        return false;
    CObject* cur = CPools::GetObject(s.objHandle);
    return cur == s.obj && cur->m_nModelIndex == s.modelId;
}

static void DeleteObject(Slot& s) {
    if (ObjectStillValid(s))
        plugin::Command<plugin::Commands::DELETE_OBJECT>(s.objHandle);
    s.obj = nullptr;
    s.objHandle = -1;
}

static bool SpawnObject(Slot& s) {
    if (gFreeObjectSlots < kMinFreeObjectSlots)
        return false;
    CObject* obj = CObject::Create(s.modelId);
    if (!obj) {
        Log("Collision: CObject::Create failed (object pool full?)");
        return false;
    }
    obj->m_nObjectType = OBJECT_MISSION;
    obj->bIsVisible = false;
    obj->bDontStream = true;
    obj->SetPosn(CVector((float)(s.column.x * CS), (float)(s.column.y * CS), 0.0f));
    obj->SetOrientation(0.0f, 0.0f, 0.0f);
    obj->UpdateRwMatrix();
    obj->UpdateRwFrame();
    CWorld::Add(obj);
    s.obj = obj;
    s.objHandle = CPools::GetObjectRef(obj);
    gFreeObjectSlots--;
    return true;
}

static void ReleaseSlot(Slot& s) {
    DeleteObject(s);
    gColumnToSlot.erase(s.column);
    s.used = false;
    s.version = 0xFFFFFFFF;
    s.boxes.clear();
    if (s.data)
        memset(s.data, 0, sizeof(CCollisionData));
}

static int CountFreeObjectSlots() {
    auto* pool = CPools::ms_pObjectPool;
    if (!pool)
        return 0;
    int free = 0;
    for (int i = 0; i < pool->m_nSize; ++i)
        if (pool->IsFreeSlotAtIndex(i))
            free++;
    return free;
}

void CollisionUpdate() {
    if (gDisabled)
        return;
    if (!FindPlayerPed())
        return;
    if (gSlots.empty())
        gSlots.resize(kMaxSlots);

    if (--gFreeCheckTimer <= 0) {
        gFreeCheckTimer = 30;
        gFreeObjectSlots = CountFreeObjectSlots();
    }

    CVector p = FindPlayerCoors();
    float R = gConfig.collisionRadius;
    float releaseR = R + 24.0f;

    // drop objects that the game removed behind our back, and columns that are far away
    for (auto& s : gSlots) {
        if (!s.used)
            continue;
        if (s.obj && !ObjectStillValid(s)) {
            s.obj = nullptr;
            s.objHandle = -1;
        }
        float cx = s.column.x * CS + CS * 0.5f - p.x, cy = s.column.y * CS + CS * 0.5f - p.y;
        if (cx * cx + cy * cy > releaseR * releaseR || !gWorld.columnChunks.count({ s.column.x, s.column.y, 0 }))
            ReleaseSlot(s);
    }

    // columns that need collision, nearest first
    struct Want { Int3 col; float d2; };
    static std::vector<Want> wants;
    wants.clear();
    for (auto& kv : gWorld.columnChunks) {
        float cx = kv.first.x * CS + CS * 0.5f - p.x, cy = kv.first.y * CS + CS * 0.5f - p.y;
        float d2 = cx * cx + cy * cy;
        if (d2 <= (R + 12.0f) * (R + 12.0f))
            wants.push_back({ kv.first, d2 });
    }
    std::sort(wants.begin(), wants.end(), [](const Want& a, const Want& b) { return a.d2 < b.d2; });

    int budget = 4;
    for (auto& w : wants) {
        if (budget <= 0)
            break;
        // (a hole opened anywhere can bring natural ground in: the terrain's version counts too)
        uint32_t ver = gWorld.columnVersion[w.col] ^ (TerrainVersion() * 2654435761u);
        Slot* s = nullptr;
        auto it = gColumnToSlot.find(w.col);
        if (it != gColumnToSlot.end()) {
            s = &gSlots[it->second];
            if (s->version == ver && s->obj)
                continue;
        } else {
            // a free slot that already has a model, else one that can get a model
            int pick = -1;
            for (int i = 0; i < (int)gSlots.size() && pick < 0; ++i)
                if (!gSlots[i].used && gSlots[i].modelId >= 0)
                    pick = i;
            if (pick < 0 && !gStoreFull)
                for (int i = 0; i < (int)gSlots.size() && pick < 0; ++i)
                    if (!gSlots[i].used)
                        pick = i;
            if (pick < 0) {
                // all models busy: take the one of the farthest column if it is farther than this one
                float worst = w.d2;
                for (int i = 0; i < (int)gSlots.size(); ++i) {
                    Slot& o = gSlots[i];
                    if (!o.used || o.modelId < 0)
                        continue;
                    const float ox = o.column.x * CS + CS * 0.5f - p.x, oy = o.column.y * CS + CS * 0.5f - p.y;
                    const float od2 = ox * ox + oy * oy;
                    if (od2 > worst + 1.0f) {
                        worst = od2;
                        pick = i;
                    }
                }
                if (pick < 0)
                    break; // the nearest columns have all the slots
                ReleaseSlot(gSlots[pick]);
            }
            s = &gSlots[pick];
            s->used = true;
            s->column = w.col;
            s->version = 0xFFFFFFFF;
            gColumnToSlot[w.col] = pick;
        }
        if (!InitSlotModel(*s)) {
            ReleaseSlot(*s);
            continue;
        }

        // rebuild: take the object out of the world while its collision changes
        if (s->obj)
            CWorld::Remove(s->obj);
        RebuildBoxes(*s);
        s->version = ver;
        budget--;
        if (s->boxes.empty()) {
            if (s->obj)
                CWorld::Add(s->obj);
            ReleaseSlot(*s);
            continue;
        }
        if (s->obj)
            CWorld::Add(s->obj);
        else if (!SpawnObject(*s))
            break;
    }
}

void CollisionForgetAll() {
    for (auto& s : gSlots) {
        s.obj = nullptr;
        s.objHandle = -1;
        s.used = false;
        s.version = 0xFFFFFFFF;
        s.boxes.clear();
        if (s.data)
            memset(s.data, 0, sizeof(CCollisionData));
    }
    gColumnToSlot.clear();
}

bool IsCollisionObject(const CEntity* e) {
    if (!e)
        return false;
    int mi = e->m_nModelIndex;
    for (auto& s : gSlots)
        if (s.modelId == mi)
            return true;
    return false;
}

// ================================================================ GTA's own physics and the holes
namespace {
SafetyHookInline gColModelsHook;

// CCollision::ProcessColModels against GTA's map: contacts inside the holes dug into its ground or broken into its
// buildings do not count, so its vehicles and people fall in and land on the blocks there (those are ours: kept)
int __cdecl HookProcessColModels(const CMatrix* ma, CColModel* ca, const CMatrix* mb, CColModel* cb, CColPoint* sphereCPs,
                                 CColPoint* lineCPs, float* maxTouch, bool all) {
    const bool holes = TerrainAnyHole() && std::find(gOurColModels.begin(), gOurColModels.end(), cb) == gOurColModels.end();
    if (!holes)
        return gColModelsHook.ccall<int>(ma, ca, mb, cb, sphereCPs, lineCPs, maxTouch, all);
    // the wheels' lines as they were: a hit in a hole is taken back
    CColPoint savedCP[16];
    float savedTouch[16];
    int lines = 0;
    if (lineCPs && maxTouch && ca && ca->m_pColData) {
        lines = std::min<int>(16, ca->m_pColData->m_nNumLines);
        for (int i = 0; i < lines; ++i) {
            savedCP[i] = lineCPs[i];
            savedTouch[i] = maxTouch[i];
        }
    }
    const int n = gColModelsHook.ccall<int>(ma, ca, mb, cb, sphereCPs, lineCPs, maxTouch, all);
    int kept = 0;
    for (int i = 0; i < n; ++i) {
        if (TerrainIgnoreHit(sphereCPs[i].m_vecPoint, nullptr))
            continue;
        if (kept != i)
            sphereCPs[kept] = sphereCPs[i];
        ++kept;
    }
    if (kept != n)
        sphereCPs[kept].m_fDepth = -1.0f; // (the end marker GTA expects after the last point)
    for (int i = 0; i < lines; ++i)
        if (maxTouch[i] != savedTouch[i] && TerrainIgnoreHit(lineCPs[i].m_vecPoint, nullptr)) {
            lineCPs[i] = savedCP[i];
            maxTouch[i] = savedTouch[i];
        }
    return kept;
}
} // namespace

void InstallCollisionHooks() {
    static bool done = false;
    if (done)
        return;
    done = true;
    gColModelsHook = safetyhook::create_inline(reinterpret_cast<void*>(0x4185C0), reinterpret_cast<void*>(&HookProcessColModels));
    Log("Hooks: vehicles and people fall into holes %s", gColModelsHook ? "ok" : "FAILED");
}

} // namespace mc
