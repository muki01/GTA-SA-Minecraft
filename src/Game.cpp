#include "Game.h"

#include "CCamera.h"
#include "CColPoint.h"
#include "CCutsceneMgr.h"
#include "CDraw.h"
#include "CExplosion.h"
#include "CFireManager.h"
#include "CHud.h"
#include "CMenuManager.h"
#include "CObject.h"
#include "CPad.h"
#include "CGame.h"
#include "CPlayerPed.h"
#include "CPools.h"
#include "CScene.h"
#include "CTheScripts.h"
#include "CVehicle.h"
#include "CWaterLevel.h"
#include "CWeather.h"
#include "CWorld.h"
#include "RenderWare.h"
#include "common.h"
#include "extensions/ScriptCommands.h"

#include "Blocks.h"
#include "Buildings.h"
#include "Carve.h"
#include "Collision.h"
#include "Combat.h"
#include "Config.h"
#include "Effects.h"
#include "Fishing.h"
#include "GtaWorld.h"
#include "Gui.h"
#include "Input.h"
#include "Inventory.h"
#include "Items.h"
#include "Mobs.h"
#include "Movement.h"
#include "PedSkins.h"
#include "Player3D.h"
#include "Pose.h"
#include "Render3D.h"
#include "Sound.h"
#include "Terrain.h"
#include "Xp.h"

