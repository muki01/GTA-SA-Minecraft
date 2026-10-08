#include "Fishing.h"

#include <cstdlib>

#include "Audio.h"
#include "Entities.h"
#include "GameState.h"
#include "Inventory.h"
#include "Items.h"
#include "Mobs.h"
#include "Particles.h"
#include "Survival.h"
#include "World.h"

namespace mc {

Bobber gBobber;

namespace {
float Pitch() { return 0.4f / (Rand01() * 0.4f + 0.8f); }

void WaterParticle(const Vec3& at, uint16_t tile, float up, float spread) {
    Particle p;
    p.pos = at + Vec3((Rand01() - 0.5f) * spread, (Rand01() - 0.5f) * spread, 0.05f);
    p.vel = Vec3((Rand01() - 0.5f) * 0.6f, (Rand01() - 0.5f) * 0.6f, up * (0.5f + Rand01()));
    p.maxLife = p.life = 0.35f + Rand01() * 0.35f;
    p.tile = tile;
    p.size = 0.07f;
    p.gravity = 6.0f;
    SpawnParticle(p);
}

ItemStack RollLoot() {
    ItemStack s;
    s.count = 1;
    float r = Rand01();
    if (r < 0.85f) {
        float f = Rand01();
        s.id = f < 0.6f ? ID_COD : (f < 0.85f ? ID_SALMON : (f < 0.87f ? ID_TROPICAL_FISH : ID_PUFFERFISH));
    } else if (r < 0.95f) {
        static const uint16_t junk[] = { ID_LEATHER, ID_BONE, ID_STRING, ID_BOWL, ID_STICK, ID_INK_SAC, ID_ROTTEN_FLESH,
                                         ID_FISHING_ROD };
        s.id = junk[rand() % 8];
        if (s.id == ID_FISHING_ROD)
            s.damage = (uint16_t)(Item(ID_FISHING_ROD).durability * (0.3f + Rand01() * 0.6f));
    } else {
        static const uint16_t treasure[] = { ID_NAME_TAG, ID_SADDLE, ID_BOW, ID_NAUTILUS_SHELL };
        s.id = treasure[rand() % 4];
    }
    return s;
}

void StartFloating(float waterZ) {
    gBobber.state = BOB_FLOATING;
    gBobber.waterZ = waterZ;
    gBobber.pos.z = waterZ;
    gBobber.vel = Vec3(0, 0, 0);
    gBobber.wait = 4.0f + Rand01() * 14.0f;
    gBobber.dip = 0.15f;
    for (int i = 0; i < 6; ++i)
        WaterParticle(gBobber.pos, TILE_P_SPLASH_0 + rand() % 4, 2.0f, 0.3f);
    PlaySfx(SND_BOBBER_SPLASH, &gBobber.pos, 0.2f, 1.2f + Rand01() * 0.4f);
}
} // namespace

bool FishingIsCast() { return gBobber.active; }
void FishingClear() { gBobber = Bobber(); }

bool FishingUse() {
    Vec3 playerPos;
    if (!TheHost().PlayerPos(&playerPos))
        return false;
    if (!gBobber.active) {
        gBobber = Bobber();
        gBobber.active = true;
        const Vec3 look = gGame.lookDir;
        gBobber.pos = gGame.eyePos + look * 0.7f - Vec3(0, 0, 0.1f);
        gBobber.vel = look * 21.0f + Vec3(0, 0, 3.0f);
        PlaySfx(SND_BOBBER_THROW, nullptr, 0.5f, Pitch());
        StartSwing();
        return true;
    }
    // reel in: whatever hangs on the hook flies towards the player
    const Vec3 d = playerPos - gBobber.pos;
    const float dist = d.Length();
    const Vec3 pull(d.x * 2.0f, d.y * 2.0f, d.z * 2.0f + std::sqrt(dist) * 1.6f);
    int wear = 0;
    if (gBobber.state == BOB_FLOATING && gBobber.nibble > 0.0f) {
        SpawnDrop(gBobber.pos + Vec3(0, 0, 0.3f), RollLoot(), pull, 0.3f);
        SpawnXp(playerPos + Vec3(0, 0, -0.5f), 1 + rand() % 6);
        for (int i = 0; i < 8; ++i)
            WaterParticle(gBobber.pos, TILE_P_SPLASH_0 + rand() % 4, 3.0f, 0.4f);
        gSurvival.exhaustion += 0.05f;
        wear = 1;
    } else if (gBobber.state == BOB_HOOKED_MOB) {
        int i = MobIndexById(gBobber.mobId);
        if (i >= 0)
            MobPush(i, Vec3(pull.x * 0.7f, pull.y * 0.7f, 5.0f + dist * 0.3f));
        wear = 3;
    } else if (gBobber.state == BOB_HOOKED_BEING) {
        TheHost().Fling(gBobber.hooked, Vec3(d.x * 1.5f, d.y * 1.5f, 6.0f + dist * 0.4f));
        wear = 3;
    } else if (gBobber.state == BOB_HOOKED_VEHICLE || gBobber.state == BOB_HOOKED_OBJECT) {
        const float k = gBobber.state == BOB_HOOKED_VEHICLE ? 0.9f : 1.3f;
        if (TheHost().Fling(gBobber.hooked, Vec3(d.x * k, d.y * k, 5.0f + dist * 0.3f)))
            wear = 5;
    } else if (gBobber.state == BOB_STUCK) {
        wear = 2;
    }
    // items lying around the hook come along
    for (auto& dr : gDrops)
        if ((dr.pos - gBobber.pos).Length() < 2.0f) {
            Vec3 to = playerPos - dr.pos;
            dr.vel = Vec3(to.x * 1.6f, to.y * 1.6f, 4.0f + to.Length() * 0.3f);
            dr.pickupDelay = std::min(dr.pickupDelay, dr.age);
        }
    if (wear)
        DamageHeldItem(wear);
    PlaySfx(SND_BOBBER_RETRIEVE, nullptr, 1.0f, Pitch());
    gBobber.active = false;
    StartSwing();
    return true;
}

void FishingTick(float dt) {
    if (!gBobber.active)
        return;
    const ItemStack& held = gInv.Held();
    const bool holding = !held.Empty() && Item(held.id).special == SP_FISHING_ROD;
    Vec3 playerPos;
    if (!TheHost().PlayerPos(&playerPos) || TheHost().PlayerVehicle() >= 0 || !holding ||
        (gBobber.pos - playerPos).Length() > 40.0f) {
        gBobber.active = false;
        return;
    }
    gBobber.age += dt;

    switch (gBobber.state) {
    case BOB_FLYING: {
        gBobber.vel.z -= 12.0f * dt;
        gBobber.vel = gBobber.vel * std::pow(0.92f, dt * 20.0f);
        const Vec3 step = gBobber.vel * dt;
        const float len = step.Length();
        if (len < 1e-5f)
            break;
        const Vec3 dir = step * (1.0f / len);
        const Vec3 next = gBobber.pos + step;

        float best = len;
        int what = 0; // 1 a wall or a block, 2 somebody of the host's, 3 an animal, 4 water, 5 a vehicle, 6 a loose object
        const HostHit hh = TheHost().HookTrace(gBobber.pos, dir, len);
        if (hh.hit && hh.dist < best) {
            best = hh.dist;
            what = hh.vehicle >= 0 ? 5 : hh.object >= 0 ? 6 : 1;
        }
        VoxelHit vh = RaycastVoxels(gBobber.pos, dir, len);
        if (vh.hit && vh.dist < best) {
            best = vh.dist;
            what = 1;
        }
        if (hh.being >= 0 && hh.beingDist < best) {
            best = hh.beingDist;
            what = 2;
        }
        MobHit mh = MobsRaycast(gBobber.pos, dir, len);
        if (mh.index >= 0 && mh.dist < best) {
            best = mh.dist;
            what = 3;
        }
        float wl = 0.0f;
        if (TheHost().WaterLevel(next, &wl) && next.z <= wl && gBobber.pos.z >= wl - 0.3f &&
            step.z < 0.0f) {
            float d = Clamp((gBobber.pos.z - wl) / -step.z, 0.0f, 1.0f) * len;
            if (d <= best) {
                best = d;
                what = 4;
            }
        }
        gBobber.pos = gBobber.pos + dir * std::max(0.0f, best - (what == 1 ? 0.05f : 0.0f));
        switch (what) {
        case 1:
            gBobber.state = BOB_STUCK;
            gBobber.vel = Vec3(0, 0, 0);
            break;
        case 2:
            gBobber.state = BOB_HOOKED_BEING;
            gBobber.hooked = HostHit();
            gBobber.hooked.being = hh.being;
            break;
        case 3:
            gBobber.state = BOB_HOOKED_MOB;
            gBobber.mobId = MobIdAt(mh.index);
            break;
        case 4:
            StartFloating(wl);
            break;
        case 5:
        case 6:
            gBobber.state = what == 5 ? BOB_HOOKED_VEHICLE : BOB_HOOKED_OBJECT;
            gBobber.hooked = HostHit();
            gBobber.hooked.vehicle = hh.vehicle;
            gBobber.hooked.object = hh.object;
            gBobber.hooked.local = hh.local;
            break;
        default:
            break;
        }
        break;
    }
    case BOB_FLOATING: {
        float wl;
        if (TheHost().WaterLevel(gBobber.pos, &wl))
            gBobber.waterZ = wl;
        gBobber.dip *= std::pow(0.05f, dt);
        gBobber.pos.z = gBobber.waterZ + std::sin(gBobber.age * 3.0f) * 0.025f - gBobber.dip;
        const Vec3 surface(gBobber.pos.x, gBobber.pos.y, gBobber.waterZ);
        if (gBobber.nibble > 0.0f) {
            gBobber.nibble -= dt;
            if (rand() % 3 == 0)
                WaterParticle(surface, TILE_P_BUBBLE, 1.0f, 0.3f);
            if (gBobber.nibble <= 0.0f)
                gBobber.wait = 4.0f + Rand01() * 10.0f; // it got away
        } else if (gBobber.approach > 0.0f) {
            gBobber.approach -= dt;
            // the wake of the fish coming closer
            float r = gBobber.approach * 1.6f;
            Vec3 at = surface + Vec3(std::cos(gBobber.approachAngle) * r, std::sin(gBobber.approachAngle) * r, 0.0f);
            if (rand() % 2 == 0)
                WaterParticle(at, TILE_P_SPLASH_0 + rand() % 4, 0.8f, 0.12f);
            if (gBobber.approach <= 0.0f) {
                gBobber.nibble = 1.0f + Rand01() * 1.0f;
                gBobber.dip = 0.3f;
                PlaySfx(SND_BOBBER_SPLASH, &gBobber.pos, 0.4f, 1.0f + (Rand01() - Rand01()) * 0.4f);
                for (int i = 0; i < 10; ++i)
                    WaterParticle(surface, i % 2 ? TILE_P_BUBBLE : TILE_P_SPLASH_0 + rand() % 4, 2.5f, 0.35f);
            }
        } else {
            gBobber.wait -= dt;
            if (gBobber.wait <= 0.0f) {
                gBobber.approach = 1.0f + Rand01() * 2.0f;
                gBobber.approachAngle = Rand01() * 6.2831853f;
            }
        }
        break;
    }
    case BOB_HOOKED_MOB: {
        int i = MobIndexById(gBobber.mobId);
        if (i < 0) {
            gBobber.state = BOB_STUCK;
            break;
        }
        gBobber.pos = MobCentre(i) + Vec3(0, 0, 0.2f);
        break;
    }
    case BOB_HOOKED_BEING:
    case BOB_HOOKED_VEHICLE:
    case BOB_HOOKED_OBJECT: {
        Vec3 at;
        if (!TheHost().HookPoint(gBobber.hooked, &at)) {
            gBobber.state = BOB_STUCK;
            break;
        }
        gBobber.pos = at;
        break;
    }
    default:
        break;
    }
}

} // namespace mc
