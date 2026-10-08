#include "GtaMining.h"

#include "CColModel.h"
#include "CColPoint.h"
#include "CEntity.h"
#include "CGame.h"
#include "CObject.h"
#include "CPlayerPed.h"
#include "CPools.h"
#include "CVehicle.h"
#include "CWorld.h"
#include "common.h"
#include "extensions/ScriptCommands.h"

#include "Buildings.h"
#include "Carve.h"
#include "Collision.h"
#include "Config.h"
#include "Entities.h"
#include "GameState.h"
#include "GtaWorld.h"
#include "Inventory.h"
#include "Items.h"
#include "Particles.h"
#include "PedSkins.h"
#include "Survival.h"
#include "Terrain.h"

namespace mc {

namespace {
// what the looked-at thing is in GTA's terms (next to the core's gTarget)
struct GtaTarget {
    GtaMaterial mat;
    int vehicleRef = -1; // mining a vehicle
    int objectRef = -1;  // mining a prop
    bool dig = false;    // GTA ground that can be dug into
    bool carve = false;  // a GTA building or cliff: breaking opens a hole into it
    bool skin = false;   // the rim of such a hole
    bool cap = false;    // the edge of the GTA ground next to a hole
    CColPoint cp;
    CEntity* ent = nullptr;
};
GtaTarget gGtaTarget;

std::vector<int> gBrokenObjects; // props already mined (they give materials once)
// vehicles on their way out: {vehicle ref, frames tried}
std::vector<std::pair<int, int>> gBrokenVehicles;

// nobody blows up: everyone inside is put on the street next to it, then the vehicle is simply gone.
// Returns true when the vehicle is gone (or no longer exists).
bool RemoveBrokenVehicle(int ref, int tries) {
    CVehicle* v = CPools::GetVehicle(ref);
    if (!v)
        return true;
    const CMatrix& m = *v->m_matrix;
    float half = 1.2f;
    if (CColModel* col = v->GetColModel())
        half = std::max(std::fabs(col->m_boundBox.m_vecMin.x), std::fabs(col->m_boundBox.m_vecMax.x)) + 0.7f;
    auto putOut = [&](CPed* p) {
        if (!p)
            return;
        const CVector pp = p->GetPosition();
        const CVector d = pp - m.pos;
        const float side = d.x * m.right.x + d.y * m.right.y + d.z * m.right.z >= 0.0f ? 1.0f : -1.0f;
        const float along = d.x * m.up.x + d.y * m.up.y + d.z * m.up.z;
        CVector to = m.pos + m.right * (side * half) + m.up * along;
        float gz;
        to.z = GroundBelow(CVector(to.x, to.y, pp.z + 1.5f), 6.0f, &gz) ? gz + 1.0f : pp.z + 0.3f;
        plugin::Command<plugin::Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(p), to.x, to.y, to.z);
    };
    putOut(v->m_pDriver);
    for (int i = 0; i < 8; ++i)
        putOut(v->m_apPassengers[i]);
    bool occupied = v->m_pDriver != nullptr;
    for (int i = 0; i < 8; ++i)
        occupied = occupied || v->m_apPassengers[i] != nullptr;
    if (occupied && tries < 90)
        return false; // someone is still getting out: again next frame
    if (v->m_pTrailer)
        plugin::Command<plugin::Commands::DETACH_TRAILER_FROM_CAB>(CPools::GetVehicleRef(v->m_pTrailer), ref);
    if (v->m_pTractor)
        plugin::Command<plugin::Commands::DETACH_TRAILER_FROM_CAB>(ref, CPools::GetVehicleRef(v->m_pTractor));
    if (occupied)
        plugin::Command<plugin::Commands::EXPLODE_CAR>(ref); // last resort, never delete a car with people in it
    else
        plugin::Command<plugin::Commands::DELETE_CAR>(ref);
    return true;
}

void BreakVehicle(CVehicle* v) {
    const int ref = CPools::GetVehicleRef(v);
    const CVector c = v->GetPosition();
    for (int i = 0; i < 4; ++i)
        SpawnBreakParticles({ FloorI(c.x) + (i & 1) - 1, FloorI(c.y) + (i >> 1) - 1, FloorI(c.z) }, ID_IRON_BLOCK);
    if (!RemoveBrokenVehicle(ref, 0)) {
        // gone from sight at once, deleted as soon as it is empty
        v->bIsVisible = false;
        v->bUsesCollision = false;
        gBrokenVehicles.push_back({ ref, 0 });
    }
}
} // namespace

