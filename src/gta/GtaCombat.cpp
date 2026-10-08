#include "GtaCombat.h"

#include "CCamera.h"
#include "CColPoint.h"
#include "CFireManager.h"
#include "CObject.h"
#include "CPed.h"
#include "CPlayerPed.h"
#include "CPools.h"
#include "CTimer.h"
#include "CVehicle.h"
#include "CWeapon.h"
#include "CWorld.h"
#include "common.h"
#include "extensions/ScriptCommands.h"
#include "safetyhook.hpp"

#include "Collision.h"
#include "Combat.h"
#include "Config.h"
#include "Draw3D.h"
#include "Game.h"
#include "Items.h"
#include "McModel.h"
#include "Mobs.h"
#include "Movement.h"
#include "Particles.h"
#include "PedSkins.h"
#include "Render3D.h"
#include "Sound.h"
#include "Terrain.h"
#include "Textures.h"
#include "Villagers.h"

// The GTA side of fighting: who and what a blow or a shot meets on the GTA map, hurting GTA's people and vehicles, the
// magic stick, the rocket strapped to a car, the loot of the dead, drawing what flies, and the engine hooks. The blows,
// the shots and the items themselves are the core's business (src/core/Combat.h).

namespace mc {

namespace {
struct HurtPed {
    int handle;
    float time;
};
std::vector<HurtPed> gHurt;
std::vector<int> gLooted; // the dead who left their loot already (ped handles)

float gCarBoost = 0.0f;
int gCarBoostVeh = -1;
float gHeliShotTimer = 0.0f;

CVector Norm(const CVector& v) {
    float m = v.Magnitude();
    return m > 1e-5f ? v * (1.0f / m) : CVector(0, 0, 1);
}

void TrackPed(CPed* p) {
    int h = CPools::GetPedRef(p);
    for (auto& hp : gHurt)
        if (hp.handle == h) {
            hp.time = 0.0f;
            return;
        }
    gHurt.push_back({ h, 0.0f });
}

void HurtPedBy(CPed* victim, CEntity* attacker, float halfHearts, eWeaponType weapon) {
    if (!victim || victim == attacker || victim->m_fHealth <= 0.0f)
        return;
    int dmg = std::max(1, (int)std::round(halfHearts * 5.0f)); // 20 half hearts ~ 100 GTA health
    if (victim->bInVehicle)
        weapon = WEAPONTYPE_PISTOL; // GTA lets fists and swords pass people in vehicles, shots it does not
    CWeapon::GenerateDamageEvent(victim, attacker, weapon, dmg, (ePedPieceTypes)3, 0);
    if (attacker == FindPlayerPed())
        TrackPed(victim);
}

// line test that skips some entities
bool LineHit(const CVector& a, const CVector& b, CColPoint& cp, CEntity*& ent, bool peds, CEntity* skip1, CEntity* skip2,
             CEntity* ignoreVeh = nullptr) {
    CVector start = a;
    CVector dir = Norm(b - a);
    CEntity* old = CWorld::pIgnoreEntity;
    CWorld::pIgnoreEntity = ignoreVeh;
    bool result = false;
    for (int i = 0; i < 4; ++i) {
        ent = nullptr;
        if (!CWorld::ProcessLineOfSight(start, b, cp, ent, true, true, peds, true, false, false, false, false))
            break;
        if (ent && (ent == skip1 || ent == skip2 || IsCollisionObject(ent) || TerrainIgnoreHit(cp.m_vecPoint, ent))) {
            start = cp.m_vecPoint + dir * 0.15f;
            continue;
        }
        result = true;
        break;
    }
    CWorld::pIgnoreEntity = old;
    return result;
}

CPed* PedFromRef(int ref) { return ref >= 0 && CPools::ms_pPedPool ? CPools::ms_pPedPool->GetAtRef(ref) : nullptr; }
CVehicle* VehFromRef(int ref) { return ref >= 0 ? CPools::GetVehicle(ref) : nullptr; }

void Magic(const CVector& at, int count, float speed) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        CVector d = Norm(CVector(Rand01() * 2 - 1, Rand01() * 2 - 1, Rand01() * 2 - 1));
        p.pos = at + d * 0.2f;
        p.vel = d * (speed * (0.4f + Rand01() * 0.6f));
        p.maxLife = p.life = 0.5f + Rand01() * 0.5f;
        p.tile = TILE_P_ENCHANTED_HIT;
        p.size = 0.1f;
        p.gravity = 1.0f;
        p.glow = true;
        SpawnParticle(p);
    }
}