namespace mc {

GameState gGame;

void ShowMessage(const std::string& text, float seconds) {
    gGame.message = text;
    gGame.messageTimer = seconds;
}

float Rand01() { return (rand() % 10000) / 10000.0f; }

void StartSwing() {
    if (gGame.offhandActive) {
        if (gGame.offSwing < 0.0f || gGame.offSwing > 0.5f)
            gGame.offSwing = 0.0f;
        return;
    }
    if (gGame.swing < 0.0f || gGame.swing > 0.5f)
        gGame.swing = 0.0f;
}

float AttackCharge() {
    const ItemStack& h = gInv.Held();
    float speed = h.Empty() ? 4.0f : Item(h.id).attackSpeed;
    if (speed <= 0.0f)
        speed = 4.0f;
    return Clamp(gGame.attackTimer * speed, 0.0f, 1.0f);
}

bool GroundBelow(const CVector& from, float maxDrop, float* zOut) {
    bool found = false;
    float best = -1e9f;
    float gz = CWorld::FindGroundZFor3DCoord(from.x, from.y, from.z, &found, nullptr);
    const int bx = FloorI(from.x), by = FloorI(from.y);
    if (found && TerrainIsOpened(bx, by) && std::fabs(gz - TerrainSurface(bx, by)) < 0.8f)
        found = false; // that ground has been dug away
    if (found && from.z - gz <= maxDrop)
        best = gz;
    else
        found = false;
    const int z0 = FloorI(from.z), z1 = FloorI(from.z - std::min(maxDrop, 48.0f));
    for (int z = z0; z >= z1; --z) {
        float capTop;
        if (TerrainCapAt(bx, by, z, &capTop) && capTop <= from.z + 0.01f) {
            if (capTop > best) {
                best = capTop;
                found = true;
            }
            break;
        }
        if (gWorld.IsSolid(bx, by, z)) {
            if (z + 1.0f > best) {
                best = z + 1.0f;
                found = true;
            }
            break;
        }
    }
    if (found && zOut)
        *zOut = best;
    return found;
}

// ================================================================ helpers
namespace {
struct Target {
    bool valid = false;
    bool voxel = false;
    Int3 pos;
    int face = FACE_TOP;
    CVector point, normal;
    int virtualBlock = ID_AIR;
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

Target gTarget;
Int3 gMiningPos;
bool gMiningVoxel = false;
float gMiningProgress = 0.0f;
float gBreakCooldown = 0.0f;
float gPlaceCooldown = 0.0f;
float gHitSoundTimer = 0.0f;
float gEatSoundTimer = 0.0f;
bool gWasDead = false;
bool gHudFlagsTouched = false;
bool gMeleeHeld = false;
CVector gLastPlayerPos;
bool gWasInAir = false;
uint8_t gExpCounter[16] = {};
CVector gExpPos[16];
struct OwnBlast {
    CVector pos;
    float age;
};
std::vector<OwnBlast> gOwnBlasts; // explosions we made ourselves (already handled)
bool gInitDone = false;
// field of view override
float gFovWritten = 0.0f;  // what we stored into CDraw::ms_fFOV last frame (0 = nothing)
float gFovInEffect = 0.0f; // the value the last frame was rendered with
float gGameFov = 70.0f;    // the game's own value
float gFovK = 0.75f;       // measured: tan(vertical fov / 2) = k * tan(CDraw fov / 2)
bool gFovKMeasured = false;
constexpr float kFirstPersonNear = 0.1f;
float gSavedNearClip = 0.0f;
bool gNearClipOverridden = false;
float gHealthSeen = -1.0f;        // player health at the end of the last check
std::vector<int> gBrokenObjects;  // props already mined (they give materials once)

SoundEvent DigSound(int block) { return (SoundEvent)(SND_DIG_STONE + Block(block).sound); }
SoundEvent HitSound(int block) { return (SoundEvent)(SND_HIT_STONE + Block(block).sound); }
SoundEvent PlaceSound(int block) { return (SoundEvent)(SND_PLACE_STONE + Block(block).sound); }

CVector Normalized(CVector v) {
    float m = v.Magnitude();
    return m > 1e-5f ? v * (1.0f / m) : CVector(0, 1, 0);
}

void UpdateTarget(CPlayerPed* ped) {
    gTarget = Target();
    CVector origin = gGame.rayOrigin, dir = gGame.lookDir;
    if (dir.Magnitude() < 0.5f || !std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
        return;
    CVector head = gGame.eyePos;
    float reach = gGame.gameMode == MODE_CREATIVE ? 5.0f : 4.5f;
    float maxDist = (head - origin).Magnitude() + reach + 0.5f;

    VoxelHit vh = RaycastVoxels(origin, dir, maxDist);

    CColPoint cp;
    CEntity* ent = nullptr;
    CVector start = origin, end = origin + dir * maxDist;
    bool gtaHit = false;
    float gtaDist = 1e9f;
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

    // the edge of the GTA ground around a hole
    Int3 capCell;
    int capFace = FACE_TOP;
    float capDist = 0.0f;
    if (RaycastCaps(origin, dir, maxDist, &capCell, &capFace, &capDist) && (!vh.hit || capDist < vh.dist) &&
        (!gtaHit || capDist < gtaDist)) {
        CVector hp = origin + dir * capDist;
        if ((hp - head).Magnitude() > reach + 0.5f)
            return;
        gTarget.valid = true;
        gTarget.cap = true;
        gTarget.pos = capCell;
        gTarget.face = capFace;
        gTarget.point = hp;
        const Int3& n = FACE_DIR[capFace];
        gTarget.normal = CVector((float)n.x, (float)n.y, (float)n.z);
        gTarget.virtualBlock = TerrainCapBlock(capCell.x, capCell.y);
        gTarget.mat.block = (uint16_t)gTarget.virtualBlock;
        gTarget.mat.kind = GM_BLOCK;
        return;
    }

    // the rim of a hole broken into a building
    Int3 skinCell;
    int skinFace = FACE_TOP;
    float skinDist = 0.0f;
    if (CarveRaycastSkins(origin, dir, maxDist, &skinCell, &skinFace, &skinDist) && (!vh.hit || skinDist < vh.dist) &&
        (!gtaHit || skinDist < gtaDist)) {
        CVector hp = origin + dir * skinDist;
        if ((hp - head).Magnitude() > reach + 0.5f)
            return;
        gTarget.valid = true;
        gTarget.skin = true;
        gTarget.pos = skinCell;
        gTarget.face = skinFace;
        gTarget.point = hp;
        const Int3& n = FACE_DIR[skinFace];
        gTarget.normal = CVector((float)n.x, (float)n.y, (float)n.z);
        gTarget.virtualBlock = CarveSkinBlock(skinCell.x, skinCell.y, skinCell.z);
        gTarget.mat.block = (uint16_t)gTarget.virtualBlock;
        gTarget.mat.kind = GM_BLOCK;
        return;
    }

    if (vh.hit && (!gtaHit || vh.dist <= gtaDist + 0.01f)) {
        CVector hp = origin + dir * vh.dist;
        if ((hp - head).Magnitude() > reach + 0.5f)
            return;
        gTarget.valid = true;
        gTarget.voxel = true;
        gTarget.pos = vh.pos;
        gTarget.face = vh.face;
        gTarget.point = hp;
        const Int3& n = FACE_DIR[vh.face];
        gTarget.normal = CVector((float)n.x, (float)n.y, (float)n.z);
        return;
    }
    if (gtaHit) {
        if ((cp.m_vecPoint - head).Magnitude() > reach)
            return;
        if (ent && ent->m_nType == ENTITY_TYPE_PED)
            return;
        gTarget.valid = true;
        gTarget.voxel = false;
        gTarget.point = cp.m_vecPoint;
        gTarget.normal = cp.m_vecNormal;
        CVector inside = cp.m_vecPoint - cp.m_vecNormal * 0.05f;
        gTarget.pos = { FloorI(inside.x), FloorI(inside.y), FloorI(inside.z) };
        if (!gConfig.mineGtaWorld)
            return;
        if (ent && ent->m_nType == ENTITY_TYPE_VEHICLE) {
            CVehicle* v = static_cast<CVehicle*>(ent);
            if (v->m_nCreatedBy == MISSION_VEHICLE)
                return; // mission cars stay
            gTarget.mat = MaterialForVehicle(v);
            gTarget.vehicleRef = CPools::GetVehicleRef(v);
        } else {
            gTarget.mat = MaterialForTarget(cp, ent, origin, dir);
            // earth, grass, sand and roads are dug downwards like ground; walls, roofs, floors and cliffs are
            // broken into: the model stays, a hole opens where the player mines
            const bool building = ent && ent->m_nType == ENTITY_TYPE_BUILDING && CGame::currArea == 0;
            const bool ground = cp.m_vecNormal.z >= 0.55f &&
                                (TerrainNaturalSurface(HitSurface(cp)) || !IsStructureModel(ent) ||
                                 TerrainIsConverted(FloorI(cp.m_vecPoint.x), FloorI(cp.m_vecPoint.y)));
            gTarget.dig = building && ground && TerrainDiggable(cp, ent, gTarget.mat);
            gTarget.carve = building && !gTarget.dig && gConfig.breakBuildings;
            gTarget.cp = cp;
            gTarget.ent = ent;
            if (ent && ent->m_nType == ENTITY_TYPE_OBJECT) {
                CObject* o = static_cast<CObject*>(ent);
                const int ref = CPools::GetObjectRef(o);
                const bool broken = std::find(gBrokenObjects.begin(), gBrokenObjects.end(), ref) != gBrokenObjects.end();
                if (o->m_nObjectType == OBJECT_MISSION || o->m_nObjectType == OBJECT_MISSION2 || broken)
                    gTarget.mat = GtaMaterial();
                else
                    gTarget.objectRef = ref;
            }
        }
        gTarget.virtualBlock = gTarget.mat.block;
    }
}

// ---------------------------------------------------------------- mining rules
float BreakSecondsFor(int block, float hardnessOverride, const ItemStack& tool, bool* canHarvest) {
    const BlockDef& d = Block(block);
    const float hardness = hardnessOverride > -1.0f ? hardnessOverride : d.hardness;
    if (hardness < 0) {
        if (canHarvest)
            *canHarvest = false;
        return 1e9f;
    }
    const ItemDef* td = tool.Empty() ? nullptr : &Item(tool.id);
    bool correct = td && d.tool != TOOL_NONE && td->tool == d.tool;
    float speed = correct ? td->speed : 1.0f;
    if (td && td->tool == TOOL_SWORD && (d.sound == SG_GRASS || d.sound == SG_WOOL))
        speed = 1.5f;
    bool harvest = !d.requiresTool || (correct && td->tier >= std::max<int>(d.tier, 1));
    if (canHarvest)
        *canHarvest = harvest;
    if (hardness == 0)
        return 0.0f;
    return hardness * (harvest ? 30.0f : 100.0f) / speed / 20.0f;
}

float BreakSeconds(int block, const ItemStack& tool, bool* canHarvest) { return BreakSecondsFor(block, -2.0f, tool, canHarvest); }

void SpawnBlockDropsImpl(int block, const CVector& at) {
    const BlockDef& d = Block(block);
    bool groupDone[8] = {};
    for (int i = 0; i < d.numDrops; ++i) {
        const BlockDrop& dr = d.drops[i];
        if (dr.group && groupDone[dr.group & 7])
            continue;
        if (Rand01() >= dr.chance)
            continue;
        if (dr.group)
            groupDone[dr.group & 7] = true;
        int count = dr.min + (dr.max > dr.min ? rand() % (dr.max - dr.min + 1) : 0);
        if (IsValidItem(dr.item) && count > 0)
            SpawnDropItem(at, dr.item, count);
    }
}

void DropContainerContents(const Int3& p) {
    CVector c(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f);
    auto f = gWorld.furnaces.find(p);
    if (f != gWorld.furnaces.end()) {
        SpawnDrop(c, f->second.input);
        SpawnDrop(c, f->second.fuel);
        SpawnDrop(c, f->second.output);
        gWorld.furnaces.erase(f);
    }
    auto ch = gWorld.chests.find(p);
    if (ch != gWorld.chests.end()) {
        for (auto& s : ch->second.slots)
            SpawnDrop(c, s);
        gWorld.chests.erase(ch);
    }
}

void BreakVoxel(const Int3& p, bool withDrops) {
    int block = gWorld.GetBlock(p.x, p.y, p.z);
    if (block == ID_AIR)
        return;
    if (gGame.screen != SCREEN_NONE && gGame.openPos == p)
        CloseScreen();
    CVector c(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f);
    if (IsFireBlock(block)) {
        // punching a fire puts it out
        PlaySfx(SND_FIRE_EXTINGUISH, &c, 0.5f, 2.0f + (Rand01() - Rand01()) * 0.8f);
        gWorld.Set(p.x, p.y, p.z, MakeVox(ID_AIR));
        return;
    }
    SpawnBreakParticles(p, block);
    PlaySfx(DigSound(block), &c);
    if (withDrops) {
        bool harvest = true;
        BreakSeconds(block, gInv.Held(), &harvest);
        if (harvest) {
            SpawnBlockDropsImpl(block, c);
            SpawnXp(c, XpForBlock(block));
        }
    }
    DropContainerContents(p);
    gWorld.Set(p.x, p.y, p.z, MakeVox(ID_AIR));
}

void HitParticles(const CVector& at, const CVector& normal, int block) {
    uint16_t tile = BlockFaceTile(block, FACE_NORTH, 0);
    for (int i = 0; i < 4; ++i) {
        Particle pt;
        pt.pos = at + normal * 0.05f;
        pt.vel = normal * 1.5f + CVector(Rand01() - 0.5f, Rand01() - 0.5f, Rand01()) * 1.5f;
        pt.maxLife = pt.life = 0.4f + Rand01() * 0.3f;
        pt.tile = tile;
        pt.u = (rand() % 4) * 0.25f;
        pt.v = (rand() % 4) * 0.25f;
        pt.sub = 0.25f;
        pt.size = 0.05f;
        SpawnParticle(pt);
    }
}

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

void BreakVirtual(const Target& t, CPlayerPed* ped) {
    if (t.carve || t.skin) {
        // into a building: the cell opens, what is behind it is blocks
        const int b = t.skin ? CarveBreakSkin(t.pos) : CarveBreak(t.cp, t.ent, gGame.lookDir, t.mat.block);
        if (b == ID_AIR)
            return;
        SpawnBreakParticles(t.pos, b);
        PlaySfx(DigSound(b), &t.point);
        if (gGame.gameMode == MODE_SURVIVAL) {
            bool harvest = true;
            BreakSeconds(b, gInv.Held(), &harvest);
            if (harvest)
                SpawnBlockDropsImpl(b, t.point + t.normal * 0.3f);
        }
        return;
    }
    if (t.cap || t.dig) {
        // digging into the GTA map: the ground opens and the block under it is ours
        int b = t.cap ? TerrainCapBlock(t.pos.x, t.pos.y) : t.virtualBlock;
        if (t.cap)
            TerrainOpenColumn(t.pos.x, t.pos.y, false);
        else
            b = TerrainDig(t.cp, t.ent, t.mat);
        if (b == ID_AIR)
            return;
        SpawnBreakParticles(t.pos, b);
        PlaySfx(DigSound(b), &t.point);
        if (gGame.gameMode == MODE_SURVIVAL) {
            bool harvest = true;
            BreakSeconds(b, gInv.Held(), &harvest);
            if (harvest) {
                SpawnBlockDropsImpl(b, t.point + t.normal * 0.3f);
                SpawnXp(t.point + t.normal * 0.3f, XpForBlock(b));
            }
        }
        return;
    }
    int block = t.virtualBlock;
    {
        // why did this ground not open? (the log tells, a few times per session)
        static int logged = 0;
        if (logged < 25 && t.ent && t.ent->m_nType == ENTITY_TYPE_BUILDING && t.normal.z > 0.55f) {
            ++logged;
            Log("Dig: ground not dug: surface %d, model %d '%s', area %d, normal %.2f, block %d kind %d",
                (int)HitSurface(t.cp), (int)t.ent->m_nModelIndex, GtaModelName(t.ent->m_nModelIndex),
                (int)CGame::currArea, t.normal.z, (int)t.mat.block, (int)t.mat.kind);
        }
    }
    SpawnBreakParticles(t.pos, block);
    PlaySfx(DigSound(block), &t.point);
    CVehicle* veh = t.vehicleRef >= 0 ? CPools::GetVehicle(t.vehicleRef) : nullptr;
    CObject* obj = t.objectRef >= 0 ? CPools::GetObject(t.objectRef) : nullptr;
    if (gGame.gameMode == MODE_SURVIVAL) {
        bool harvest = true;
        BreakSecondsFor(block, t.mat.hardness, gInv.Held(), &harvest);
        if (harvest) {
            int tier = gInv.Held().Empty() ? 0 : Item(gInv.Held().id).tier;
            SpawnMaterialDrops(t.mat, t.point + t.normal * 0.4f, tier, veh);
        }
    }
    // the GTA thing itself goes too
    if (veh) {
        BreakVehicle(veh);
    } else if (obj) {
        gBrokenObjects.push_back(t.objectRef);
        if (gBrokenObjects.size() > 512)
            gBrokenObjects.erase(gBrokenObjects.begin());
        CVector at = t.point, dir = gGame.lookDir;
        obj->ObjectDamage(10000.0f, &at, &dir, ped, WEAPONTYPE_EXPLOSION);
        LaunchEntity(obj, dir * 3.0f + CVector(0, 0, 2.0f));
    }
}

bool CanPlaceAt(const Int3& p, CPlayerPed* ped) {
    if (gWorld.GetBlock(p.x, p.y, p.z) != ID_AIR)
        return false;
    auto overlaps = [&](const CVector& c, float r, float z0, float z1) {
        return c.x + r > p.x && c.x - r < p.x + 1 && c.y + r > p.y && c.y - r < p.y + 1 && z1 > p.z && z0 < p.z + 1;
    };
    // the player and other people (GTA's own test uses bounding spheres that are far too big)
    CVector pp = ped->GetPosition();
    if (!ped->bInVehicle && overlaps(pp, 0.3f, pp.z - 0.99f, pp.z + 0.8f))
        return false;
    const CVector centre(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f);
    if (auto* pool = CPools::ms_pPedPool)
        for (int i = 0; i < pool->m_nSize; ++i) {
            CPed* o = pool->GetAt(i);
            if (!o || o == ped || o->bInVehicle || o->m_fHealth <= 0.0f)
                continue;
            CVector c = o->GetPosition();
            if (overlaps(c, 0.3f, c.z - 0.99f, c.z + 0.8f))
                return false;
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
                return false;
        }
    return true;
}

int PlacementMeta(int block) {
    const BlockDef& d = Block(block);
    if (d.shape == SHAPE_FACING) {
        const CVector& dir = gGame.lookDir;
        if (std::fabs(dir.x) > std::fabs(dir.y))
            return dir.x > 0 ? 3 : 1;
        return dir.y > 0 ? 2 : 0;
    }
    if (d.shape == SHAPE_COLUMN) {
        CVector n = gTarget.normal;
        float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
        if (az >= ax && az >= ay)
            return 0;
        return ax > ay ? 1 : 2;
    }
    return 0;
}

void PlaceHeldBlock(CPlayerPed* ped) {
    ItemStack& h = gInv.Held();
    if (h.Empty() || !IsBlockItem(h.id) || !gTarget.valid || IsFluidBlock(h.id))
        return;
    Int3 p;
    if (IsPlantBlock(h.id)) {
        // plants go on top of soil (blocks or the GTA map)
        if (gTarget.voxel) {
            if (gTarget.face != FACE_TOP)
                return;
            p = gTarget.pos + FACE_DIR[FACE_TOP];
        } else {
            if (gTarget.normal.z < 0.5f)
                return;
            p = { FloorI(gTarget.point.x), FloorI(gTarget.point.y), PlantCellOnGround(gTarget.point.z) };
        }
        if (!CanPlantAt(h.id, p, gGame.gameMode == MODE_CREATIVE))
            return;
    } else {
        if (gTarget.voxel) {
            p = gTarget.pos + FACE_DIR[gTarget.face];
        } else {
            CVector q = gTarget.point + gTarget.normal * 0.5f;
            p = { FloorI(q.x), FloorI(q.y), FloorI(q.z) };
        }
        const int there = gWorld.GetBlock(p.x, p.y, p.z);
        if (IsPlantBlock(there) || IsFluidBlock(there) || IsFireBlock(there)) {
            if (IsPlantBlock(there))
                SpawnBlockDropsImpl(there, CVector(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f));
            gWorld.SetRaw(p.x, p.y, p.z, MakeVox(ID_AIR)); // plants, fluids and fire make room
        }
        if (!CanPlaceAt(p, ped))
            return;
    }
    int block = h.id;
    gWorld.Set(p.x, p.y, p.z, MakeVox(block, PlacementMeta(block)));
    if (block == ID_FURNACE || block == ID_BLAST_FURNACE || block == ID_SMOKER)
        gWorld.furnaces[p] = FurnaceState();
    if (block == ID_CHEST || block == ID_BARREL)
        gWorld.chests[p] = ChestState();
    CVector c(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f);
    PlaySfx(PlaceSound(block), &c);
    if (gGame.gameMode == MODE_SURVIVAL && --h.count == 0)
        h.Clear();
    gPlaceCooldown = 0.25f;
    StartSwing();
}

// ---------------------------------------------------------------- pad control
void BlockAllPadInput() {
    CPad* pad = CPad::GetPad(0);
    memset(&pad->NewState, 0, sizeof(pad->NewState));
    CPad::NewMouseControllerState.x = 0.0f;
    CPad::NewMouseControllerState.y = 0.0f;
    CPad::NewMouseControllerState.wheelUp = 0;
    CPad::NewMouseControllerState.wheelDown = 0;
}

void BlockActionInput() {
    CPad* pad = CPad::GetPad(0);
    pad->NewState.ButtonCircle = 0; // fire / punch
    pad->OldState.ButtonCircle = 0;
    pad->NewState.RightShoulder1 = 0; // aim
    pad->OldState.RightShoulder1 = 0;
    pad->NewState.LeftShoulder2 = 0;  // weapon cycling
    pad->NewState.RightShoulder2 = 0;
    pad->OldState.LeftShoulder2 = 0;
    pad->OldState.RightShoulder2 = 0;
    CPad::NewMouseControllerState.wheelUp = 0;
    CPad::NewMouseControllerState.wheelDown = 0;
}

void SetGtaHud(bool minecraft) {
    if (minecraft) {
        CTheScripts::bDisplayHud = false;
        CHud::bScriptDontDisplayRadar = !gConfig.showRadar;
        gHudFlagsTouched = true;
    } else if (gHudFlagsTouched) {
        CTheScripts::bDisplayHud = true;
        CHud::bScriptDontDisplayRadar = false;
        gHudFlagsTouched = false;
    }
}

// ---------------------------------------------------------------- simulation
void UpdateDrops(float dt, CPlayerPed* ped) {
    CVector pp = ped ? ped->GetPosition() : CVector(0, 0, -10000);
    static float mergeTimer = 0.0f;
    mergeTimer += dt;
    if (mergeTimer > 0.5f) {
        // ItemEntity.mergeWithNeighbours: equal items lying next to each other become one stack
        mergeTimer = 0.0f;
        for (size_t i = 0; i < gDrops.size(); ++i)
            for (size_t j = i + 1; j < gDrops.size(); ++j) {
                DropEntity& a = gDrops[i];
                DropEntity& b = gDrops[j];
                if (!a.stack.SameItem(b.stack) || a.stack.count + b.stack.count > MaxStack(a.stack.id) ||
                    (a.pos - b.pos).Magnitude() > 0.7f)
                    continue;
                a.stack.count = (uint8_t)(a.stack.count + b.stack.count);
                a.age = std::min(a.age, b.age);
                b.stack.count = 0;
            }
    }
    for (size_t i = 0; i < gDrops.size();) {
        DropEntity& d = gDrops[i];
        d.age += dt;
        bool remove = d.stack.count == 0;
        const int fluid = FireAt(d.pos + CVector(0, 0, 0.1f)) ? ID_LAVA : FluidAt(d.pos + CVector(0, 0, 0.1f));
        float wl;
        const bool gtaWater = CWaterLevel::GetWaterLevelNoWaves(d.pos.x, d.pos.y, d.pos.z, &wl) && d.pos.z < wl;
        if (fluid == ID_LAVA) {
            // lava burns items
            PlaySfx(SND_LAVA_EXTINGUISH, &d.pos, 0.4f, 2.0f);
            remove = true;
        } else if (fluid == ID_WATER || gtaWater) {
            d.vel.z += 18.0f * dt; // items float up
            d.vel = d.vel * std::pow(0.8f, dt * 20.0f);
            if (fluid == ID_WATER) {
                // and drift with the current
                const CVector flow = FluidFlowAt({ FloorI(d.pos.x), FloorI(d.pos.y), FloorI(d.pos.z + 0.1f) });
                d.vel.x += flow.x * 5.6f * dt;
                d.vel.y += flow.y * 5.6f * dt;
            }
        } else {
            d.vel.z -= 16.0f * dt;
            d.vel.z *= std::pow(0.98f, dt * 20.0f);
        }
        CVector np = d.pos + d.vel * dt;
        // pushed out of blocks it ended up in
        if (gWorld.IsSolid(FloorI(np.x), FloorI(np.y), FloorI(np.z + 0.1f)))
            np.z = (float)FloorI(np.z + 0.1f) + 1.0f;
        if (--d.groundCheck <= 0 || d.vel.z < -0.5f) {
            d.groundCheck = 6;
            float gz;
            d.groundZ = GroundBelow(CVector(np.x, np.y, d.pos.z + 0.6f), 2.0f, &gz) ? gz : -1000.0f;
        }
        bool onGround = false;
        if (np.z <= d.groundZ + 0.001f && d.groundZ - np.z < 1.2f && d.vel.z <= 0.0f) {
            np.z = d.groundZ;
            d.vel.z = 0.0f;
            onGround = true;
        }
        // friction: 0.6 (block slipperiness) * 0.98 per tick on the ground, 0.98 in the air
        const float fr = std::pow(onGround ? 0.588f : 0.98f, dt * 20.0f);
        d.vel.x *= fr;
        d.vel.y *= fr;
        d.pos = np;
        remove = remove || d.age > 300.0f || d.pos.z < -200.0f;
        if (!remove && ped && d.age > d.pickupDelay && ped->m_fHealth > 0.0f) {
            CVector diff = d.pos - CVector(pp.x, pp.y, pp.z - 0.5f);
            if (diff.Magnitude() < (ped->bInVehicle ? 2.8f : 1.6f)) {
                int left = gInv.Add(d.stack);
                if (left == 0)
                    remove = true;
                if (left != d.stack.count)
                    PlaySfx(SND_PICKUP, nullptr, 0.25f, 1.0f + (Rand01() - Rand01()) * 0.7f);
                d.stack.count = (uint8_t)left;
                gWorld.dirty = true;
            }
        }
        if (remove) {
            gDrops[i] = gDrops.back();
            gDrops.pop_back();
        } else {
            ++i;
        }
    }
}

void UpdateParticles(float dt) {
    for (size_t i = 0; i < gParticles.size();) {
        Particle& p = gParticles[i];
        p.life -= dt;
        p.vel.z -= p.gravity * dt;
        p.vel = p.vel * (1.0f - Clamp(dt * 0.8f, 0.0f, 1.0f));
        p.pos += p.vel * dt;
        if (p.life <= 0) {
            gParticles[i] = gParticles.back();
            gParticles.pop_back();
        } else {
            ++i;
        }
    }
}

void UpdateTnt(float dt) {
    for (size_t i = 0; i < gPrimedTnt.size();) {
        PrimedTnt& t = gPrimedTnt[i];
        t.fuse -= dt;
        t.vel.z -= 16.0f * dt;
        CVector np = t.pos + t.vel * dt;
        const int cz = FloorI(t.pos.z + 0.5f);
        // walls made of blocks
        if (t.vel.x != 0.0f && gWorld.IsSolid(FloorI(np.x + (t.vel.x > 0 ? 0.49f : -0.49f)), FloorI(t.pos.y), cz)) {
            np.x = t.pos.x;
            t.vel.x *= -0.3f;
        }
        if (t.vel.y != 0.0f && gWorld.IsSolid(FloorI(np.x), FloorI(np.y + (t.vel.y > 0 ? 0.49f : -0.49f)), cz)) {
            np.y = t.pos.y;
            t.vel.y *= -0.3f;
        }
        // walls of the GTA map
        const float hs = std::sqrt(t.vel.x * t.vel.x + t.vel.y * t.vel.y);
        if (hs > 1.0f) {
            CColPoint cp;
            CEntity* e = nullptr;
            CVector a = t.pos + CVector(0, 0, 0.5f);
            CVector b = a + CVector(t.vel.x, t.vel.y, 0.0f) * (dt + 0.5f / hs);
            if (CWorld::ProcessLineOfSight(a, b, cp, e, true, true, false, true, false, false, false, false) &&
                !IsCollisionObject(e)) {
                np.x = t.pos.x;
                np.y = t.pos.y;
                t.vel.x *= -0.3f;
                t.vel.y *= -0.3f;
            }
        }
        if (t.vel.z > 0.0f && gWorld.IsSolid(FloorI(np.x), FloorI(np.y), FloorI(np.z + 1.0f)))
            t.vel.z = 0.0f;
        float gz;
        t.onGround = false;
        if (t.vel.z <= 0.0f && GroundBelow(CVector(np.x, np.y, t.pos.z + 0.6f), 200.0f, &gz) && np.z <= gz) {
            np.z = gz;
            t.vel.z = 0.0f;
            t.onGround = true;
        }
        float drag = std::pow(t.onGround ? 0.7f : 0.98f, dt * 20.0f);
        t.vel.x *= drag;
        t.vel.y *= drag;
        t.pos = np;
        // smoke from the fuse
        if (rand() % 3 == 0) {
            Particle p;
            p.pos = t.pos + CVector(0, 0, 1.1f);
            p.vel = CVector(0, 0, 0.8f);
            p.maxLife = p.life = 0.6f;
            p.tile = TILE_P_GENERIC_0;
            p.anim = 2;
            p.size = 0.07f;
            p.gravity = -0.5f;
            p.color = 0xFF505050;
            SpawnParticle(p);
        }
        if (t.fuse <= 0 || t.pos.z < -150.0f) {
            CVector at(t.pos.x, t.pos.y, t.pos.z + 0.5f);
            gPrimedTnt[i] = gPrimedTnt.back();
            gPrimedTnt.pop_back();
            ExplodeAt(at, 3.4f, true, EXPLOSION_GRENADE); // may prime more TNT (changes gPrimedTnt)
        } else {
            ++i;
        }
    }
}

void ExplodeBlocks(const CVector& c, float radius) {
    TerrainExplode(c, radius);
    // GTA buildings next to the blast: a crater in them too
    for (const CarveOpened& o : CarveExplode(c, radius)) {
        if (o.surface && gGame.gameMode == MODE_SURVIVAL && Rand01() < 0.3f)
            SpawnBlockDrops(o.block, CVector(o.cell.x + 0.5f, o.cell.y + 0.5f, o.cell.z + 0.5f));
        if (o.surface)
            SpawnBreakParticles(o.cell, o.block);
    }
    int r = (int)std::ceil(radius);
    int cx = FloorI(c.x), cy = FloorI(c.y), cz = FloorI(c.z);
    for (int z = cz - r; z <= cz + r; ++z)
        for (int y = cy - r; y <= cy + r; ++y)
            for (int x = cx - r; x <= cx + r; ++x) {
                int b = gWorld.GetBlock(x, y, z);
                if (b == ID_AIR || Block(b).hardness < 0 || Block(b).hardness >= 50.0f)
                    continue;
                float dx = x + 0.5f - c.x, dy = y + 0.5f - c.y, dz = z + 0.5f - c.z;
                float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                Int3 p{ x, y, z };
                if (b == ID_TNT) {
                    // chain reaction: neighbouring TNT is lit and thrown away from the blast
                    if (dist > radius + 0.5f)
                        continue;
                    float f = 1.0f - Clamp(dist / (radius + 0.5f), 0.0f, 1.0f);
                    CVector dir = dist > 0.05f ? CVector(dx, dy, dz) * (1.0f / dist) : CVector(0, 0, 1);
                    CVector vel = dir * (5.0f + 9.0f * f) + CVector(0, 0, 5.0f + 4.0f * f);
                    vel.x += (Rand01() - 0.5f) * 3.0f;
                    vel.y += (Rand01() - 0.5f) * 3.0f;
                    IgniteTnt(p, 0.5f + Rand01(), &vel);
                    continue;
                }
                if (dist > radius * (0.75f + 0.25f * Rand01()))
                    continue;
                if (gGame.screen != SCREEN_NONE && gGame.openPos == p)
                    CloseScreen();
                if (gGame.gameMode == MODE_SURVIVAL && Rand01() < 0.3f)
                    SpawnBlockDrops(b, CVector(x + 0.5f, y + 0.5f, z + 0.5f));
                SpawnBreakParticles(p, b);
                DropContainerContents(p);
                gWorld.Set(x, y, z, MakeVox(ID_AIR));
            }
}

void PollExplosions(float dt) {
    for (size_t i = 0; i < gOwnBlasts.size();) {
        gOwnBlasts[i].age += dt;
        if (gOwnBlasts[i].age > 2.0f) {
            gOwnBlasts[i] = gOwnBlasts.back();
            gOwnBlasts.pop_back();
        } else {
            ++i;
        }
    }
    for (int i = 0; i < 16; ++i) {
        CExplosion& e = aExplosions[i];
        // the "active counter" byte lives at offset 0x28 (starts at 1, counts frames, 0 = free slot)
        const uint8_t counter = *(reinterpret_cast<const uint8_t*>(&e) + 0x28);
        const bool fresh = counter != 0 && (gExpCounter[i] == 0 || counter < gExpCounter[i] ||
                                            (e.m_vecPosition - gExpPos[i]).Magnitude() > 0.01f);
        gExpCounter[i] = counter;
        gExpPos[i] = e.m_vecPosition;
        if (!fresh)
            continue;
        bool own = false;
        for (auto& o : gOwnBlasts)
            if ((o.pos - e.m_vecPosition).Magnitude() < 0.05f)
                own = true;
        if (own)
            continue;
        float radius = 0.0f;
        switch (e.m_nType) {
        case EXPLOSION_GRENADE: radius = 3.2f; break;
        case EXPLOSION_ROCKET: case EXPLOSION_TANK_FIRE: radius = 3.5f; break;
        case EXPLOSION_WEAK_ROCKET: case EXPLOSION_MINE: radius = 2.5f; break;
        case EXPLOSION_CAR: case EXPLOSION_QUICK_CAR: case EXPLOSION_BOAT: radius = 3.0f; break;
        case EXPLOSION_AIRCRAFT: radius = 4.5f; break;
        case EXPLOSION_SMALL: case EXPLOSION_RC_VEHICLE: case EXPLOSION_OBJECT: radius = 1.5f; break;
        default: radius = 0.0f; break;
        }
        if (radius > 0)
            ExplodeAt(e.m_vecPosition, radius, false);
    }
}

void UpdateSurvival(float dt, CPlayerPed* ped) {
    float maxH = ped->m_fMaxHealth > 1.0f ? ped->m_fMaxHealth : 100.0f;
    if (gGame.gameMode == MODE_CREATIVE) {
        gGame.food = 20.0f;
        if (ped->m_fHealth > 0.0f && ped->m_fHealth < maxH)
            ped->m_fHealth = maxH;
        return;
    }
    CVector p = ped->GetPosition();
    if (!ped->bInVehicle && !gGame.gliding) {
        CVector delta = p - gLastPlayerPos;
        delta.z = 0;
        float dist = delta.Magnitude();
        if (dt > 0 && dist / dt > 4.5f && dist < 5.0f)
            gGame.exhaustion += 0.1f * dist;
        if (ped->bIsInTheAir && !gWasInAir)
            gGame.exhaustion += 0.05f;
        gWasInAir = ped->bIsInTheAir;
    }
    gLastPlayerPos = p;
    while (gGame.exhaustion >= 4.0f) {
        gGame.exhaustion -= 4.0f;
        if (gGame.saturation > 0)
            gGame.saturation = std::max(0.0f, gGame.saturation - 1.0f);
        else
            gGame.food = std::max(0.0f, gGame.food - 1.0f);
    }
    gGame.foodTimer += dt;
    if (gGame.foodTimer >= 4.0f) {
        gGame.foodTimer = 0;
        if (gGame.food >= 18.0f && ped->m_fHealth > 0 && ped->m_fHealth < maxH) {
            ped->m_fHealth = std::min(maxH, ped->m_fHealth + maxH / 20.0f);
            gGame.exhaustion += 6.0f;
        } else if (gGame.food <= 0.0f && ped->m_fHealth > maxH * 0.1f) {
            ped->m_fHealth = std::max(maxH * 0.1f, ped->m_fHealth - maxH / 20.0f);
            gGame.hurtTimer = 0.5f;
            PlaySfx(SND_HURT, nullptr, 0.6f);
        }
        gHealthSeen = ped->m_fHealth;
    }
}

void HandleDeath(CPlayerPed* ped) {
    bool dead = ped->m_fHealth <= 0.0f;
    if (dead && !gWasDead) {
        if (gGame.screen != SCREEN_NONE)
            CloseScreen();
        StopFlying(ped);
        StopRiding(ped);
        PlaySfx(SND_PLAYER_DEATH);
        if (!gConfig.keepInventory && gGame.gameMode == MODE_SURVIVAL) {
            CVector p = ped->GetPosition();
            for (auto& s : gInv.slots) {
                SpawnDrop(p, s, CVector(0, 0, 0), 2.0f);
                s.Clear();
            }
            for (auto& s : gInv.armor) {
                SpawnDrop(p, s, CVector(0, 0, 0), 2.0f);
                s.Clear();
            }
        }
    }
    if (!dead && gWasDead) {
        gGame.food = 20.0f;
        gGame.saturation = 5.0f;
    }
    gWasDead = dead;
}

// creative mode: no damage at all, like in Minecraft
bool gProofsSet = false;
void SetCreativeProofs(CPlayerPed* ped, bool on) {
    if (on == gProofsSet)
        return;
    ped->bBulletProof = ped->bFireProof = ped->bCollisionProof = ped->bMeleeProof = ped->bExplosionProof = on;
    gProofsSet = on;
}


// ---------------------------------------------------------------- items that act on the world
void ReplaceHeldWith(uint16_t id) {
    ItemStack& held = gInv.Held();
    if (gGame.gameMode == MODE_CREATIVE)
        return;
    ItemStack s;
    s.id = id;
    s.count = 1;
    if (held.count <= 1) {
        held = s;
    } else {
        --held.count;
        if (gInv.Add(s) > 0)
            DropStackAtPlayer(s, false);
    }
    gWorld.dirty = true;
}

// right click with buckets, bone meal, armour
bool UseWorldItem(CPlayerPed* ped) {
    ItemStack& held = gInv.Held();
    if (held.Empty())
        return false;
    const ItemDef& d = Item(held.id);
    const bool survival = gGame.gameMode == MODE_SURVIVAL;
    switch (d.special) {
    case SP_WATER_BUCKET:
    case SP_LAVA_BUCKET: {
        if (!gTarget.valid)
            return false;
        Int3 c;
        if (gTarget.voxel) {
            c = gTarget.pos + FACE_DIR[gTarget.face];
        } else {
            CVector q = gTarget.point + gTarget.normal * 0.5f;
            c = { FloorI(q.x), FloorI(q.y), FloorI(q.z) };
        }
        const bool water = d.special == SP_WATER_BUCKET;
        if (!PlaceFluid(water ? ID_WATER : ID_LAVA, c))
            return false;
        const CVector at(c.x + 0.5f, c.y + 0.5f, c.z + 0.5f);
        if (water)
            gFireManager.ExtinguishPoint(at, 3.0f);
        PlaySfx(water ? SND_BUCKET_EMPTY : SND_BUCKET_EMPTY_LAVA, &at);
        ReplaceHeldWith(ID_BUCKET);
        StartSwing();
        return true;
    }
    case SP_BUCKET: {
        VoxelHit fh = RaycastVoxels(gGame.rayOrigin, gGame.lookDir, (gGame.eyePos - gGame.rayOrigin).Magnitude() + 5.0f, true);
        int fluid;
        if (fh.hit && TakeFluid(fh.pos, &fluid)) {
            const CVector at(fh.pos.x + 0.5f, fh.pos.y + 0.5f, fh.pos.z + 0.5f);
            PlaySfx(fluid == ID_LAVA ? SND_BUCKET_FILL_LAVA : SND_BUCKET_FILL, &at);
            ReplaceHeldWith(fluid == ID_LAVA ? ID_LAVA_BUCKET : ID_WATER_BUCKET);
            StartSwing();
            return true;
        }
        return false;
    }
    case SP_BONE_MEAL: {
        if (!gTarget.valid)
            return false;
        const bool gtaGrass = !gTarget.voxel && !gTarget.cap && gTarget.mat.kind == GM_GRASS;
        if (!ApplyBoneMeal(gTarget.voxel, gTarget.pos, gTarget.point, gTarget.normal, gtaGrass))
            return false;
        PlaySfx(SND_BONE_MEAL, &gTarget.point);
        if (survival && --held.count == 0)
            held.Clear();
        StartSwing();
        return true;
    }
    default:
        break;
    }
    // armour: a right click puts it on
    if (d.armorSlot >= 1 && d.armorSlot <= 4 && gInv.armor[d.armorSlot - 1].Empty()) {
        gInv.armor[d.armorSlot - 1] = held;
        held.Clear();
        PlaySfx(d.special == SP_ELYTRA ? SND_EQUIP_ELYTRA : SND_EQUIP_IRON);
        gWorld.dirty = true;
        return true;
    }
    (void)ped;
    return false;
}

// does the main hand do anything with a right click? (if not, the off hand gets the click)
bool HasRightClickUse(const ItemStack& s) {
    if (s.Empty())
        return false;
    if (IsBlockItem(s.id))
        return true;
    const ItemDef& d = Item(s.id);
    if (d.food > 0 || d.armorSlot)
        return true;
    return d.special != SP_NONE && d.special != SP_ARROW && d.special != SP_TOTEM;
}

bool NearVehicle(CPlayerPed* ped, float r) {
    auto* pool = CPools::ms_pVehiclePool;
    if (!pool)
        return false;
    for (int i = 0; i < pool->m_nSize; ++i)
        if (CVehicle* v = pool->GetAt(i))
            if ((v->GetPosition() - ped->GetPosition()).Magnitude() < r)
                return true;
    return false;
}

bool IsContainer(int b) {
    return b == ID_CRAFTING_TABLE || b == ID_FURNACE || b == ID_BLAST_FURNACE || b == ID_SMOKER || b == ID_CHEST ||
           b == ID_BARREL;
}

// ---------------------------------------------------------------- eating / drinking
void ApplyFood(CPlayerPed* ped) {
    ItemStack& held = gInv.Held();
    const ItemDef& d = Item(held.id);
    const bool survival = gGame.gameMode == MODE_SURVIVAL;
    const float maxH = ped->m_fMaxHealth > 1.0f ? ped->m_fMaxHealth : 100.0f;
    if (d.special == SP_MILK) {
        // milk clears every effect... and the police's memory of you
        plugin::Command<plugin::Commands::CLEAR_WANTED_LEVEL>(0);
        gFireManager.ExtinguishPoint(ped->GetPosition(), 2.5f);
        ClearEffects(ped);
        ShowMessage("Süt içtin: etkiler ve aranma seviyen sıfırlandı");
        if (survival) {
            held.id = ID_BUCKET;
            held.count = 1;
            held.damage = 0;
        }
        PlaySfx(SND_BURP, nullptr, 0.5f);
        gWorld.dirty = true;
        return;
    }
    gGame.food = std::min(20.0f, gGame.food + d.food);
    gGame.saturation = std::min(gGame.food, gGame.saturation + d.saturation);
    (void)maxH;
    switch (held.id) {
    case ID_GOLDEN_APPLE: // Regeneration II 5 s, Absorption I 2 min (two yellow hearts)
        AddEffect(EFFECT_REGENERATION, 5.0f, 1);
        AddEffect(EFFECT_ABSORPTION, 120.0f, 0);
        break;
    case ID_ENCHANTED_GOLDEN_APPLE: // Regeneration II 20 s, Absorption IV 2 min, Resistance and Fire Resistance 5 min
        AddEffect(EFFECT_REGENERATION, 20.0f, 1);
        AddEffect(EFFECT_ABSORPTION, 120.0f, 3);
        AddEffect(EFFECT_RESISTANCE, 300.0f, 0);
        AddEffect(EFFECT_FIRE_RESISTANCE, 300.0f, 0);
        break;
    case ID_ROTTEN_FLESH:
        if (Rand01() < 0.8f)
            AddEffect(EFFECT_HUNGER, 30.0f, 0);
        break;
    case ID_CHICKEN:
        if (Rand01() < 0.3f)
            AddEffect(EFFECT_HUNGER, 30.0f, 0);
        break;
    case ID_SPIDER_EYE:
        AddEffect(EFFECT_POISON, 5.0f, 0);
        break;
    case ID_POISONOUS_POTATO:
        if (Rand01() < 0.6f)
            AddEffect(EFFECT_POISON, 5.0f, 0);
        break;
    case ID_PUFFERFISH:
        AddEffect(EFFECT_HUNGER, 15.0f, 2);
        AddEffect(EFFECT_POISON, 60.0f, 1);
        break;
    case ID_HONEY_BOTTLE:
        RemoveEffect(EFFECT_POISON, ped);
        break;
    default:
        break;
    }
    if (survival && --held.count == 0)
        held.Clear();
    PlaySfx(SND_BURP, nullptr, 0.5f);
    gWorld.dirty = true;
}

// returns true while the player eats or drinks
bool UpdateEating(float dt, CPlayerPed* ped, bool rmbDown) {
    ItemStack& held = gInv.Held();
    if (held.Empty()) {
        gGame.eatTimer = 0.0f;
        return false;
    }
    const ItemDef& d = Item(held.id);
    const bool milk = d.special == SP_MILK;
    const bool golden = held.id == ID_GOLDEN_APPLE || held.id == ID_ENCHANTED_GOLDEN_APPLE;
    const bool canEat = milk || golden || (d.food > 0 && gGame.gameMode == MODE_SURVIVAL && gGame.food < 20.0f);
    if (!canEat || !rmbDown) {
        gGame.eatTimer = 0.0f;
        return false;
    }
    gGame.eatTimer += dt;
    gEatSoundTimer -= dt;
    if (gEatSoundTimer <= 0.0f) {
        gEatSoundTimer = milk ? 0.25f : 0.2f;
        PlaySfx(milk ? SND_DRINK : SND_EAT, nullptr, 0.5f, 0.8f + Rand01() * 0.4f);
    }
    if (gGame.eatTimer >= 1.6f) {
        gGame.eatTimer = 0.0f;
        ApplyFood(ped);
    }
    return true;
}

// ---------------------------------------------------------------- items in vehicles
void VehicleUse(float dt, CPlayerPed* ped) {
    gMiningProgress = 0;
    if (KeyPressed(gConfig.keyInventory)) {
        OpenScreen(gGame.gameMode == MODE_CREATIVE ? SCREEN_CREATIVE : SCREEN_INVENTORY);
        BlockAllPadInput();
        return;
    }
    const bool rmb = MouseRight();
    const bool charging = UpdateChargedItems(dt, ped, rmb, MouseRightPressed());
    const bool eating = !charging && UpdateEating(dt, ped, rmb);
    if (MouseRightPressed() && !charging && !eating)
        UseSpecialItem(ped, false, false, Int3{}, gGame.eyePos + gGame.lookDir * 3.0f, gGame.lookDir * -1.0f);
}

// ---------------------------------------------------------------- health: armour, totem, hurt flash
void UpdateHealthEffects(float dt, CPlayerPed* ped) {
    const float maxH = ped->m_fMaxHealth > 1.0f ? ped->m_fMaxHealth : 100.0f;
    float h = ped->m_fHealth;
    gGame.hurtTimer = std::max(0.0f, gGame.hurtTimer - dt);
    if (h <= 0.0f) {
        if (gGame.deathTime < 0.0f && gGame.age - gGame.deathCauseTime > 3.0f)
            gGame.deathCause = STR_DEATH_GENERIC;
        gGame.deathTime = gGame.deathTime < 0.0f ? 0.0f : gGame.deathTime + dt;
        gHealthSeen = h;
        if (CHud::m_BigMessage)
            for (int i = 0; i < 7; ++i)
                CHud::m_BigMessage[i][0] = 0; // "WASTED": the Minecraft death screen says it
        return;
    }
    gGame.deathTime = -1.0f;
    if (gHealthSeen < 0.0f || gHealthSeen > maxH * 2.0f)
        gHealthSeen = h;
    const bool survival = gGame.gameMode == MODE_SURVIVAL;
    const float direct = std::min(gGame.directDamage, std::max(0.0f, gHealthSeen - h));
    gGame.directDamage = 0.0f;
    if (h < gHealthSeen - 0.5f) {
        float loss = gHealthSeen - h - direct;
        if (survival && loss > 0.0f) {
            // Minecraft armour: every point blocks 4% of the damage (up to 80%) and wears the pieces
            int points = 0;
            for (auto& a : gInv.armor)
                if (!a.Empty())
                    points += Item(a.id).armorPoints;
            float block = Clamp(points * 0.04f, 0.0f, 0.8f);
            if (block > 0.0f) {
                h = std::min(maxH, h + loss * block);
                ped->m_fHealth = h;
                int wear = std::max(1, (int)(loss / 20.0f));
                for (auto& a : gInv.armor) {
                    if (a.Empty() || a.id == ID_ELYTRA)
                        continue;
                    const ItemDef& d = Item(a.id);
                    a.damage += (uint16_t)wear;
                    if (d.durability && a.damage >= d.durability) {
                        PlaySfx(SND_TOOL_BREAK);
                        a.Clear();
                    }
                }
            }
            // then resistance and the yellow absorption hearts
            const float left = std::max(0.0f, gHealthSeen - direct - h);
            const float after = EffectsAbsorbDamage(left, maxH);
            if (after < left) {
                h = std::min(maxH, h + (left - after));
                ped->m_fHealth = h;
            }
        }
        if (survival && loss + direct > 2.0f)
            PlaySfx(SND_HURT, nullptr, 0.7f);
        gGame.hurtTimer = 0.5f;
    }
    // totem of undying: saves the player at the last moment (held in either hand)
    if (survival && h < maxH * 0.15f) {
        ItemStack* totem = nullptr;
        if (!gInv.Held().Empty() && Item(gInv.Held().id).special == SP_TOTEM)
            totem = &gInv.Held();
        else if (!gInv.offhand.Empty() && Item(gInv.offhand.id).special == SP_TOTEM)
            totem = &gInv.offhand;
        if (totem) {
            if (--totem->count == 0)
                totem->Clear();
            ped->m_fHealth = h = maxH * 0.5f;
            ClearEffects(ped);
            AddEffect(EFFECT_REGENERATION, 45.0f, 1);
            AddEffect(EFFECT_ABSORPTION, 5.0f, 1);
            AddEffect(EFFECT_FIRE_RESISTANCE, 40.0f, 0);
            gFireManager.ExtinguishPoint(ped->GetPosition(), 2.5f);
            PlaySfx(SND_TOTEM);
            for (int i = 0; i < 60; ++i) {
                Particle p;
                CVector d(Rand01() * 2 - 1, Rand01() * 2 - 1, Rand01() * 2);
                p.pos = ped->GetPosition();
                p.vel = d * (2.0f + Rand01() * 3.0f);
                p.maxLife = p.life = 1.0f + Rand01();
                p.tile = TILE_P_SPARK_0;
                p.anim = 1;
                p.size = 0.12f;
                p.gravity = 1.5f;
                p.color = rand() % 2 ? 0xFFF0E040 : 0xFF60E040;
                p.glow = true;
                SpawnParticle(p);
            }
            ShowMessage("Ölümsüzlük Totemi seni kurtardı!");
            gWorld.dirty = true;
        }
    }
    gHealthSeen = ped->m_fHealth;
}
} // namespace

// ================================================================ shared helpers
void PushLooseThings(const CVector& at, float radius, float speed) {
    auto push = [&](const CVector& pos, CVector& vel) {
        CVector d = pos - at;
        float dist = d.Magnitude();
        if (dist >= radius)
            return false;
        float f = 1.0f - dist / radius;
        CVector dir = dist > 0.05f ? d * (1.0f / dist) : CVector(0, 0, 1);
        vel += dir * (speed * f) + CVector(0, 0, speed * 0.45f * f);
        return true;
    };
    for (auto& t : gPrimedTnt)
        if (push(t.pos + CVector(0, 0, 0.5f), t.vel))
            t.onGround = false;
    for (auto& d : gDrops)
        push(d.pos + CVector(0, 0, 0.2f), d.vel);
}

void SpawnBlockDrops(int block, const CVector& at) { SpawnBlockDropsImpl(block, at); }

void ExplodeAt(const CVector& at, float radius, bool own, int gtaType) {
    if (own) {
        // our own blast: GTA does the fire ball and the damage, we do the rest right away
        gOwnBlasts.push_back({ at, 0.0f });
        CExplosion::AddExplosion(nullptr, FindPlayerPed(), (eExplosionType)gtaType, at, 0, true, -1.0f, false);
        PlaySfx(SND_EXPLODE, &at, 4.0f, (1.0f + (Rand01() - Rand01()) * 0.2f) * (radius > 2.0f ? 0.7f : 1.0f));
    }
    if (gConfig.explosionsBreakBlocks && radius > 0.0f)
        ExplodeBlocks(at, radius);
    const float r = std::max(radius, 1.5f);
    PushLooseThings(at, r * 2.4f, 14.0f);
    MobsRadial(at, r * 2.2f, own ? 6.0f + radius * 8.0f : 22.0f, 13.0f);
}

void SpawnDrop(const CVector& pos, const ItemStack& s, const CVector& vel, float delay) {
    if (s.Empty())
        return;
    if (gDrops.size() > 400)
        gDrops.erase(gDrops.begin());
    DropEntity d;
    d.pos = pos;
    d.vel = vel;
    if (vel.x == 0 && vel.y == 0 && vel.z == 0)
        d.vel = CVector((Rand01() - 0.5f) * 2.0f, (Rand01() - 0.5f) * 2.0f, 3.0f);
    d.stack = s;
    d.pickupDelay = delay;
    d.spin = Rand01() * 6.28f;
    gDrops.push_back(d);
}

void SpawnDropItem(const CVector& pos, uint16_t id, int count) {
    while (count > 0) {
        ItemStack s;
        s.id = id;
        s.count = (uint8_t)std::min(count, MaxStack(id));
        count -= s.count;
        SpawnDrop(pos, s);
    }
}

void DamageHeldItem(int amount) {
    ItemStack& h = gInv.Held();
    if (h.Empty() || gGame.gameMode == MODE_CREATIVE)
        return;
    const ItemDef& d = Item(h.id);
    if (!d.durability)
        return;
    h.damage += (uint16_t)amount;
    if (h.damage >= d.durability) {
        ShowMessage(std::string(d.name) + " k\xC4\xB1r\xC4\xB1ld\xC4\xB1!", 1.5f);
        PlaySfx(SND_TOOL_BREAK);
        h.Clear();
    }
}

// ================================================================ save / load
static const uint32_t kSaveMagic = 0x4153434D; // "MCSA"
static const uint32_t kSaveVersion = 3; // 3: item ids moved to 1024, xp, dug terrain

static std::string WorldPath(int id);
static int gWorldId = -1; // the world being played: -1 none yet, 0 new (not in a save slot), 1..8 save slot

void SaveAll() {
    if (gWorldId < 1)
        return; // only a world that belongs to a GTA save slot is written
    std::string path = WorldPath(gWorldId), tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) {
        Log("ERROR: cannot write %s", tmp.c_str());
        return;
    }
    fwrite(&kSaveMagic, 4, 1, f);
    fwrite(&kSaveVersion, 4, 1, f);
    int32_t vals[5] = { gGame.gameMode, gInv.selected, gGame.steve ? 1 : 0, gGame.cameraMode, FIRST_ITEM };
    fwrite(vals, sizeof(vals), 1, f);
    fwrite(&gGame.food, 4, 1, f);
    fwrite(&gGame.saturation, 4, 1, f);
    for (auto& s : gInv.slots)
        WriteStack(f, s);
    for (auto& s : gInv.armor)
        WriteStack(f, s);
    WriteStack(f, gInv.offhand);
    gWorld.Write(f);
    int32_t xp[2] = { gGame.xpLevel, gGame.xpTotal };
    fwrite(xp, sizeof(xp), 1, f);
    fwrite(&gGame.xpProgress, 4, 1, f);
    TerrainWrite(f);
    BuildingsWrite(f);
    CarveWrite(f);
    fclose(f);
    MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    gWorld.dirty = false;
    Log("Saved: %u chunks", (unsigned)gWorld.chunks.size());
}

static void LoadAll(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        Log("No save found, starting a new world");
        return;
    }
    uint32_t magic = 0, version = 0;
    fread(&magic, 4, 1, f);
    fread(&version, 4, 1, f);
    if (magic != kSaveMagic || version < 2 || version > kSaveVersion) {
        Log("Save has an old format (v%u), starting a new world", version);
        fclose(f);
        MoveFileExA(path.c_str(), (path + ".old").c_str(), MOVEFILE_REPLACE_EXISTING);
        return;
    }
    int32_t vals[5] = {};
    fread(vals, sizeof(vals), 1, f);
    gGame.gameMode = vals[0] == MODE_CREATIVE ? MODE_CREATIVE : MODE_SURVIVAL;
    gInv.selected = std::clamp(vals[1], 0, 8);
    gGame.steve = vals[2] != 0;
    gGame.cameraMode = std::clamp(vals[3], 0, 2);
    SetItemIdMigration(version == 2 ? 385 : vals[4]); // items used to start at 385
    fread(&gGame.food, 4, 1, f);
    fread(&gGame.saturation, 4, 1, f);
    for (auto& s : gInv.slots)
        s = ReadStack(f);
    for (auto& s : gInv.armor)
        s = ReadStack(f);
    gInv.offhand = ReadStack(f);
    bool ok = gWorld.Read(f);
    if (ok && version >= 3) {
        int32_t xp[2] = {};
        if (fread(xp, sizeof(xp), 1, f) == 1 && fread(&gGame.xpProgress, 4, 1, f) == 1) {
            gGame.xpLevel = std::max(0, xp[0]);
            gGame.xpTotal = std::max(0, xp[1]);
            gGame.xpProgress = std::clamp(gGame.xpProgress, 0.0f, 0.999f);
        }
        if (!TerrainRead(f))
            Log("Dug terrain could not be read");
        else if (BuildingsRead(f)) // (saves from before 0.7 end here)
            CarveRead(f);          // (saves from 0.7 end here)
    }
    SetItemIdMigration(0);
    fclose(f);
    Log("Loaded save: %s, %u chunks", ok ? "ok" : "PARTIAL", (unsigned)gWorld.chunks.size());
}

// ================================================================ worlds
// Every GTA save game has its own world file. 0 = a world that was started with "new game" and has not been
// saved into a slot yet.
static int gNextWorld = -2;      // -2: the next session keeps the world it has
static bool gNextFresh = false;

static std::string WorldPath(int id) {
    if (id <= 0)
        return ModPath("world_new.dat");
    char name[32];
    snprintf(name, sizeof(name), "world_slot%d.dat", id);
    return ModPath(name);
}

bool WorldFileExists(int id) { return GetFileAttributesA(WorldPath(id).c_str()) != INVALID_FILE_ATTRIBUTES; }

void SetNextWorld(int id, bool fresh) {
    gNextWorld = std::clamp(id, 0, 8);
    gNextFresh = fresh;
}

static void ResetWorldState() {
    gWorld.Clear();
    TerrainClear();
    CarveClear();
    BuildingsClear();
    for (auto& s : gInv.slots)
        s.Clear();
    for (auto& s : gInv.armor)
        s.Clear();
    gInv.offhand.Clear();
    gInv.cursor.Clear();
    gInv.selected = 0;
    gGame.food = 20.0f;
    gGame.saturation = 5.0f;
    gGame.exhaustion = 0.0f;
    gGame.xpLevel = 0;
    gGame.xpTotal = 0;
    gGame.xpProgress = 0.0f;
    gGame.gameMode = gConfig.startGameMode == 1 ? MODE_CREATIVE : MODE_SURVIVAL;
}

static void StarterKit() {
    if (!gWorld.chunks.empty() || gInv.CountOf(ID_CRAFTING_TABLE) != 0 || !gInv.slots[0].Empty())
        return;
    ItemStack s;
    s.id = ID_CRAFTING_TABLE; s.count = 1; gInv.Add(s);
    s.id = ID_OAK_PLANKS; s.count = 16; gInv.Add(s);
    s.id = ID_APPLE; s.count = 4; gInv.Add(s);
}

static int gProcessTicks = 0;    // GameProcess calls so far
static int gActivatedTick = -1;  // gProcessTicks when a world was last activated

// Called when a game session starts (first start, new game, loading a save). The world is always read again from
// what was saved with that GTA save game: whatever was changed after the last save is gone, as in GTA itself.
static void ActivateWorld() {
    if (gWorldId >= 0 && gActivatedTick == gProcessTicks)
        return; // GTA reports one restart twice (before and after it loads the save)
    int want = gNextWorld;
    gNextWorld = -2;
    gNextFresh = false;
    if (want == -2) {
        // started from GTA's own menu: a save game that is being loaded, or a new game
        const uint8_t* menu = reinterpret_cast<const uint8_t*>(&FrontEndMenuManager);
        want = menu[0x60] ? std::clamp((int)(int8_t)menu[0x15F], 0, 7) + 1 : 0; // m_bLoadingData, m_SelectedSlot
    }
    gActivatedTick = gProcessTicks;
    ResetWorldState();
    gWorldId = want;
    if (want == 0) {
        Log("World: a new world (kept only when the game is saved)");
    } else if (WorldFileExists(want)) {
        LoadAll(WorldPath(want));
        Log("World: world %d as it was saved", want);
    } else {
        // the one world of the versions before 0.8 goes to the first save game that is played
        const std::string legacy = ModPath("world.dat");
        if (GetFileAttributesA(legacy.c_str()) != INVALID_FILE_ATTRIBUTES) {
            LoadAll(legacy);
            MoveFileExA(legacy.c_str(), ModPath("world_eski_tek_dunya.dat").c_str(), MOVEFILE_REPLACE_EXISTING);
            Log("World: the old single world now belongs to world %d", want);
            SaveAll();
        } else {
            Log("World: world %d starts empty", want);
        }
    }
    gWorld.dirty = false;
    StarterKit();
}

void WorldSavedToSlot(int slot) {
    if (slot < 1 || slot > 8 || gWorldId < 0)
        return;
    gWorldId = slot;
    SaveAll();
    Log("World: saved with GTA save slot %d", slot);
}

void WorldSlotDeleted(int slot) {
    if (slot < 1 || slot > 8)
        return;
    DeleteFileA(WorldPath(slot).c_str());
    if (gWorldId == slot)
        gWorldId = 0; // the world being played lives on as an unsaved one
}

// ================================================================ screens
void OpenScreen(int screen, const Int3& pos) {
    gGame.screen = screen;
    gGame.openPos = pos;
    gGame.cursorX = RsGlobal.maximumWidth * 0.5f;
    gGame.cursorY = RsGlobal.maximumHeight * 0.5f;
    if (screen == SCREEN_FURNACE && !gWorld.furnaces.count(pos))
        gWorld.furnaces[pos] = FurnaceState();
    if (screen == SCREEN_CHEST) {
        if (!gWorld.chests.count(pos))
            gWorld.chests[pos] = ChestState();
        CVector c(pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f);
        PlaySfx(SND_CHEST_OPEN, &c);
    }
}

void DropStackAtPlayer(const ItemStack& s, bool thrown) {
    CPlayerPed* ped = FindPlayerPed();
    if (!ped || s.Empty())
        return;
    CVector dir = gGame.lookDir;
    CVector p = ped->GetPosition();
    CVector pos(p.x + dir.x * 0.6f, p.y + dir.y * 0.6f, p.z + 0.4f);
    CVector vel = thrown ? CVector(dir.x * 4.0f, dir.y * 4.0f, 2.5f) : CVector(0, 0, 1.0f);
    SpawnDrop(pos, s, vel, 1.5f);
}

void CloseScreen() {
    auto give = [](ItemStack& s) {
        if (s.Empty())
            return;
        int left = gInv.Add(s);
        if (left > 0) {
            ItemStack rest = s;
            rest.count = (uint8_t)left;
            DropStackAtPlayer(rest, false);
        }
        s.Clear();
    };
    for (auto& s : gInv.craft)
        give(s);
    for (auto& s : gInv.craft3)
        give(s);
    give(gInv.cursor);
    if (gGame.screen == SCREEN_CHEST) {
        CVector c(gGame.openPos.x + 0.5f, gGame.openPos.y + 0.5f, gGame.openPos.z + 0.5f);
        PlaySfx(SND_CHEST_CLOSE, &c);
    }
    gGame.screen = SCREEN_NONE;
    gWorld.dirty = true;
}

// ================================================================ main hooks
void GameInit() {
    if (gInitDone)
        return;
    gInitDone = true;
    gGame.enabled = gConfig.startEnabled;
    gGame.gameMode = gConfig.startGameMode == 1 ? MODE_CREATIVE : MODE_SURVIVAL;
    gGame.steve = gConfig.startAsSteve;
    LoadGtaModelNames();
    InstallCombatHooks();
    InstallMovementHooks();
    ActivateWorld();
}

void GameOnNewSession() {
    CollisionForgetAll();
    gDrops.clear();
    gPrimedTnt.clear();
    gParticles.clear();
    ClearProjectiles();
    MobsClear();
    PedSkinsForget();
    FishingClear();
    BlocksClear();
    XpClear();
    ClearEffects();
    gBrokenVehicles.clear();
    gGame.air = kMaxAir;
    gGame.directDamage = 0.0f;
    gGame.screen = SCREEN_NONE;
    gGame.flying = gGame.gliding = gGame.jumping = false;
    gGame.ridingMob = 0;
    gGame.crossbowSlot = -1;
    gGame.hidPlayer = false;
    gHealthSeen = -1.0f;
    gBrokenObjects.clear();
    gHudFlagsTouched = false;
    gProofsSet = false; // a fresh player ped has default flags
    SetLoopSfx(SND_ELYTRA_FLYING, false);
    Log("New GTA session: collision objects will be recreated");
    if (gInitDone)
        ActivateWorld(); // the save game that is being loaded brings its own world
}

void GameShutdown() {
    // nothing is written here: the world was saved with the GTA save game, or it was not saved at all
    ShutdownSfx();
}

void GameProcess() {
    float dt = FrameDelta();
    PollKeys();
    gGame.messageTimer = std::max(0.0f, gGame.messageTimer - dt);
    gGame.selectedNameTimer = std::max(0.0f, gGame.selectedNameTimer - dt);
    gBreakCooldown = std::max(0.0f, gBreakCooldown - dt);
    gPlaceCooldown = std::max(0.0f, gPlaceCooldown - dt);
    gHitSoundTimer = std::max(0.0f, gHitSoundTimer - dt);
    gGame.attackTimer += dt;

    CPlayerPed* ped = FindPlayerPed();
    if (ped)
        SetCreativeProofs(ped, gGame.enabled && gGame.gameMode == MODE_CREATIVE);

    // the world keeps running even when the Minecraft HUD is switched off
    TickFurnaces(dt);
    UpdateDrops(dt, ped);
    UpdateParticles(dt);
    UpdateTnt(dt);
    UpdateProjectiles(dt, ped);
    UpdatePedLoot();
    PollExplosions(dt);
    CollisionUpdate();
    BlocksUpdate(dt);
    BuildingsUpdate(dt);
    CarveUpdate();
    UpdateBrokenVehicles();
    XpUpdate(dt, ped);
    MobsUpdate(dt, ped);
    FishingUpdate(dt, ped);
    UpdateLaunchedPeds(dt);
    PedSkinsUpdate(dt, ped);

    ++gProcessTicks;

    if (gGame.screen == SCREEN_NONE) {
        if (KeyPressed(gConfig.keyToggleMode)) {
            gGame.enabled = !gGame.enabled;
            ShowMessage(gGame.enabled ? "Minecraft modu: A\xC3\x87IK" : "Minecraft modu: KAPALI");
            if (!gGame.enabled)
                SetGtaHud(false);
        }
        if (gGame.enabled && KeyPressed(gConfig.keyGameMode)) {
            gGame.gameMode = gGame.gameMode == MODE_SURVIVAL ? MODE_CREATIVE : MODE_SURVIVAL;
            ShowMessage(gGame.gameMode == MODE_CREATIVE ? "Oyun modu: Yarat\xC4\xB1" "c\xC4\xB1"
                                                        : "Oyun modu: Hayatta Kalma");
            gWorld.dirty = true;
        }
        if (KeyPressed(gConfig.keyRadar)) {
            gConfig.showRadar = !gConfig.showRadar;
            SaveConfigValue("Settings", "ShowRadar", gConfig.showRadar ? "1" : "0");
            ShowMessage(gConfig.showRadar ? "Harita (radar): A\xC3\x87IK" : "Harita (radar): KAPALI");
        }
        if (gGame.enabled && KeyPressed(gConfig.keySteve)) {
            gGame.steve = !gGame.steve;
            ShowMessage(gGame.steve ? "Karakter: Steve" : "Karakter: CJ");
            gWorld.dirty = true;
        }
        if (gGame.enabled && KeyPressed(gConfig.keyCamera)) {
            // Minecraft order: first person -> third person back -> third person front
            gGame.cameraMode = gGame.cameraMode == CAM_FIRST ? CAM_THIRD_BACK
                             : gGame.cameraMode == CAM_THIRD_BACK ? CAM_THIRD_FRONT
                                                                  : CAM_FIRST;
        }
    }

    bool blocked = FrontEndMenuManager.m_bMenuActive || CCutsceneMgr::ms_running || TheCamera.m_bWideScreenOn;
    gGame.inWorld = ped && !blocked;
    gGame.hudVisible = gGame.enabled && gGame.inWorld;
    gTargetVisual.show = false;
    if (!gGame.enabled || !ped) {
        if (gGame.screen != SCREEN_NONE)
            CloseScreen();
        if (ped && (gGame.flying || gGame.gliding || gGame.jumping))
            StopFlying(ped);
        gGame.sprinting = false;
        return;
    }
    SetGtaHud(!blocked);
    HandleDeath(ped);
    EffectsUpdate(dt, ped);
    UpdateHealthEffects(dt, ped);
    UpdateSurvival(dt, ped);
    UpdatePlayerAnimation(dt);
    if (blocked) {
        // a cutscene took over: never leave the player hanging in the air without collision
        if (gGame.flying || gGame.gliding || gGame.jumping)
            StopFlying(ped);
        return;
    }

    const bool alive = ped->m_fHealth > 0.0f;
    const bool inVehicle = alive && ped->bInVehicle && ped->m_pVehicle;
    const bool onFoot = alive && !ped->bInVehicle;
    gGame.inVehicle = inVehicle;
    UpdateMovement(dt, ped);
    UpdateCarBoost(dt, ped);

    // ------------------------------------------------ open screen
    if (gGame.screen != SCREEN_NONE) {
        float sx = CPad::NewMouseControllerState.x * 1.5f * gConfig.mouseSensitivity;
        float sy = CPad::NewMouseControllerState.y * 1.5f * gConfig.mouseSensitivity * (CMenuManager::bInvertMouseY ? -1.0f : 1.0f);
        gGame.cursorX = Clamp(gGame.cursorX + sx, 0.0f, (float)RsGlobal.maximumWidth - 1);
        gGame.cursorY = Clamp(gGame.cursorY + sy, 0.0f, (float)RsGlobal.maximumHeight - 1);
        GuiProcessInput();
        if (KeyPressed(gConfig.keyInventory) || KeyPressed(VK_ESCAPE) || !alive)
            CloseScreen();
        BlockAllPadInput();
        return;
    }

    // ------------------------------------------------ hotbar (also while driving)
    int prevSel = gInv.selected;
    uint16_t prevId = gInv.Held().id;
    if (alive) {
        for (int i = 0; i < 9; ++i)
            if (KeyPressed('1' + i))
                gInv.selected = i;
        if (CPad::NewMouseControllerState.wheelUp)
            gInv.selected = (gInv.selected + 8) % 9;
        if (CPad::NewMouseControllerState.wheelDown)
            gInv.selected = (gInv.selected + 1) % 9;
        // the wheel belongs to the hotbar, not to the radio / weapons
        CPad::NewMouseControllerState.wheelUp = 0;
        CPad::NewMouseControllerState.wheelDown = 0;
    }
    if (gInv.selected != prevSel) {
        gGame.selectedNameTimer = 2.0f;
        if (gInv.Held().id != prevId)
            gGame.attackTimer = 0.0f; // Minecraft resets the attack cooldown when the item changes
    }

    if (!alive) {
        gMiningProgress = 0;
        gGame.bowDraw = gGame.crossbowCharge = gGame.tridentCharge = -1.0f;
        gGame.eatTimer = 0.0f;
        gGame.spyglass = false;
        return;
    }
    if (inVehicle) {
        VehicleUse(dt, ped);
        return;
    }
    BlockActionInput();
    LateMovementInput(ped);

    if (KeyPressed(gConfig.keyInventory)) {
        OpenScreen(gGame.gameMode == MODE_CREATIVE ? SCREEN_CREATIVE : SCREEN_INVENTORY);
        BlockAllPadInput();
        return;
    }
    if (KeyPressed(gConfig.keySwap) && !NearVehicle(ped, 5.0f)) {
        std::swap(gInv.slots[gInv.selected], gInv.offhand);
        PlaySfx(SND_EQUIP_GENERIC, nullptr, 0.6f);
        gGame.handHeight = 0.0f;
        gGame.offHeight = 0.0f;
    }
    if (KeyPressed(gConfig.keyDrop) && !gInv.Held().Empty()) {
        ItemStack d = gInv.Held();
        if (!KeyDown(VK_CONTROL))
            d.count = 1;
        gInv.Held().count -= d.count;
        if (gInv.Held().count == 0)
            gInv.Held().Clear();
        DropStackAtPlayer(d, true);
        StartSwing();
    }

    // ------------------------------------------------ look at / fight / mine / place
    UpdateTarget(ped);
    if (gTarget.valid && gTarget.vehicleRef < 0 && (gTarget.voxel || gTarget.virtualBlock != ID_AIR)) {
        gTargetVisual.show = true;
        gTargetVisual.pos = gTarget.pos;
    }

    // creative pick block (middle mouse)
    if (gGame.gameMode == MODE_CREATIVE && CPad::NewMouseControllerState.mmb && !CPad::OldMouseControllerState.mmb &&
        gTarget.valid) {
        int b = gTarget.voxel ? gWorld.GetBlock(gTarget.pos.x, gTarget.pos.y, gTarget.pos.z) : gTarget.virtualBlock;
        if (b != ID_AIR) {
            gInv.Held().id = (uint16_t)b;
            gInv.Held().count = 1;
            gInv.Held().damage = 0;
        }
    }

    ItemStack& held = gInv.Held();
    bool targetIsContainer = gTarget.valid && gTarget.voxel &&
                             IsContainer(gWorld.GetBlock(gTarget.pos.x, gTarget.pos.y, gTarget.pos.z));

    // the off hand gets the right click when the main hand has no use for it
    const bool useOff = !HasRightClickUse(gInv.Held()) && !gInv.offhand.Empty();
    if (useOff) {
        std::swap(gInv.slots[gInv.selected], gInv.offhand);
        gGame.offhandActive = true;
    }
    struct SwapBack {
        bool on;
        ~SwapBack() {
            if (on) {
                std::swap(gInv.slots[gInv.selected], gInv.offhand);
                gGame.offhandActive = false;
            }
        }
    } swapBack{ useOff };
    ItemStack& useItem = gInv.Held();

    // bow, crossbow, trident, spyglass; then food
    const bool rmb = MouseRight() && !targetIsContainer;
    const bool charging = UpdateChargedItems(dt, ped, rmb, MouseRightPressed() && !targetIsContainer);
    const bool eating = !charging && UpdateEating(dt, ped, rmb);

    // right click
    if (MouseRightPressed() && !charging) {
        bool used = false;
        if (targetIsContainer && !ped->bIsDucking) {
            const Int3& p = gTarget.pos;
            int b = gWorld.GetBlock(p.x, p.y, p.z);
            if (b == ID_CRAFTING_TABLE)
                OpenScreen(SCREEN_CRAFTING, p);
            else if (b == ID_CHEST || b == ID_BARREL)
                OpenScreen(SCREEN_CHEST, p);
            else
                OpenScreen(SCREEN_FURNACE, p);
            used = true;
        }
        const float targetDist = gTarget.valid ? (gTarget.point - gGame.rayOrigin).Magnitude() : 1e9f;
        const float reach = (gGame.eyePos - gGame.rayOrigin).Magnitude() + 4.0f;
        if (!used && !eating) {
            // a villager under the crosshair: trade
            PedHit ph = RaycastPeds(gGame.rayOrigin, gGame.lookDir, reach, ped, false);
            if (ph.ped && (ph.point - gGame.eyePos).Magnitude() <= 4.0f && ph.dist <= targetDist)
                used = VillagerInteract(ph.ped);
        }
        if (!used) {
            // an animal under the crosshair: shear, milk, feed, saddle or ride it
            MobHit mh = MobsRaycast(gGame.rayOrigin, gGame.lookDir, reach);
            if (mh.index >= 0 && (mh.point - gGame.eyePos).Magnitude() <= 4.0f && mh.dist <= targetDist)
                used = MobInteract(mh.index, ped);
        }
        if (!used && !eating)
            used = UseWorldItem(ped);
        if (!used && !eating)
            used = UseSpecialItem(ped, gTarget.valid, gTarget.voxel, gTarget.pos, gTarget.point, gTarget.normal);
        if (gGame.screen != SCREEN_NONE) {
            BlockAllPadInput();
            return;
        }
        if (!used && !eating)
            PlaceHeldBlock(ped);
    } else if (MouseRight() && gPlaceCooldown <= 0.0f && !eating && !charging && !targetIsContainer &&
               IsBlockItem(useItem.id)) {
        PlaceHeldBlock(ped);
    }
    gGame.usingOffhand = useOff && (eating || charging);
    if (useOff) {
        // back to the main hand for fighting and mining
        swapBack.on = false;
        std::swap(gInv.slots[gInv.selected], gInv.offhand);
        gGame.offhandActive = false;
    }

    // left click: attack, otherwise mine
    if (MouseLeftPressed()) {
        gMeleeHeld = TryMeleeAttack(ped);
        if (!gMeleeHeld) {
            StartSwing();
            if (!gTarget.valid)
                gGame.attackTimer = 0.0f; // swinging at the air also resets the cooldown
        }
    }
    if (!MouseLeft())
        gMeleeHeld = false;
    const bool wand = !held.Empty() && Item(held.id).special == SP_WAND;
    if (MouseLeft() && !gMeleeHeld && gTarget.valid && !wand) {
        bool voxel = gTarget.voxel;
        int block = voxel ? gWorld.GetBlock(gTarget.pos.x, gTarget.pos.y, gTarget.pos.z) : gTarget.virtualBlock;
        const Int3 key = gTarget.vehicleRef >= 0 ? Int3{ gTarget.vehicleRef, -77777, 0 }
                       : gTarget.objectRef >= 0  ? Int3{ gTarget.objectRef, -88888, 0 }
                                                 : gTarget.pos;
        if (block == ID_AIR) {
            gMiningProgress = 0;
        } else if (gGame.gameMode == MODE_CREATIVE) {
            if (gBreakCooldown <= 0.0f && !(held.id && Item(held.id).tool == TOOL_SWORD)) {
                if (voxel)
                    BreakVoxel(gTarget.pos, false);
                else
                    BreakVirtual(gTarget, ped);
                gBreakCooldown = 0.25f;
                StartSwing();
            }
        } else {
            if (gMiningPos != key || gMiningVoxel != voxel) {
                gMiningPos = key;
                gMiningVoxel = voxel;
                gMiningProgress = 0;
            }
            const float hardness = voxel ? -2.0f : gTarget.mat.hardness;
            float secs = BreakSecondsFor(block, hardness, held, nullptr);
            if (gBreakCooldown <= 0.0f) {
                gMiningProgress += secs <= 0.0f ? 1.0f : dt / secs;
                if (gHitSoundTimer <= 0.0f) {
                    gHitSoundTimer = 0.25f;
                    PlaySfx(HitSound(block), &gTarget.point, 0.25f, 0.5f);
                    StartSwing();
                    if (!voxel)
                        HitParticles(gTarget.point, gTarget.normal, block);
                }
                if (gMiningProgress >= 1.0f) {
                    if (voxel)
                        BreakVoxel(gTarget.pos, true);
                    else
                        BreakVirtual(gTarget, ped);
                    if (Block(block).hardness > 0.0f)
                        DamageHeldItem(Item(held.id).tool == TOOL_SWORD ? 2 : 1);
                    gGame.exhaustion += 0.005f;
                    gMiningProgress = 0;
                    gBreakCooldown = 0.25f;
                    gWorld.dirty = true;
                }
            }
        }
    } else {
        gMiningProgress = 0;
    }
    gTargetVisual.progress = gGame.gameMode == MODE_SURVIVAL ? gMiningProgress : 0.0f;
}

// ---------------------------------------------------------------- camera
namespace {
// GTA's field of view is horizontal and narrow; Minecraft's is vertical (70 by default).
// The relation between CDraw's value and what ends up on screen depends on the aspect ratio and on
// widescreen patches, so it is measured from the camera the last frame was drawn with.
void ApplyFov(bool active) {
    const float cur = CDraw::ms_fFOV;
    if (Scene.m_pCamera && gFovInEffect > 1.0f && gFovInEffect < 179.0f) {
        float k = Scene.m_pCamera->viewWindow.y / std::tan(Rad(gFovInEffect) * 0.5f);
        if (k > 0.2f && k < 2.5f) {
            gFovK = gFovKMeasured ? gFovK + (k - gFovK) * 0.5f : k;
            gFovKMeasured = true;
        }
    }
    if (gFovWritten == 0.0f || cur != gFovWritten)
        gGameFov = cur; // the game computed a new value this frame
    if (active && gConfig.fov > 0.0f && gGameFov > 1.0f && gGameFov < 170.0f) {
        float want = std::tan(Rad(gConfig.fov * gGame.fovMod * (gGame.spyglass ? 0.1f : 1.0f)) * 0.5f);
        float scale = want / (gFovK * std::tan(Rad(70.0f) * 0.5f));
        float fov = 2.0f * std::atan(std::tan(Rad(gGameFov) * 0.5f) * scale) * (180.0f / kPi);
        fov = Clamp(fov, 20.0f, 160.0f);
        CDraw::ms_fFOV = fov;
        gFovWritten = fov;
        TheCamera.CalculateDerivedValues(false, false); // frustum planes for the wider view
    }
    gFovInEffect = CDraw::ms_fFOV;
}

void SetCamera(const CVector& pos, const CVector& forward) {
    CVector f = Normalized(forward);
    CVector right = CVector::Cross(f, CVector(0, 0, 1));
    float m = right.Magnitude();
    right = m > 1e-3f ? right * (1.0f / m) : CVector(1, 0, 0);
    CVector up = CVector::Cross(right, f);
    CMatrix mat = TheCamera.m_mCameraMatrix;
    // keep the handedness the game uses for its camera matrix (its "right" is the left vector)
    CVector c = CVector::Cross(mat.up, mat.at);
    float sign = (c.x * mat.right.x + c.y * mat.right.y + c.z * mat.right.z) < 0.0f ? -1.0f : 1.0f;
    mat.right = right * sign;
    mat.up = f;
    mat.at = up;
    mat.pos = pos;
    TheCamera.m_mCameraMatrix = mat;
    TheCamera.SetMatrix(mat);
    TheCamera.CopyCameraMatrixToRWCam(true);
    TheCamera.CalculateDerivedValues(false, false);
}

// how far a camera can go from `from` along `dir`
float CameraRoom(const CVector& from, const CVector& dir, float want, bool vehicles) {
    float dist = want;
    CColPoint cp;
    CEntity* e = nullptr;
    if (CWorld::ProcessLineOfSight(from, from + dir * want, cp, e, true, vehicles, false, true, false, false, true, false))
        dist = std::max(0.3f, (cp.m_vecPoint - from).Magnitude() - 0.2f);
    VoxelHit vh = RaycastVoxels(from, dir, dist);
    if (vh.hit)
        dist = std::max(0.3f, vh.dist - 0.2f);
    return dist;
}
} // namespace

// After CGame::Process: field of view, camera modes and player visibility.
void GameAfterProcess() {
    CPlayerPed* ped = FindPlayerPed();
    bool active = gGame.enabled && gGame.inWorld && ped && ped->m_fHealth > 0.0f;
    ApplyFov(active);
    if (LightningFlashActive())
        CWeather::LightningFlash = true;
    if (!ped)
        return;
    PedSkinsAfterProcess(ped);
    MovementAfterProcess(ped);

    CCam& cam = TheCamera.m_aCams[TheCamera.m_nActiveCam];
    CVector front = Normalized(cam.m_vecFront);
    CVector pp = ped->GetPosition();
    const bool inVehicle = ped->bInVehicle && ped->m_pVehicle;
    CVector eye = pp + CVector(0, 0, ped->bIsDucking ? 0.27f : 0.62f);
    if (inVehicle)
        eye = pp + ped->m_pVehicle->m_matrix->at * 0.6f;
    else if (gGame.swimming)
        eye = pp + CVector(front.x, front.y, 0.0f) * 0.45f + CVector(0, 0, 0.25f);
    gGame.eyePos = eye;
    gGame.lookDir = front;
    gGame.rayOrigin = cam.m_vecSource;

    bool hide = false;
    bool nearChanged = false;
    if (active) {
        bool override = false;
        CVector pos, look = front;
        if (gGame.cameraMode == CAM_FIRST) {
            pos = eye;
            override = true;
            hide = true;
            if (inVehicle) {
                // GTA's chase camera looks down at the car; level it out for the driver's eyes
                look = Normalized(front + CVector(0, 0, 0.16f));
                // aiming, throwing and breaking go where the crosshair of this view is
                gGame.lookDir = look;
                gGame.rayOrigin = eye;
            } else {
                gGame.rayOrigin = eye;
                if (gConfig.viewBobbing && gGame.bob > 0.001f) {
                    // GameRenderer.bobView
                    CVector right = CVector::Cross(front, CVector(0, 0, 1));
                    float rm = right.Magnitude();
                    if (rm > 1e-3f) {
                        right = right * (1.0f / rm);
                        CVector up = CVector::Cross(right, front);
                        float f1 = -gGame.walkDist * kPi, f2 = gGame.bob;
                        pos = pos + up * std::fabs(std::cos(f1) * f2) - right * (std::sin(f1) * f2 * 0.5f);
                        look = Normalized(front - up * std::tan(Rad(std::fabs(std::cos(f1 - 0.2f) * f2) * 5.0f)));
                    }
                }
            }
        } else if (gGame.cameraMode == CAM_THIRD_FRONT) {
            CVector out;
            float want;
            if (inVehicle) {
                // mirror GTA's own chase camera to the other side of the vehicle
                CVector off = cam.m_vecSource - eye;
                out = Normalized(CVector(-off.x, -off.y, std::max(off.z, 0.5f)));
                want = Clamp(off.Magnitude(), 4.0f, 45.0f);
            } else {
                out = front;
                want = 4.0f;
                gGame.rayOrigin = eye;
            }
            pos = eye + out * CameraRoom(eye, out, want, !inVehicle);
            look = out * -1.0f;
            override = true;
        }
        if (override) {
            SetCamera(pos, look);
            if (gGame.cameraMode == CAM_FIRST && Scene.m_pCamera) {
                float cur = RwCameraGetNearClipPlane(Scene.m_pCamera);
                if (cur > kFirstPersonNear + 0.01f)
                    gSavedNearClip = cur; // the game's own value
                RwCameraSetNearClipPlane(Scene.m_pCamera, kFirstPersonNear);
                nearChanged = true;
            }
        }
        if (gGame.steve)
            hide = true;
    }
    if (!nearChanged && gNearClipOverridden && Scene.m_pCamera) {
        // left first person: give the game its near plane back if it did not reset it itself
        if (RwCameraGetNearClipPlane(Scene.m_pCamera) <= kFirstPersonNear + 0.01f && gSavedNearClip > 0.0f)
            RwCameraSetNearClipPlane(Scene.m_pCamera, gSavedNearClip);
    }
    gNearClipOverridden = nearChanged;
    if (hide) {
        SetPedDrawn(ped, false); // every frame: the model is rebuilt when clothes change
        gGame.hidPlayer = true;
    } else if (gGame.hidPlayer) {
        SetPedDrawn(ped, true);
        gGame.hidPlayer = false;
    }
}

} // namespace mc