void UpdateBrokenVehicles() {
    for (size_t i = 0; i < gBrokenVehicles.size();) {
        if (RemoveBrokenVehicle(gBrokenVehicles[i].first, ++gBrokenVehicles[i].second)) {
            gBrokenVehicles[i] = gBrokenVehicles.back();
            gBrokenVehicles.pop_back();
        } else {
            ++i;
        }
    }
}

void GtaMiningClear() {
    gBrokenVehicles.clear();
    gBrokenObjects.clear();
}

// ---------------------------------------------------------------- what of GTA's world is looked at
bool GtaPick(const PickRay& ray, const VoxelHit& vh, Target& out) {
    gGtaTarget = GtaTarget();
    CPlayerPed* ped = FindPlayerPed();
    const CVector origin = ray.origin, dir = ray.dir, head = ray.head;
    const float reach = ray.reach, maxDist = ray.maxDist;

    CColPoint cp;
    CEntity* ent = nullptr;
    CVector start = origin, end = origin + dir * maxDist;
    bool gtaHit = false;
    float gtaDist = 1e9f;
    // at the wheel the player looks through his own vehicle
    CEntity* const ignoredBefore = CWorld::pIgnoreEntity;
    CWorld::pIgnoreEntity = ped && ped->bInVehicle ? ped->m_pVehicle : nullptr;
    for (int tries = 0; tries < 5; ++tries) {
        ent = nullptr;
        if (!CWorld::ProcessLineOfSight(start, end, cp, ent, true, true, false, true, false, false, false, false))
            break;
        if (ent == ped || TerrainIgnoreHit(cp.m_vecPoint, ent)) {
            start = cp.m_vecPoint + dir * 0.05f;
            continue;
        }
        if (IsCollisionObject(ent))
            break; // our own blocks: the voxel ray decides
        gtaHit = true;
        gtaDist = (cp.m_vecPoint - origin).Magnitude();
        break;
    }
    CWorld::pIgnoreEntity = ignoredBefore;

    // the edge of the GTA ground around a hole
    Int3 capCell;
    int capFace = FACE_TOP;
    float capDist = 0.0f;
    if (RaycastCaps(origin, dir, maxDist, &capCell, &capFace, &capDist) && (!vh.hit || capDist < vh.dist) &&
        (!gtaHit || capDist < gtaDist)) {
        CVector hp = origin + dir * capDist;
        if ((hp - head).Magnitude() > reach + 0.5f)
            return true;
        out.valid = true;
        gGtaTarget.cap = true;
        out.pos = out.key = capCell;
        out.face = capFace;
        out.point = hp;
        const Int3& n = FACE_DIR[capFace];
        out.normal = CVector((float)n.x, (float)n.y, (float)n.z);
        out.virtualBlock = TerrainCapBlock(capCell.x, capCell.y);
        gGtaTarget.mat.block = (uint16_t)out.virtualBlock;
        gGtaTarget.mat.kind = GM_BLOCK;
        out.hardness = gGtaTarget.mat.hardness;
        return true;
    }

    // the rim of a hole broken into a building
    Int3 skinCell;
    int skinFace = FACE_TOP;
    float skinDist = 0.0f;
    if (CarveRaycastSkins(origin, dir, maxDist, &skinCell, &skinFace, &skinDist) && (!vh.hit || skinDist < vh.dist) &&
        (!gtaHit || skinDist < gtaDist)) {
        CVector hp = origin + dir * skinDist;
        if ((hp - head).Magnitude() > reach + 0.5f)
            return true;
        out.valid = true;
        gGtaTarget.skin = true;
        out.pos = out.key = skinCell;
        out.face = skinFace;
        out.point = hp;
        const Int3& n = FACE_DIR[skinFace];
        out.normal = CVector((float)n.x, (float)n.y, (float)n.z);
        out.virtualBlock = CarveSkinBlock(skinCell.x, skinCell.y, skinCell.z);
        gGtaTarget.mat.block = (uint16_t)out.virtualBlock;
        gGtaTarget.mat.kind = GM_BLOCK;
        out.hardness = gGtaTarget.mat.hardness;
        return true;
    }

    if (vh.hit && (!gtaHit || vh.dist <= gtaDist + 0.01f))
        return false; // one of our blocks is in front
    if (!gtaHit)
        return false;

    if ((cp.m_vecPoint - head).Magnitude() > reach)
        return true;
    if (ent && ent->m_nType == ENTITY_TYPE_PED)
        return true;
    out.valid = true;
    out.voxel = false;
    out.point = cp.m_vecPoint;
    out.normal = cp.m_vecNormal;
    CVector inside = cp.m_vecPoint - cp.m_vecNormal * 0.05f;
    out.pos = out.key = { FloorI(inside.x), FloorI(inside.y), FloorI(inside.z) };
    if (!gConfig.mineGtaWorld)
        return true;
    GtaTarget& g = gGtaTarget;
    if (ent && ent->m_nType == ENTITY_TYPE_VEHICLE) {
        CVehicle* v = static_cast<CVehicle*>(ent);
        if (v->m_nCreatedBy == MISSION_VEHICLE)
            return true; // mission cars stay
        g.mat = MaterialForVehicle(v);
        g.vehicleRef = CPools::GetVehicleRef(v);
    } else {
        g.mat = MaterialForTarget(cp, ent, origin, dir);
        // earth, grass, sand and roads are dug downwards like ground; walls, roofs, floors and cliffs are
        // broken into: the model stays, a hole opens where the player mines
        const bool building = ent && ent->m_nType == ENTITY_TYPE_BUILDING && CGame::currArea == 0;
        const bool ground = cp.m_vecNormal.z >= 0.55f &&
                            (TerrainNaturalSurface(HitSurface(cp)) || !IsStructureModel(ent) ||
                             TerrainIsConverted(FloorI(cp.m_vecPoint.x), FloorI(cp.m_vecPoint.y)));
        g.dig = building && ground && TerrainDiggable(cp, ent, g.mat);
        g.carve = building && !g.dig && gConfig.breakBuildings;
        g.cp = cp;
        g.ent = ent;
        if (ent && ent->m_nType == ENTITY_TYPE_OBJECT) {
            CObject* o = static_cast<CObject*>(ent);
            const int ref = CPools::GetObjectRef(o);
            const bool broken = std::find(gBrokenObjects.begin(), gBrokenObjects.end(), ref) != gBrokenObjects.end();
            if (o->m_nObjectType == OBJECT_MISSION || o->m_nObjectType == OBJECT_MISSION2 || broken)
                g.mat = GtaMaterial();
            else
                g.objectRef = ref;
        }
    }
    out.virtualBlock = g.mat.block;
    out.hardness = g.mat.hardness;
    out.grass = g.mat.kind == GM_GRASS;
    out.outline = g.vehicleRef < 0;
    // a car or a prop is one thing, whichever of its cells is looked at
    if (g.vehicleRef >= 0)
        out.key = Int3{ g.vehicleRef, -77777, 0 };
    else if (g.objectRef >= 0)
        out.key = Int3{ g.objectRef, -88888, 0 };
    return true;
}

