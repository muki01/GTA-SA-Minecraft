#include "Mobs.h"

#include <cstdlib>

#include "Audio.h"
#include "BlockRules.h"
#include "Entities.h"
#include "GameState.h"
#include "Host.h"
#include "Inventory.h"
#include "Items.h"
#include "Particles.h"

namespace mc {

namespace {
struct MobType {
    float width, height, health, walk, run;
    SoundEvent say, hurt, death;
    uint16_t food[3]; // what it follows and eats
};
const MobType kTypes[MOB_KIND_COUNT] = {
    { 0.9f, 1.4f, 10.0f, 1.25f, 3.4f, SND_COW_SAY, SND_COW_HURT, SND_COW_DEATH, { ID_WHEAT, 0, 0 } },
    { 0.9f, 0.9f, 10.0f, 1.25f, 3.4f, SND_PIG_SAY, SND_PIG_HURT, SND_PIG_DEATH, { ID_CARROT, ID_POTATO, ID_BEETROOT } },
    { 0.9f, 1.3f, 8.0f, 1.15f, 3.2f, SND_SHEEP_SAY, SND_SHEEP_HURT, SND_SHEEP_DEATH, { ID_WHEAT, 0, 0 } },
    { 0.4f, 0.7f, 4.0f, 1.0f, 2.8f, SND_CHICKEN_SAY, SND_CHICKEN_HURT, SND_CHICKEN_DEATH,
      { ID_WHEAT_SEEDS, ID_MELON_SEEDS, ID_PUMPKIN_SEEDS } },
};

uint32_t gNextMobId = 1;
float gSpawnTimer = 3.0f;
} // namespace

std::vector<Mob> gMobs;

float MobScale(const Mob& m) { return m.baby > 0.0f ? 0.5f : 1.0f; }
float MobWidth(const Mob& m) { return kTypes[m.kind].width * MobScale(m); }
float MobHeight(const Mob& m) { return kTypes[m.kind].height * MobScale(m); }

namespace {

Vec3 Flat(Vec3 v) {
    v.z = 0.0f;
    float m = v.Length();
    return m > 1e-4f ? v * (1.0f / m) : Vec3(0, 1, 0);
}

Vec3 RandomDir() {
    float a = Rand01() * 6.2831853f;
    return Vec3(std::cos(a), std::sin(a), 0.0f);
}

float WrapAngle(float a) {
    while (a > kPi)
        a -= 2.0f * kPi;
    while (a < -kPi)
        a += 2.0f * kPi;
    return a;
}

bool IsFood(const Mob& m, uint16_t id) {
    if (!id)
        return false;
    for (uint16_t f : kTypes[m.kind].food)
        if (f == id)
            return true;
    return false;
}

void Puff(const Vec3& at, uint16_t tile, int count, float spread, uint32_t color = 0xFFFFFFFF, bool smoke = false) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.pos = at + Vec3((Rand01() - 0.5f) * spread, (Rand01() - 0.5f) * spread, (Rand01() - 0.5f) * spread);
        p.vel = Vec3((Rand01() - 0.5f) * 1.2f, (Rand01() - 0.5f) * 1.2f, Rand01() * 1.2f);
        p.maxLife = p.life = 0.6f + Rand01() * 0.6f;
        p.tile = tile;
        p.size = smoke ? 0.14f : 0.1f;
        p.gravity = smoke ? -0.6f : 0.0f;
        p.color = color;
        p.anim = smoke ? 2 : 0;
        SpawnParticle(p);
    }
}