// ---------------------------------------------------------------- the magic stick
bool WandAllowed() {
    if (gGame.gameMode == MODE_CREATIVE)
        return true;
    ShowMessage("Büyülü Sopa sadece Yaratıcı modda çalışır");
    return false;
}

// left click: whatever the stick points at is hurled away
void WandStrike(CPlayerPed* ped) {
    StartSwing();
    if (!WandAllowed())
        return;
    const CVector origin = gGame.rayOrigin, dir = gGame.lookDir;
    const float reach = 70.0f;
    MobHit mh = MobsRaycast(origin, dir, reach);
    PedHit ph = RaycastPeds(origin, dir, reach, ped, false);
    CColPoint cp;
    CEntity* ent = nullptr;
    bool hit = LineHit(origin, origin + dir * reach, cp, ent, true, ped, nullptr);
    float gtaDist = hit ? (cp.m_vecPoint - origin).Magnitude() : 1e9f;
    VoxelHit vh = RaycastVoxels(origin, dir, reach);
    float wall = vh.hit ? vh.dist : 1e9f;
    const CVector vel = dir * 42.0f + CVector(0, 0, 15.0f);
    CVector at;
    bool did = false;
    const float mobD = mh.index >= 0 ? mh.dist : 1e9f, pedD = ph.ped ? ph.dist : 1e9f;
    if (mobD < gtaDist && mobD < wall && mobD <= pedD) {
        MobPush(mh.index, vel * 0.6f);
        at = mh.point;
        did = true;
    } else if (pedD < gtaDist && pedD < wall) {
        LaunchPed(ph.ped, vel);
        at = ph.point;
        did = true;
    } else if (hit && gtaDist < wall && ent &&
               (ent->m_nType == ENTITY_TYPE_PED || ent->m_nType == ENTITY_TYPE_VEHICLE || ent->m_nType == ENTITY_TYPE_OBJECT)) {
        LaunchEntity(ent, vel);
        at = cp.m_vecPoint;
        did = true;
    }
    if (did) {
        Magic(at, 24, 5.0f);
        PlaySfx(SND_KNOCKBACK, &at, 1.0f, 0.9f + Rand01() * 0.2f);
    }
}

// right click: a shock wave throws everything around the player into the air
void WandShockwave(CPlayerPed* ped) {
    StartSwing();
    if (!WandAllowed())
        return;
    const CVector centre = ped->GetPosition() - CVector(0, 0, 0.6f);
    Gust(centre, 18.0f, 20.0f, 14.0f, false);
    for (int i = 0; i < 48; ++i) {
        float a = i * (6.2831853f / 48.0f);
        Particle p;
        p.pos = centre + CVector(std::cos(a), std::sin(a), 0.2f);
        p.vel = CVector(std::cos(a) * 14.0f, std::sin(a) * 14.0f, 1.0f + Rand01() * 2.0f);
        p.maxLife = p.life = 0.7f + Rand01() * 0.4f;
        p.tile = TILE_P_ENCHANTED_HIT;
        p.size = 0.14f;
        p.gravity = 0.5f;
        p.glow = true;
        SpawnParticle(p);
    }
    PlaySfx(SND_WAND, nullptr, 1.0f);
}