// ---------------------------------------------------------------- breaking it
void GtaBreakTarget() {
    const Target& t = gTarget;
    const GtaTarget& g = gGtaTarget;
    CPlayerPed* ped = FindPlayerPed();
    if (g.carve || g.skin) {
        // into a building: the cell opens, what is behind it is blocks
        const int b = g.skin ? CarveBreakSkin(t.pos) : CarveBreak(g.cp, g.ent, gGame.lookDir, g.mat.block);
        if (b == ID_AIR)
            return;
        SpawnBreakParticles(t.pos, b);
        PlaySfx(DigSound(b), &t.point);
        if (gGame.gameMode == MODE_SURVIVAL) {
            bool harvest = true;
            BreakSeconds(b, gInv.Held(), &harvest);
            if (harvest)
                SpawnBlockDrops(b, t.point + t.normal * 0.3f);
        }
        return;
    }
    if (g.cap || g.dig) {
        // digging into the GTA map: the ground opens and the block under it is ours
        int b = g.cap ? TerrainCapBlock(t.pos.x, t.pos.y) : t.virtualBlock;
        if (g.cap)
            TerrainOpenColumn(t.pos.x, t.pos.y, false);
        else
            b = TerrainDig(g.cp, g.ent, g.mat);
        if (b == ID_AIR)
            return;
        SpawnBreakParticles(t.pos, b);
        PlaySfx(DigSound(b), &t.point);
        if (gGame.gameMode == MODE_SURVIVAL) {
            bool harvest = true;
            BreakSeconds(b, gInv.Held(), &harvest);
            if (harvest) {
                SpawnBlockDrops(b, t.point + t.normal * 0.3f);
                SpawnXp(t.point + t.normal * 0.3f, XpForBlock(b));
            }
        }
        return;
    }
    int block = t.virtualBlock;
    {
        // why did this ground not open? (the log tells, a few times per session)
        static int logged = 0;
        if (logged < 25 && g.ent && g.ent->m_nType == ENTITY_TYPE_BUILDING && t.normal.z > 0.55f) {
            ++logged;
            Log("Dig: ground not dug: surface %d, model %d '%s', area %d, normal %.2f, block %d kind %d",
                (int)HitSurface(g.cp), (int)g.ent->m_nModelIndex, GtaModelName(g.ent->m_nModelIndex),
                (int)CGame::currArea, t.normal.z, (int)g.mat.block, (int)g.mat.kind);
        }
    }
    SpawnBreakParticles(t.pos, block);
    PlaySfx(DigSound(block), &t.point);
    CVehicle* veh = g.vehicleRef >= 0 ? CPools::GetVehicle(g.vehicleRef) : nullptr;
    CObject* obj = g.objectRef >= 0 ? CPools::GetObject(g.objectRef) : nullptr;
    if (gGame.gameMode == MODE_SURVIVAL) {
        bool harvest = true;
        BreakSecondsFor(block, g.mat.hardness, gInv.Held(), &harvest);
        if (harvest) {
            int tier = gInv.Held().Empty() ? 0 : Item(gInv.Held().id).tier;
            SpawnMaterialDrops(g.mat, t.point + t.normal * 0.4f, tier, veh);
        }
    }
    // the GTA thing itself goes too
    if (veh) {
        BreakVehicle(veh);
    } else if (obj) {
        gBrokenObjects.push_back(g.objectRef);
        if (gBrokenObjects.size() > 512)
            gBrokenObjects.erase(gBrokenObjects.begin());
        CVector at = t.point, dir = gGame.lookDir;
        obj->ObjectDamage(10000.0f, &at, &dir, ped, WEAPONTYPE_EXPLOSION);
        LaunchEntity(obj, dir * 3.0f + CVector(0, 0, 2.0f));
    }
}