void DropLoot(const Mob& m) {
    if (m.baby > 0.0f)
        return;
    Vec3 at = m.pos + Vec3(0, 0, MobHeight(m) * 0.5f);
    SpawnXp(at, 1 + rand() % 3);
    auto drop = [&](uint16_t id, int lo, int hi) {
        int n = lo + (hi > lo ? rand() % (hi - lo + 1) : 0);
        if (m.burn > 0.0f) {
            // died on fire: the meat comes out cooked
            id = id == ID_BEEF ? ID_COOKED_BEEF : id == ID_PORKCHOP ? ID_COOKED_PORKCHOP
               : id == ID_MUTTON ? ID_COOKED_MUTTON : id == ID_CHICKEN ? ID_COOKED_CHICKEN : id;
        }
        if (n > 0)
            SpawnDropItem(at, id, n);
    };
    switch (m.kind) {
    case MOB_COW:
        drop(ID_LEATHER, 0, 2);
        drop(ID_BEEF, 1, 3);
        break;
    case MOB_PIG:
        drop(ID_PORKCHOP, 1, 3);
        break;
    case MOB_SHEEP:
        drop(ID_MUTTON, 1, 2);
        if (!m.sheared)
            drop(ID_WHITE_WOOL, 1, 1);
        break;
    case MOB_CHICKEN:
        drop(ID_FEATHER, 0, 2);
        drop(ID_CHICKEN, 1, 1);
        break;
    }
}

bool VoxelSolidAt(const Vec3& p) { return gWorld.IsSolid(FloorI(p.x), FloorI(p.y), FloorI(p.z)); }

void Kill(Mob& m);