bool SpawnGtaVehicle(int model, const CVector& at, float headingDeg) {
    using namespace plugin;
    Command<Commands::REQUEST_MODEL>(model);
    Command<Commands::LOAD_ALL_MODELS_NOW>();
    if (!Command<Commands::HAS_MODEL_LOADED>(model))
        return false;
    int handle = -1;
    Command<Commands::CREATE_CAR>(model, at.x, at.y, at.z, &handle);
    if (handle < 0) {
        Command<Commands::MARK_MODEL_AS_NO_LONGER_NEEDED>(model);
        return false;
    }
    Command<Commands::SET_CAR_HEADING>(handle, headingDeg);
    Command<Commands::MARK_CAR_AS_NO_LONGER_NEEDED>(handle);
    Command<Commands::MARK_MODEL_AS_NO_LONGER_NEEDED>(model);
    return true;
}
} // namespace

// ================================================================ what the core asks (Host)
float GtaAimDistance(const CVector& origin, const CVector& dir, float reach, float nothing) {
    CPlayerPed* ped = FindPlayerPed();
    float dist = nothing;
    if (!ped)
        return dist;
    CColPoint cp;
    CEntity* ent = nullptr;
    if (LineHit(origin, origin + dir * reach, cp, ent, true, ped, nullptr, ped->bInVehicle ? ped->m_pVehicle : nullptr))
        dist = (cp.m_vecPoint - origin).Magnitude();
    const PedHit ph = RaycastPeds(origin, dir, reach, ped, false, true);
    if (ph.ped)
        dist = std::min(dist, ph.dist);
    return dist;
}

HostHit GtaBlowTrace(const CVector& origin, const CVector& dir, float reach) {
    HostHit out;
    CPlayerPed* ped = FindPlayerPed();
    if (!ped)
        return out;
    CColPoint cp;
    CEntity* ent = nullptr;
    // (at the wheel the blow goes through the player's own vehicle)
    bool hit = LineHit(origin, origin + dir * reach, cp, ent, true, ped, nullptr, ped->bInVehicle ? ped->m_pVehicle : nullptr);
    float gtaDist = hit ? (cp.m_vecPoint - origin).Magnitude() : 1e9f;
    PedHit ph = RaycastPeds(origin, dir, reach, ped, false, true);
    float pedD = 1e9f;
    CPed* victim = nullptr;
    CVector point;
    if (hit && ent && ent->m_nType == ENTITY_TYPE_PED) {
        victim = static_cast<CPed*>(ent);
        pedD = gtaDist;
        point = cp.m_vecPoint;
    }
    if (ph.ped && ph.dist < pedD) {
        victim = ph.ped;
        pedD = ph.dist;
        point = ph.point;
    }
    const bool seatedVictim = victim && victim->bInVehicle && hit && ent == victim->m_pVehicle && pedD <= gtaDist + 2.5f;
    if (pedD > gtaDist && !(hit && ent && ent->m_nType == ENTITY_TYPE_PED) && !seatedVictim)
        victim = nullptr; // a wall or a car is in the way
    out.hit = hit;
    if (hit) {
        out.dist = gtaDist;
        out.point = cp.m_vecPoint;
    }
    if (victim) {
        out.being = CPools::GetPedRef(victim);
        out.beingDist = pedD;
        out.beingPoint = point;
    }
    return out;
}

