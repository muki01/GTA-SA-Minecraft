#include "Combat.h"

#include "CCamera.h"
#include "CColPoint.h"
#include "CExplosion.h"
#include "CFireManager.h"
#include "CObject.h"
#include "CPed.h"
#include "CPlayerPed.h"
#include "CPools.h"
#include "CStreaming.h"
#include "CTimer.h"
#include "CVehicle.h"
#include "CWaterLevel.h"
#include "CWeapon.h"
#include "CWeather.h"
#include "CWorld.h"
#include "common.h"
#include "extensions/ScriptCommands.h"
#include "safetyhook.hpp"

#include "Blocks.h"
#include "Collision.h"
#include "Config.h"
#include "Draw3D.h"
#include "Fishing.h"
#include "Game.h"
#include "Inventory.h"
#include "Items.h"
#include "McModel.h"
#include "Mobs.h"
#include "Movement.h"
#include "PedSkins.h"
#include "Render3D.h"
#include "Sound.h"
#include "Terrain.h"
#include "Textures.h"
#include "Xp.h"

namespace mc {

namespace {
enum ProjType { PJ_ARROW, PJ_SNOWBALL, PJ_EGG, PJ_PEARL, PJ_FIREWORK, PJ_FIREBALL, PJ_WIND, PJ_TRIDENT, PJ_ROCKET, PJ_XPBOTTLE };

struct Projectile {
    int type;
    CVector pos, vel;
    float life = 60.0f;
    bool stuck = false;
    bool crit = false;
    bool pickup = false;
    float fuse = 0.0f;
    bool npc = false;        // shot by a pillager or the helicopter: it can hit the player
    int shooterRef = -1;     // ped pool reference of the shooter
    int ignoreVehRef = -1;   // vehicle the shot left from
    float ignoreTime = 0.5f;
    float damage = 0.0f;     // half hearts, 0 = from the speed
    ItemStack stack;         // a thrown trident
    int slot = -1;           // hotbar slot the trident came from
    bool returning = false;
    float stuckTime = 0.0f;
};

std::vector<Projectile> gProj;

struct HurtPed {
    int handle;
    float time;
};
std::vector<HurtPed> gHurt;

struct Bolt {
    CVector at;
    float age;
    uint32_t seed;
};
std::vector<Bolt> gBolts;
float gFlash = 0.0f;

float gCarBoost = 0.0f;
int gCarBoostVeh = -1;
float gHeliShotTimer = 0.0f;

const RwUInt32 kFireworkColors[] = { 0xFFB3312C, 0xFFEB8844, 0xFFDECF2A, 0xFF41CD34, 0xFF6689D3, 0xFF7B2FBE, 0xFFD88198, 0xFFF0F0F0 };

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

void Sparks(const CVector& at, RwUInt32 color, int count, float speed) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        CVector d(Rand01() * 2 - 1, Rand01() * 2 - 1, Rand01() * 2 - 1);
        d = Norm(d);
        p.pos = at;
        p.vel = d * (speed * (0.6f + Rand01() * 0.4f));
        p.maxLife = p.life = 1.0f + Rand01() * 0.8f;
        p.tile = TILE_P_SPARK_0;
        p.anim = 1;
        p.size = 0.12f;
        p.gravity = 2.0f;
        p.color = color;
        p.glow = true;
        SpawnParticle(p);
    }
}

void Puff(const CVector& at, uint16_t tile, int count) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.pos = at;
        p.vel = CVector(Rand01() - 0.5f, Rand01() - 0.5f, Rand01()) * 2.0f;
        p.maxLife = p.life = 0.4f + Rand01() * 0.4f;
        p.tile = tile;
        p.size = 0.06f;
        p.sub = 0.5f;
        p.u = (rand() % 2) * 0.5f;
        p.v = (rand() % 2) * 0.5f;
        SpawnParticle(p);
    }
}

void Smoke(const CVector& at, int count, float spread, float speed, RwUInt32 color) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        CVector d = Norm(CVector(Rand01() * 2 - 1, Rand01() * 2 - 1, Rand01() * 2 - 1));
        p.pos = at + d * (spread * Rand01());
        p.vel = d * (speed * (0.5f + Rand01()));
        p.maxLife = p.life = 0.5f + Rand01() * 0.5f;
        p.tile = TILE_P_GENERIC_0;
        p.anim = 2;
        p.size = 0.15f;
        p.gravity = -0.5f;
        p.color = color;
        SpawnParticle(p);
    }
}

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

void FireworkBlast(const CVector& at) {
    RwUInt32 c1 = kFireworkColors[rand() % 8], c2 = kFireworkColors[rand() % 8];
    Sparks(at, c1, 60, 9.0f);
    Sparks(at, c2, 30, 5.0f);
    PlaySfx(SND_FIREWORK_BLAST, &at, 4.0f);
    PlaySfx(SND_FIREWORK_TWINKLE, &at, 4.0f);
}

// where the player's shots start (outside of the vehicle he sits in)
CVector ShotOrigin(CPlayerPed* ped) {
    if (ped && ped->bInVehicle && ped->m_pVehicle)
        return gGame.eyePos + gGame.lookDir * 1.2f;
    return gGame.eyePos + gGame.lookDir * 0.6f;
}

float ProjGravity(int type) {
    switch (type) {
    case PJ_ARROW: case PJ_TRIDENT: return 20.0f;
    case PJ_FIREWORK: return -16.0f;
    case PJ_FIREBALL: case PJ_WIND: case PJ_ROCKET: return 0.0f;
    default: return 12.0f;
    }
}