void MoveMob(Mob& m, float dt) {
    const float h = MobHeight(m), r = MobWidth(m) * 0.5f;
    Vec3 np = m.pos + m.vel * dt;
    Vec3 hv(m.vel.x, m.vel.y, 0.0f);
    const float hs = hv.Length();
    bool blocked = false;
    if (hs > 0.05f) {
        const Vec3 dir = hv * (1.0f / hs);
        const Vec3 probe = Vec3(np.x, np.y, m.pos.z) + dir * r;
        const bool low = VoxelSolidAt(probe + Vec3(0, 0, 0.3f));
        const bool high = VoxelSolidAt(probe + Vec3(0, 0, 1.3f)) || VoxelSolidAt(m.pos + Vec3(0, 0, h + 0.9f));
        if (low) {
            blocked = true;
            if (!high && m.onGround && m.moving)
                m.vel.z = 8.4f; // hop onto the block
        } else if (h > 1.0f && VoxelSolidAt(probe + Vec3(0, 0, h - 0.1f))) {
            blocked = true;
        }
        if (!blocked) {
            // walls, cars and props of the host's map
            Vec3 a = m.pos + Vec3(0, 0, std::max(0.35f, h * 0.5f));
            Vec3 b = a + dir * (r + 0.2f + hs * dt);
            if (TheHost().LineBlocked(a, b))
                blocked = true;
        }
    }
    if (blocked) {
        np.x = m.pos.x;
        np.y = m.pos.y;
        m.vel.x *= 0.3f;
        m.vel.y *= 0.3f;
        if (m.aiTimer > 0.4f)
            m.aiTimer = 0.4f; // think again soon
    }

    const bool movedFlat = np.x != m.pos.x || np.y != m.pos.y;
    if (movedFlat || !m.onGround || --m.groundTick <= 0) {
        m.groundTick = 12;
        float gz;
        if (GroundBelow(Vec3(np.x, np.y, np.z + 0.7f), 120.0f, &gz)) {
            if (m.onGround && gz > m.pos.z + 0.7f) {
                // a step that is too high: do not walk through it
                np.x = m.pos.x;
                np.y = m.pos.y;
                if (m.aiTimer > 0.4f)
                    m.aiTimer = 0.4f;
            } else {
                m.groundZ = gz;
            }
        } else {
            m.groundZ = -1000.0f;
        }
    }

    if (np.z <= m.groundZ + 0.001f && m.vel.z <= 0.0f) {
        if (!m.onGround && m.fallFrom > -900.0f && m.kind != MOB_CHICKEN && m.death < 0.0f) {
            float fall = m.fallFrom - m.groundZ;
            if (fall > 3.5f) {
                m.health -= fall - 3.0f;
                m.hurt = 0.5f;
            }
        }
        np.z = m.groundZ;
        m.vel.z = 0.0f;
        m.onGround = true;
        m.fallFrom = -1000.0f;
    } else {
        if (m.onGround || m.fallFrom < -900.0f || np.z > m.fallFrom)
            m.fallFrom = np.z;
        m.onGround = false;
    }

    // swim: keep the head above the water
    float wl;
    if (TheHost().WaterLevel(np, &wl) && wl > np.z + h * 0.55f) {
        float target = wl - h * 0.55f;
        m.vel.z = Clamp((target - np.z) * 6.0f, -3.0f, 3.0f);
        m.onGround = true; // can keep walking / turning
        m.fallFrom = -1000.0f;
        np.z += m.vel.z * dt;
    }
    // placed water, lava and fire
    const int fluid = FluidAt(np + Vec3(0, 0, h * 0.4f));
    const bool inFire = FireAt(np + Vec3(0, 0, 0.2f));
    float hostLevel;
    const bool hostWater = TheHost().WaterLevel(np, &hostLevel) && hostLevel > np.z + 0.2f;
    if (fluid == ID_WATER || hostWater) {
        m.burn = 0.0f; // water puts it out
        if (fluid == ID_WATER) {
            m.vel.z = std::min(m.vel.z + 40.0f * dt, 2.0f);
            m.vel.x *= std::pow(0.8f, dt * 20.0f);
            m.vel.y *= std::pow(0.8f, dt * 20.0f);
            const Vec3 flow = FluidFlowAt({ FloorI(np.x), FloorI(np.y), FloorI(np.z + h * 0.4f) });
            m.vel.x += flow.x * 5.6f * dt;
            m.vel.y += flow.y * 5.6f * dt;
            m.onGround = true;
            m.fallFrom = -1000.0f;
        }
    } else if ((fluid == ID_LAVA || inFire) && m.death < 0.0f) {
        m.burn = std::max(m.burn, fluid == ID_LAVA ? 15.0f : 8.0f);
        m.lavaTimer -= dt;
        if (m.lavaTimer <= 0.0f) {
            m.lavaTimer = 0.5f;
            m.health -= fluid == ID_LAVA ? 4.0f : 1.0f;
            m.hurt = 0.5f;
            m.panic = 4.0f;
            if (m.health <= 0.0f)
                Kill(m);
        }
    }
    if (m.burn > 0.0f && m.death < 0.0f) {
        // on fire: one point of damage a second, and flames all over
        const float before = m.burn;
        m.burn -= dt;
        if (std::floor(before) != std::floor(std::max(0.0f, m.burn))) {
            m.health -= 1.0f;
            m.hurt = 0.5f;
            m.panic = std::max(m.panic, 2.0f);
            if (m.health <= 0.0f)
                Kill(m);
        }
        if (Rand01() < 12.0f * dt) {
            Particle f;
            f.pos = np + Vec3((Rand01() - 0.5f) * MobWidth(m), (Rand01() - 0.5f) * MobWidth(m), Rand01() * h);
            f.vel = Vec3(0, 0, 0.6f + Rand01() * 0.6f);
            f.maxLife = f.life = 0.4f + Rand01() * 0.3f;
            f.tile = TILE_P_FLAME;
            f.size = 0.08f + Rand01() * 0.06f;
            f.gravity = 0.0f;
            f.glow = true;
            SpawnParticle(f);
        }
    }
    m.pos = np;
}