HostHit GtaShotTrace(const CVector& from, const CVector& dir, float len, const ShotOwner& by) {
    HostHit out;
    CPlayerPed* ped = FindPlayerPed();
    CPed* shooter = by.hostile ? PedFromRef(by.shooter) : ped;
    CColPoint cp;
    CEntity* ent = nullptr;
    const bool hit = LineHit(from, from + dir * len, cp, ent, true, shooter, by.hostile ? nullptr : ped, VehFromRef(by.vehicle));
    const float hitDist = hit ? (cp.m_vecPoint - from).Magnitude() : 1e9f;
    // (people in a car sit behind its body: a shot that hits the car reaches them)
    PedHit ph = RaycastPeds(from, dir, len + 2.5f, shooter, by.hostile, true);
    const bool vehicleHit = hit && ent && ent->m_nType == ENTITY_TYPE_VEHICLE;
    if (ph.ped && !(ph.dist < std::min(hitDist, len) ||
                    (vehicleHit && ph.ped->bInVehicle && ph.ped->m_pVehicle == ent && ph.dist < hitDist + 2.5f)))
        ph.ped = nullptr;
    out.hit = hit;
    if (hit) {
        out.dist = hitDist;
        out.point = cp.m_vecPoint;
    }
    if (vehicleHit)
        out.vehicle = CPools::GetVehicleRef(static_cast<CVehicle*>(ent));
    if (ph.ped) {
        out.being = CPools::GetPedRef(ph.ped);
        out.beingDist = ph.dist;
        out.beingPoint = ph.point;
    } else if (hit && ent && ent->m_nType == ENTITY_TYPE_PED) {
        out.being = CPools::GetPedRef(static_cast<CPed*>(ent));
        out.beingDist = hitDist;
        out.beingPoint = cp.m_vecPoint;
    }
    return out;
}

void GtaHurtBeing(int being, float halfHearts, int how, const ShotOwner* by) {
    CEntity* attacker = FindPlayerPed();
    if (by && by->hostile)
        attacker = PedFromRef(by->shooter);
    HurtPedBy(PedFromRef(being), attacker, halfHearts,
              how == HURT_SWORD ? WEAPONTYPE_KATANA : how == HURT_FIST ? WEAPONTYPE_UNARMED : WEAPONTYPE_PISTOL);
}

void GtaPushBeing(int being, const CVector& velocity) {
    CPed* p = PedFromRef(being);
    if (p && !p->bInVehicle)
        p->m_vecMoveSpeed += velocity * (1.0f / 50.0f); // GTA keeps it per 1/50 s
}

void GtaHurtVehicle(int vehicle, float halfHearts) {
    CVehicle* v = VehFromRef(vehicle);
    if (v && v->m_fHealth > 0.0f)
        v->m_fHealth -= halfHearts * 4.0f;
}

// Host::Gust: GTA's people, vehicles and loose objects inside `radius` are thrown away from `centre`
void GtaGust(const CVector& centre, float radius, float side, float up, bool playerToo) {
    CPlayerPed* self = FindPlayerPed();
    auto velFor = [&](const CVector& pos, bool& inside) {
        CVector d = pos - centre;
        d.z = 0.0f;
        float dist = d.Magnitude();
        inside = (pos - centre).Magnitude() < radius;
        float f = 1.0f - Clamp((pos - centre).Magnitude() / radius, 0.0f, 1.0f);
        CVector dir = dist > 0.2f ? d * (1.0f / dist) : CVector(Rand01() - 0.5f, Rand01() - 0.5f, 0.0f);
        return dir * (side * (0.5f + f)) + CVector(0, 0, up * (0.5f + f));
    };
    bool inside = false;
    if (auto* pool = CPools::ms_pPedPool)
        for (int i = 0; i < pool->m_nSize; ++i) {
            CPed* p = pool->GetAt(i);
            if (!p || p == self || p->bInVehicle)
                continue;
            CVector v = velFor(p->GetPosition(), inside);
            if (inside)
                LaunchPed(p, v);
        }
    if (auto* pool = CPools::ms_pVehiclePool)
        for (int i = 0; i < pool->m_nSize; ++i) {
            CVehicle* v = pool->GetAt(i);
            if (!v || (self && self->bInVehicle && self->m_pVehicle == v))
                continue;
            CVector vel = velFor(v->GetPosition(), inside);
            if (inside)
                LaunchEntity(v, vel * 0.7f);
        }
    if (auto* pool = CPools::ms_pObjectPool)
        for (int i = 0; i < pool->m_nSize; ++i) {
            CObject* o = pool->GetAt(i);
            if (!o || IsCollisionObject(o))
                continue;
            CVector vel = velFor(o->GetPosition(), inside);
            if (inside)
                LaunchEntity(o, vel);
        }
    if (playerToo && self && !self->bInVehicle) {
        CVector v = velFor(self->GetPosition(), inside);
        if (inside)
            LaunchPlayer(self, v);
    }
}

