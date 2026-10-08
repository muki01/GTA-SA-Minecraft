#include "Entities.h"

#include <cstdlib>

#include "Audio.h"
#include "BlockRules.h"
#include "GameState.h"
#include "Inventory.h"
#include "Items.h"
#include "Mobs.h"
#include "Particles.h"
#include "Survival.h"

namespace mc {

std::vector<DropEntity> gDrops;
std::vector<XpOrb> gXpOrbs;
std::vector<PrimedTnt> gPrimedTnt;

namespace {
float gMergeTimer = 0.0f;
} // namespace

// ---------------------------------------------------------------- items on the ground
void SpawnDrop(const Vec3& pos, const ItemStack& s, const Vec3& vel, float delay) {
    if (s.Empty())
        return;
    if (gDrops.size() > 400)
        gDrops.erase(gDrops.begin());
    DropEntity d;
    d.pos = pos;
    d.vel = vel;
    if (vel.x == 0 && vel.y == 0 && vel.z == 0)
        d.vel = Vec3((Rand01() - 0.5f) * 2.0f, (Rand01() - 0.5f) * 2.0f, 3.0f);
    d.stack = s;
    d.pickupDelay = delay;
    d.spin = Rand01() * 6.28f;
    gDrops.push_back(d);
}

void SpawnDropItem(const Vec3& pos, uint16_t id, int count) {
    while (count > 0) {
        ItemStack s;
        s.id = id;
        s.count = (uint8_t)std::min(count, MaxStack(id));
        count -= s.count;
        SpawnDrop(pos, s);
    }
}

void SpawnBlockDrops(int block, const Vec3& at) {
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
    Vec3 c(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f);
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

void DropsTick(float dt, const Vec3* collector, float reach) {
    gMergeTimer += dt;
    if (gMergeTimer > 0.5f) {
        // ItemEntity.mergeWithNeighbours: equal items lying next to each other become one stack
        gMergeTimer = 0.0f;
        for (size_t i = 0; i < gDrops.size(); ++i)
            for (size_t j = i + 1; j < gDrops.size(); ++j) {
                DropEntity& a = gDrops[i];
                DropEntity& b = gDrops[j];
                if (!a.stack.SameItem(b.stack) || a.stack.count + b.stack.count > MaxStack(a.stack.id) ||
                    (a.pos - b.pos).Length() > 0.7f)
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
        const int fluid = FireAt(d.pos + Vec3(0, 0, 0.1f)) ? ID_LAVA : FluidAt(d.pos + Vec3(0, 0, 0.1f));
        float wl;
        const bool hostWater = TheHost().WaterLevel(d.pos, &wl) && d.pos.z < wl;
        if (fluid == ID_LAVA) {
            // lava burns items
            PlaySfx(SND_LAVA_EXTINGUISH, &d.pos, 0.4f, 2.0f);
            remove = true;
        } else if (fluid == ID_WATER || hostWater) {
            d.vel.z += 18.0f * dt; // items float up
            d.vel = d.vel * std::pow(0.8f, dt * 20.0f);
            if (fluid == ID_WATER) {
                // and drift with the current
                const Vec3 flow = FluidFlowAt({ FloorI(d.pos.x), FloorI(d.pos.y), FloorI(d.pos.z + 0.1f) });
                d.vel.x += flow.x * 5.6f * dt;
                d.vel.y += flow.y * 5.6f * dt;
            }
        } else {
            d.vel.z -= 16.0f * dt;
            d.vel.z *= std::pow(0.98f, dt * 20.0f);
        }
        Vec3 np = d.pos + d.vel * dt;
        // pushed out of blocks it ended up in
        if (gWorld.IsSolid(FloorI(np.x), FloorI(np.y), FloorI(np.z + 0.1f)))
            np.z = (float)FloorI(np.z + 0.1f) + 1.0f;
        if (--d.groundCheck <= 0 || d.vel.z < -0.5f) {
            d.groundCheck = 6;
            float gz;
            d.groundZ = GroundBelow(Vec3(np.x, np.y, d.pos.z + 0.6f), 2.0f, &gz) ? gz : -1000.0f;
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
        if (!remove && collector && d.age > d.pickupDelay) {
            Vec3 diff = d.pos - Vec3(collector->x, collector->y, collector->z - 0.5f);
            if (diff.Length() < reach) {
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

// ---------------------------------------------------------------- experience orbs
void SpawnXp(const Vec3& at, int amount) {
    while (amount > 0 && gXpOrbs.size() < 300) {
        const int v = XpOrbSize(amount);
        amount -= v;
        XpOrb o;
        o.pos = at;
        o.vel = Vec3((Rand01() - 0.5f) * 4.0f, (Rand01() - 0.5f) * 4.0f, 2.0f + Rand01() * 3.0f);
        o.value = v;
        gXpOrbs.push_back(o);
    }
}

void XpTick(float dt, const Vec3* collector) {
    const bool canCollect = collector != nullptr;
    const Vec3 target = collector ? *collector : Vec3(0, 0, -10000.0f);
    for (size_t i = 0; i < gXpOrbs.size();) {
        XpOrb& o = gXpOrbs[i];
        o.age += dt;
        Vec3 d = target - o.pos;
        const float dist = d.Length();
        if (canCollect && dist < 8.0f && o.age > 0.5f) {
            // the orb is pulled towards the player, faster as it comes closer
            float pull = 1.0f - dist / 8.0f;
            o.vel += d * (1.0f / std::max(dist, 0.1f)) * (pull * pull * 40.0f * dt);
            o.vel = o.vel * std::pow(0.9f, dt * 20.0f);
        } else {
            o.vel.z -= 6.0f * dt;
            o.vel.x *= std::pow(0.98f, dt * 20.0f);
            o.vel.y *= std::pow(0.98f, dt * 20.0f);
        }
        Vec3 np = o.pos + o.vel * dt;
        float gz;
        if (o.vel.z < 0.0f && GroundBelow(Vec3(np.x, np.y, o.pos.z + 0.2f), 1.0f, &gz) && np.z < gz + 0.1f) {
            np.z = gz + 0.1f;
            o.vel.z *= -0.4f;
            o.vel.x *= 0.7f;
            o.vel.y *= 0.7f;
        }
        o.pos = np;
        bool remove = o.age > 300.0f || o.pos.z < -200.0f;
        if (canCollect && dist < 1.1f && o.age > 0.5f) {
            AddXp(o.value);
            PlaySfx(SND_XP_ORB, nullptr, 0.1f, 0.55f + Rand01() * 0.9f);
            remove = true;
        }
        if (remove) {
            gXpOrbs[i] = gXpOrbs.back();
            gXpOrbs.pop_back();
        } else {
            ++i;
        }
    }
}

// ---------------------------------------------------------------- primed TNT
void IgniteTnt(const Int3& p, float fuse, const Vec3* velocity) {
    gWorld.Set(p.x, p.y, p.z, MakeVox(ID_AIR));
    PrimedTnt t;
    t.pos = Vec3(p.x + 0.5f, p.y + 0.5f, (float)p.z);
    if (velocity) {
        t.vel = *velocity;
    } else {
        float a = Rand01() * 6.2831853f;
        t.vel = Vec3(-std::sin(a) * 0.4f, -std::cos(a) * 0.4f, 4.0f);
    }
    t.fuse = fuse;
    if (gPrimedTnt.size() < 600)
        gPrimedTnt.push_back(t);
    PlaySfx(SND_FUSE, &t.pos);
}

void TntTick(float dt) {
    for (size_t i = 0; i < gPrimedTnt.size();) {
        PrimedTnt& t = gPrimedTnt[i];
        t.fuse -= dt;
        t.vel.z -= 16.0f * dt;
        Vec3 np = t.pos + t.vel * dt;
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
        // walls of the host's map
        const float hs = std::sqrt(t.vel.x * t.vel.x + t.vel.y * t.vel.y);
        if (hs > 1.0f) {
            Vec3 a = t.pos + Vec3(0, 0, 0.5f);
            Vec3 b = a + Vec3(t.vel.x, t.vel.y, 0.0f) * (dt + 0.5f / hs);
            if (TheHost().LineBlocked(a, b)) {
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
        if (t.vel.z <= 0.0f && GroundBelow(Vec3(np.x, np.y, t.pos.z + 0.6f), 200.0f, &gz) && np.z <= gz) {
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
            p.pos = t.pos + Vec3(0, 0, 1.1f);
            p.vel = Vec3(0, 0, 0.8f);
            p.maxLife = p.life = 0.6f;
            p.tile = TILE_P_GENERIC_0;
            p.anim = 2;
            p.size = 0.07f;
            p.gravity = -0.5f;
            p.color = 0xFF505050;
            SpawnParticle(p);
        }
        if (t.fuse <= 0 || t.pos.z < -150.0f) {
            Vec3 at(t.pos.x, t.pos.y, t.pos.z + 0.5f);
            gPrimedTnt[i] = gPrimedTnt.back();
            gPrimedTnt.pop_back();
            ExplodeAt(at, 3.4f, true, BLAST_TNT); // may prime more TNT (changes gPrimedTnt)
        } else {
            ++i;
        }
    }
}

// ---------------------------------------------------------------- explosions
namespace {
void ExplodeBlocks(const Vec3& c, float radius) {
    // the host's map next to the blast: a crater in it too
    std::vector<BlastedCell> opened;
    TheHost().BlastMap(c, radius, opened);
    for (const BlastedCell& o : opened) {
        if (o.surface && gGame.gameMode == MODE_SURVIVAL && Rand01() < 0.3f)
            SpawnBlockDrops(o.block, Vec3(o.cell.x + 0.5f, o.cell.y + 0.5f, o.cell.z + 0.5f));
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
                    Vec3 dir = dist > 0.05f ? Vec3(dx, dy, dz) * (1.0f / dist) : Vec3(0, 0, 1);
                    Vec3 vel = dir * (5.0f + 9.0f * f) + Vec3(0, 0, 5.0f + 4.0f * f);
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
                    SpawnBlockDrops(b, Vec3(x + 0.5f, y + 0.5f, z + 0.5f));
                SpawnBreakParticles(p, b);
                DropContainerContents(p);
                gWorld.Set(x, y, z, MakeVox(ID_AIR));
            }
}
} // namespace

void PushLooseThings(const Vec3& at, float radius, float speed) {
    auto push = [&](const Vec3& pos, Vec3& vel) {
        Vec3 d = pos - at;
        float dist = d.Length();
        if (dist >= radius)
            return false;
        float f = 1.0f - dist / radius;
        Vec3 dir = dist > 0.05f ? d * (1.0f / dist) : Vec3(0, 0, 1);
        vel += dir * (speed * f) + Vec3(0, 0, speed * 0.45f * f);
        return true;
    };
    for (auto& t : gPrimedTnt)
        if (push(t.pos + Vec3(0, 0, 0.5f), t.vel))
            t.onGround = false;
    for (auto& d : gDrops)
        push(d.pos + Vec3(0, 0, 0.2f), d.vel);
}

void ExplodeAt(const Vec3& at, float radius, bool own, int kind) {
    if (own) {
        // our own blast: the host does the fire ball and the damage, we do the rest right away
        TheHost().Explosion(at, kind);
        PlaySfx(SND_EXPLODE, &at, 4.0f, (1.0f + (Rand01() - Rand01()) * 0.2f) * (radius > 2.0f ? 0.7f : 1.0f));
    }
    if (gRules.explosionsBreakBlocks && radius > 0.0f)
        ExplodeBlocks(at, radius);
    const float r = std::max(radius, 1.5f);
    PushLooseThings(at, r * 2.4f, 14.0f);
    MobsRadial(at, r * 2.2f, own ? 6.0f + radius * 8.0f : 22.0f, 13.0f);
}

} // namespace mc