void Think(Mob& m, float dt, bool playerOnFoot, const Vec3& playerPos, uint16_t playerHeld) {
    const MobType& t = kTypes[m.kind];
    m.aiTimer -= dt;
    float speed = 0.0f;
    Vec3 toPlayer = playerPos - m.pos;
    toPlayer.z = 0.0f;
    const float playerDist = toPlayer.Length();
    bool looking = false;

    if (m.panic > 0.0f) {
        m.panic -= dt;
        if (m.aiTimer <= 0.0f) {
            m.goal = RandomDir();
            m.aiTimer = 0.7f + Rand01() * 0.8f;
        }
        m.moving = true;
        speed = t.run;
    } else if (playerOnFoot && playerDist < 10.0f && IsFood(m, playerHeld)) {
        // TemptGoal
        m.goal = Flat(toPlayer);
        m.moving = playerDist > 2.4f;
        speed = t.walk * 1.25f;
        looking = true;
    } else {
        if (m.aiTimer <= 0.0f) {
            if (Rand01() < 0.45f) {
                m.moving = true;
                m.goal = RandomDir();
                m.aiTimer = 1.5f + Rand01() * 3.5f;
            } else {
                m.moving = false;
                m.aiTimer = 2.0f + Rand01() * 5.0f;
            }
        }
        speed = t.walk;
        looking = !m.moving && playerDist < 7.0f;
    }
    if (m.baby > 0.0f)
        speed *= 1.1f;

    // turn the body towards where it walks
    if (m.moving) {
        float want = std::atan2(-m.goal.x, m.goal.y);
        float d = WrapAngle(want - m.yaw);
        float maxTurn = 7.0f * dt;
        m.yaw = WrapAngle(m.yaw + Clamp(d, -maxTurn, maxTurn));
    }
    Vec3 fwd(-std::sin(m.yaw), std::cos(m.yaw), 0.0f);
    Vec3 target = m.moving ? fwd * speed : Vec3(0, 0, 0);
    float k = Clamp(dt * (m.onGround ? 9.0f : 1.2f), 0.0f, 1.0f);
    m.vel.x += (target.x - m.vel.x) * k;
    m.vel.y += (target.y - m.vel.y) * k;

    // head: look at the player now and then
    float wantYaw = 0.0f, wantPitch = 0.0f;
    if (looking && playerDist > 0.3f) {
        Vec3 dir = toPlayer * (1.0f / playerDist);
        Vec3 right(fwd.y, -fwd.x, 0.0f);
        wantYaw = Clamp(std::atan2(dir.x * right.x + dir.y * right.y, dir.x * fwd.x + dir.y * fwd.y), -1.1f, 1.1f);
        float dz = (playerPos.z + 0.6f) - (m.pos.z + MobHeight(m) * 0.9f);
        wantPitch = Clamp(-std::atan2(dz, playerDist), -0.6f, 0.6f);
    }
    float hk = Clamp(dt * 6.0f, 0.0f, 1.0f);
    m.headYaw += (wantYaw - m.headYaw) * hk;
    m.headPitch += (wantPitch - m.headPitch) * hk;
}

void Kill(Mob& m) {
    if (m.death >= 0.0f)
        return;
    m.death = 0.0f;
    m.health = 0.0f;
    m.moving = false;
    Vec3 c = m.pos + Vec3(0, 0, MobHeight(m) * 0.5f);
    PlaySfx(kTypes[m.kind].death, &c, 1.0f, m.baby > 0.0f ? 1.5f : 0.9f + Rand01() * 0.2f);
}

void TrySpawn(const Vec3& playerPos) {
    if (!gRules.animals || !TheHost().Outdoors())
        return;
    int wild = 0;
    for (auto& m : gMobs)
        if (!m.persistent)
            ++wild;
    if (wild >= gRules.maxAnimals)
        return;
    float a = Rand01() * 6.2831853f, dist = 30.0f + Rand01() * 45.0f;
    Vec3 from(playerPos.x + std::cos(a) * dist, playerPos.y + std::sin(a) * dist, playerPos.z + 30.0f);
    Vec3 ground;
    if (!TheHost().SpawnGround(from, &ground))
        return;
    float r = Rand01();
    int kind = r < 0.3f ? MOB_COW : (r < 0.55f ? MOB_SHEEP : (r < 0.78f ? MOB_PIG : MOB_CHICKEN));
    int pack = 2 + rand() % 3;
    for (int i = 0; i < pack && wild < gRules.maxAnimals; ++i) {
        Vec3 p = ground;
        if (i > 0) {
            Vec3 f(ground.x + (Rand01() - 0.5f) * 8.0f, ground.y + (Rand01() - 0.5f) * 8.0f, ground.z + 8.0f);
            if (!TheHost().SpawnGround(f, &p))
                continue;
        }
        if (SpawnMob(kind, p, false) >= 0)
            ++wild;
    }
}
} // namespace

