#include "Combat.h"

#include <cstdlib>

#include "Audio.h"
#include "BlockRules.h"
#include "Entities.h"
#include "Fishing.h"
#include "GameState.h"
#include "Host.h"
#include "Interact.h"
#include "Inventory.h"
#include "Items.h"
#include "Mobs.h"
#include "Particles.h"
#include "Survival.h"

namespace mc {

std::vector<Projectile> gProjectiles;
std::vector<Bolt> gBolts;

namespace {
float gFlash = 0.0f; // seconds the sky stays lit by a bolt

const uint32_t kFireworkColors[] = { 0xFFB3312C, 0xFFEB8844, 0xFFDECF2A, 0xFF41CD34, 0xFF6689D3, 0xFF7B2FBE, 0xFFD88198, 0xFFF0F0F0 };

Vec3 Norm(const Vec3& v) {
    float m = v.Length();
    return m > 1e-5f ? v * (1.0f / m) : Vec3(0, 0, 1);
}

void Sparks(const Vec3& at, uint32_t color, int count, float speed) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        Vec3 d(Rand01() * 2 - 1, Rand01() * 2 - 1, Rand01() * 2 - 1);
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

void Puff(const Vec3& at, uint16_t tile, int count) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.pos = at;
        p.vel = Vec3(Rand01() - 0.5f, Rand01() - 0.5f, Rand01()) * 2.0f;
        p.maxLife = p.life = 0.4f + Rand01() * 0.4f;
        p.tile = tile;
        p.size = 0.06f;
        p.sub = 0.5f;
        p.u = (rand() % 2) * 0.5f;
        p.v = (rand() % 2) * 0.5f;
        SpawnParticle(p);
    }
}

void Smoke(const Vec3& at, int count, float spread, float speed, uint32_t color) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        Vec3 d = Norm(Vec3(Rand01() * 2 - 1, Rand01() * 2 - 1, Rand01() * 2 - 1));
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

void FireworkBlast(const Vec3& at) {
    uint32_t c1 = kFireworkColors[rand() % 8], c2 = kFireworkColors[rand() % 8];
    Sparks(at, c1, 60, 9.0f);
    Sparks(at, c2, 30, 5.0f);
    PlaySfx(SND_FIREWORK_BLAST, &at, 4.0f);
    PlaySfx(SND_FIREWORK_TWINKLE, &at, 4.0f);
}