void GtaIgnite(const CVector& at, float seconds, int spread) {
    gFireManager.StartFire(at, 1.0f, 0, FindPlayerPed(), (unsigned int)std::lround(seconds * 1000.0f), (signed char)spread, 0);
}

bool GtaPlaceVehicle(int kind, const CVector& at, float headingDeg) {
    return SpawnGtaVehicle(kind == HOST_BOAT ? 473 : 571, at, headingDeg); // Dinghy, Kart
}

void GtaBoostVehicle(float seconds) {
    CPlayerPed* ped = FindPlayerPed();
    if (!ped || !ped->bInVehicle || !ped->m_pVehicle)
        return;
    gCarBoost = seconds;
    gCarBoostVeh = CPools::GetVehicleRef(ped->m_pVehicle);
}

bool GtaItemAttack(int special) {
    CPlayerPed* ped = FindPlayerPed();
    if (special != SP_WAND || !ped)
        return false;
    WandStrike(ped);
    return true;
}

bool GtaItemUse(int special) {
    CPlayerPed* ped = FindPlayerPed();
    if (!ped)
        return false;
    if (special != SP_WAND)
        return false;
    WandShockwave(ped);
    return true;
}

// ================================================================ public
// the rocket strapped to the player's car pushes it on
void UpdateCarBoost(float dt, CPlayerPed* ped) {
    gHeliShotTimer = std::max(0.0f, gHeliShotTimer - dt);
    if (gCarBoost <= 0.0f)
        return;
    gCarBoost -= dt;
    CVehicle* v = VehFromRef(gCarBoostVeh);
    if (!v || !ped || !ped->bInVehicle || ped->m_pVehicle != v) {
        gCarBoost = 0.0f;
        return;
    }
    const CVector fwd = v->m_matrix->up;
    v->m_vecMoveSpeed += fwd * (22.0f * dt / 50.0f);
    CVector rear = v->GetPosition() - fwd * 2.4f;
    Particle sp;
    sp.pos = rear;
    sp.vel = fwd * -4.0f + CVector(Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f);
    sp.maxLife = sp.life = 0.5f;
    sp.tile = TILE_P_SPARK_0;
    sp.anim = 1;
    sp.size = 0.12f;
    sp.gravity = 0.0f;
    sp.glow = true;
    SpawnParticle(sp);
}