uint32_t MobIdAt(int index) { return index >= 0 && index < (int)gMobs.size() ? gMobs[index].id : 0; }

int MobIndexById(uint32_t id) {
    if (!id)
        return -1;
    for (size_t i = 0; i < gMobs.size(); ++i)
        if (gMobs[i].id == id)
            return (int)i;
    return -1;
}

bool MobRide(uint32_t id, const Vec3& velocity, Vec3* seat, float* yaw) {
    int i = MobIndexById(id);
    if (i < 0 || gMobs[i].death >= 0.0f)
        return false;
    Mob& m = gMobs[i];
    m.rideVel = velocity;
    *seat = m.pos + Vec3(0, 0, MobHeight(m) + 0.25f);
    *yaw = m.yaw;
    return true;
}

void MobsClear() {
    gMobs.clear();
    gSpawnTimer = 3.0f;
}

int SpawnMob(int kind, const Vec3& feet, bool persistent, bool baby) {
    if (kind < 0 || kind >= MOB_KIND_COUNT || gMobs.size() >= 96)
        return -1;
    Mob m;
    m.kind = kind;
    m.pos = feet;
    m.yaw = Rand01() * 6.2831853f - kPi;
    m.health = kTypes[kind].health;
    m.aiTimer = Rand01() * 3.0f;
    m.sayTimer = 3.0f + Rand01() * 15.0f;
    m.eggTimer = 90.0f + Rand01() * 120.0f;
    m.persistent = persistent;
    m.groundZ = feet.z;
    m.baby = baby ? 90.0f : 0.0f;
    m.id = gNextMobId++;
    gMobs.push_back(m);
    return (int)gMobs.size() - 1;
}

Vec3 MobCentre(int index) {
    if (index < 0 || index >= (int)gMobs.size())
        return Vec3(0, 0, 0);
    const Mob& m = gMobs[index];
    return m.pos + Vec3(0, 0, MobHeight(m) * 0.5f);
}

MobHit MobsRaycast(const Vec3& o, const Vec3& dir, float maxDist) {
    MobHit best;
    best.dist = maxDist;
    for (size_t i = 0; i < gMobs.size(); ++i) {
        const Mob& m = gMobs[i];
        if (m.death >= 0.0f)
            continue;
        const float r = MobWidth(m) * 0.5f + 0.05f;
        const float lo[3] = { m.pos.x - r, m.pos.y - r, m.pos.z };
        const float hi[3] = { m.pos.x + r, m.pos.y + r, m.pos.z + MobHeight(m) };
        const float ro[3] = { o.x, o.y, o.z }, rd[3] = { dir.x, dir.y, dir.z };
        float t0 = 0.0f, t1 = best.dist;
        bool miss = false;
        for (int a = 0; a < 3 && !miss; ++a) {
            if (std::fabs(rd[a]) < 1e-6f) {
                miss = ro[a] < lo[a] || ro[a] > hi[a];
                continue;
            }
            float ta = (lo[a] - ro[a]) / rd[a], tb = (hi[a] - ro[a]) / rd[a];
            if (ta > tb)
                std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            miss = t0 > t1;
        }
        if (miss)
            continue;
        best.index = (int)i;
        best.dist = t0;
        best.point = o + dir * t0;
    }
    return best;
}