// Sends what the player shoots or throws to the spot under the crosshair. Like in Minecraft it keeps the player's
// own motion (a car's, a flight's). Where the shot does not leave from the eye (third person, in a car, flying)
// the throw is aimed so that it really lands there, its fall included.
void AimFromPlayer(Projectile& pr, CPlayerPed* ped) {
    const float speed = pr.vel.Magnitude();
    if (!ped || speed < 0.1f)
        return;
    CVector v0 = PlayerVelocity(ped);
    if (!ped->bInVehicle && !gGame.flying && !gGame.gliding && PlayerOnGround(ped))
        v0.z = 0.0f; // standing on something: only the sideways motion goes with it
    const bool aim = ped->bInVehicle || gGame.flying || gGame.gliding || gGame.cameraMode != CAM_FIRST;
    if (!aim) {
        pr.vel += v0;
        return;
    }
    // the spot under the crosshair
    const CVector origin = gGame.rayOrigin, dir = gGame.lookDir;
    const float reach = 120.0f;
    float dist = 60.0f;
    CColPoint cp;
    CEntity* ent = nullptr;
    if (LineHit(origin, origin + dir * reach, cp, ent, true, ped, nullptr, ped->bInVehicle ? ped->m_pVehicle : nullptr))
        dist = (cp.m_vecPoint - origin).Magnitude();
    const VoxelHit vh = RaycastVoxels(origin, dir, reach);
    if (vh.hit)
        dist = std::min(dist, vh.dist);
    const MobHit mh = MobsRaycast(origin, dir, reach);
    if (mh.index >= 0)
        dist = std::min(dist, mh.dist);
    const PedHit ph = RaycastPeds(origin, dir, reach, ped, false, true);
    if (ph.ped)
        dist = std::min(dist, ph.dist);
    const CVector to = origin + dir * dist - pr.pos;
    const float len = to.Magnitude();
    if (len < 2.0f || (to.x * dir.x + to.y * dir.y + to.z * dir.z) < 0.5f * len) {
        pr.vel = dir * speed + v0; // too close to aim at
        return;
    }
    // w(t): the throw that is at the spot after t seconds; the right t makes it as fast as the throw can be
    const float g = ProjGravity(pr.type);
    auto need = [&](float t) { return to * (1.0f / t) - v0 + CVector(0, 0, 0.5f * g * t); };
    float tFound = -1.0f, tBest = 0.05f, bestErr = 1e9f;
    float prev = 0.0f;
    for (float t = 0.02f; t < 8.0f; t *= 1.15f) {
        const float m = need(t).Magnitude();
        if (std::fabs(m - speed) < bestErr) {
            bestErr = std::fabs(m - speed);
            tBest = t;
        }
        if (m <= speed) {
            // the first time fast enough: narrow it down between the last step and this one
            float lo = prev, hi = t;
            for (int i = 0; i < 20 && lo > 0.0f; ++i) {
                const float mid = (lo + hi) * 0.5f;
                if (need(mid).Magnitude() <= speed)
                    hi = mid;
                else
                    lo = mid;
            }
            tFound = hi;
            break;
        }
        prev = t;
    }
    CVector w = need(tFound > 0.0f ? tFound : tBest); // out of reach: as far as it goes towards it
    const float wm = w.Magnitude();
    if (wm < 1e-3f)
        w = dir;
    pr.vel = v0 + w * (speed / std::max(wm, 1e-3f));
}