void RenderProjectiles(float light) {
    if (gProjectiles.empty())
        return;
    const CMatrix& cm = TheCamera.m_mCameraMatrix;
    const CVector camR = cm.right * -1.0f, camU = cm.at;
    for (auto& pr : gProjectiles) {
        if (pr.type == PJ_ARROW) {
            d3::SetRaster(gEntityTex.Raster());
            CVector dir = Norm(pr.vel);
            CVector side = Norm(CVector(-dir.y, dir.x, 0.0f));
            if (std::fabs(dir.z) > 0.99f)
                side = CVector(1, 0, 0);
            CVector up = Norm(CVector::Cross(side, dir));
            // ArrowRenderer: two crossed 16x5 strips, the feathers at u = 0 and the tip at u = 16
            CVector tail = pr.pos - dir * 0.675f, head = pr.pos + dir * 0.225f;
            const float w = 0.14f;
            const float u0 = (float)ENT_ARROW.x / ENT_TEX_W, u1 = (ENT_ARROW.x + 16.0f) / ENT_TEX_W;
            const float v0 = (float)ENT_ARROW.y / ENT_TEX_H, v1 = (ENT_ARROW.y + 5.0f) / ENT_TEX_H;
            RwUInt32 c = d3::Gray(light);
            d3::Quad(tail + side * w, head + side * w, head - side * w, tail - side * w, u0, v0, u1, v1, c);
            d3::Quad(tail + up * w, head + up * w, head - up * w, tail - up * w, u0, v0, u1, v1, c);
        } else if (pr.type == PJ_TRIDENT) {
            // the item sprite with its diagonal along the flight direction (handle behind)
            CVector dir = pr.returning ? Norm(pr.vel * -1.0f) : Norm(pr.vel);
            if (pr.stuck && !pr.returning)
                dir = Norm(pr.vel * -10.0f);
            CVector side = Norm(CVector::Cross(dir, CVector(0, 0, 1)));
            if (side.Magnitude() < 0.5f)
                side = CVector(1, 0, 0);
            // ThrownTridentRenderer: the 3D trident, spikes first
            Pose p;
            p.Y = dir * -1.0f;
            p.X = side;
            p.Z = CVector::Cross(p.X, p.Y);
            p.o = pr.pos;
            DrawTridentModel(p, light);
        } else {
            d3::SetRaster(gAtlasTex.Raster());
            uint16_t id = pr.type == PJ_SNOWBALL  ? ID_SNOWBALL
                        : pr.type == PJ_EGG       ? ID_EGG
                        : pr.type == PJ_PEARL     ? ID_ENDER_PEARL
                        : pr.type == PJ_FIREBALL  ? ID_FIRE_CHARGE
                        : pr.type == PJ_WIND      ? ID_WIND_CHARGE
                        : pr.type == PJ_XPBOTTLE  ? ID_EXPERIENCE_BOTTLE
                                                  : ID_FIREWORK_ROCKET;
            float size = pr.type == PJ_FIREBALL ? 0.4f : 0.15f;
            EmitItemSprite(pr.pos, camR, camU, size, Item(id).tile,
                           pr.type == PJ_FIREWORK || pr.type == PJ_FIREBALL || pr.type == PJ_ROCKET ? 1.0f : light);
        }
    }
    d3::Flush();
}

void RenderLightning() {
    if (gBolts.empty())
        return;
    const CVector cam = TheCamera.GetPosition();
    d3::SetRaster(gEntityTex.Raster());
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    const float u = (ENT_WHITE.x + 4.0f) / ENT_TEX_W, v = (ENT_WHITE.y + 4.0f) / ENT_TEX_H;
    for (auto& b : gBolts) {
        const CVector at = b.at;
        // LightningBoltRenderer: a jagged column that flickers
        if (((int)(b.age * 30.0f)) % 3 == 2)
            continue;
        uint32_t seed = b.seed;
        auto rnd = [&]() {
            seed = seed * 1664525u + 1013904223u;
            return ((seed >> 8) & 0xFFFF) / 65535.0f - 0.5f;
        };
        int alpha = (int)(Clamp(1.0f - b.age / 0.5f, 0.0f, 1.0f) * 200.0f);
        for (int branch = 0; branch < 3; ++branch) {
            CVector p = at + CVector(0, 0, branch == 0 ? 0.0f : 20.0f + branch * 15.0f);
            CVector top = at + CVector(rnd() * 20.0f, rnd() * 20.0f, 110.0f);
            const int segs = branch == 0 ? 16 : 6;
            CVector prev = branch == 0 ? at : p + CVector(rnd() * 6.0f, rnd() * 6.0f, 0.0f);
            for (int k = 1; k <= segs; ++k) {
                float t = (float)k / segs;
                CVector next = (branch == 0 ? at : prev) * (1.0f - t) + top * t;
                if (branch == 0)
                    next = at * (1.0f - t) + top * t;
                next += CVector(rnd() * 3.0f, rnd() * 3.0f, 0.0f);
                CVector mid = (next + prev) * 0.5f;
                CVector side = CVector::Cross(next - prev, cam - mid);
                float sm = side.Magnitude();
                if (sm > 1e-4f) {
                    side = side * ((branch == 0 ? 0.35f : 0.18f) / sm);
                    d3::Quad(prev - side, prev + side, next + side, next - side, u, v, u, v, d3::Argb(200, 210, 255, alpha));
                    d3::Quad(prev - side * 2.5f, prev + side * 2.5f, next + side * 2.5f, next - side * 2.5f, u, v, u, v,
                             d3::Argb(120, 140, 255, alpha / 3));
                }
                prev = next;
            }
        }
    }
    d3::Flush();
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}