void MobHurt(int index, float halfHearts, const Vec3& from, float knock) {
    if (index < 0 || index >= (int)gMobs.size())
        return;
    Mob& m = gMobs[index];
    if (m.death >= 0.0f || m.hurt > 0.25f)
        return; // dead, or still invulnerable from the last hit
    m.health -= halfHearts;
    m.hurt = 0.5f;
    m.panic = 4.0f + Rand01() * 2.0f;
    m.aiTimer = 0.0f;
    m.love = 0.0f;
    Vec3 c = m.pos + Vec3(0, 0, MobHeight(m) * 0.5f);
    if (knock > 0.0f) {
        Vec3 away = Flat(m.pos - from);
        m.vel.x = m.vel.x * 0.5f + away.x * 7.0f * knock;
        m.vel.y = m.vel.y * 0.5f + away.y * 7.0f * knock;
        m.vel.z = std::min(8.0f, m.vel.z * 0.5f + 6.0f);
        m.onGround = false;
        m.goal = away;
    }
    if (m.health <= 0.0f)
        Kill(m);
    else
        PlaySfx(kTypes[m.kind].hurt, &c, 1.0f, m.baby > 0.0f ? 1.5f : 0.9f + Rand01() * 0.2f);
}

void MobPush(int index, const Vec3& velocity) {
    if (index < 0 || index >= (int)gMobs.size())
        return;
    Mob& m = gMobs[index];
    m.vel += velocity;
    if (velocity.z > 0.5f)
        m.onGround = false;
    if (m.death < 0.0f) {
        m.panic = std::max(m.panic, 3.0f);
        m.aiTimer = 0.0f;
    }
}

void MobsRadial(const Vec3& at, float radius, float halfHearts, float push) {
    for (size_t i = 0; i < gMobs.size(); ++i) {
        Mob& m = gMobs[i];
        Vec3 c = m.pos + Vec3(0, 0, MobHeight(m) * 0.5f);
        Vec3 d = c - at;
        float dist = d.Length();
        if (dist >= radius)
            continue;
        float f = 1.0f - dist / radius;
        if (halfHearts > 0.0f) {
            m.hurt = 0.0f; // explosions ignore the invulnerability window
            MobHurt((int)i, halfHearts * f, at, 0.0f);
        }
        Vec3 dir = dist > 0.05f ? d * (1.0f / dist) : Vec3(0, 0, 1);
        dir.z = std::max(dir.z, 0.0f) + 0.5f;
        MobPush((int)i, dir * (push * f));
    }
}

bool MobInteract(int index, bool playerOnFoot) {
    if (index < 0 || index >= (int)gMobs.size())
        return false;
    Mob& m = gMobs[index];
    ItemStack& held = gInv.Held();
    if (m.death >= 0.0f)
        return false;
    const int special = held.Empty() ? 0 : Item(held.id).special;
    Vec3 c = m.pos + Vec3(0, 0, MobHeight(m) * 0.6f);
    const bool survival = gGame.gameMode == MODE_SURVIVAL;
    if (special == SP_SADDLE && m.kind == MOB_PIG && !m.saddled && m.baby <= 0.0f) {
        m.saddled = true;
        m.persistent = true;
        if (survival && --held.count == 0)
            held.Clear();
        PlaySfx(SND_SADDLE, &c);
        StartSwing();
        return true;
    }
    if (m.saddled && !IsFood(m, held.id) && special != SP_SHEARS && special != SP_BUCKET && special != SP_WAND &&
        !gGame.ridingMob && playerOnFoot) {
        gGame.ridingMob = m.id;
        PlaySfx(SND_SADDLE, &c, 0.5f);
        return true;
    }
    if (held.Empty())
        return false;
    const ItemDef& d = Item(held.id);
    if (d.special == SP_SHEARS && m.kind == MOB_SHEEP && !m.sheared && m.baby <= 0.0f) {
        m.sheared = true;
        m.woolTimer = 60.0f + Rand01() * 60.0f;
        int n = 1 + rand() % 3;
        for (int i = 0; i < n; ++i) {
            ItemStack s;
            s.id = ID_WHITE_WOOL;
            s.count = 1;
            SpawnDrop(c, s, Vec3((Rand01() - 0.5f) * 3.0f, (Rand01() - 0.5f) * 3.0f, 3.0f + Rand01() * 2.0f), 0.5f);
        }
        PlaySfx(SND_SHEEP_SHEAR, &c);
        DamageHeldItem(1);
        StartSwing();
        return true;
    }
    if (d.special == SP_BUCKET && m.kind == MOB_COW && m.baby <= 0.0f) {
        ItemStack milk;
        milk.id = ID_MILK_BUCKET;
        milk.count = 1;
        if (!survival) {
            if (gInv.CountOf(ID_MILK_BUCKET) == 0)
                gInv.Add(milk);
        } else if (held.count <= 1) {
            held = milk;
        } else {
            --held.count;
            if (gInv.Add(milk) > 0)
                DropStackAtPlayer(milk, false);
        }
        PlaySfx(SND_COW_MILK, &c);
        StartSwing();
        gWorld.dirty = true;
        return true;
    }
    if (IsFood(m, held.id) && m.baby <= 0.0f && m.love <= 0.0f && m.breedCooldown <= 0.0f) {
        m.love = 30.0f;
        if (survival && --held.count == 0)
            held.Clear();
        Puff(c + Vec3(0, 0, 0.4f), TILE_P_HEART, 7, 0.8f);
        PlaySfx(SND_EAT, &c, 0.6f);
        StartSwing();
        return true;
    }
    return false;
}