void FromPlayer(Projectile& pr, CPlayerPed* ped) {
    pr.shooterRef = ped ? CPools::GetPedRef(ped) : -1;
    if (ped && ped->bInVehicle && ped->m_pVehicle) {
        pr.ignoreVehRef = CPools::GetVehicleRef(ped->m_pVehicle);
        pr.ignoreTime = 1.0f;
    }
    AimFromPlayer(pr, ped);
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

// pushes everything inside `radius` away from `centre`
void RadialLaunch(const CVector& centre, float radius, float side, float up, CPlayerPed* self, bool launchSelf) {
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
    MobsRadial(centre, radius, 0.0f, side + up);
    PushLooseThings(centre, radius, side);
    if (launchSelf && self && !self->bInVehicle) {
        CVector v = velFor(self->GetPosition(), inside);
        if (inside)
            LaunchPlayer(self, v);
    }
}

// right click: a shock wave throws everything around the player into the air
void WandShockwave(CPlayerPed* ped) {
    StartSwing();
    if (!WandAllowed())
        return;
    const CVector centre = ped->GetPosition() - CVector(0, 0, 0.6f);
    RadialLaunch(centre, 18.0f, 20.0f, 14.0f, ped, false);
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

void WindBurst(const CVector& at, CPlayerPed* ped) {
    RadialLaunch(at, 4.0f, 7.0f, 9.0f, ped, true);
    Smoke(at, 24, 0.8f, 3.0f, 0xFFE0E8F0);
    PlaySfx(SND_WIND_BURST, &at, 2.0f);
}

int MobKindForEgg(int special) {
    switch (special) {
    case SP_EGG_COW: return MOB_COW;
    case SP_EGG_PIG: return MOB_PIG;
    case SP_EGG_SHEEP: return MOB_SHEEP;
    case SP_EGG_CHICKEN: return MOB_CHICKEN;
    default: return -1;
    }
}

// first water surface along the look direction
bool LookAtWater(float maxDist, CVector* at) {
    const CVector o = gGame.eyePos, d = gGame.lookDir;
    for (float t = 0.3f; t <= maxDist; t += 0.2f) {
        CVector p = o + d * t;
        if (gWorld.IsSolid(FloorI(p.x), FloorI(p.y), FloorI(p.z)))
            return false;
        float wl;
        if (CWaterLevel::GetWaterLevelNoWaves(p.x, p.y, p.z, &wl) && p.z <= wl) {
            *at = CVector(p.x, p.y, wl);
            return true;
        }
    }
    return false;
}

void ReplaceHeld(uint16_t id) {
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

float LookHeadingDeg() { return std::atan2(-gGame.lookDir.x, gGame.lookDir.y) * (180.0f / kPi); }

// ---------------------------------------------------------------- shooting
void ShootArrow(CPlayerPed* ped, float speed, float damage, bool crit, bool pickup) {
    Projectile pr;
    pr.type = PJ_ARROW;
    pr.pos = ShotOrigin(ped);
    pr.vel = gGame.lookDir * speed;
    pr.crit = crit;
    pr.damage = damage;
    pr.pickup = pickup;
    FromPlayer(pr, ped);
    gProj.push_back(pr);
}

void ShootBow(CPlayerPed* ped, float power, bool crit) {
    ShootArrow(ped, power * 60.0f, 0.0f, crit, gGame.gameMode == MODE_SURVIVAL);
    PlaySfx(SND_BOW_SHOOT, nullptr, 1.0f, 1.0f / (Rand01() * 0.4f + 1.2f) + power * 0.5f);
}

void ShootCrossbow(CPlayerPed* ped) {
    if (gGame.crossbowRocket) {
        Projectile pr;
        pr.type = PJ_ROCKET;
        pr.pos = ShotOrigin(ped);
        pr.vel = gGame.lookDir * 32.0f;
        pr.life = 3.0f;
        FromPlayer(pr, ped);
        gProj.push_back(pr);
        PlaySfx(SND_FIREWORK_LAUNCH, nullptr, 1.0f);
    } else {
        ShootArrow(ped, 63.0f, 9.0f, false, gGame.gameMode == MODE_SURVIVAL);
    }
    PlaySfx(SND_CROSSBOW_SHOOT, nullptr, 1.0f, 1.0f / (Rand01() * 0.5f + 1.8f) + 0.6f);
    DamageHeldItem(1);
    StartSwing();
}

void ThrowTrident(CPlayerPed* ped) {
    ItemStack& held = gInv.Held();
    Projectile pr;
    pr.type = PJ_TRIDENT;
    pr.pos = ShotOrigin(ped);
    pr.vel = gGame.lookDir * 50.0f;
    pr.damage = 8.0f;
    pr.stack = held;
    pr.slot = gInv.selected;
    pr.life = 30.0f;
    FromPlayer(pr, ped);
    if (gGame.gameMode == MODE_SURVIVAL) {
        pr.stack.damage += 1;
        held.Clear();
    } else {
        pr.stack.Clear(); // creative keeps its trident
    }
    gProj.push_back(pr);
    PlaySfx(SND_TRIDENT_THROW, nullptr, 1.0f);
    StartSwing();
}

void ReturnTrident(const Projectile& pr) {
    if (pr.stack.Empty())
        return;
    ItemStack s = pr.stack;
    const ItemDef& d = Item(s.id);
    if (d.durability && s.damage >= d.durability) {
        PlaySfx(SND_TOOL_BREAK);
        return;
    }
    if (pr.slot >= 0 && pr.slot < 9 && gInv.slots[pr.slot].Empty())
        gInv.slots[pr.slot] = s;
    else if (gInv.Add(s) > 0)
        DropStackAtPlayer(s, false);
    PlaySfx(SND_PICKUP, nullptr, 0.4f, 1.4f);
    gWorld.dirty = true;
}
} // namespace

// ================================================================ public
void IgniteTnt(const Int3& p, float fuse, const CVector* velocity) {
    gWorld.Set(p.x, p.y, p.z, MakeVox(ID_AIR));
    PrimedTnt t;
    t.pos = CVector(p.x + 0.5f, p.y + 0.5f, (float)p.z);
    if (velocity) {
        t.vel = *velocity;
    } else {
        float a = Rand01() * 6.2831853f;
        t.vel = CVector(-std::sin(a) * 0.4f, -std::cos(a) * 0.4f, 4.0f);
    }
    t.fuse = fuse;
    if (gPrimedTnt.size() < 600)
        gPrimedTnt.push_back(t);
    PlaySfx(SND_FUSE, &t.pos);
}

void StrikeLightning(const CVector& at) {
    gBolts.push_back({ at, 0.0f, (uint32_t)rand() * 2654435761u });
    gFlash = 0.25f;
    PlaySfx(SND_THUNDER, nullptr, 1.0f, 0.8f + Rand01() * 0.2f);
    PlaySfx(SND_LIGHTNING_IMPACT, &at, 2.0f, 0.5f + Rand01() * 0.2f);
    gFireManager.StartFire(at, 1.0f, 0, FindPlayerPed(), 6000, 1, 0);
    ExplodeAt(at, 0.0f, true, EXPLOSION_SMALL);
}

bool LightningFlashActive() { return gFlash > 0.0f; }

bool TryMeleeAttack(CPlayerPed* ped) {
    const ItemStack& held = gInv.Held();
    const ItemDef& d = Item(held.id);
    if (!held.Empty() && d.special == SP_WAND) {
        WandStrike(ped);
        return true;
    }
    CVector origin = gGame.rayOrigin, dir = gGame.lookDir;
    float reach = (gGame.eyePos - origin).Magnitude() + 3.5f;
    CColPoint cp;
    CEntity* ent = nullptr;
    bool hit = LineHit(origin, origin + dir * reach, cp, ent, true, ped, nullptr);
    float gtaDist = hit ? (cp.m_vecPoint - origin).Magnitude() : 1e9f;
    VoxelHit vh = RaycastVoxels(origin, dir, reach);
    float wall = vh.hit ? vh.dist : 1e9f;
    MobHit mh = MobsRaycast(origin, dir, reach);
    PedHit ph = RaycastPeds(origin, dir, reach, ped, false, true);

    const float mobD = mh.index >= 0 ? mh.dist : 1e9f;
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
    const bool mobHit = mobD < wall && mobD < pedD && mobD <= gtaDist + 0.01f && (mh.point - gGame.eyePos).Magnitude() <= 3.6f;
    const bool pedHit = !mobHit && victim && pedD < wall && (point - gGame.eyePos).Magnitude() <= 3.6f;
    if (!mobHit && !pedHit)
        return false;
    if (mobHit)
        point = mh.point;

    float charge = AttackCharge();
    float base = held.Empty() ? 1.0f : d.damage;
    float dmg = base * (0.2f + 0.8f * charge * charge);
    bool falling = gGame.jumping ? gGame.flyVel.z < 0.0f : (!ped->bIsStanding && ped->m_vecMoveSpeed.z < 0.0f);
    bool crit = charge > 0.9f && falling && !gGame.flying && !gGame.sprinting;
    if (crit)
        dmg *= 1.5f;
    bool sword = d.tool == TOOL_SWORD;
    float knock = (gGame.sprinting && charge > 0.9f) ? 1.6f : 1.0f;
    if (mobHit) {
        MobHurt(mh.index, dmg, ped->GetPosition(), knock);
    } else {
        HurtPedBy(victim, ped, dmg, sword ? WEAPONTYPE_KATANA : WEAPONTYPE_UNARMED);
        if (!victim->bInVehicle) {
            CVector push = dir;
            push.z = 0;
            push = Norm(push);
            victim->m_vecMoveSpeed += push * ((0.06f + 0.06f * charge) * knock) + CVector(0, 0, 0.05f);
        }
    }
    if (gGame.sprinting && charge > 0.9f)
        gGame.sprintLatch = false; // a knockback hit stops the sprint, like in Minecraft
    if (crit) {
        PlaySfx(SND_ATTACK_CRIT, &point);
        for (int i = 0; i < 12; ++i) {
            Particle p;
            p.pos = point;
            p.vel = CVector(Rand01() - 0.5f, Rand01() - 0.5f, Rand01()) * 4.0f;
            p.maxLife = p.life = 0.5f;
            p.tile = TILE_P_CRITICAL_HIT;
            p.size = 0.08f;
            p.gravity = 4.0f;
            p.glow = true;
            SpawnParticle(p);
        }
    } else if (charge > 0.9f) {
        PlaySfx(knock > 1.0f ? SND_KNOCKBACK : (sword ? SND_ATTACK_SWEEP : SND_ATTACK_STRONG), &point);
    } else {
        PlaySfx(SND_ATTACK_WEAK, &point);
    }
    if (!held.Empty() && d.durability)
        DamageHeldItem(sword ? 1 : 2);
    gGame.attackTimer = 0.0f;
    gGame.exhaustion += 0.1f;
    StartSwing();
    return true;
}

bool UpdateChargedItems(float dt, CPlayerPed* ped, bool rmbDown, bool rmbPressed) {
    ItemStack& held = gInv.Held();
    const int special = held.Empty() ? 0 : Item(held.id).special;
    const bool creative = gGame.gameMode == MODE_CREATIVE;

    // spyglass
    const bool wasSpy = gGame.spyglass;
    gGame.spyglass = special == SP_SPYGLASS && rmbDown;
    if (gGame.spyglass && !wasSpy)
        PlaySfx(SND_SPYGLASS);

    // bow
    if (special == SP_BOW && (creative || gInv.CountOf(ID_ARROW) > 0)) {
        if (rmbDown) {
            if (gGame.bowDraw < 0.0f)
                gGame.bowDraw = 0.0f;
            gGame.bowDraw += dt;
            return true;
        }
        if (gGame.bowDraw >= 0.0f) {
            float t = std::min(gGame.bowDraw, 1.0f);
            float f = std::min(1.0f, (t * t + 2.0f * t) / 3.0f);
            if (f >= 0.1f) {
                ShootBow(ped, f, f >= 1.0f);
                if (!creative)
                    gInv.Remove(ID_ARROW, 1);
                DamageHeldItem(1);
            }
            gGame.bowDraw = -1.0f;
        }
    } else {
        gGame.bowDraw = -1.0f;
    }

    // crossbow
    if (special == SP_CROSSBOW) {
        const bool loaded = gGame.crossbowSlot == gInv.selected;
        if (loaded) {
            gGame.crossbowCharge = -1.0f;
            if (rmbPressed) {
                ShootCrossbow(ped);
                gGame.crossbowSlot = -1;
            }
            return rmbDown;
        }
        const bool rocket = gInv.offhand.id == ID_FIREWORK_ROCKET;
        const bool ammo = creative || rocket || gInv.CountOf(ID_ARROW) > 0;
        if (rmbDown && ammo) {
            if (gGame.crossbowCharge < 0.0f)
                gGame.crossbowCharge = 0.0f;
            gGame.crossbowCharge += dt;
            if (gGame.crossbowCharge >= 1.25f) {
                gGame.crossbowSlot = gInv.selected;
                gGame.crossbowRocket = rocket;
                gGame.crossbowCharge = -1.0f;
                if (!creative) {
                    if (rocket) {
                        if (--gInv.offhand.count == 0)
                            gInv.offhand.Clear();
                    } else {
                        gInv.Remove(ID_ARROW, 1);
                    }
                }
                PlaySfx(SND_CLICK, nullptr, 0.6f, 1.4f);
            }
            return true;
        }
        gGame.crossbowCharge = -1.0f;
    } else {
        gGame.crossbowCharge = -1.0f;
    }

    // trident
    if (special == SP_TRIDENT) {
        if (rmbDown) {
            if (gGame.tridentCharge < 0.0f)
                gGame.tridentCharge = 0.0f;
            gGame.tridentCharge += dt;
            return true;
        }
        if (gGame.tridentCharge >= 0.5f)
            ThrowTrident(ped);
        gGame.tridentCharge = -1.0f;
    } else {
        gGame.tridentCharge = -1.0f;
    }
    return gGame.spyglass;
}

bool UseSpecialItem(CPlayerPed* ped, bool targetValid, bool targetVoxel, const Int3& targetPos, const CVector& hitPoint,
                    const CVector& hitNormal) {
    ItemStack& held = gInv.Held();
    if (held.Empty())
        return false;
    const ItemDef& d = Item(held.id);
    const bool survival = gGame.gameMode == MODE_SURVIVAL;
    const bool inVehicle = ped->bInVehicle && ped->m_pVehicle;
    auto consume = [&]() {
        if (survival && --held.count == 0)
            held.Clear();
    };
    if (held.id == ID_EXPERIENCE_BOTTLE) {
        // bottle o' enchanting: thrown, breaks into experience
        Projectile pr;
        pr.type = PJ_XPBOTTLE;
        pr.pos = ShotOrigin(ped);
        pr.vel = gGame.lookDir * 14.0f + CVector(0, 0, 4.0f);
        pr.life = 10.0f;
        FromPlayer(pr, ped);
        gProj.push_back(pr);
        PlaySfx(SND_THROW, nullptr, 0.5f, 0.4f + Rand01() * 0.4f);
        consume();
        StartSwing();
        return true;
    }
    switch (d.special) {
    case SP_IGNITER:
    case SP_FIRE_CHARGE: {
        if (targetValid && targetVoxel && gWorld.GetBlock(targetPos.x, targetPos.y, targetPos.z) == ID_TNT) {
            IgniteTnt(targetPos, 4.0f);
            PlaySfx(SND_IGNITE, &hitPoint);
        } else if (d.special == SP_FIRE_CHARGE) {
            // a ghast fireball
            Projectile pr;
            pr.type = PJ_FIREBALL;
            pr.pos = ShotOrigin(ped);
            pr.vel = gGame.lookDir * 24.0f;
            pr.life = 8.0f;
            FromPlayer(pr, ped);
            gProj.push_back(pr);
            PlaySfx(SND_FIREBALL, nullptr, 1.0f, 0.9f + Rand01() * 0.2f);
        } else {
            if (!targetValid)
                return false;
            const CVector q = hitPoint + hitNormal * 0.5f;
            if (!PlaceFire({ FloorI(q.x), FloorI(q.y), FloorI(q.z) })) {
                // walls of GTA buildings, cars...: GTA's own fire
                CVector at = hitPoint + hitNormal * 0.3f;
                gFireManager.StartFire(at, 1.0f, 0, ped, 10000, 1, 0);
            }
            PlaySfx(SND_IGNITE, &hitPoint);
        }
        if (held.id == ID_FLINT_AND_STEEL)
            DamageHeldItem(1);
        else
            consume();
        StartSwing();
        return true;
    }
    case SP_FIREWORK: {
        if (FireworkBoost()) {
            Projectile pr;
            pr.type = PJ_FIREWORK;
            pr.pos = ped->GetPosition();
            pr.fuse = gGame.boostTime;
            pr.life = gGame.boostTime;
            pr.pickup = true; // marks "attached to the player"
            gProj.push_back(pr);
            consume();
            return true;
        }
        if (inVehicle) {
            // a rocket strapped to the car
            gCarBoost = 1.6f;
            gCarBoostVeh = CPools::GetVehicleRef(ped->m_pVehicle);
            PlaySfx(SND_FIREWORK_LAUNCH, nullptr, 1.0f);
            consume();
            return true;
        }
        if (!targetValid)
            return false;
        Projectile pr;
        pr.type = PJ_FIREWORK;
        pr.pos = hitPoint + hitNormal * 0.25f;
        pr.vel = CVector((Rand01() - 0.5f) * 0.8f, (Rand01() - 0.5f) * 0.8f, 8.0f);
        pr.fuse = 1.2f + Rand01() * 0.5f;
        pr.life = 5.0f;
        gProj.push_back(pr);
        PlaySfx(SND_FIREWORK_LAUNCH, &pr.pos);
        consume();
        StartSwing();
        return true;
    }
    case SP_SNOWBALL:
    case SP_EGG:
    case SP_ENDER_PEARL:
    case SP_WIND_CHARGE: {
        Projectile pr;
        pr.type = d.special == SP_SNOWBALL ? PJ_SNOWBALL
                : d.special == SP_EGG      ? PJ_EGG
                : d.special == SP_WIND_CHARGE ? PJ_WIND
                                              : PJ_PEARL;
        pr.pos = ShotOrigin(ped);
        pr.vel = gGame.lookDir * 30.0f;
        pr.life = 10.0f;
        FromPlayer(pr, ped);
        gProj.push_back(pr);
        PlaySfx(d.special == SP_ENDER_PEARL ? SND_PEARL_THROW : d.special == SP_WIND_CHARGE ? SND_WIND_THROW : SND_THROW);
        consume();
        StartSwing();
        return true;
    }
    case SP_WAND:
        WandShockwave(ped);
        return true;
    case SP_FISHING_ROD:
        return !inVehicle && FishingUse(ped);
    case SP_EGG_COW:
    case SP_EGG_PIG:
    case SP_EGG_SHEEP:
    case SP_EGG_CHICKEN: {
        if (!targetValid || inVehicle)
            return false;
        CVector at = hitPoint + hitNormal * (hitNormal.z > 0.5f ? 0.02f : 0.6f);
        float gz;
        if (GroundBelow(at + CVector(0, 0, 0.5f), 4.0f, &gz))
            at.z = gz;
        if (SpawnMob(MobKindForEgg(d.special), at, true) < 0)
            return false;
        PlaySfx(SND_POP, &at, 0.5f);
        consume();
        StartSwing();
        return true;
    }
    case SP_WATER_BUCKET: {
        CVector at = targetValid ? hitPoint : gGame.eyePos + gGame.lookDir * 3.0f;
        gFireManager.ExtinguishPoint(at, 4.0f);
        gFireManager.ExtinguishPoint(ped->GetPosition(), 3.0f);
        for (int i = 0; i < 30; ++i) {
            Particle p;
            p.pos = at + CVector((Rand01() - 0.5f) * 1.5f, (Rand01() - 0.5f) * 1.5f, Rand01() * 0.5f);
            p.vel = CVector((Rand01() - 0.5f) * 3.0f, (Rand01() - 0.5f) * 3.0f, 1.0f + Rand01() * 3.0f);
            p.maxLife = p.life = 0.6f + Rand01() * 0.4f;
            p.tile = TILE_P_SPLASH_0 + rand() % 4;
            p.size = 0.09f;
            p.gravity = 9.0f;
            SpawnParticle(p);
        }
        PlaySfx(SND_SPLASH, &at);
        ReplaceHeld(ID_BUCKET);
        StartSwing();
        return true;
    }
    case SP_LAVA_BUCKET: {
        if (!targetValid)
            return false;
        CVector at = hitPoint + hitNormal * 0.3f;
        gFireManager.StartFire(at, 1.0f, 0, ped, 9000, 2, 0);
        for (int i = 0; i < 3; ++i)
            gFireManager.StartFire(at + CVector((Rand01() - 0.5f) * 2.0f, (Rand01() - 0.5f) * 2.0f, 0.0f), 1.0f, 0, ped,
                                   7000, 1, 0);
        PlaySfx(SND_IGNITE, &at, 1.0f, 0.6f);
        ReplaceHeld(ID_BUCKET);
        StartSwing();
        return true;
    }
    case SP_BUCKET: {
        CVector water;
        if (inVehicle || !LookAtWater(5.0f, &water))
            return false;
        ReplaceHeld(ID_WATER_BUCKET);
        PlaySfx(SND_SPLASH, &water, 0.6f);
        StartSwing();
        return true;
    }
    case SP_BOAT: {
        CVector water;
        if (inVehicle || !LookAtWater(8.0f, &water)) {
            ShowMessage("Tekneyi suya koymalısın");
            return true;
        }
        if (!SpawnGtaVehicle(473, water + CVector(0, 0, 0.6f), LookHeadingDeg()))
            return false;
        consume();
        StartSwing();
        return true;
    }
    case SP_MINECART: {
        if (inVehicle || !targetValid || hitNormal.z < 0.5f)
            return false;
        if (!SpawnGtaVehicle(571, hitPoint + CVector(0, 0, 0.6f), LookHeadingDeg()))
            return false;
        consume();
        StartSwing();
        return true;
    }
    default:
        return false;
    }
}

void UpdateCarBoost(float dt, CPlayerPed* ped) {
    gFlash = std::max(0.0f, gFlash - dt);
    for (size_t i = 0; i < gBolts.size();) {
        gBolts[i].age += dt;
        if (gBolts[i].age > 0.5f) {
            gBolts[i] = gBolts.back();
            gBolts.pop_back();
        } else {
            ++i;
        }
    }
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

void UpdateProjectiles(float dt, CPlayerPed* ped) {
    for (size_t i = 0; i < gProj.size();) {
        Projectile& pr = gProj[i];
        bool remove = false;
        pr.life -= dt;
        pr.ignoreTime -= dt;
        if (pr.type == PJ_FIREWORK && pr.pickup) {
            // boosting rocket follows the gliding player
            if (ped)
                pr.pos = ped->GetPosition() - gGame.lookDir * 0.6f;
            Particle sp;
            sp.pos = pr.pos;
            sp.vel = CVector(Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f);
            sp.maxLife = sp.life = 0.5f;
            sp.tile = TILE_P_SPARK_0;
            sp.anim = 1;
            sp.size = 0.08f;
            sp.gravity = 0.0f;
            sp.glow = true;
            SpawnParticle(sp);
            if (pr.life <= 0.0f || !gGame.gliding)
                remove = true;
        } else if (pr.type == PJ_TRIDENT && pr.returning) {
            // loyalty: the trident flies back to its owner
            if (!ped) {
                SpawnDrop(pr.pos, pr.stack);
                remove = true;
            } else {
                CVector to = ped->GetPosition() + CVector(0, 0, 0.4f) - pr.pos;
                float dist = to.Magnitude();
                if (dist < 1.3f || pr.life <= 0.0f) {
                    ReturnTrident(pr);
                    remove = true;
                } else {
                    pr.vel = to * (std::min(dist, 30.0f) * 1.5f / dist + 1.0f);
                    pr.pos += pr.vel * dt;
                }
            }
        } else if (pr.stuck) {
            pr.stuckTime += dt;
            if (pr.type == PJ_TRIDENT) {
                if (pr.stuckTime > 0.6f)
                    pr.returning = !pr.stack.Empty();
                if (pr.stack.Empty() && pr.stuckTime > 3.0f)
                    remove = true;
            } else if (pr.life <= 0.0f) {
                remove = true;
            } else if (pr.pickup && ped && (pr.pos - ped->GetPosition()).Magnitude() < 1.6f) {
                ItemStack s;
                s.id = ID_ARROW;
                s.count = 1;
                if (gInv.Add(s) == 0) {
                    PlaySfx(SND_PICKUP, nullptr, 0.4f, 1.6f);
                    remove = true;
                }
            }
        } else {
            float g = 12.0f;
            switch (pr.type) {
            case PJ_ARROW: case PJ_TRIDENT: g = 20.0f; break;
            case PJ_FIREWORK: g = -16.0f; break;
            case PJ_FIREBALL: case PJ_WIND: case PJ_ROCKET: g = 0.0f; break;
            default: break;
            }
            pr.vel.z -= g * dt;
            if (pr.type == PJ_ARROW || pr.type == PJ_TRIDENT)
                pr.vel = pr.vel * std::pow(0.99f, dt * 20.0f);
            if (pr.type == PJ_ROCKET)
                pr.vel = pr.vel * (1.0f + dt * 1.2f);
            CVector step = pr.vel * dt;
            CVector next = pr.pos + step;
            if (pr.type == PJ_FIREWORK) {
                Particle sp;
                sp.pos = pr.pos;
                sp.vel = CVector(0, 0, -1.0f);
                sp.maxLife = sp.life = 0.6f;
                sp.tile = TILE_P_SPARK_0;
                sp.anim = 1;
                sp.size = 0.08f;
                sp.gravity = 0.0f;
                sp.glow = true;
                SpawnParticle(sp);
                pr.pos = next;
                pr.fuse -= dt;
                if (pr.fuse <= 0.0f) {
                    FireworkBlast(pr.pos);
                    remove = true;
                }
            } else {
                // trails
                if (pr.type == PJ_FIREBALL || pr.type == PJ_ROCKET) {
                    Particle sp;
                    sp.pos = pr.pos;
                    sp.vel = CVector(Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f) * 0.6f;
                    sp.maxLife = sp.life = 0.4f;
                    sp.tile = pr.type == PJ_FIREBALL ? TILE_P_FLAME : TILE_P_SPARK_0;
                    sp.anim = pr.type == PJ_ROCKET ? 1 : 0;
                    sp.size = pr.type == PJ_FIREBALL ? 0.2f : 0.1f;
                    sp.gravity = -0.5f;
                    sp.glow = true;
                    SpawnParticle(sp);
                } else if (pr.type == PJ_WIND && rand() % 2 == 0) {
                    Smoke(pr.pos, 1, 0.1f, 0.3f, 0xFFE0E8F0);
                } else if (pr.crit && pr.type == PJ_ARROW && rand() % 2 == 0) {
                    Particle sp;
                    sp.pos = pr.pos;
                    sp.vel = CVector(0, 0, 0);
                    sp.maxLife = sp.life = 0.3f;
                    sp.tile = TILE_P_CRITICAL_HIT;
                    sp.size = 0.06f;
                    sp.gravity = 0.0f;
                    sp.glow = true;
                    SpawnParticle(sp);
                }

                const CVector dir = Norm(step);
                const float len = step.Magnitude();
                CPed* shooter = PedFromRef(pr.shooterRef);
                CVehicle* ignoreVeh = pr.ignoreTime > 0.0f ? VehFromRef(pr.ignoreVehRef) : nullptr;
                CColPoint cp;
                CEntity* ent = nullptr;
                bool hit = LineHit(pr.pos, next, cp, ent, true, shooter, pr.npc ? nullptr : ped, ignoreVeh);
                float hitDist = hit ? (cp.m_vecPoint - pr.pos).Magnitude() : 1e9f;
                VoxelHit vh = RaycastVoxels(pr.pos, dir, len);
                if (vh.hit && vh.dist < hitDist) {
                    hit = true;
                    ent = nullptr;
                    hitDist = vh.dist;
                    cp.m_vecPoint = pr.pos + dir * vh.dist;
                }
                MobHit mh = MobsRaycast(pr.pos, dir, len);
                // (people in a car sit behind its body: a shot that hits the car reaches them)
                PedHit ph = RaycastPeds(pr.pos, dir, len + 2.5f, shooter, pr.npc, true);
                const bool vehicleHit = hit && ent && ent->m_nType == ENTITY_TYPE_VEHICLE;
                if (ph.ped && !(ph.dist < std::min(hitDist, len) ||
                                (vehicleHit && ph.ped->bInVehicle && ph.ped->m_pVehicle == ent && ph.dist < hitDist + 2.5f)))
                    ph.ped = nullptr;
                const float speedBt = pr.vel.Magnitude() / 20.0f;
                const bool mobHit = mh.index >= 0 && mh.dist < hitDist && (!ph.ped || mh.dist <= ph.dist);
                CPed* victim = nullptr;
                if (!mobHit) {
                    if (ph.ped) {
                        victim = ph.ped;
                        hit = true;
                        hitDist = ph.dist;
                        cp.m_vecPoint = ph.point;
                    } else if (hit && ent && ent->m_nType == ENTITY_TYPE_PED) {
                        victim = static_cast<CPed*>(ent);
                    }
                }
                CEntity* attacker = pr.npc ? static_cast<CEntity*>(shooter) : static_cast<CEntity*>(ped);
                const float arrowDmg = pr.damage > 0.0f ? pr.damage : std::ceil(speedBt * 2.0f);
                bool impact = false;
                if (mobHit) {
                    pr.pos = mh.point;
                    const CVector from = pr.pos - dir * 3.0f;
                    if (pr.type == PJ_ARROW || pr.type == PJ_TRIDENT) {
                        MobHurt(mh.index, arrowDmg + (pr.crit ? (float)(rand() % 5) : 0.0f), from, 0.7f);
                        PlaySfx(pr.type == PJ_TRIDENT ? SND_TRIDENT_HIT : SND_ARROW_HIT, &pr.pos);
                    } else if (pr.type != PJ_PEARL) {
                        MobHurt(mh.index, 0.0f, from, 0.5f);
                    }
                    impact = true;
                } else if (!hit) {
                    pr.pos = next;
                } else if (victim) {
                    pr.pos = cp.m_vecPoint;
                    if (pr.type == PJ_ARROW || pr.type == PJ_TRIDENT) {
                        float dmg = arrowDmg + (pr.crit ? (float)(rand() % 5) : 0.0f);
                        HurtPedBy(victim, attacker, dmg, WEAPONTYPE_PISTOL);
                        victim->m_vecMoveSpeed += dir * 0.04f;
                        PlaySfx(pr.type == PJ_TRIDENT ? SND_TRIDENT_HIT : SND_ARROW_HIT, &cp.m_vecPoint);
                    } else if (pr.type == PJ_SNOWBALL || pr.type == PJ_EGG) {
                        victim->m_vecMoveSpeed += dir * 0.05f;
                    }
                    impact = true;
                } else {
                    pr.pos = cp.m_vecPoint - dir * 0.05f;
                    const bool vehicle = ent && ent->m_nType == ENTITY_TYPE_VEHICLE;
                    if (pr.type == PJ_ARROW && !vehicle) {
                        pr.stuck = true;
                        pr.life = pr.npc ? 10.0f : 60.0f;
                        PlaySfx(SND_ARROW_HIT, &pr.pos);
                    } else if (pr.type == PJ_ARROW) {
                        // arrows do not stick in cars, but a hail of them wears a car down
                        CVehicle* v = static_cast<CVehicle*>(ent);
                        if (v->m_fHealth > 0.0f)
                            v->m_fHealth -= arrowDmg * 4.0f;
                        PlaySfx(SND_ARROW_HIT, &pr.pos);
                        remove = true;
                    } else if (pr.type == PJ_TRIDENT) {
                        pr.stuck = true;
                        PlaySfx(SND_TRIDENT_HIT, &pr.pos);
                    }
                    impact = true;
                }

                if (impact) {
                    switch (pr.type) {
                    case PJ_ARROW:
                        if (!pr.stuck)
                            remove = true;
                        break;
                    case PJ_TRIDENT:
                        // channeling: in a storm (or in creative) the trident calls down lightning
                        if (CWeather::Rain > 0.1f || gGame.gameMode == MODE_CREATIVE)
                            StrikeLightning(pr.pos);
                        if (!pr.stuck) {
                            pr.stuck = true;
                            pr.vel = pr.vel * -0.1f;
                        }
                        break;
                    case PJ_SNOWBALL:
                        Puff(pr.pos, Item(ID_SNOWBALL).tile, 8);
                        remove = true;
                        break;
                    case PJ_XPBOTTLE:
                        Puff(pr.pos, Item(ID_EXPERIENCE_BOTTLE).tile, 8);
                        PlaySfx(SND_DIG_GLASS, &pr.pos);
                        SpawnXp(pr.pos - dir * 0.3f, 3 + rand() % 9);
                        remove = true;
                        break;
                    case PJ_EGG:
                        Puff(pr.pos, Item(ID_EGG).tile, 8);
                        if (rand() % 8 == 0) {
                            CVector at = pr.pos - dir * 0.3f;
                            float gz;
                            if (GroundBelow(at + CVector(0, 0, 0.5f), 6.0f, &gz))
                                at.z = gz;
                            SpawnMob(MOB_CHICKEN, at, true, true);
                        }
                        remove = true;
                        break;
                    case PJ_PEARL:
                        if (ped && !ped->bInVehicle) {
                            CVector to = pr.pos - dir * 0.5f + CVector(0, 0, 1.0f);
                            StopFlying(ped);
                            ped->Teleport(to, false);
                            PlaySfx(SND_TELEPORT);
                            if (gGame.gameMode == MODE_SURVIVAL) {
                                float maxH = ped->m_fMaxHealth > 1.0f ? ped->m_fMaxHealth : 100.0f;
                                CWeapon::GenerateDamageEvent(ped, nullptr, WEAPONTYPE_FALL, (int)(maxH / 4.0f),
                                                             (ePedPieceTypes)3, 0);
                            }
                        }
                        remove = true;
                        break;
                    case PJ_FIREBALL:
                        ExplodeAt(pr.pos - dir * 0.3f, 1.3f, true, EXPLOSION_MOLOTOV);
                        remove = true;
                        break;
                    case PJ_WIND:
                        WindBurst(pr.pos - dir * 0.3f, ped);
                        remove = true;
                        break;
                    case PJ_ROCKET:
                        FireworkBlast(pr.pos);
                        ExplodeAt(pr.pos - dir * 0.3f, 0.0f, true, EXPLOSION_SMALL);
                        remove = true;
                        break;
                    default:
                        remove = true;
                        break;
                    }
                }
            }
            if (pr.pos.z < -100.0f)
                remove = true;
            if (pr.life <= 0.0f) {
                if (pr.type == PJ_TRIDENT && !pr.stack.Empty())
                    pr.returning = true;
                else if (pr.type == PJ_ROCKET || pr.type == PJ_FIREBALL) {
                    if (pr.type == PJ_ROCKET)
                        FireworkBlast(pr.pos);
                    remove = true;
                } else
                    remove = true;
            }
        }
        if (remove) {
            gProj[i] = gProj.back();
            gProj.pop_back();
        } else {
            ++i;
        }
    }
}

void RenderProjectiles(float light) {
    if (gProj.empty())
        return;
    const CMatrix& cm = TheCamera.m_mCameraMatrix;
    const CVector camR = cm.right * -1.0f, camU = cm.at;
    for (auto& pr : gProj) {
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
            CVector p = b.at + CVector(0, 0, branch == 0 ? 0.0f : 20.0f + branch * 15.0f);
            CVector top = b.at + CVector(rnd() * 20.0f, rnd() * 20.0f, 110.0f);
            const int segs = branch == 0 ? 16 : 6;
            CVector prev = branch == 0 ? b.at : p + CVector(rnd() * 6.0f, rnd() * 6.0f, 0.0f);
            for (int k = 1; k <= segs; ++k) {
                float t = (float)k / segs;
                CVector next = (branch == 0 ? b.at : prev) * (1.0f - t) + top * t;
                if (branch == 0)
                    next = b.at * (1.0f - t) + top * t;
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

void UpdatePedLoot() {
    for (size_t i = 0; i < gHurt.size();) {
        HurtPed& h = gHurt[i];
        h.time += FrameDelta();
        CPed* p = CPools::GetPed(h.handle);
        bool remove = !p || h.time > 30.0f;
        if (p && (p->m_fHealth <= 0.0f || p->m_ePedState == PEDSTATE_DIE || p->m_ePedState == PEDSTATE_DEAD)) {
            CVector at = p->GetPosition();
            const bool illager = p->m_nPedType == PED_TYPE_COP;
            SpawnXp(at, illager ? 5 + rand() % 3 : 1 + rand() % 5);
            struct Loot { uint16_t id; int maxCount; float chance; };
            if (illager) {
                // pillager loot
                const Loot loot[] = { { ID_ARROW, 3, 0.7f }, { ID_EMERALD, 1, 0.35f }, { ID_CROSSBOW, 1, 0.09f },
                                      { ID_IRON_INGOT, 1, 0.15f } };
                for (auto& l : loot)
                    if (Rand01() < l.chance)
                        SpawnDropItem(at, l.id, 1 + rand() % l.maxCount);
            } else {
                const Loot loot[] = { { ID_ROTTEN_FLESH, 2, 0.45f }, { ID_BONE, 2, 0.3f }, { ID_STRING, 2, 0.35f },
                                      { ID_GUNPOWDER, 2, 0.35f }, { ID_ARROW, 3, 0.3f }, { ID_PAPER, 2, 0.15f },
                                      { ID_BREAD, 2, 0.25f }, { ID_EMERALD, 1, 0.12f } };
                for (auto& l : loot)
                    if (Rand01() < l.chance)
                        SpawnDropItem(at, l.id, 1 + rand() % l.maxCount);
            }
            remove = true;
        }
        if (remove) {
            gHurt[i] = gHurt.back();
            gHurt.pop_back();
        } else {
            ++i;
        }
    }
}

void ClearProjectiles() {
    gProj.clear();
    gHurt.clear();
    gBolts.clear();
    gCarBoost = 0.0f;
}

// ================================================================ engine hooks
namespace {
SafetyHookInline gFireHook;
SafetyHookInline gRoundHook;

bool NpcArrowsActive() { return gGame.enabled && gConfig.pedSkins && gConfig.npcArrows; }

bool IsGun(int w) { return w >= WEAPONTYPE_PISTOL && w <= WEAPONTYPE_SNIPERRIFLE || w == WEAPONTYPE_MINIGUN; }

void NpcShootArrow(CPed* shooter, CVector from, const CVector& to) {
    if (gProj.size() > 350)
        return;
    CVector d = to - from;
    float dist = d.Magnitude();
    if (dist < 0.5f)
        return;
    const float speed = 48.0f;
    // aim a little above the target so gravity brings the arrow down onto it, plus some spread
    float t = dist / speed;
    CVector aim = to + CVector(0, 0, 0.5f * 20.0f * t * t);
    aim += CVector((Rand01() - 0.5f), (Rand01() - 0.5f), (Rand01() - 0.5f)) * (0.03f * dist + 0.2f);
    CVector dir = Norm(aim - from);
    Projectile pr;
    pr.type = PJ_ARROW;
    pr.pos = from + dir * 0.5f;
    pr.vel = dir * speed;
    pr.npc = true;
    pr.damage = 4.0f + Rand01() * 2.0f;
    pr.life = 10.0f;
    if (shooter) {
        pr.shooterRef = CPools::GetPedRef(shooter);
        if (shooter->bInVehicle && shooter->m_pVehicle)
            pr.ignoreVehRef = CPools::GetVehicleRef(shooter->m_pVehicle);
    }
    gProj.push_back(pr);
    PlaySfx(SND_CROSSBOW_SHOOT, &from, 1.5f, 0.9f + Rand01() * 0.2f);
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

void InstallCombatHooks() {
    static bool done = false;
    if (done)
        return;
    done = true;
    gFireHook = safetyhook::create_inline(reinterpret_cast<void*>(0x742300), reinterpret_cast<void*>(&HookFire));
    gRoundHook = safetyhook::create_inline(reinterpret_cast<void*>(0x73AF00), reinterpret_cast<void*>(&HookOneRound));
    Log("Hooks: weapon fire %s, heli gun %s", gFireHook ? "ok" : "FAILED", gRoundHook ? "ok" : "FAILED");
}

} // namespace mc