// a dead villager or pillager (a policeman) leaves his loot, once
static void DropPedLoot(CPed* p) {
    const int handle = CPools::GetPedRef(p);
    if (std::find(gLooted.begin(), gLooted.end(), handle) != gLooted.end())
        return;
    gLooted.push_back(handle);
    DropVillagerLoot(p->GetPosition(), p->m_nPedType == PED_TYPE_COP);
}

static bool PedIsDead(CPed* p) {
    return p->m_fHealth <= 0.0f || p->m_ePedState == PEDSTATE_DIE || p->m_ePedState == PEDSTATE_DEAD;
}

void UpdatePedLoot() {
    // people the player hurt with a Minecraft weapon
    for (size_t i = 0; i < gHurt.size();) {
        HurtPed& h = gHurt[i];
        h.time += FrameDelta();
        CPed* p = CPools::GetPed(h.handle);
        bool remove = !p || h.time > 30.0f;
        if (p && PedIsDead(p)) {
            DropPedLoot(p);
            remove = true;
        }
        if (remove) {
            gHurt[i] = gHurt.back();
            gHurt.pop_back();
        } else {
            ++i;
        }
    }
    // people the player ran over with the vehicle he drives
    CPlayerPed* player = FindPlayerPed();
    if (auto* pool = player ? CPools::ms_pPedPool : nullptr)
        for (int i = 0; i < pool->m_nSize; ++i) {
            CPed* p = pool->GetAt(i);
            if (!p || p == player || !PedIsDead(p))
                continue;
            if (p->m_nLastWeaponDamage != WEAPONTYPE_RAMMEDBYCAR && p->m_nLastWeaponDamage != WEAPONTYPE_RUNOVERBYCAR)
                continue;
            CEntity* by = p->m_pLastEntityDamage;
            if (by == player || (by && by->m_nType == ENTITY_TYPE_VEHICLE && static_cast<CVehicle*>(by)->m_pDriver == player)) {
                if (std::find(gLooted.begin(), gLooted.end(), CPools::GetPedRef(p)) == gLooted.end())
                    Log("Loot: ran over a ped (model %d)", (int)p->m_nModelIndex);
                DropPedLoot(p);
            }
        }
    // the dead that are gone need not be remembered
    for (size_t i = 0; i < gLooted.size();) {
        if (!CPools::GetPed(gLooted[i])) {
            gLooted[i] = gLooted.back();
            gLooted.pop_back();
        } else {
            ++i;
        }
    }
}

void GtaCombatClear() {
    gHurt.clear();
    gLooted.clear();
    gCarBoost = 0.0f;
}