// where the player's shots start (outside of the vehicle he sits in)
Vec3 ShotOrigin() {
    if (TheHost().PlayerVehicle() >= 0)
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
void AimFromPlayer(Projectile& pr) {
    Vec3 playerPos;
    const float speed = pr.vel.Length();
    if (!TheHost().PlayerPos(&playerPos) || speed < 0.1f)
        return;
    const bool seated = TheHost().PlayerVehicle() >= 0;
    Vec3 v0 = TheHost().PlayerVelocity();
    if (!seated && !gGame.flying && !gGame.gliding && TheHost().PlayerOnGround())
        v0.z = 0.0f; // standing on something: only the sideways motion goes with it
    const bool aim = seated || gGame.flying || gGame.gliding || gGame.cameraMode != CAM_FIRST;
    if (!aim) {
        pr.vel += v0;
        return;
    }
    // the spot under the crosshair
    const Vec3 origin = gGame.rayOrigin, dir = gGame.lookDir;
    const float reach = 120.0f;
    float dist = TheHost().AimDistance(origin, dir, reach, 60.0f); // (60 m ahead when there is nothing)
    const VoxelHit vh = RaycastVoxels(origin, dir, reach);
    if (vh.hit)
        dist = std::min(dist, vh.dist);
    const MobHit mh = MobsRaycast(origin, dir, reach);
    if (mh.index >= 0)
        dist = std::min(dist, mh.dist);
    const Vec3 to = origin + dir * dist - pr.pos;
    const float len = to.Length();
    if (len < 2.0f || (to.x * dir.x + to.y * dir.y + to.z * dir.z) < 0.5f * len) {
        pr.vel = dir * speed + v0; // too close to aim at
        return;
    }
    // w(t): the throw that is at the spot after t seconds; the right t makes it as fast as the throw can be
    const float g = ProjGravity(pr.type);
    auto need = [&](float t) { return to * (1.0f / t) - v0 + Vec3(0, 0, 0.5f * g * t); };
    float tFound = -1.0f, tBest = 0.05f, bestErr = 1e9f;
    float prev = 0.0f;
    for (float t = 0.02f; t < 8.0f; t *= 1.15f) {
        const float m = need(t).Length();
        if (std::fabs(m - speed) < bestErr) {
            bestErr = std::fabs(m - speed);
            tBest = t;
        }
        if (m <= speed) {
            // the first time fast enough: narrow it down between the last step and this one
            float lo = prev, hi = t;
            for (int i = 0; i < 20 && lo > 0.0f; ++i) {
                const float mid = (lo + hi) * 0.5f;
                if (need(mid).Length() <= speed)
                    hi = mid;
                else
                    lo = mid;
            }
            tFound = hi;
            break;
        }
        prev = t;
    }
    Vec3 w = need(tFound > 0.0f ? tFound : tBest); // out of reach: as far as it goes towards it
    const float wm = w.Length();
    if (wm < 1e-3f)
        w = dir;
    pr.vel = v0 + w * (speed / std::max(wm, 1e-3f));
}

// what the player shoots or throws: out of the vehicle he sits in, to the spot under the crosshair
void FromPlayer(Projectile& pr) {
    pr.vehicle = TheHost().PlayerVehicle();
    if (pr.vehicle >= 0)
        pr.ignoreTime = 1.0f;
    AimFromPlayer(pr);
}

void WindBurst(const Vec3& at) {
    Gust(at, 4.0f, 7.0f, 9.0f, true);
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

// first surface of the host's water along the look direction
bool LookAtWater(float maxDist, Vec3* at) {
    const Vec3 o = gGame.eyePos, d = gGame.lookDir;
    for (float t = 0.3f; t <= maxDist; t += 0.2f) {
        Vec3 p = o + d * t;
        if (gWorld.IsSolid(FloorI(p.x), FloorI(p.y), FloorI(p.z)))
            return false;
        float wl;
        if (TheHost().WaterLevel(p, &wl) && p.z <= wl) {
            *at = Vec3(p.x, p.y, wl);
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

float LookHeadingDeg() { return std::atan2(-gGame.lookDir.x, gGame.lookDir.y) * (180.0f / kPi); }

// ---------------------------------------------------------------- shooting
void ShootArrow(float speed, float damage, bool crit, bool pickup) {
    Projectile pr;
    pr.type = PJ_ARROW;
    pr.pos = ShotOrigin();
    pr.vel = gGame.lookDir * speed;
    pr.crit = crit;
    pr.damage = damage;
    pr.pickup = pickup;
    FromPlayer(pr);
    gProjectiles.push_back(pr);
}

void ShootBow(float power, bool crit) {
    ShootArrow(power * 60.0f, 0.0f, crit, gGame.gameMode == MODE_SURVIVAL);
    PlaySfx(SND_BOW_SHOOT, nullptr, 1.0f, 1.0f / (Rand01() * 0.4f + 1.2f) + power * 0.5f);
}

void ShootCrossbow() {
    if (gGame.crossbowRocket) {
        Projectile pr;
        pr.type = PJ_ROCKET;
        pr.pos = ShotOrigin();
        pr.vel = gGame.lookDir * 32.0f;
        pr.life = 3.0f;
        FromPlayer(pr);
        gProjectiles.push_back(pr);
        PlaySfx(SND_FIREWORK_LAUNCH, nullptr, 1.0f);
    } else {
        ShootArrow(63.0f, 9.0f, false, gGame.gameMode == MODE_SURVIVAL);
    }
    PlaySfx(SND_CROSSBOW_SHOOT, nullptr, 1.0f, 1.0f / (Rand01() * 0.5f + 1.8f) + 0.6f);
    DamageHeldItem(1);
    StartSwing();
}

void ThrowTrident() {
    ItemStack& held = gInv.Held();
    Projectile pr;
    pr.type = PJ_TRIDENT;
    pr.pos = ShotOrigin();
    pr.vel = gGame.lookDir * 50.0f;
    pr.damage = 8.0f;
    pr.stack = held;
    pr.slot = gInv.selected;
    pr.life = 30.0f;
    FromPlayer(pr);
    if (gGame.gameMode == MODE_SURVIVAL) {
        pr.stack.damage += 1;
        held.Clear();
    } else {
        pr.stack.Clear(); // creative keeps its trident
    }
    gProjectiles.push_back(pr);
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

// a firework under the elytra: true if it pushes the gliding player on
bool FireworkBoost() {
    if (!gGame.gliding)
        return false;
    gGame.boostTime = 1.0f + Rand01() * 0.6f;
    PlaySfx(SND_FIREWORK_LAUNCH);
    return true;
}
} // namespace

// ================================================================ public
void Gust(const Vec3& centre, float radius, float side, float up, bool playerToo) {
    TheHost().Gust(centre, radius, side, up, playerToo);
    MobsRadial(centre, radius, 0.0f, side + up);
    PushLooseThings(centre, radius, side);
}

void StrikeLightning(const Vec3& at) {
    gBolts.push_back({ at, 0.0f, (uint32_t)rand() * 2654435761u });
    gFlash = 0.25f;
    PlaySfx(SND_THUNDER, nullptr, 1.0f, 0.8f + Rand01() * 0.2f);
    PlaySfx(SND_LIGHTNING_IMPACT, &at, 2.0f, 0.5f + Rand01() * 0.2f);
    TheHost().Ignite(at, 6.0f, 1);
    ExplodeAt(at, 0.0f, true, BLAST_SMALL);
}

bool LightningFlashActive() { return gFlash > 0.0f; }

void LightningTick(float dt) {
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
}

bool MeleeAttack() {
    const ItemStack& held = gInv.Held();
    const ItemDef& d = Item(held.id);
    if (!held.Empty() && d.special == SP_WAND)
        return TheHost().ItemAttack(d.special); // (an item of the host's own)
    const Vec3 origin = gGame.rayOrigin, dir = gGame.lookDir;
    const float reach = (gGame.eyePos - origin).Length() + 3.5f;
    // what is in the way: the host's walls and people, our blocks, the animals
    const HostHit hh = TheHost().BlowTrace(origin, dir, reach);
    const float hostDist = hh.hit ? hh.dist : 1e9f;
    const VoxelHit vh = RaycastVoxels(origin, dir, reach);
    const float wall = vh.hit ? vh.dist : 1e9f;
    const MobHit mh = MobsRaycast(origin, dir, reach);

    const float mobD = mh.index >= 0 ? mh.dist : 1e9f;
    const float beingD = hh.being >= 0 ? hh.beingDist : 1e9f;
    Vec3 point = hh.beingPoint;
    const bool mobHit = mobD < wall && mobD < beingD && mobD <= hostDist + 0.01f && (mh.point - gGame.eyePos).Length() <= 3.6f;
    const bool beingHit = !mobHit && hh.being >= 0 && beingD < wall && (point - gGame.eyePos).Length() <= 3.6f;
    if (!mobHit && !beingHit)
        return false;
    if (mobHit)
        point = mh.point;

    float charge = AttackCharge();
    float base = held.Empty() ? 1.0f : d.damage;
    float dmg = base * (0.2f + 0.8f * charge * charge);
    bool crit = charge > 0.9f && TheHost().PlayerFalling() && !gGame.flying && !gGame.sprinting;
    if (crit)
        dmg *= 1.5f;
    bool sword = d.tool == TOOL_SWORD;
    float knock = (gGame.sprinting && charge > 0.9f) ? 1.6f : 1.0f;
    if (mobHit) {
        Vec3 playerPos;
        TheHost().PlayerPos(&playerPos);
        MobHurt(mh.index, dmg, playerPos, knock);
    } else {
        TheHost().HurtBeing(hh.being, dmg, sword ? HURT_SWORD : HURT_FIST, nullptr);
        Vec3 push = dir;
        push.z = 0;
        push = Norm(push);
        TheHost().PushBeing(hh.being, push * ((3.0f + 3.0f * charge) * knock) + Vec3(0, 0, 2.5f));
    }
    if (gGame.sprinting && charge > 0.9f)
        gGame.sprintLatch = false; // a knockback hit stops the sprint, like in Minecraft
    if (crit) {
        PlaySfx(SND_ATTACK_CRIT, &point);
        for (int i = 0; i < 12; ++i) {
            Particle p;
            p.pos = point;
            p.vel = Vec3(Rand01() - 0.5f, Rand01() - 0.5f, Rand01()) * 4.0f;
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
    gSurvival.exhaustion += kExhaustAttack;
    StartSwing();
    return true;
}

bool ChargedItemsTick(float dt, bool useDown, bool usePressed) {
    ItemStack& held = gInv.Held();
    const int special = held.Empty() ? 0 : Item(held.id).special;
    const bool creative = gGame.gameMode == MODE_CREATIVE;

    // spyglass
    const bool wasSpy = gGame.spyglass;
    gGame.spyglass = special == SP_SPYGLASS && useDown;
    if (gGame.spyglass && !wasSpy)
        PlaySfx(SND_SPYGLASS);

    // bow
    if (special == SP_BOW && (creative || gInv.CountOf(ID_ARROW) > 0)) {
        if (useDown) {
            if (gGame.bowDraw < 0.0f)
                gGame.bowDraw = 0.0f;
            gGame.bowDraw += dt;
            return true;
        }
        if (gGame.bowDraw >= 0.0f) {
            float t = std::min(gGame.bowDraw, 1.0f);
            float f = std::min(1.0f, (t * t + 2.0f * t) / 3.0f);
            if (f >= 0.1f) {
                ShootBow(f, f >= 1.0f);
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
            if (usePressed) {
                ShootCrossbow();
                gGame.crossbowSlot = -1;
            }
            return useDown;
        }
        const bool rocket = gInv.offhand.id == ID_FIREWORK_ROCKET;
        const bool ammo = creative || rocket || gInv.CountOf(ID_ARROW) > 0;
        if (useDown && ammo) {
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
        if (useDown) {
            if (gGame.tridentCharge < 0.0f)
                gGame.tridentCharge = 0.0f;
            gGame.tridentCharge += dt;
            return true;
        }
        if (gGame.tridentCharge >= 0.5f)
            ThrowTrident();
        gGame.tridentCharge = -1.0f;
    } else {
        gGame.tridentCharge = -1.0f;
    }
    return gGame.spyglass;
}

bool UseHeldItem() {
    ItemStack& held = gInv.Held();
    if (held.Empty())
        return false;
    const ItemDef& d = Item(held.id);
    const bool survival = gGame.gameMode == MODE_SURVIVAL;
    const bool inVehicle = TheHost().PlayerVehicle() >= 0;
    const bool targetValid = gTarget.valid, targetVoxel = gTarget.voxel;
    const Int3 targetPos = gTarget.pos;
    const Vec3 hitPoint = gTarget.point, hitNormal = gTarget.normal;
    Vec3 playerPos;
    TheHost().PlayerPos(&playerPos);
    auto consume = [&]() {
        if (survival && --held.count == 0)
            held.Clear();
    };
    if (held.id == ID_EXPERIENCE_BOTTLE) {
        // bottle o' enchanting: thrown, breaks into experience
        Projectile pr;
        pr.type = PJ_XPBOTTLE;
        pr.pos = ShotOrigin();
        pr.vel = gGame.lookDir * 14.0f + Vec3(0, 0, 4.0f);
        pr.life = 10.0f;
        FromPlayer(pr);
        gProjectiles.push_back(pr);
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
            pr.pos = ShotOrigin();
            pr.vel = gGame.lookDir * 24.0f;
            pr.life = 8.0f;
            FromPlayer(pr);
            gProjectiles.push_back(pr);
            PlaySfx(SND_FIREBALL, nullptr, 1.0f, 0.9f + Rand01() * 0.2f);
        } else {
            if (!targetValid)
                return false;
            const Vec3 q = hitPoint + hitNormal * 0.5f;
            if (!PlaceFire({ FloorI(q.x), FloorI(q.y), FloorI(q.z) })) {
                // the host's walls, its cars...: a fire of its own
                TheHost().Ignite(hitPoint + hitNormal * 0.3f, 10.0f, 1);
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
            pr.pos = playerPos;
            pr.fuse = gGame.boostTime;
            pr.life = gGame.boostTime;
            pr.pickup = true; // marks "attached to the player"
            gProjectiles.push_back(pr);
            consume();
            return true;
        }
        if (inVehicle) {
            // a rocket strapped to the vehicle
            TheHost().BoostVehicle(1.6f);
            PlaySfx(SND_FIREWORK_LAUNCH, nullptr, 1.0f);
            consume();
            return true;
        }
        if (!targetValid)
            return false;
        Projectile pr;
        pr.type = PJ_FIREWORK;
        pr.pos = hitPoint + hitNormal * 0.25f;
        pr.vel = Vec3((Rand01() - 0.5f) * 0.8f, (Rand01() - 0.5f) * 0.8f, 8.0f);
        pr.fuse = 1.2f + Rand01() * 0.5f;
        pr.life = 5.0f;
        gProjectiles.push_back(pr);
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
        pr.pos = ShotOrigin();
        pr.vel = gGame.lookDir * 30.0f;
        pr.life = 10.0f;
        FromPlayer(pr);
        gProjectiles.push_back(pr);
        PlaySfx(d.special == SP_ENDER_PEARL ? SND_PEARL_THROW : d.special == SP_WIND_CHARGE ? SND_WIND_THROW : SND_THROW);
        consume();
        StartSwing();
        return true;
    }
    case SP_WAND: // an item of the host's own
        return TheHost().ItemUse(d.special);
    case SP_FISHING_ROD:
        return !inVehicle && FishingUse();
    case SP_EGG_COW:
    case SP_EGG_PIG:
    case SP_EGG_SHEEP:
    case SP_EGG_CHICKEN: {
        if (!targetValid || inVehicle)
            return false;
        Vec3 at = hitPoint + hitNormal * (hitNormal.z > 0.5f ? 0.02f : 0.6f);
        float gz;
        if (GroundBelow(at + Vec3(0, 0, 0.5f), 4.0f, &gz))
            at.z = gz;
        if (SpawnMob(MobKindForEgg(d.special), at, true) < 0)
            return false;
        PlaySfx(SND_POP, &at, 0.5f);
        consume();
        StartSwing();
        return true;
    }
    case SP_WATER_BUCKET: {
        // (with nothing in reach it is poured out three metres ahead)
        Vec3 at = targetValid ? hitPoint : gGame.eyePos + gGame.lookDir * 3.0f;
        TheHost().Douse(at, 4.0f);
        TheHost().Douse(playerPos, 3.0f);
        for (int i = 0; i < 30; ++i) {
            Particle p;
            p.pos = at + Vec3((Rand01() - 0.5f) * 1.5f, (Rand01() - 0.5f) * 1.5f, Rand01() * 0.5f);
            p.vel = Vec3((Rand01() - 0.5f) * 3.0f, (Rand01() - 0.5f) * 3.0f, 1.0f + Rand01() * 3.0f);
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
        Vec3 at = hitPoint + hitNormal * 0.3f;
        TheHost().Ignite(at, 9.0f, 2);
        for (int i = 0; i < 3; ++i)
            TheHost().Ignite(at + Vec3((Rand01() - 0.5f) * 2.0f, (Rand01() - 0.5f) * 2.0f, 0.0f), 7.0f, 1);
        PlaySfx(SND_IGNITE, &at, 1.0f, 0.6f);
        ReplaceHeld(ID_BUCKET);
        StartSwing();
        return true;
    }
    case SP_BUCKET: {
        Vec3 water;
        if (inVehicle || !LookAtWater(5.0f, &water))
            return false;
        ReplaceHeld(ID_WATER_BUCKET);
        PlaySfx(SND_SPLASH, &water, 0.6f);
        StartSwing();
        return true;
    }
    case SP_BOAT: {
        Vec3 water;
        if (inVehicle || !LookAtWater(8.0f, &water)) {
            ShowMessage("Tekneyi suya koymalısın");
            return true;
        }
        if (!TheHost().PlaceVehicle(HOST_BOAT, water + Vec3(0, 0, 0.6f), LookHeadingDeg()))
            return false;
        consume();
        StartSwing();
        return true;
    }
    case SP_MINECART: {
        if (inVehicle || !targetValid || hitNormal.z < 0.5f)
            return false;
        if (!TheHost().PlaceVehicle(HOST_MINECART, hitPoint + Vec3(0, 0, 0.6f), LookHeadingDeg()))
            return false;
        consume();
        StartSwing();
        return true;
    }
    default:
        return false;
    }
}

void ProjectilesTick(float dt) {
    Vec3 playerPos;
    const bool player = TheHost().PlayerPos(&playerPos);
    for (size_t i = 0; i < gProjectiles.size();) {
        Projectile& pr = gProjectiles[i];
        bool remove = false;
        pr.life -= dt;
        pr.ignoreTime -= dt;
        if (pr.type == PJ_FIREWORK && pr.pickup) {
            // boosting rocket follows the gliding player
            if (player)
                pr.pos = playerPos - gGame.lookDir * 0.6f;
            Particle sp;
            sp.pos = pr.pos;
            sp.vel = Vec3(Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f);
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
            if (!player) {
                SpawnDrop(pr.pos, pr.stack);
                remove = true;
            } else {
                Vec3 to = playerPos + Vec3(0, 0, 0.4f) - pr.pos;
                float dist = to.Length();
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
            } else if (pr.pickup && player && (pr.pos - playerPos).Length() < 1.6f) {
                ItemStack s;
                s.id = ID_ARROW;
                s.count = 1;
                if (gInv.Add(s) == 0) {
                    PlaySfx(SND_PICKUP, nullptr, 0.4f, 1.6f);
                    remove = true;
                }
            }
        } else {
            pr.vel.z -= ProjGravity(pr.type) * dt;
            if (pr.type == PJ_ARROW || pr.type == PJ_TRIDENT)
                pr.vel = pr.vel * std::pow(0.99f, dt * 20.0f);
            if (pr.type == PJ_ROCKET)
                pr.vel = pr.vel * (1.0f + dt * 1.2f);
            Vec3 step = pr.vel * dt;
            Vec3 next = pr.pos + step;
            if (pr.type == PJ_FIREWORK) {
                Particle sp;
                sp.pos = pr.pos;
                sp.vel = Vec3(0, 0, -1.0f);
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
                    sp.vel = Vec3(Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f) * 0.6f;
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
                    sp.vel = Vec3(0, 0, 0);
                    sp.maxLife = sp.life = 0.3f;
                    sp.tile = TILE_P_CRITICAL_HIT;
                    sp.size = 0.06f;
                    sp.gravity = 0.0f;
                    sp.glow = true;
                    SpawnParticle(sp);
                }

                const Vec3 dir = Norm(step);
                const float len = step.Length();
                // what is in the way: our blocks, what the host has in front of them, the animals
                const VoxelHit vh = RaycastVoxels(pr.pos, dir, len);
                ShotOwner by;
                by.hostile = pr.hostile;
                by.shooter = pr.shooter;
                by.vehicle = pr.ignoreTime > 0.0f ? pr.vehicle : -1;
                const HostHit hh = TheHost().ShotTrace(pr.pos, dir, vh.hit ? std::min(len, vh.dist) : len, by);
                bool hit = hh.hit;
                float hitDist = hit ? hh.dist : 1e9f;
                Vec3 point = hh.point;
                int vehicle = hh.vehicle;
                if (vh.hit && vh.dist < hitDist) {
                    hit = true;
                    vehicle = -1;
                    hitDist = vh.dist;
                    point = pr.pos + dir * vh.dist;
                }
                MobHit mh = MobsRaycast(pr.pos, dir, len);
                const float speedBt = pr.vel.Length() / 20.0f;
                const bool mobHit = mh.index >= 0 && mh.dist < hitDist && (hh.being < 0 || mh.dist <= hh.beingDist);
                int victim = -1; // somebody of the host's
                if (!mobHit && hh.being >= 0) {
                    victim = hh.being;
                    hit = true;
                    point = hh.beingPoint;
                }
                const float arrowDmg = pr.damage > 0.0f ? pr.damage : std::ceil(speedBt * 2.0f);
                bool impact = false;
                if (mobHit) {
                    pr.pos = mh.point;
                    const Vec3 from = pr.pos - dir * 3.0f;
                    if (pr.type == PJ_ARROW || pr.type == PJ_TRIDENT) {
                        MobHurt(mh.index, arrowDmg + (pr.crit ? (float)(rand() % 5) : 0.0f), from, 0.7f);
                        PlaySfx(pr.type == PJ_TRIDENT ? SND_TRIDENT_HIT : SND_ARROW_HIT, &pr.pos);
                    } else if (pr.type != PJ_PEARL) {
                        MobHurt(mh.index, 0.0f, from, 0.5f);
                    }
                    impact = true;
                } else if (!hit) {
                    pr.pos = next;
                } else if (victim >= 0) {
                    pr.pos = point;
                    if (pr.type == PJ_ARROW || pr.type == PJ_TRIDENT) {
                        float dmg = arrowDmg + (pr.crit ? (float)(rand() % 5) : 0.0f);
                        TheHost().HurtBeing(victim, dmg, HURT_SHOT, &by);
                        TheHost().PushBeing(victim, dir * 2.0f);
                        PlaySfx(pr.type == PJ_TRIDENT ? SND_TRIDENT_HIT : SND_ARROW_HIT, &point);
                    } else if (pr.type == PJ_SNOWBALL || pr.type == PJ_EGG) {
                        TheHost().PushBeing(victim, dir * 2.5f);
                    }
                    impact = true;
                } else {
                    pr.pos = point - dir * 0.05f;
                    if (pr.type == PJ_ARROW && vehicle < 0) {
                        pr.stuck = true;
                        pr.life = pr.hostile ? 10.0f : 60.0f;
                        PlaySfx(SND_ARROW_HIT, &pr.pos);
                    } else if (pr.type == PJ_ARROW) {
                        // arrows do not stick in vehicles, but a hail of them wears one down
                        TheHost().HurtVehicle(vehicle, arrowDmg);
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
                        if (TheHost().Thunderstorm() || gGame.gameMode == MODE_CREATIVE)
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
                            Vec3 at = pr.pos - dir * 0.3f;
                            float gz;
                            if (GroundBelow(at + Vec3(0, 0, 0.5f), 6.0f, &gz))
                                at.z = gz;
                            SpawnMob(MOB_CHICKEN, at, true, true);
                        }
                        remove = true;
                        break;
                    case PJ_PEARL:
                        if (player && TheHost().PlayerVehicle() < 0) {
                            TheHost().MovePlayer(pr.pos - dir * 0.5f + Vec3(0, 0, 1.0f));
                            PlaySfx(SND_TELEPORT);
                            if (gGame.gameMode == MODE_SURVIVAL)
                                TheHost().HurtPlayer(5.0f); // (a quarter of his health)
                        }
                        remove = true;
                        break;
                    case PJ_FIREBALL:
                        ExplodeAt(pr.pos - dir * 0.3f, 1.3f, true, BLAST_FIRE);
                        remove = true;
                        break;
                    case PJ_WIND:
                        WindBurst(pr.pos - dir * 0.3f);
                        remove = true;
                        break;
                    case PJ_ROCKET:
                        FireworkBlast(pr.pos);
                        ExplodeAt(pr.pos - dir * 0.3f, 0.0f, true, BLAST_SMALL);
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
            gProjectiles[i] = gProjectiles.back();
            gProjectiles.pop_back();
        } else {
            ++i;
        }
    }
}

void ShootArrowAt(const Vec3& from, const Vec3& to, int shooter, int vehicle) {
    if (gProjectiles.size() > 350)
        return;
    Vec3 d = to - from;
    float dist = d.Length();
    if (dist < 0.5f)
        return;
    const float speed = 48.0f;
    // aim a little above the target so gravity brings the arrow down onto it, plus some spread
    float t = dist / speed;
    Vec3 aim = to + Vec3(0, 0, 0.5f * 20.0f * t * t);
    aim += Vec3((Rand01() - 0.5f), (Rand01() - 0.5f), (Rand01() - 0.5f)) * (0.03f * dist + 0.2f);
    Vec3 dir = Norm(aim - from);
    Projectile pr;
    pr.type = PJ_ARROW;
    pr.pos = from + dir * 0.5f;
    pr.vel = dir * speed;
    pr.hostile = true;
    pr.damage = 4.0f + Rand01() * 2.0f;
    pr.life = 10.0f;
    pr.shooter = shooter;
    pr.vehicle = vehicle;
    gProjectiles.push_back(pr);
    PlaySfx(SND_CROSSBOW_SHOOT, &from, 1.5f, 0.9f + Rand01() * 0.2f);
}

void CombatClear() {
    gProjectiles.clear();
    gBolts.clear();
}

} // namespace mc