// ---------------------------------------------------------------- room for a block
bool GtaCellBlocked(const Int3& p) {
    CPlayerPed* ped = FindPlayerPed();
    auto overlaps = [&](const CVector& c, float r, float z0, float z1) {
        return c.x + r > p.x && c.x - r < p.x + 1 && c.y + r > p.y && c.y - r < p.y + 1 && z1 > p.z && z0 < p.z + 1;
    };
    // the player and other people (GTA's own test uses bounding spheres that are far too big)
    if (ped) {
        CVector pp = ped->GetPosition();
        if (!ped->bInVehicle && overlaps(pp, 0.3f, pp.z - 0.99f, pp.z + 0.8f))
            return true;
    }
    const CVector centre(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f);
    if (auto* pool = CPools::ms_pPedPool)
        for (int i = 0; i < pool->m_nSize; ++i) {
            CPed* o = pool->GetAt(i);
            if (!o || o == ped || o->bInVehicle || o->m_fHealth <= 0.0f)
                continue;
            CVector c = o->GetPosition();
            if (overlaps(c, 0.3f, c.z - 0.99f, c.z + 0.8f))
                return true;
        }
    // vehicles: their collision box in their own space
    if (auto* pool = CPools::ms_pVehiclePool)
        for (int i = 0; i < pool->m_nSize; ++i) {
            CVehicle* v = pool->GetAt(i);
            if (!v || (v->GetPosition() - centre).Magnitude() > 12.0f)
                continue;
            CColModel* col = v->GetColModel();
            if (!col)
                continue;
            const CMatrix& m = *v->m_matrix;
            CVector d = centre - m.pos;
            CVector l(d.x * m.right.x + d.y * m.right.y + d.z * m.right.z, d.x * m.up.x + d.y * m.up.y + d.z * m.up.z,
                      d.x * m.at.x + d.y * m.at.y + d.z * m.at.z);
            const CVector& mn = col->m_boundBox.m_vecMin;
            const CVector& mx = col->m_boundBox.m_vecMax;
            const float e = 0.4f;
            if (l.x > mn.x - e && l.x < mx.x + e && l.y > mn.y - e && l.y < mx.y + e && l.z > mn.z - e && l.z < mx.z + e)
                return true;
        }
    return false;
}

} // namespace mc