// ================================================================ engine hooks
namespace {
SafetyHookInline gFireHook;
SafetyHookInline gRoundHook;

bool NpcArrowsActive() { return gGta.enabled && gConfig.pedSkins && gConfig.npcArrows; }

bool IsGun(int w) { return w >= WEAPONTYPE_PISTOL && w <= WEAPONTYPE_SNIPERRIFLE || w == WEAPONTYPE_MINIGUN; }

void NpcShootArrow(CPed* shooter, const CVector& from, const CVector& to) {
    int who = -1, vehicle = -1;
    if (shooter) {
        who = CPools::GetPedRef(shooter);
        if (shooter->bInVehicle && shooter->m_pVehicle)
            vehicle = CPools::GetVehicleRef(shooter->m_pVehicle);
    }
    ShootArrowAt(from, to, who, vehicle);
}

bool __fastcall HookFire(CWeapon* w, void* /*edx*/, CEntity* by, CVector* origin, CVector* muzzle, CEntity* target,
                         CVector* targetPos, CVector* alt) {
    if (w && by && by->m_nType == ENTITY_TYPE_PED && NpcArrowsActive() && IsGun(w->m_eWeaponType)) {
        CPed* p = static_cast<CPed*>(by);
        if (!p->IsPlayer()) {
            if (w->m_nState != WEAPONSTATE_READY && w->m_nState != WEAPONSTATE_FIRING)
                return false;
            if (CTimer::m_snTimeInMilliseconds < w->m_nTimeForNextShot && w->m_nState == WEAPONSTATE_FIRING)
                return false;
            CVector from = origin ? *origin : p->GetPosition() + CVector(0, 0, 0.6f);
            CVector to;
            if (target)
                to = target->GetPosition() + CVector(0, 0, target->m_nType == ENTITY_TYPE_PED ? 0.2f : 0.5f);
            else if (targetPos)
                to = *targetPos;
            else
                to = from + p->GetForward() * 30.0f;
            NpcShootArrow(p, from, to);
            w->m_nState = WEAPONSTATE_FIRING;
            w->m_nTimeForNextShot = CTimer::m_snTimeInMilliseconds + 1100 + rand() % 500;
            return true;
        }
    }
    return gFireHook.thiscall<bool>(w, by, origin, muzzle, target, targetPos, alt);
}

void __cdecl HookOneRound(CVector* start, CVector* end, int intensity) {
    if (start && end && NpcArrowsActive()) {
        // the police helicopter: one arrow now and then instead of a stream of bullets
        if (gHeliShotTimer <= 0.0f) {
            gHeliShotTimer = 0.25f;
            CVector dir = Norm(*end - *start);
            NpcShootArrow(nullptr, *start + dir * 3.0f, *end);
        }
        return;
    }
    gRoundHook.ccall<void>(start, end, intensity);
}
} // namespace

namespace {
SafetyHookInline gFightHook;

// GTA lets a punch land only on somebody who has collision (as its fight code did since GTA III). The Minecraft
// movement keeps the player's switched off, so it is lent to him for as long as an attacker's fight is worked out
// (CTaskSimpleFight::ProcessPed).
bool __fastcall HookFightProcessPed(void* task, void* /*edx*/, CPed* attacker) {
    CPlayerPed* player = FindPlayerPed();
    const bool lend = gGta.enabled && player && attacker != player && !player->bInVehicle && !player->bUsesCollision &&
                      player->m_fHealth > 0.0f;
    if (lend) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            Log("Fight: somebody fights next to the player; his punches can land now");
        }
        player->bUsesCollision = true;
    }
    const bool finished = gFightHook.thiscall<bool>(task, attacker);
    if (lend)
        player->bUsesCollision = false;
    return finished;
}
} // namespace

void InstallCombatHooks() {
    static bool done = false;
    if (done)
        return;
    done = true;
    gFightHook = safetyhook::create_inline(reinterpret_cast<void*>(0x629920), reinterpret_cast<void*>(&HookFightProcessPed));
    Log("Hooks: fight %s", gFightHook ? "ok" : "FAILED");
    gFireHook = safetyhook::create_inline(reinterpret_cast<void*>(0x742300), reinterpret_cast<void*>(&HookFire));
    gRoundHook = safetyhook::create_inline(reinterpret_cast<void*>(0x73AF00), reinterpret_cast<void*>(&HookOneRound));
    Log("Hooks: weapon fire %s, heli gun %s", gFireHook ? "ok" : "FAILED", gRoundHook ? "ok" : "FAILED");
}

} // namespace mc