void MobsSpawnTick(float dt, const Vec3& playerPos) {
    gSpawnTimer -= dt;
    if (gSpawnTimer <= 0.0f) {
        gSpawnTimer = 1.5f;
        TrySpawn(playerPos);
    }
}

void MobsTick(float dt, const Vec3& playerPos, uint16_t playerHeld, bool playerOnFoot) {
    for (size_t i = 0; i < gMobs.size();) {
        Mob& m = gMobs[i];
        const MobType& t = kTypes[m.kind];
        bool remove = false;
        m.hurt = std::max(0.0f, m.hurt - dt);
        m.hitByCar = std::max(0.0f, m.hitByCar - dt);
        m.breedCooldown = std::max(0.0f, m.breedCooldown - dt);
        if (m.baby > 0.0f)
            m.baby = std::max(0.0f, m.baby - dt);
        Vec3 centre = m.pos + Vec3(0, 0, MobHeight(m) * 0.5f);
        const float distToPlayer = (m.pos - playerPos).Length();

        if (m.death >= 0.0f) {
            m.death += dt;
            m.vel.x *= 1.0f - Clamp(dt * 4.0f, 0.0f, 1.0f);
            m.vel.y *= 1.0f - Clamp(dt * 4.0f, 0.0f, 1.0f);
            if (m.death >= 1.0f) {
                Puff(centre, TILE_P_GENERIC_0, 14, MobWidth(m), 0xFFFFFFFF, true);
                DropLoot(m);
                remove = true;
            }
        } else {
            if (m.id != 0 && m.id == gGame.ridingMob) {
                Vec3 want = m.rideVel;
                float k = Clamp(dt * (m.onGround ? 8.0f : 1.0f), 0.0f, 1.0f);
                m.vel.x += (want.x - m.vel.x) * k;
                m.vel.y += (want.y - m.vel.y) * k;
                m.moving = want.Length() > 0.2f;
                if (m.moving) {
                    float target = std::atan2(-want.x, want.y);
                    float d = WrapAngle(target - m.yaw);
                    m.yaw = WrapAngle(m.yaw + Clamp(d, -6.0f * dt, 6.0f * dt));
                }
                m.headYaw *= 0.9f;
                m.panic = 0.0f;
            } else {
                Think(m, dt, playerOnFoot, playerPos, playerHeld);
            }

            m.sayTimer -= dt;
            if (m.sayTimer <= 0.0f) {
                m.sayTimer = 8.0f + Rand01() * 18.0f;
                if (distToPlayer < 32.0f)
                    PlaySfx(t.say, &centre, 1.0f, m.baby > 0.0f ? 1.5f : 0.9f + Rand01() * 0.2f);
            }
            if (m.kind == MOB_CHICKEN && m.baby <= 0.0f) {
                m.eggTimer -= dt;
                if (m.eggTimer <= 0.0f) {
                    m.eggTimer = 120.0f + Rand01() * 180.0f;
                    ItemStack s;
                    s.id = ID_EGG;
                    s.count = 1;
                    SpawnDrop(centre, s, Vec3(0, 0, 1.0f), 0.5f);
                    PlaySfx(SND_CHICKEN_EGG, &centre);
                }
            }
            if (m.sheared) {
                m.woolTimer -= dt;
                if (m.woolTimer <= 0.0f)
                    m.sheared = false;
            }
            if (m.love > 0.0f) {
                m.love -= dt;
                if (rand() % 20 == 0)
                    Puff(centre + Vec3(0, 0, MobHeight(m) * 0.5f), TILE_P_HEART, 1, 0.6f);
                for (size_t j = 0; j < gMobs.size(); ++j) {
                    Mob& o = gMobs[j];
                    if (j == i || o.kind != m.kind || o.love <= 0.0f || o.death >= 0.0f)
                        continue;
                    if ((o.pos - m.pos).Length() > 6.0f)
                        continue;
                    m.love = o.love = 0.0f;
                    m.breedCooldown = o.breedCooldown = 60.0f;
                    Vec3 at = (m.pos + o.pos) * 0.5f;
                    const int kind = m.kind;
                    const bool persistent = m.persistent || o.persistent;
                    SpawnXp(at + Vec3(0, 0, 0.5f), 1 + rand() % 7);
                    SpawnMob(kind, at, persistent, true); // may move gMobs: do not touch m / o after this
                    Puff(at + Vec3(0, 0, 0.8f), TILE_P_HEART, 7, 0.8f);
                    break;
                }
            }
        }
        // (gMobs may have been reallocated by SpawnMob above)
        Mob& mm = gMobs[i];

        // gravity; chickens flutter down
        mm.vel.z -= 32.0f * dt;
        if (mm.kind == MOB_CHICKEN && mm.vel.z < 0.0f && mm.death < 0.0f)
            mm.vel.z *= std::pow(0.6f, dt * 20.0f);
        if (mm.vel.z < -60.0f)
            mm.vel.z = -60.0f;
        MoveMob(mm, dt);

        // animation
        float hs = std::sqrt(mm.vel.x * mm.vel.x + mm.vel.y * mm.vel.y);
        float amount = mm.death >= 0.0f ? 0.0f : Clamp(hs / 20.0f * 4.0f / MobScale(mm), 0.0f, 1.0f);
        mm.limbAmount += (amount - mm.limbAmount) * Clamp(dt * 8.0f, 0.0f, 1.0f);
        mm.limbSwing += mm.limbAmount * 20.0f * dt * (mm.baby > 0.0f ? 1.5f : 1.0f);
        if (mm.kind == MOB_CHICKEN) {
            const float ticks = dt * 20.0f;
            mm.flapSpeed = Clamp(mm.flapSpeed + (mm.onGround ? -1.0f : 4.0f) * 0.3f * ticks, 0.0f, 1.0f);
            if (!mm.onGround && mm.flapping < 1.0f)
                mm.flapping = 1.0f;
            mm.flapping *= std::pow(0.9f, ticks);
            mm.flap += mm.flapping * 2.0f * ticks;
        }

        const float limit = mm.id == gGame.ridingMob ? 1e9f : mm.persistent ? 320.0f : 130.0f;
        if (distToPlayer > limit || mm.pos.z < -120.0f)
            remove = true;
        if (remove) {
            gMobs[i] = gMobs.back();
            gMobs.pop_back();
        } else {
            ++i;
        }
    }
}

} // namespace mc
