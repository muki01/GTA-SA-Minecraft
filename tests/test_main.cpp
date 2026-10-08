// Offline checks of the pure game logic (recipes, cooking, tables, movement, blocks, loose things, arm's length, survival, controls, game state, world file). Not part of the mod.
#include <cstdarg>
#include <cstdio>
#include <initializer_list>

#include "Audio.h"
#include "BlockRules.h"
#include "Controls.h"
#include "Entities.h"
#include "GameState.h"
#include "Host.h"
#include "Interact.h"
#include "Inventory.h"
#include "Combat.h"
#include "Items.h"
#include "Mobs.h"
#include "Particles.h"
#include "Physics.h"
#include "Save.h"
#include "Survival.h"
#include "World.h"

using namespace mc;

// the sounds the core asked for, written down instead of played
struct Played {
    SoundEvent ev;
    bool placed; // at a position in the world (not at the listener)
    Vec3 pos;
    float volume, pitch;
};
static std::vector<Played> gPlayed;

static int Heard(SoundEvent ev) {
    int n = 0;
    for (const Played& p : gPlayed)
        n += p.ev == ev;
    return n;
}

// What the core expects from the game it runs in (see Core.h, World.h, Audio.h): here, next to nothing.
namespace mc {
void PlaySfx(SoundEvent ev, const Vec3* pos, float volume, float pitch) {
    gPlayed.push_back({ ev, pos != nullptr, pos ? *pos : Vec3(), volume, pitch });
}
void SetLoopSfx(SoundEvent, bool, float) {}
struct ChunkMesh {};
Chunk::Chunk() = default;
Chunk::~Chunk() = default;
void Log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    putchar('\n');
}
} // namespace mc

static int gFail = 0;
#define CHECK(cond, ...) \
    do { \
        if (!(cond)) { \
            gFail++; \
            printf("FAIL: "); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

static ItemStack Craft(int w, int h, std::initializer_list<uint16_t> cells) {
    ItemStack grid[9];
    int i = 0;
    for (uint16_t c : cells) {
        if (c) {
            grid[i].id = c;
            grid[i].count = 1;
        }
        ++i;
    }
    return MatchRecipe(grid, w, h);
}

static void Expect(const char* what, const ItemStack& r, uint16_t id, int count) {
    CHECK(r.id == id && r.count == count, "%s -> got %s x%d, want %s x%d", what, r.id ? Item(r.id).key : "nothing",
          r.count, Item(id).key, count);
}

// ---------------------------------------------------------------- movement (Physics.h)
// A host with no map of its own: only blocks, plus what a test puts in (a thin box).
struct TestMap : MoveWorld {
    std::vector<Aabb> extra;   // host boxes, each inside one cell
    void CellBoxes(int x, int y, int z, std::vector<Aabb>& out) override {
        for (const Aabb& b : extra)
            if (FloorI(b.x0) == x && FloorI(b.y0) == y && FloorI(b.z0) == z)
                out.push_back(b);
    }
};

static const float kFloor = 10.0f; // top of the test floor

// three blocks of `block` (water, lava, or air to drain it) on the whole test floor
static void Pool(int block) {
    for (int x = -8; x <= 40; ++x)
        for (int y = -8; y <= 8; ++y)
            for (int z = 10; z <= 12; ++z)
                gWorld.SetRaw(x, y, z, MakeVox(block));
}

// a body standing on the floor at (x, y)
static Body Standing(float x, float y) {
    Body b;
    b.Place(Vec3(x, y, kFloor + 1.0f), Vec3(), true);
    return b;
}

static float HorizSpeed(const Body& b) { return std::sqrt(b.vel.x * b.vel.x + b.vel.y * b.vel.y); }

// jumps once and returns how high the feet got above the floor
static float JumpHeight(TestMap& map, float dt, bool sprint) {
    Body b = Standing(0.5f, 0.5f);
    WalkInput in;
    in.look = Vec3(1, 0, 0);
    in.sprint = sprint;
    in.fwd = sprint ? 1.0f : 0.0f;
    WalkEvents ev;
    for (int i = 0; i < 10; ++i)
        WalkStep(b, in, dt, map, ev);
    in.jump = true;
    WalkStep(b, in, dt, map, ev);
    CHECK(ev.jumped && !b.onGround, "jump starts (dt %.4f)", dt);
    in.jump = false;
    float top = b.pos.z;
    for (int i = 0; i < (int)(3.0f / dt) && !b.onGround; ++i) {
        WalkStep(b, in, dt, map, ev);
        top = std::max(top, b.pos.z);
    }
    CHECK(b.onGround && std::fabs(b.pos.z - (kFloor + 1.0f)) < 1e-3f, "jump lands on the floor (z %.3f)", b.pos.z);
    return top - (kFloor + 1.0f);
}

static void TestPhysics() {
    const float dt = 1.0f / 60.0f;
    gWorld.Clear();
    for (int x = -8; x <= 40; ++x)
        for (int y = -8; y <= 8; ++y)
            gWorld.SetRaw(x, y, 9, MakeVox(ID_STONE));
    TestMap map;
    WalkEvents ev;

    // box clipping
    {
        std::vector<Aabb> boxes = { { 2, 0, 0, 3, 1, 1 } };
        const Aabb p = { 0.0f, 0.2f, 0.0f, 0.6f, 0.8f, 1.8f };
        CHECK(std::fabs(ClipX(p, 5.0f, boxes) - 1.4f) < 1e-5f, "ClipX stops at the box (%.3f)", ClipX(p, 5.0f, boxes));
        CHECK(ClipX(p, -5.0f, boxes) == -5.0f, "ClipX away from the box is free");
        CHECK(ClipY(p, 5.0f, boxes) == 5.0f, "ClipY beside the box is free");
        const Aabb above = { 2.2f, 0.2f, 3.0f, 2.8f, 0.8f, 4.8f };
        CHECK(std::fabs(ClipZ(above, -9.0f, boxes) + 2.0f) < 1e-5f, "ClipZ lands on the box");
        const Aabb body = BodyBox(Vec3(5.0f, 6.0f, 11.0f), kBodyHeight);
        CHECK(std::fabs(body.z0 - 10.0f) < 1e-5f && std::fabs(body.z1 - 11.8f) < 1e-5f && std::fabs(body.x1 - body.x0 - 0.6f) < 1e-5f,
              "body box is 0.6 wide, 1.8 tall, feet 1 m under the position");
    }

    // footsteps: one every 1.6 m at the feet, quiet when sneaking
    {
        const SoundEvent step = (SoundEvent)(SND_STEP_STONE + Block(ID_STONE).sound);
        for (int sneak = 0; sneak < 2; ++sneak) {
            Body b = Standing(0.5f, 0.5f);
            WalkInput in;
            in.look = Vec3(1, 0, 0);
            in.fwd = 1.0f;
            in.sneak = sneak != 0;
            gPlayed.clear();
            for (int i = 0; i < 360; ++i)
                WalkStep(b, in, dt, map, ev);
            const float steps = (b.pos.x - 0.5f) / 1.6f;
            CHECK(std::fabs(Heard(step) - steps) <= 1.0f && Heard(step) == (int)gPlayed.size() && Heard(step) > 0,
                  "%d footsteps on %.1f m", Heard(step), b.pos.x - 0.5f);
            bool right = true;
            for (const Played& p : gPlayed)
                right = right && p.placed && std::fabs(p.pos.z - kFloor) < 1e-3f && p.volume == (sneak ? 0.08f : 0.18f);
            CHECK(right, "footsteps sound at the feet, %s", sneak ? "quietly when sneaking" : "normally when walking");
        }
    }

    // standing still stays put
    {
        Body b = Standing(0.5f, 0.5f);
        b.onGround = false;
        WalkInput in;
        in.look = Vec3(1, 0, 0);
        for (int i = 0; i < 60; ++i)
            WalkStep(b, in, dt, map, ev);
        CHECK(b.onGround && std::fabs(b.pos.z - 11.0f) < 1e-3f && std::fabs(b.pos.x - 0.5f) < 1e-4f, "stands on the floor (z %.4f)",
              b.pos.z);
    }

    // Minecraft's speeds: walking 4.317, sprinting 5.612, sneaking 1.295 blocks per second
    {
        const struct { const char* name; bool sprint, sneak, item; float want; } kCases[] = {
            { "walk", false, false, false, kWalkSpeed },
            { "sprint", true, false, false, kSprintSpeed },
            { "sneak", false, true, false, kSneakSpeed },
            { "walk while eating", false, false, true, kWalkSpeed * 0.2f },
        };
        for (const auto& c : kCases) {
            Body b = Standing(0.5f, 0.5f);
            WalkInput in;
            in.look = Vec3(1, 0, -0.4f); // looking down a bit must not slow the walk
            in.fwd = 1.0f;
            in.sprint = c.sprint;
            in.sneak = c.sneak;
            in.usingItem = c.item;
            for (int i = 0; i < 120; ++i)
                WalkStep(b, in, dt, map, ev);
            const float x0 = b.pos.x;
            for (int i = 0; i < 60; ++i)
                WalkStep(b, in, dt, map, ev);
            CHECK(std::fabs(HorizSpeed(b) - c.want) < 0.01f, "%s speed %.3f, want %.3f", c.name, HorizSpeed(b), c.want);
            CHECK(std::fabs((b.pos.x - x0) - c.want) < 0.02f, "%s covers %.3f m in a second, want %.3f", c.name, b.pos.x - x0,
                  c.want);
            CHECK(b.onGround && std::fabs(b.pos.y - 0.5f) < 1e-3f, "%s goes straight on the ground", c.name);
        }
        // diagonal input is no faster
        Body b = Standing(0.5f, 0.5f);
        WalkInput in;
        in.look = Vec3(1, 0, 0);
        in.fwd = in.strafe = 1.0f;
        for (int i = 0; i < 120; ++i)
            WalkStep(b, in, dt, map, ev);
        CHECK(std::fabs(HorizSpeed(b) - kWalkSpeed) < 0.01f, "diagonal speed %.3f", HorizSpeed(b));
    }

    // the jump: 1.25 blocks, whatever the frame rate
    {
        const float h60 = JumpHeight(map, 1.0f / 60.0f, false), h30 = JumpHeight(map, 1.0f / 30.0f, false),
                    h144 = JumpHeight(map, 1.0f / 144.0f, false);
        printf("jump height: %.3f (60 fps), %.3f (30 fps), %.3f (144 fps)\n", h60, h30, h144);
        CHECK(std::fabs(h60 - 1.25f) < 0.02f, "jump height %.3f, want 1.25", h60);
        CHECK(std::fabs(h30 - 1.25f) < 0.03f && std::fabs(h144 - 1.25f) < 0.02f, "the jump is as high at any frame rate (%.3f / %.3f)", h30,
              h144);
        CHECK(h60 > 1.0f && h30 > 1.0f && h144 > 1.0f, "a jump clears one block");
        CHECK(std::fabs(JumpHeight(map, 1.0f / 60.0f, true) - h60) < 1e-3f, "sprint jumps are as high");
    }

    // steps: half a block is walked up, a full block stops the walk
    {
        map.extra.push_back({ 5.0f, 0.0f, kFloor, 6.0f, 1.0f, kFloor + 0.5f });
        gWorld.SetRaw(12, 0, 10, MakeVox(ID_STONE));
        Body b = Standing(3.5f, 0.5f);
        WalkInput in;
        in.look = Vec3(1, 0, 0);
        in.fwd = 1.0f;
        bool onSlab = false;
        for (int i = 0; i < 60 * 6; ++i) {
            WalkStep(b, in, dt, map, ev);
            if (b.pos.x > 5.4f && b.pos.x < 5.6f)
                onSlab = onSlab || (std::fabs(b.pos.z - (kFloor + 1.5f)) < 1e-3f && b.onGround);
        }
        CHECK(onSlab, "walks up a half block");
        CHECK(std::fabs(b.pos.x - (12.0f - kBodyHalfWidth)) < 1e-3f && b.vel.x == 0.0f, "a full block stops the walk (x %.3f)",
              b.pos.x);
        CHECK(b.onGround && std::fabs(b.pos.z - 11.0f) < 1e-3f, "back on the floor after the half block (z %.3f)", b.pos.z);
        // sprinting into it ends the sprint
        in.sprint = true;
        b = Standing(10.5f, 0.5f);
        bool stopped = false;
        for (int i = 0; i < 120; ++i) {
            WalkStep(b, in, dt, map, ev);
            stopped = stopped || ev.sprintStopped;
        }
        CHECK(stopped, "running into a wall stops the sprint");
        // and a jump gets over it
        in.sprint = false;
        b = Standing(10.5f, 0.5f);
        for (int i = 0; i < 60 * 3; ++i) {
            in.jump = b.pos.x > 11.0f && b.pos.x < 12.5f;
            WalkStep(b, in, dt, map, ev);
        }
        CHECK(b.pos.x > 13.5f, "jumps over a full block (x %.3f)", b.pos.x);
        map.extra.clear();
        gWorld.SetRaw(12, 0, 10, MakeVox(ID_AIR));
    }

    // edges: sneaking never walks off, walking does and the fall hurts by its height
    {
        Body b = Standing(38.5f, 0.5f);
        WalkInput in;
        in.look = Vec3(1, 0, 0);
        in.fwd = 1.0f;
        in.sneak = true;
        for (int i = 0; i < 60 * 8; ++i)
            WalkStep(b, in, dt, map, ev);
        CHECK(b.onGround && b.pos.x > 41.0f && b.pos.x < 41.0f + kBodyHalfWidth + 1e-3f, "sneaking stops at the edge (x %.3f)",
              b.pos.x);

        for (int x = 41; x <= 60; ++x) // ground 10 blocks below the floor
            for (int y = -2; y <= 2; ++y)
                gWorld.SetRaw(x, y, -1, MakeVox(ID_STONE));
        in.sneak = false;
        in.mortal = true;
        bool landed = false;
        float damage = 0.0f, height = 0.0f;
        gPlayed.clear();
        for (int i = 0; i < 60 * 6 && !landed; ++i) {
            WalkStep(b, in, dt, map, ev);
            if (ev.landed) {
                landed = true;
                damage = ev.fallDamage;
                height = ev.fallHeight;
            }
        }
        CHECK(landed && std::fabs(b.pos.z - 1.0f) < 1e-3f, "falls off the edge to the ground below (z %.3f)", b.pos.z);
        CHECK(std::fabs(height - 10.0f) < 0.01f && std::fabs(damage - 7.0f) < 0.01f, "10 block fall: %.2f high, %.2f damage, want 7",
              height, damage);
        CHECK(Heard(SND_FALL_BIG) == 1 && Heard(SND_FALL_SMALL) == 0, "a long fall ends with the big thud");

        // three blocks do not hurt, creative never does, a wind charge flight neither
        const struct { const char* name; float fall; bool mortal, charge; float want; } kFalls[] = {
            { "3 blocks", 3.0f, true, false, 0.0f },   { "4 blocks", 4.0f, true, false, 1.0f },
            { "23 blocks", 23.0f, true, false, 20.0f }, { "creative", 23.0f, false, false, 0.0f },
            { "wind charge", 23.0f, true, true, 0.0f },
        };
        for (const auto& c : kFalls) {
            Body f;
            f.Place(Vec3(45.5f, 0.5f, 1.0f + c.fall), Vec3(), false);
            f.noFallDamage = c.charge;
            WalkInput still;
            still.look = Vec3(1, 0, 0);
            still.mortal = c.mortal;
            float got = -1.0f;
            gPlayed.clear();
            for (int i = 0; i < 60 * 6 && got < 0.0f; ++i) {
                WalkStep(f, still, dt, map, ev);
                if (ev.landed)
                    got = ev.fallDamage;
            }
            CHECK(std::fabs(got - c.want) < 0.01f, "fall of %s: damage %.2f, want %.2f", c.name, got, c.want);
            CHECK(!f.noFallDamage, "fall of %s: the free landing is used up", c.name);
            if (c.fall > 3.5f) // (exactly three blocks is on the edge)
                CHECK(Heard(SND_FALL_BIG) == (c.want > 0.0f && c.fall > 7.0f) && Heard(SND_FALL_SMALL) == (c.want > 0.0f && c.fall <= 7.0f),
                      "fall of %s: the thud fits the fall", c.name);
        }
    }

    // water: slow, no fall damage, Space swims up, lava burns twice a second
    {
        Pool(ID_WATER);
        Body b = Standing(0.5f, 0.5f);
        WalkInput in;
        in.look = Vec3(1, 0, 0);
        in.fwd = 1.0f;
        in.mortal = true;
        for (int i = 0; i < 180; ++i)
            WalkStep(b, in, dt, map, ev);
        CHECK(ev.inFluid && ev.fluid == ID_WATER, "in the water");
        CHECK(std::fabs(HorizSpeed(b) - 2.0f) < 0.01f, "swimming speed %.3f, want 2", HorizSpeed(b));
        in.fwd = 0.0f;
        in.jump = true;
        for (int i = 0; i < 180; ++i)
            WalkStep(b, in, dt, map, ev);
        CHECK(b.pos.z - 1.0f > kFloor + 1.0f, "Space swims up (feet at %.3f)", b.pos.z - 1.0f);

        Body f;
        f.Place(Vec3(20.5f, 0.5f, kFloor + 30.0f), Vec3(), false);
        in.jump = false;
        bool hurt = false;
        gPlayed.clear();
        for (int i = 0; i < 60 * 8; ++i) {
            WalkStep(f, in, dt, map, ev);
            hurt = hurt || ev.fallDamage > 0.0f;
        }
        CHECK(Heard(SND_SPLASH) == 1 && Heard(SND_FALL_BIG) == 0 && !hurt && f.onGround,
              "a fall into water splashes and does not hurt");

        Pool(ID_LAVA);
        b = Standing(0.5f, 0.5f);
        int burns = 0;
        for (int i = 0; i < 180; ++i) {
            WalkStep(b, in, dt, map, ev);
            burns += ev.lavaBurn;
        }
        CHECK(ev.fluid == ID_LAVA && burns >= 6 && burns <= 7, "lava burned %d times in 3 s, want 6", burns);
        Pool(ID_AIR);
    }

    // sprinting is latched by the key and ends when the player stops
    {
        bool latch = false;
        CHECK(!UpdateSprintLatch(latch, true, false, false), "no sprint without the key");
        CHECK(UpdateSprintLatch(latch, true, true, false), "the key starts the sprint");
        CHECK(UpdateSprintLatch(latch, true, false, false), "the sprint lasts after the key is released");
        CHECK(!UpdateSprintLatch(latch, false, false, false), "stopping ends the sprint");
        CHECK(!UpdateSprintLatch(latch, false, true, false), "no sprint standing still");
        CHECK(!UpdateSprintLatch(latch, true, true, true), "no sprint when sneaking or hungry");
    }

    // creative flight
    {
        Vec3 v;
        for (int i = 0; i < 180; ++i)
            FlySteer(v, Vec3(0, 1, 0), 1.0f, 0.0f, false, false, false, dt);
        CHECK(std::fabs(v.y - kFlySpeed) < 0.01f && std::fabs(v.x) < 1e-4f && std::fabs(v.z) < 1e-4f, "flying speed %.3f", v.y);
        for (int i = 0; i < 180; ++i)
            FlySteer(v, Vec3(0, 1, 0), 1.0f, 0.0f, true, true, false, dt);
        CHECK(std::fabs(v.y - kFlySprintSpeed) < 0.01f && std::fabs(v.z - kFlyClimbSpeed) < 0.01f, "fast flying %.3f, climbing %.3f",
              v.y, v.z);

        for (int z = 10; z <= 16; ++z)
            for (int y = -2; y <= 2; ++y)
                gWorld.SetRaw(20, y, z, MakeVox(ID_STONE));
        Vec3 pos(15.5f, 0.5f, 13.0f);
        v = Vec3(kFlySprintSpeed, 0, 0);
        float impact = 0.0f;
        bool ground = false;
        for (int i = 0; i < 60; ++i)
            ground = FlyStep(pos, v, dt, false, map, &impact) || ground;
        CHECK(std::fabs(pos.x - (20.0f - kBodyHalfWidth)) < 1e-3f && v.x == 0.0f, "flying into a wall stops there (x %.3f)", pos.x);
        CHECK(std::fabs(impact - kFlySprintSpeed) < 0.01f && !ground, "the wall was hit at %.2f m/s", impact);
        v = Vec3(0, 0, -kFlyClimbSpeed);
        for (int i = 0; i < 60 && !ground; ++i)
            ground = FlyStep(pos, v, dt, false, map, nullptr);
        CHECK(ground && std::fabs(pos.z - 11.0f) < 1e-3f, "flying down lands on the floor (z %.3f)", pos.z);
        for (int z = 10; z <= 16; ++z)
            for (int y = -2; y <= 2; ++y)
                gWorld.SetRaw(20, y, z, MakeVox(ID_AIR));
    }

    // elytra: a dive speeds up, pulling up trades speed for height, a rocket pushes
    {
        Vec3 v;
        float boost = 0.0f, acc = 0.0f;
        const Vec3 down(0.7071f, 0, -0.7071f), level(1, 0, 0), up(0.866f, 0, 0.5f);
        CHECK(ElytraAdvance(v, down, boost, acc, 0.125f) == 2 && std::fabs(acc - 0.5f) < 1e-4f, "elytra runs 20 ticks a second");
        for (int i = 0; i < 60; ++i)
            ElytraTick(v, down, boost);
        const float dive = std::sqrt(v.x * v.x + v.y * v.y), sink = v.z;
        printf("elytra: 3 s dive at 45 degrees -> %.1f m/s forward, %.1f m/s down\n", dive, -sink);
        CHECK(dive > 15.0f && sink < -10.0f, "a dive speeds up (%.1f forward, %.1f down)", dive, sink);
        for (int i = 0; i < 40; ++i)
            ElytraTick(v, up, boost);
        CHECK(v.z > 0.0f, "pulling up after a dive climbs (%.1f up)", v.z);
        v = Vec3(10, 0, 0);
        for (int i = 0; i < 100; ++i)
            ElytraTick(v, level, boost);
        CHECK(v.z < 0.0f && v.z > -4.0f && v.x > 0.0f, "level gliding sinks slowly (%.2f)", v.z);
        boost = 1.0f;
        int ticks = 0;
        while (boost > 0.0f && ticks < 100) {
            ElytraTick(v, level, boost);
            ++ticks;
        }
        CHECK(ticks >= 20 && ticks <= 21 && v.x > 30.0f, "a rocket pushes for a second (%d ticks, %.1f m/s)", ticks, v.x);
        CHECK(GlideImpactDamage(30.0f, 0.0f) == 12.0f && GlideImpactDamage(5.0f, 0.0f) < 0.0f, "flying into a wall hurts by speed");
    }
    gWorld.Clear();
}

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

// ---------------------------------------------------------------- blocks that do things (BlockRules.h)
// The game the tests run in: no map of its own, rain and a player when a test says so; it counts what it is asked
// to do.
struct TestHost : Host {
    bool rain = false;
    bool hasPlayer = false;
    Vec3 player;
    int placed = 0, explosions = 0, doused = 0, thingsBroken = 0;
    bool outdoors = true;
    bool herdGround = false; // animals may appear anywhere, at z = 10
    float sea = -1000.0f;    // its water stands this high everywhere
    bool soil = false;     // its ground is everywhere, and it is earth
    bool blocking = false; // somebody stands in `blocked`
    Int3 blocked;
    bool thing = false;    // something of its own is in front of the player, two metres away
    bool Raining() override { return rain; }
    bool Outdoors() override { return outdoors; }
    bool SpawnGround(const Vec3& from, Vec3* ground) override {
        if (herdGround)
            *ground = Vec3(from.x, from.y, 10.0f);
        return herdGround;
    }
    bool WaterLevel(const Vec3&, float* level) override {
        if (sea > -900.0f)
            *level = sea;
        return sea > -900.0f;
    }
    bool Supports(const Int3&) override { return soil; }
    bool SoilBelow(const Vec3& from, bool, float* z) override {
        if (soil && z)
            *z = from.z - 2.0f;
        return soil;
    }
    bool CellBlocked(const Int3& c) override { return blocking && c == blocked; }
    bool Pick(const PickRay& ray, const VoxelHit&, Target& out) override {
        if (!thing)
            return false;
        out.valid = true;
        out.pos = out.key = Int3{ 7, 7, 7 };
        out.point = ray.origin + ray.dir * 2.0f;
        out.normal = Vec3(0, 0, 1);
        out.virtualBlock = ID_DIRT;
        out.hardness = 0.5f;
        return true;
    }
    void BreakTarget() override { ++thingsBroken; }
    void Douse(const Vec3&, float) override { ++doused; }
    bool PlayerPos(Vec3* pos) override {
        if (hasPlayer)
            *pos = player;
        return hasPlayer;
    }
    void FluidPlaced(const Int3&) override { ++placed; }
    void Explosion(const Vec3&, int kind) override {
        ++explosions;
        lastBlast = kind;
    }

    // ---- fighting: its people are balls, and everything beyond x = carX is one of its vehicles (number 77)
    struct Person {
        Vec3 centre;
        float r = 0.5f;
        float hurt = 0.0f;
        int how = -1;
        bool byHostile = false;
        Vec3 push;
    };
    enum { kPlayerNumber = 1000, kCarNumber = 77 };
    std::vector<Person> people;
    float carX = 1e9f, carHurt = 0.0f;
    Vec3 velocity;
    bool onGround = true, falling = false, storm = false, vehiclesOk = true, gustPlayerToo = false;
    int vehicle = -1;     // the one the player sits in
    int shotVehicle = -2; // what the last shot was said to leave from
    int gusts = 0, moves = 0, itemAttacks = 0, itemUses = 0, lastBlast = -1, lastPlaced = -1, lastShooter = -1;
    float playerHurt = 0.0f, boost = 0.0f;
    std::vector<float> fires; // seconds of every fire it was asked for

    HostHit Trace(const Vec3& from, const Vec3& dir, float len, bool playerToo) {
        HostHit h;
        if (dir.x > 1e-6f && from.x < carX && from.x + dir.x * len >= carX) {
            h.hit = true;
            h.dist = (carX - from.x) / dir.x;
            h.point = from + dir * h.dist;
            h.vehicle = kCarNumber;
        }
        auto ball = [&](const Vec3& c, float r, int number) {
            const Vec3 oc = c - from;
            const float along = oc.Dot(dir), off2 = oc.Dot(oc) - along * along;
            if (along < 0.0f || off2 > r * r)
                return;
            const float t = std::max(0.0f, along - std::sqrt(r * r - off2));
            if (t < len && t < h.dist && t < h.beingDist) {
                h.being = number;
                h.beingDist = t;
                h.beingPoint = from + dir * t;
            }
        };
        for (size_t i = 0; i < people.size(); ++i)
            ball(people[i].centre, people[i].r, (int)i);
        if (playerToo && hasPlayer)
            ball(player, 0.5f, kPlayerNumber);
        return h;
    }
    Vec3 PlayerVelocity() override { return velocity; }
    bool PlayerOnGround() override { return onGround; }
    bool PlayerFalling() override { return falling; }
    int PlayerVehicle() override { return vehicle; }
    void MovePlayer(const Vec3& to) override {
        player = to;
        ++moves;
    }
    void HurtPlayer(float halfHearts) override { playerHurt += halfHearts; }
    float AimDistance(const Vec3& origin, const Vec3& dir, float reach, float nothing) override {
        const HostHit h = Trace(origin, dir, reach, false);
        const float dist = std::min(h.hit ? h.dist : 1e9f, h.beingDist);
        return dist < 1e8f ? dist : nothing;
    }
    HostHit BlowTrace(const Vec3& origin, const Vec3& dir, float reach) override { return Trace(origin, dir, reach, false); }
    HostHit ShotTrace(const Vec3& from, const Vec3& dir, float len, const ShotOwner& by) override {
        shotVehicle = by.vehicle;
        return Trace(from, dir, len, by.hostile);
    }
    void HurtBeing(int being, float halfHearts, int how, const ShotOwner* by) override {
        if (being == kPlayerNumber) {
            playerHurt += halfHearts;
            lastShooter = by ? by->shooter : -1;
            return;
        }
        Person& p = people[being];
        p.hurt += halfHearts;
        p.how = how;
        p.byHostile = by && by->hostile;
    }
    void PushBeing(int being, const Vec3& v) override {
        if (being != kPlayerNumber)
            people[being].push += v;
    }
    void HurtVehicle(int number, float halfHearts) override {
        if (number == kCarNumber)
            carHurt += halfHearts;
    }
    void Gust(const Vec3&, float, float, float, bool playerToo) override {
        ++gusts;
        gustPlayerToo = playerToo;
    }
    void Ignite(const Vec3&, float seconds, int) override { fires.push_back(seconds); }
    bool Thunderstorm() override { return storm; }
    bool PlaceVehicle(int kind, const Vec3&, float) override {
        if (vehiclesOk)
            lastPlaced = kind;
        return vehiclesOk;
    }
    void BoostVehicle(float seconds) override { boost = seconds; }
    bool ItemAttack(int special) override {
        ++itemAttacks;
        return special == SP_WAND;
    }
    bool ItemUse(int special) override {
        ++itemUses;
        return special == SP_WAND;
    }
};

// how many of an item lie on the ground
static int CountDrops(uint16_t id) {
    int n = 0;
    for (const DropEntity& d : gDrops)
        if (d.stack.id == id)
            n += d.stack.count;
    return n;
}

static float gBlockClock = 0.0f;

// lets the blocks live for a while
static void RunBlocks(float seconds) {
    const float dt = 0.05f;
    for (float t = 0.0f; t < seconds; t += dt) {
        gBlockClock += dt;
        BlocksTick(dt, gBlockClock);
    }
}

// an empty world with a stone floor (top at z = 10) around the origin
static void BlockStage() {
    gWorld.Clear();
    BlockRulesClear();
    gDrops.clear();
    gXpOrbs.clear();
    gPrimedTnt.clear();
    gParticles.clear();
    for (int x = -12; x <= 12; ++x)
        for (int y = -12; y <= 12; ++y)
            gWorld.SetRaw(x, y, 9, MakeVox(ID_STONE));
}

static int CountBlocks(int block, int r = 12, int z0 = 9, int z1 = 30) {
    int n = 0;
    for (int z = z0; z <= z1; ++z)
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x)
                n += gWorld.GetBlock(x, y, z) == block;
    return n;
}

static void TestBlockRules() {
    TestHost host;
    SetHost(&host);
    srand(2024);
    // the sound of a block being put down, by what it is made of
    auto placeSound = [](int block) { return (SoundEvent)(SND_PLACE_STONE + Block(block).sound); };

    // a bucket of water on flat ground: seven blocks out, a step lower each block
    {
        BlockStage();
        CHECK(PlaceFluid(ID_WATER, { 0, 0, 10 }) && host.placed == 1, "a bucket of water goes into the air");
        CHECK(!PlaceFluid(ID_WATER, { 0, 0, 9 }), "but not into stone");
        RunBlocks(8.0f);
        bool levels = true;
        for (int d = 1; d <= 7; ++d) {
            const Voxel v = gWorld.Get(d, 0, 10);
            levels = levels && VoxBlock(v) == ID_WATER && (VoxMeta(v) & META_FLUID_LEVEL) == d;
        }
        CHECK(levels, "water gets a step lower with every block from the source");
        CHECK(gWorld.GetBlock(8, 0, 10) == ID_AIR && gWorld.GetBlock(0, -8, 10) == ID_AIR, "and stops after seven blocks");
        CHECK(VoxMeta(gWorld.Get(3, 4, 10)) == 7 && gWorld.GetBlock(4, 4, 10) == ID_AIR, "it spreads as a diamond");
        CHECK(CountBlocks(ID_WATER) == 113, "113 cells of water (%d)", CountBlocks(ID_WATER));

        // how deep it is, where it is, where it flows
        CHECK(FluidAt(Vec3(0.5f, 0.5f, 10.5f)) == ID_WATER && FluidAt(Vec3(0.5f, 0.5f, 10.95f)) == ID_AIR,
              "a source fills 8/9 of its cell");
        CHECK(FluidAt(Vec3(6.5f, 0.5f, 10.1f)) == ID_WATER && FluidAt(Vec3(6.5f, 0.5f, 10.4f)) == ID_AIR, "thin water is thin");
        CHECK(Near(FluidOwnHeight(gWorld.Get(0, 0, 10)), 8.0f / 9.0f) && Near(FluidOwnHeight(gWorld.Get(7, 0, 10)), 1.0f / 9.0f),
              "fluid heights");
        const Vec3 flow = FluidFlowAt({ 3, 0, 10 });
        CHECK(Near(flow.x, 1.0f) && Near(flow.y, 0.0f) && flow.z == 0.0f, "water flows away from the source (%.2f, %.2f)", flow.x,
              flow.y);
        CHECK(FluidFlowAt({ 5, 5, 20 }).Length() == 0.0f, "no fluid, no flow");
        int fluid = 0;
        CHECK(FluidDepth(Vec3(0.5f, 0.5f, 10.0f), 1.8f, &fluid) == 0.5f && fluid == ID_WATER, "a player stands half in it");
        CHECK(FluidDepth(Vec3(0.5f, 0.5f, 12.0f), 1.8f, &fluid) == 0.0f && fluid == ID_AIR, "and not at all above it");

        // the bucket takes the source back; the rest dries up
        int taken = 0;
        CHECK(!TakeFluid({ 3, 0, 10 }, &taken) && TakeFluid({ 0, 0, 10 }, &taken) && taken == ID_WATER,
              "only a source fills a bucket");
        RunBlocks(12.0f);
        CHECK(CountBlocks(ID_WATER) == 0, "without its source the water dries up (%d cells left)", CountBlocks(ID_WATER));
    }

    // water falls, and two sources make a third
    {
        BlockStage();
        PlaceFluid(ID_WATER, { 0, 0, 14 });
        RunBlocks(6.0f);
        bool column = true;
        for (int z = 10; z <= 13; ++z) {
            const Voxel v = gWorld.Get(0, 0, z);
            column = column && VoxBlock(v) == ID_WATER && (VoxMeta(v) & META_FLUID_FALLING);
        }
        CHECK(column && gWorld.GetBlock(1, 0, 14) == ID_AIR, "water falls straight down before it spreads");
        CHECK(VoxBlock(gWorld.Get(1, 0, 10)) == ID_WATER && (VoxMeta(gWorld.Get(1, 0, 10)) & 0xF) == 1, "and spreads at the bottom");
        CHECK(FluidAt(Vec3(0.5f, 0.5f, 12.99f)) == ID_WATER, "a falling column is full");

        BlockStage();
        PlaceFluid(ID_WATER, { 0, 0, 10 });
        PlaceFluid(ID_WATER, { 2, 0, 10 });
        RunBlocks(3.0f);
        CHECK(gWorld.Get(1, 0, 10) == MakeVox(ID_WATER, 0), "two sources make a third between them");
    }

    // lava is slow, short, and hardens where it meets water
    {
        BlockStage();
        PlaceFluid(ID_LAVA, { 0, 0, 10 });
        RunBlocks(1.0f);
        CHECK(gWorld.GetBlock(1, 0, 10) == ID_AIR, "lava takes its time");
        RunBlocks(14.0f);
        CHECK(VoxMeta(gWorld.Get(1, 0, 10)) == 2 && VoxMeta(gWorld.Get(3, 0, 10)) == 6 && gWorld.GetBlock(4, 0, 10) == ID_AIR,
              "lava reaches three blocks");
        gPlayed.clear();
        gParticles.clear();
        PlaceFluid(ID_WATER, { 0, 0, 11 });
        RunBlocks(6.0f);
        CHECK(gWorld.GetBlock(0, 0, 10) == ID_OBSIDIAN, "water on a lava source makes obsidian");
        CHECK(Heard(SND_LAVA_EXTINGUISH) > 0 && (int)gParticles.size() == 8 * Heard(SND_LAVA_EXTINGUISH),
              "with a hiss and a puff of smoke each time (%d)", Heard(SND_LAVA_EXTINGUISH));
        CHECK(CountBlocks(ID_LAVA) == 0 && CountBlocks(ID_COBBLESTONE) + CountBlocks(ID_STONE) > 0,
              "and flowing lava turns to stone (%d lava left)", CountBlocks(ID_LAVA));
    }

    // sand and gravel fall; plants and fire need something under them
    {
        BlockStage();
        gPlayed.clear();
        gWorld.Set(0, 0, 15, MakeVox(ID_SAND));
        gWorld.Set(0, 0, 16, MakeVox(ID_GRAVEL));
        gWorld.Set(3, 0, 15, MakeVox(ID_STONE));
        RunBlocks(0.05f);
        CHECK(gWorld.GetBlock(0, 0, 15) == ID_AIR && FallingBlocks().size() >= 1, "sand lets go");
        RunBlocks(3.0f);
        CHECK(gWorld.GetBlock(0, 0, 10) == ID_SAND && gWorld.GetBlock(0, 0, 11) == ID_GRAVEL && FallingBlocks().empty(),
              "sand and gravel land on top of each other");
        CHECK(gPlayed.size() == 2 && Heard(placeSound(ID_SAND)) >= 1 && Heard(placeSound(ID_GRAVEL)) >= 1 && gPlayed[0].placed &&
                  std::fabs(gPlayed[0].pos.z - 10.5f) < 1e-3f,
              "each with the sound of what it is made of (%d sounds)", (int)gPlayed.size());
        CHECK(gWorld.GetBlock(3, 0, 15) == ID_STONE, "stone stays in the air");
        gWorld.Set(0, 0, 9, MakeVox(ID_AIR)); // the floor under the pile goes
        gWorld.SetRaw(0, 0, 3, MakeVox(ID_STONE));
        RunBlocks(3.0f);
        CHECK(gWorld.GetBlock(0, 0, 4) == ID_SAND && gWorld.GetBlock(0, 0, 5) == ID_GRAVEL && gWorld.GetBlock(0, 0, 10) == ID_AIR,
              "the pile falls when the ground under it goes");

        gDrops.clear();
        gWorld.Set(5, 5, 10, MakeVox(ID_POPPY));
        RunBlocks(0.5f);
        CHECK(gWorld.GetBlock(5, 5, 10) == ID_POPPY, "a flower stands on the ground");
        gWorld.Set(5, 5, 9, MakeVox(ID_AIR));
        RunBlocks(0.5f);
        CHECK(gWorld.GetBlock(5, 5, 10) == ID_AIR && gDrops.size() == 1 && CountDrops(ID_POPPY) == 1,
              "and drops when the ground goes");
        gWorld.Set(6, 6, 10, MakeVox(ID_POPPY));
        PlaceFluid(ID_WATER, { 6, 5, 10 });
        RunBlocks(2.0f);
        CHECK(VoxBlock(gWorld.Get(6, 6, 10)) == ID_WATER && CountDrops(ID_POPPY) == 2, "water washes flowers away");

        // sand that lands where a fire burns has no place: it drops as an item
        BlockStage();
        gPlayed.clear();
        CHECK(PlaceFire({ 0, 0, 10 }) && FireAt(Vec3(0.5f, 0.5f, 10.5f)) && !FireAt(Vec3(1.5f, 0.5f, 10.5f)), "fire on the ground");
        gWorld.Set(0, 0, 13, MakeVox(ID_SAND));
        RunBlocks(0.9f);
        CHECK(CountDrops(ID_SAND) == 1 && Heard(placeSound(ID_SAND)) == 0 && CountBlocks(ID_SAND) == 0,
              "sand falling into a fire drops as an item (%d on the ground)", CountDrops(ID_SAND));
    }

    // fire: needs ground or fuel, burns wood, lights TNT, dies in the rain
    {
        BlockStage();
        CHECK(!PlaceFire({ 0, 0, 15 }), "no fire in thin air");
        CHECK(!PlaceFire({ 0, 0, 9 }), "no fire inside a block");
        gWorld.SetRaw(0, 0, 16, MakeVox(ID_OAK_PLANKS));
        CHECK(PlaceFire({ 0, 0, 15 }), "fire next to wood");
        CHECK(FireIgnite(ID_OAK_PLANKS) > 0 && FireBurn(ID_OAK_PLANKS) > 0 && FireBurn(ID_TNT) > 0 && FireIgnite(ID_STONE) == 0,
              "what burns");

        // bare stone: the fire dies down by itself
        BlockStage();
        PlaceFire({ 0, 0, 10 });
        RunBlocks(300.0f);
        CHECK(CountBlocks(ID_FIRE) == 0, "fire on bare stone dies down");

        // a wooden hut burns
        BlockStage();
        for (int x = -2; x <= 2; ++x)
            for (int y = -2; y <= 2; ++y)
                for (int z = 10; z <= 13; ++z)
                    if (std::abs(x) == 2 || std::abs(y) == 2 || z == 13)
                        gWorld.SetRaw(x, y, z, MakeVox(ID_OAK_PLANKS));
        const int wood = CountBlocks(ID_OAK_PLANKS);
        CHECK(PlaceFire({ 0, 1, 10 }) || PlaceFire({ 1, 1, 10 }), "fire in the hut");
        gWorld.SetRaw(1, 1, 10, MakeVox(ID_TNT));
        PlaceFire({ 1, 0, 10 });
        gPrimedTnt.clear();
        gPlayed.clear();
        RunBlocks(600.0f);
        CHECK(CountBlocks(ID_OAK_PLANKS) < wood / 2, "the hut burns down (%d of %d planks left)", CountBlocks(ID_OAK_PLANKS), wood);
        CHECK(gPrimedTnt.size() == 1 && Heard(SND_FUSE) == 1 && CountBlocks(ID_TNT) == 0, "and the fire lights the TNT");

        // rain
        BlockStage();
        for (int x = -3; x <= 3; ++x)
            for (int y = -3; y <= 3; ++y)
                gWorld.SetRaw(x, y, 10, MakeVox(ID_OAK_PLANKS));
        for (int x = -3; x <= 3; ++x)
            PlaceFire({ x, 0, 11 });
        host.rain = true;
        RunBlocks(40.0f);
        host.rain = false;
        CHECK(CountBlocks(ID_FIRE) == 0 && CountBlocks(ID_OAK_PLANKS) > 30, "rain puts the fire out (%d fires left)", CountBlocks(ID_FIRE));

        // lava lights what burns around it
        BlockStage();
        for (int x = -1; x <= 1; ++x)
            for (int y = -1; y <= 1; ++y)
                gWorld.SetRaw(x, y, 12, MakeVox(ID_OAK_PLANKS));
        gWorld.SetRaw(0, 0, 10, MakeVox(ID_LAVA));
        RunBlocks(60.0f);
        CHECK(CountBlocks(ID_FIRE) > 0 || CountBlocks(ID_OAK_PLANKS) < 9, "lava sets the wood above it on fire");

        // water puts fire out
        BlockStage();
        gPlayed.clear();
        PlaceFire({ 1, 0, 10 });
        PlaceFluid(ID_WATER, { 0, 0, 10 });
        RunBlocks(1.0f);
        CHECK(VoxBlock(gWorld.Get(1, 0, 10)) == ID_WATER && Heard(SND_FIRE_EXTINGUISH) == 1, "water puts fire out, with a hiss");
    }

    // saplings grow into trees of their kind
    {
        BlockStage();
        gWorld.SetRaw(0, 0, 9, MakeVox(ID_GRASS_BLOCK));
        gWorld.SetRaw(0, 0, 10, MakeVox(ID_OAK_SAPLING));
        CHECK(gWorld.ticking.count({ 0, 0, 10 }) == 1, "a sapling waits for its random tick");
        gWorld.SetRaw(0, 0, 13, MakeVox(ID_STONE));
        CHECK(!GrowSapling({ 0, 0, 10 }) && gWorld.GetBlock(0, 0, 10) == ID_OAK_SAPLING, "no room, no tree");
        gWorld.SetRaw(0, 0, 13, MakeVox(ID_AIR));
        gPlayed.clear();
        CHECK(GrowSapling({ 0, 0, 10 }) && gParticles.size() == 15 && Heard(SND_PLACE_GRASS) == 1, "an oak grows, with sparkles");
        const int logs = CountBlocks(ID_OAK_LOG), leaves = CountBlocks(ID_OAK_LEAVES);
        CHECK(logs >= 4 && logs <= 6 && leaves > 30 && gWorld.GetBlock(0, 0, 10) == ID_OAK_LOG, "oak: %d logs, %d leaves", logs, leaves);
        CHECK(gWorld.GetBlock(0, 0, 9) == ID_DIRT && gWorld.ticking.count({ 0, 0, 10 }) == 0, "the grass under it becomes dirt");
        CHECK(!GrowSapling({ 0, 0, 10 }), "a log is no sapling");

        BlockStage();
        gWorld.SetRaw(0, 0, 10, MakeVox(ID_SPRUCE_SAPLING));
        CHECK(GrowSapling({ 0, 0, 10 }) && CountBlocks(ID_SPRUCE_LOG) >= 6 && CountBlocks(ID_SPRUCE_LEAVES) > 10 &&
                  CountBlocks(ID_OAK_LOG) == 0,
              "a spruce grows from a spruce sapling");

        // left alone, a sapling grows by itself (one chance in fifty a second)
        BlockStage();
        gPlayed.clear();
        gWorld.SetRaw(0, 0, 10, MakeVox(ID_BIRCH_SAPLING));
        RunBlocks(600.0f);
        CHECK(Heard(SND_PLACE_GRASS) == 1 && CountBlocks(ID_BIRCH_LOG) >= 5, "a sapling grows by itself in time");

        CHECK(IsSoil(ID_GRASS_BLOCK) && IsSoil(ID_DIRT) && !IsSoil(ID_STONE) && !IsSoil(ID_SAND), "soil");
        for (int i = 0; i < 50; ++i)
            CHECK(IsPlantBlock(RandomFlower()), "flowers are plants");
        CHECK(PlantCellOnGround(10.0f) == 10 && PlantCellOnGround(9.8f) == 10 && PlantCellOnGround(9.7f) == 9,
              "a plant on uneven ground sits in the cell its foot is in");
    }

    SetHost(nullptr);
    BlockStage();
    gWorld.Clear();
}

// ---------------------------------------------------------------- survival (Survival.h)
static void ResetSurvival() {
    gSurvival = Survival();
    gInv = PlayerInventory();
}

static ItemStack Stack(uint16_t id, int count = 1) {
    ItemStack s;
    s.id = id;
    s.count = (uint8_t)count;
    return s;
}

static void TestSurvival() {
    const float kFull = 100.0f; // the host's full health here: one Minecraft point is 5
    Survival& s = gSurvival;

    for (int i = 0; i < 1000; ++i) {
        const float r = Rand01();
        CHECK(r >= 0.0f && r < 1.0f, "Rand01 out of range: %f", r);
    }

    // experience: 7 points for the first level, more for each one after
    {
        ResetSurvival();
        gPlayed.clear();
        CHECK(XpToNextLevel(0) == 7 && XpToNextLevel(14) == 35 && XpToNextLevel(15) == 37 && XpToNextLevel(30) == 112 &&
                  XpToNextLevel(31) == 121,
              "experience needed per level");
        CHECK(!AddXp(7) && s.xpLevel == 1 && s.xpProgress == 0.0f && s.xpTotal == 7, "7 points make level 1 (level %d)", s.xpLevel);
        CHECK(!AddXp(0) && !AddXp(-5) && s.xpTotal == 7, "no points, no experience");
        CHECK(Heard(SND_LEVELUP) == 0, "no fanfare for level 1");
        CHECK(AddXp(9 + 11 + 13 + 15) && s.xpLevel == 5 && Heard(SND_LEVELUP) == 1, "every fifth level is announced (level %d)",
              s.xpLevel);
        CHECK(!AddXp(3) && s.xpLevel == 5 && Near(s.xpProgress, 3.0f / 17.0f), "progress towards level 6: %.3f", s.xpProgress);
        CHECK(s.xpTotal == 7 + 48 + 3, "total experience %d", s.xpTotal);
        int sum = 0, orbs = 0;
        for (int left = 100; left > 0; ++orbs) {
            const int v = XpOrbSize(left);
            left -= v;
            sum += v;
        }
        CHECK(sum == 100 && orbs == 4, "100 points split into 73 + 17 + 7 + 3 (%d orbs)", orbs);
        CHECK(XpOrbSize(5000) == 2477 && XpOrbSize(2) == 1 && XpOrbSize(1) == 1, "orb sizes");
        for (int i = 0; i < 300; ++i) {
            const int coal = XpForBlock(ID_COAL_ORE), diamond = XpForBlock(ID_DIAMOND_ORE);
            CHECK(coal >= 0 && coal <= 2 && diamond >= 3 && diamond <= 7, "ore experience: coal %d, diamond %d", coal, diamond);
        }
        CHECK(XpForBlock(ID_STONE) == 0 && XpForBlock(ID_IRON_ORE) == 0, "only some ores give experience");
    }

    // hunger: exhaustion eats saturation first, then food
    {
        ResetSurvival();
        float h = kFull;
        SurvivalEvents ev;
        s.exhaustion = 4.5f;
        HungerTick(0.1f, h, kFull, ev);
        CHECK(s.saturation == 4.0f && s.food == 20.0f && Near(s.exhaustion, 0.5f), "exhaustion costs saturation first");
        s.saturation = 0.0f;
        s.exhaustion = 8.0f;
        HungerTick(0.1f, h, kFull, ev);
        CHECK(s.food == 18.0f && Near(s.exhaustion, 0.0f), "then food (%.1f)", s.food);

        // a full stomach heals one point every four seconds, and that costs hunger
        ResetSurvival();
        h = 50.0f;
        int checks = 0;
        for (int i = 0; i < 45; ++i)
            checks += HungerTick(0.1f, h, kFull, ev);
        CHECK(checks == 1 && h == 55.0f && s.saturation == 4.0f && Near(s.exhaustion, 2.0f), "healing by food: health %.1f", h);
        // nothing to heal: no cost
        ResetSurvival();
        h = kFull;
        for (int i = 0; i < 100; ++i)
            HungerTick(0.1f, h, kFull, ev);
        CHECK(h == kFull && s.exhaustion == 0.0f && s.saturation == 5.0f, "full health costs no hunger");
        // not full enough: no healing
        ResetSurvival();
        s.food = 17.0f;
        h = 50.0f;
        for (int i = 0; i < 100; ++i)
            HungerTick(0.1f, h, kFull, ev);
        CHECK(h == 50.0f, "17 food does not heal");

        // an empty stomach starves, but not to death
        ResetSurvival();
        s.food = s.saturation = 0.0f;
        h = 30.0f;
        int starved = 0;
        gPlayed.clear();
        for (int i = 0; i < 400; ++i) {
            SurvivalEvents e;
            HungerTick(0.1f, h, kFull, e);
            starved += e.starved;
        }
        CHECK(h == 10.0f && starved == 4 && Heard(SND_HURT) == 4, "starving stops at a tenth of full health (%.1f after %d hits)", h,
              starved);

        s.food = 3.0f;
        h = 40.0f;
        CreativeTick(h, kFull);
        float dead = 0.0f;
        CreativeTick(dead, kFull);
        CHECK(s.food == 20.0f && h == kFull && dead == 0.0f, "creative: fed and healthy, but the dead stay dead");
        s.food = 6.0f;
        const bool hungry = TooHungryToSprint();
        s.food = 7.0f;
        CHECK(hungry && !TooHungryToSprint(), "no sprinting at 6 food or less");
        s.food = 3.0f;
        s.saturation = 0.0f;
        s.exhaustion = 2.0f;
        s.xpLevel = 7;
        s.Respawn();
        CHECK(s.food == 20.0f && s.saturation == 5.0f && s.xpLevel == 7, "respawn fills the stomach");
        s.NewGame();
        CHECK(s.xpLevel == 0 && s.exhaustion == 0.0f, "a new game starts without experience");
    }

    // status effects
    {
        ResetSurvival();
        AddEffect(EFFECT_POISON, 10.0f, 0);
        AddEffect(EFFECT_POISON, 5.0f, 0);
        CHECK(s.effects[EFFECT_POISON].time == 10.0f, "the same strength keeps the longer time");
        AddEffect(EFFECT_POISON, 3.0f, 1);
        CHECK(s.effects[EFFECT_POISON].time == 3.0f && s.effects[EFFECT_POISON].amp == 1, "a stronger effect replaces a weaker one");
        AddEffect(EFFECT_POISON, 60.0f, 0);
        CHECK(s.effects[EFFECT_POISON].amp == 1, "a weaker effect does not");
        AddEffect(EFFECT_HUNGER, 0.0f, 0);
        AddEffect(-1, 5.0f, 0);
        AddEffect(EFFECT_COUNT, 5.0f, 0);
        CHECK(!HasEffect(EFFECT_HUNGER), "no time, no effect");
        AddEffect(EFFECT_ABSORPTION, 120.0f, 0);
        CHECK(s.absorption == 4.0f, "absorption I: two yellow hearts");
        AddEffect(EFFECT_ABSORPTION, 120.0f, 3);
        CHECK(s.absorption == 16.0f, "absorption IV: eight yellow hearts");
        RemoveEffect(EFFECT_ABSORPTION);
        CHECK(s.absorption == 0.0f && !HasEffect(EFFECT_ABSORPTION) && HasEffect(EFFECT_POISON), "removing one effect");
        ClearEffects();
        CHECK(!HasEffect(EFFECT_POISON), "clearing all effects");

        // regeneration II: a point every 1.25 s
        float h = 50.0f;
        SurvivalEvents ev;
        AddEffect(EFFECT_REGENERATION, 5.0f, 1);
        for (int i = 0; i < 120; ++i)
            EffectsTick(0.05f, h, kFull, ev);
        CHECK(h >= 65.0f && h <= 70.0f && !HasEffect(EFFECT_REGENERATION) && ev.expired == (1u << EFFECT_REGENERATION),
              "regeneration II for 5 s: health %.1f", h);
        h = 99.0f;
        AddEffect(EFFECT_REGENERATION, 5.0f, 1);
        for (int i = 0; i < 120; ++i)
            EffectsTick(0.05f, h, kFull, ev);
        CHECK(h == kFull, "regeneration stops at full health (%.1f)", h);

        // poison hurts past the armour, but never kills
        ev = SurvivalEvents();
        h = 12.0f;
        AddEffect(EFFECT_POISON, 30.0f, 0);
        for (int i = 0; i < 400; ++i)
            EffectsTick(0.05f, h, kFull, ev);
        CHECK(h == 7.0f && ev.directDamage == 5.0f && HasEffect(EFFECT_POISON), "poison leaves the last heart (health %.1f)", h);
        ClearEffects();

        // hunger tires: 0.1 a second per level
        AddEffect(EFFECT_HUNGER, 30.0f, 2);
        s.exhaustion = 0.0f;
        for (int i = 0; i < 50; ++i)
            EffectsTick(0.1f, h, kFull, ev);
        CHECK(Near(s.exhaustion, 1.5f, 1e-3f), "hunger III for 5 s: exhaustion %.3f", s.exhaustion);

        // resistance takes a fifth off a hit, the yellow hearts take it first
        ResetSurvival();
        CHECK(AbsorbDamage(50.0f, kFull) == 50.0f && AbsorbDamage(0.0f, kFull) == 0.0f && AbsorbDamage(-3.0f, kFull) == 0.0f,
              "no effects, the whole hit");
        AddEffect(EFFECT_RESISTANCE, 60.0f, 0);
        CHECK(Near(AbsorbDamage(50.0f, kFull), 40.0f), "resistance I: %.2f of 50", AbsorbDamage(50.0f, kFull));
        ResetSurvival();
        AddEffect(EFFECT_ABSORPTION, 60.0f, 0);
        CHECK(AbsorbDamage(5.0f, kFull) == 0.0f && Near(s.absorption, 3.0f), "a small hit only costs a yellow half heart");
        CHECK(Near(AbsorbDamage(30.0f, kFull), 15.0f) && s.absorption == 0.0f && !HasEffect(EFFECT_ABSORPTION),
              "a big hit uses the yellow hearts up");
    }

    // air: fifteen seconds of it, then a heart a second
    {
        ResetSurvival();
        float h = kFull;
        SurvivalEvents ev;
        for (int i = 0; i < 150; ++i)
            BreathTick(0.1f, true, true, h, kFull, ev);
        CHECK(!ev.drowned && h == kFull && Near(s.air, 0.0f, 0.01f), "15 s under water: out of air (%.1f)", s.air);
        for (int i = 0; i < 10; ++i)
            BreathTick(0.1f, true, true, h, kFull, ev);
        CHECK(ev.drowned && h == 90.0f && ev.directDamage == 10.0f, "then drowning: health %.1f", h);
        for (int i = 0; i < 10; ++i)
            BreathTick(0.1f, true, true, h, kFull, ev);
        CHECK(h == 80.0f, "a hit every second (%.1f)", h);
        BreathTick(1.0f, false, true, h, kFull, ev);
        CHECK(Near(s.air, 80.0f, 0.01f), "air comes back above water (%.1f)", s.air);
        for (int i = 0; i < 5; ++i)
            BreathTick(1.0f, false, true, h, kFull, ev);
        CHECK(s.air == kMaxAir, "up to ten bubbles");
        s.air = 100.0f;
        BreathTick(0.1f, true, false, h, kFull, ev);
        CHECK(s.air == kMaxAir, "creative needs no air");
        s.air = -19.0f;
        h = 5.0f;
        BreathTick(0.1f, true, true, h, kFull, ev);
        CHECK(h == 0.0f, "drowning kills");
    }

    // armour: 4% a point, at most 80%; hits wear it out
    {
        ResetSurvival();
        CHECK(ArmorPoints() == 0 && ArmorBlock() == 0.0f, "no armour");
        gInv.armor[ARMOR_HEAD] = Stack(ID_IRON_HELMET);
        gInv.armor[ARMOR_CHEST] = Stack(ID_IRON_CHESTPLATE);
        gInv.armor[ARMOR_LEGS] = Stack(ID_IRON_LEGGINGS);
        gInv.armor[ARMOR_FEET] = Stack(ID_IRON_BOOTS);
        CHECK(ArmorPoints() == 15 && Near(ArmorBlock(), 0.6f), "iron armour: %d points", ArmorPoints());
        CHECK(WearArmor(3) == 0 && gInv.armor[ARMOR_HEAD].damage == 3 && gInv.armor[ARMOR_FEET].damage == 3, "a hit wears every piece");
        gPlayed.clear();
        const int broke = WearArmor(Item(ID_IRON_BOOTS).durability - 3);
        CHECK(broke >= 1 && gInv.armor[ARMOR_FEET].Empty() && Heard(SND_TOOL_BREAK) == broke, "worn out pieces break (%d did)", broke);
        gInv.armor[ARMOR_HEAD] = Stack(ID_DIAMOND_HELMET);
        gInv.armor[ARMOR_CHEST] = Stack(ID_DIAMOND_CHESTPLATE);
        gInv.armor[ARMOR_LEGS] = Stack(ID_DIAMOND_LEGGINGS);
        gInv.armor[ARMOR_FEET] = Stack(ID_DIAMOND_BOOTS);
        CHECK(ArmorPoints() == 20 && Near(ArmorBlock(), 0.8f), "diamond armour: %d points", ArmorPoints());
        gInv = PlayerInventory();
        gInv.armor[ARMOR_CHEST] = Stack(ID_ELYTRA);
        CHECK(WearArmor(30000) == 0 && gInv.armor[ARMOR_CHEST].damage == 0, "hits do not wear the elytra");
    }

    // totem of undying
    {
        ResetSurvival();
        gInv.offhand = Stack(ID_TOTEM_OF_UNDYING);
        float h = 20.0f;
        CHECK(!UseTotem(h, kFull) && !gInv.offhand.Empty(), "the totem waits for the last moment");
        h = 10.0f;
        AddEffect(EFFECT_POISON, 30.0f, 0);
        gPlayed.clear();
        CHECK(UseTotem(h, kFull) && h == 50.0f && gInv.offhand.Empty() && Heard(SND_TOTEM) == 1, "the totem saves: health %.1f", h);
        CHECK(!HasEffect(EFFECT_POISON) && HasEffect(EFFECT_REGENERATION) && HasEffect(EFFECT_FIRE_RESISTANCE) && s.absorption == 8.0f,
              "and swaps the effects");
        h = 10.0f;
        CHECK(!UseTotem(h, kFull) && h == 10.0f, "once");
        gInv.Held() = Stack(ID_TOTEM_OF_UNDYING);
        gInv.offhand = Stack(ID_TOTEM_OF_UNDYING);
        CHECK(UseTotem(h, kFull) && gInv.Held().Empty() && !gInv.offhand.Empty(), "the main hand's totem goes first");
    }

    // eating and drinking
    {
        ResetSurvival();
        const ItemDef& bread = Item(ID_BREAD);
        CHECK(bread.food == 5 && Item(ID_MILK_BUCKET).special == SP_MILK, "bread is worth %d", bread.food);
        s.food = 10.0f;
        CHECK(CanEat(ID_BREAD, true) && !CanEat(ID_BREAD, false) && !CanEat(ID_STONE, true), "food is for the hungry survivor");
        s.food = 20.0f;
        CHECK(!CanEat(ID_BREAD, true) && CanEat(ID_GOLDEN_APPLE, true) && CanEat(ID_GOLDEN_APPLE, false) && CanEat(ID_MILK_BUCKET, false),
              "golden apples and milk always go down");

        s.food = 10.0f;
        s.saturation = 0.0f;
        gInv.Held() = Stack(ID_BREAD, 2);
        FinishEating(true);
        CHECK(s.food == 15.0f && Near(s.saturation, std::min(15.0f, bread.saturation)) && gInv.Held().count == 1,
              "bread: food %.1f, saturation %.1f", s.food, s.saturation);
        FinishEating(true);
        CHECK(s.food == 20.0f && gInv.Held().Empty() && s.saturation <= s.food, "the last one empties the hand");
        gInv.Held() = Stack(ID_COOKED_BEEF);
        FinishEating(true);
        CHECK(s.food == 20.0f && s.saturation <= 20.0f, "the stomach holds 20");

        gInv.Held() = Stack(ID_GOLDEN_APPLE);
        FinishEating(false);
        CHECK(gInv.Held().count == 1 && HasEffect(EFFECT_REGENERATION) && s.absorption == 4.0f, "golden apple (creative keeps it)");
        gInv.Held() = Stack(ID_ENCHANTED_GOLDEN_APPLE);
        FinishEating(true);
        CHECK(gInv.Held().Empty() && s.absorption == 16.0f && HasEffect(EFFECT_RESISTANCE) && HasEffect(EFFECT_FIRE_RESISTANCE),
              "enchanted golden apple");
        gInv.Held() = Stack(ID_MILK_BUCKET);
        FinishEating(true);
        CHECK(!HasEffect(EFFECT_RESISTANCE) && s.absorption == 0.0f && gInv.Held().id == ID_BUCKET && gInv.Held().count == 1,
              "milk clears the effects and leaves the bucket");
        AddEffect(EFFECT_POISON, 30.0f, 0);
        AddEffect(EFFECT_HUNGER, 30.0f, 0);
        gInv.Held() = Stack(ID_HONEY_BOTTLE);
        FinishEating(true);
        CHECK(!HasEffect(EFFECT_POISON) && HasEffect(EFFECT_HUNGER), "honey cures poison only");
        gInv.Held() = Stack(ID_SPIDER_EYE);
        FinishEating(true);
        CHECK(HasEffect(EFFECT_POISON), "spider eyes poison");

        // eating takes 1.6 s of holding "use", with chewing on the way and a burp at the end
        ResetSurvival();
        gPlayed.clear();
        s.food = 10.0f;
        gInv.Held() = Stack(ID_BREAD, 2);
        EatResult r;
        int meals = 0;
        for (int i = 0; i < 30; ++i) { // 1.5 s
            r = EatingTick(0.05f, true, true);
            meals += r.finished != 0;
        }
        CHECK(r.eating && meals == 0 && s.food == 10.0f && Heard(SND_EAT) >= 6 && Heard(SND_EAT) <= 8 && Heard(SND_BURP) == 0,
              "chewing for 1.5 s (%d bites heard)", Heard(SND_EAT));
        r = EatingTick(0.05f, false, true);
        CHECK(!r.eating && s.eatTimer == 0.0f, "letting go starts over");
        int eaten = 0;
        for (int i = 0; i < 40; ++i) { // 2 s
            r = EatingTick(0.05f, true, true);
            if (r.finished) {
                eaten = r.finished;
                ++meals;
            }
        }
        CHECK(meals == 1 && eaten == ID_BREAD && s.food == 15.0f && gInv.Held().count == 1 && Heard(SND_BURP) == 1,
              "one bread eaten after 1.6 s (food %.1f)", s.food);
        gInv.Held() = Stack(ID_STONE);
        CHECK(!EatingTick(0.05f, true, true).eating, "stone is not food");
        gInv.Held().Clear();
        CHECK(!EatingTick(0.05f, true, true).eating, "nor is an empty hand");
        gInv.Held() = Stack(ID_MILK_BUCKET);
        eaten = 0;
        for (int i = 0; i < 34 && !eaten; ++i)
            eaten = EatingTick(0.05f, true, false).finished;
        CHECK(eaten == ID_MILK_BUCKET && Heard(SND_DRINK) >= 5 && Heard(SND_BURP) == 2, "milk is drunk, not chewed");

        srand(12345);
        int sick = 0;
        for (int i = 0; i < 2000; ++i) {
            ClearEffects();
            gInv.Held() = Stack(ID_ROTTEN_FLESH);
            FinishEating(true);
            sick += HasEffect(EFFECT_HUNGER);
        }
        CHECK(sick > 1500 && sick < 1700, "rotten flesh makes hungry 4 times in 5 (%d of 2000)", sick);
    }
    ResetSurvival();
}

// ---------------------------------------------------------------- loose things (Entities.h, Particles.h, Host.h)
// lets the items, the TNT, the particles and the blocks live for a while
static void RunEntities(float seconds, const Vec3* collector = nullptr, float reach = 1.6f) {
    const float dt = 0.05f;
    for (float t = 0.0f; t < seconds; t += dt) {
        DropsTick(dt, collector, reach);
        TntTick(dt);
        ParticlesTick(dt);
        gBlockClock += dt;
        BlocksTick(dt, gBlockClock);
    }
}

static void TestEntities() {
    TestHost host;
    SetHost(&host);
    srand(77);
    const Vec3 still(0, 0, 0.01f); // (an item spawned without any speed hops off in a random direction)

    // the ground under a point
    {
        BlockStage();
        float z = 0.0f;
        CHECK(GroundBelow(Vec3(0.5f, 0.5f, 13.0f), 5.0f, &z) && z == 10.0f, "the floor is the ground (%.2f)", z);
        CHECK(!GroundBelow(Vec3(0.5f, 0.5f, 13.0f), 2.0f, &z), "but not when it is further down than asked for");
        CHECK(!GroundBelow(Vec3(30.5f, 0.5f, 13.0f), 40.0f, &z), "no floor, no ground");
        gWorld.SetRaw(0, 0, 11, MakeVox(ID_STONE));
        CHECK(GroundBelow(Vec3(0.5f, 0.5f, 13.0f), 5.0f, &z) && z == 12.0f, "a block on the floor is higher ground");
    }

    // items on the ground
    {
        BlockStage();
        ResetSurvival();
        gPlayed.clear();
        SpawnDrop(Vec3(0.5f, 0.5f, 14.0f), Stack(ID_DIAMOND, 3), still);
        RunEntities(3.0f);
        CHECK(gDrops.size() == 1 && Near(gDrops[0].pos.z, 10.0f, 1e-3f) && gDrops[0].stack.count == 3,
              "an item falls to the floor and stays there");
        const Vec3 away(8.5f, 0.5f, 11.0f), close(1.0f, 0.5f, 11.0f);
        RunEntities(1.0f, &away);
        CHECK(gDrops.size() == 1 && gInv.CountOf(ID_DIAMOND) == 0, "too far away to pick up");
        RunEntities(0.2f, &close);
        CHECK(gDrops.empty() && gInv.CountOf(ID_DIAMOND) == 3 && Heard(SND_PICKUP) == 1, "walking up to it picks it up");

        SpawnDrop(Vec3(1.0f, 0.5f, 10.2f), Stack(ID_STICK, 1), still, 1.5f);
        RunEntities(1.0f, &close);
        CHECK(gDrops.size() == 1, "an item that was just thrown waits before it can be picked up");
        RunEntities(1.0f, &close);
        CHECK(gDrops.empty() && gInv.CountOf(ID_STICK) == 1, "then it can");

        for (auto& slot : gInv.slots)
            slot = Stack(ID_STONE, 64);
        SpawnDrop(Vec3(1.0f, 0.5f, 10.2f), Stack(ID_DIRT, 5), still, 0.0f);
        RunEntities(1.0f, &close);
        CHECK(gDrops.size() == 1 && gDrops[0].stack.count == 5, "a full inventory leaves it lying");
        gInv.slots[7] = Stack(ID_DIRT, 62);
        RunEntities(0.2f, &close);
        CHECK(gDrops.size() == 1 && gDrops[0].stack.count == 3 && gInv.slots[7].count == 64, "what fits is taken, the rest stays");
        gDrops.clear();
        ResetSurvival();

        SpawnDrop(Vec3(0.5f, 0.5f, 10.2f), Stack(ID_COAL, 10), still);
        SpawnDrop(Vec3(0.9f, 0.5f, 10.2f), Stack(ID_COAL, 20), still);
        SpawnDrop(Vec3(0.7f, 0.5f, 10.2f), Stack(ID_STICK, 1), still);
        RunEntities(1.5f);
        CHECK(gDrops.size() == 2 && CountDrops(ID_COAL) == 30 && CountDrops(ID_STICK) == 1, "equal items lying together become one stack");

        gDrops.clear();
        gPlayed.clear();
        gWorld.SetRaw(3, 3, 10, MakeVox(ID_LAVA));
        SpawnDrop(Vec3(3.5f, 3.5f, 12.0f), Stack(ID_DIAMOND, 1), still);
        RunEntities(2.0f);
        CHECK(gDrops.empty() && Heard(SND_LAVA_EXTINGUISH) == 1, "lava burns items");
        for (int wz = 10; wz <= 12; ++wz)
            gWorld.SetRaw(-3, -3, wz, MakeVox(ID_WATER));
        SpawnDrop(Vec3(-2.5f, -2.5f, 10.1f), Stack(ID_DIAMOND, 1), still);
        RunEntities(3.0f);
        CHECK(gDrops.size() == 1 && gDrops[0].pos.z > 12.0f, "items float up in water (%.2f)", gDrops.empty() ? 0.0f : gDrops[0].pos.z);

        gDrops.clear();
        SpawnDrop(Vec3(0.5f, 0.5f, 10.2f), Stack(ID_DIRT, 1), still);
        gDrops[0].age = 299.5f;
        RunEntities(1.0f);
        CHECK(gDrops.empty(), "items vanish after five minutes");

        // what blocks and containers leave behind
        SpawnBlockDrops(ID_STONE, Vec3(0.5f, 0.5f, 10.5f));
        CHECK(CountDrops(ID_COBBLESTONE) == 1 && CountDrops(ID_STONE) == 0, "stone leaves cobblestone");
        gDrops.clear();
        SpawnBlockDrops(ID_OAK_LOG, Vec3(0.5f, 0.5f, 10.5f));
        CHECK(CountDrops(ID_OAK_LOG) == 1, "a log leaves itself");
        gDrops.clear();
        SpawnDropItem(Vec3(0.5f, 0.5f, 10.5f), ID_DIRT, 150);
        CHECK(gDrops.size() == 3 && CountDrops(ID_DIRT) == 150, "a big pile is split into stacks");
        gDrops.clear();
        gWorld.SetRaw(5, 5, 10, MakeVox(ID_CHEST));
        gWorld.chests[Int3{ 5, 5, 10 }].slots[3] = Stack(ID_DIAMOND, 7);
        DropContainerContents({ 5, 5, 10 });
        CHECK(CountDrops(ID_DIAMOND) == 7 && gWorld.chests.count(Int3{ 5, 5, 10 }) == 0, "a chest that goes spills what is in it");

        // throwing something out of the hand
        gDrops.clear();
        gGame = GameState();
        gGame.lookDir = Vec3(1, 0, 0);
        DropStackAtPlayer(Stack(ID_STICK, 2), true);
        CHECK(gDrops.empty(), "nobody there, nothing thrown");
        host.hasPlayer = true;
        host.player = Vec3(0.5f, 0.5f, 11.0f);
        DropStackAtPlayer(Stack(ID_STICK, 2), true);
        CHECK(gDrops.size() == 1 && Near(gDrops[0].pos.x, 1.1f) && Near(gDrops[0].vel.x, 4.0f) && gDrops[0].pickupDelay == 1.5f,
              "an item is thrown the way the player looks");
    }

    // TNT and what an explosion does
    {
        BlockStage();
        ResetSurvival();
        gGame = GameState();
        gPlayed.clear();
        host.explosions = 0;
        gWorld.SetRaw(0, 0, 10, MakeVox(ID_TNT));
        gWorld.SetRaw(1, 0, 10, MakeVox(ID_STONE));
        gWorld.SetRaw(-1, 0, 10, MakeVox(ID_OBSIDIAN));
        gWorld.SetRaw(2, 0, 10, MakeVox(ID_TNT));
        IgniteTnt({ 0, 0, 10 }, 1.0f);
        CHECK(gWorld.GetBlock(0, 0, 10) == ID_AIR && gPrimedTnt.size() == 1 && Heard(SND_FUSE) == 1, "lit TNT becomes a primed one");
        RunEntities(0.5f);
        CHECK(gPrimedTnt.size() == 1 && gWorld.GetBlock(1, 0, 10) == ID_STONE && Heard(SND_EXPLODE) == 0, "the fuse burns");
        RunEntities(0.6f);
        CHECK(Heard(SND_EXPLODE) == 1 && host.explosions == 1, "then it blows up");
        CHECK(gWorld.GetBlock(1, 0, 10) == ID_AIR && gWorld.GetBlock(0, 0, 9) == ID_AIR && gWorld.GetBlock(-1, 0, 10) == ID_OBSIDIAN,
              "stone and the floor go, obsidian stays");
        CHECK(gWorld.GetBlock(2, 0, 10) == ID_AIR && gPrimedTnt.size() == 1 && Heard(SND_FUSE) == 2, "TNT next to it is lit and thrown");
        CHECK(!gParticles.empty(), "bits fly");
        RunEntities(4.0f);
        CHECK(gPrimedTnt.empty() && Heard(SND_EXPLODE) == 2 && host.explosions == 2, "and blows up in turn");

        // survival: some of the broken blocks lie around as items; creative: none
        for (int mode = 0; mode < 2; ++mode) {
            BlockStage();
            gGame.gameMode = mode == 0 ? MODE_SURVIVAL : MODE_CREATIVE;
            const int before = CountBlocks(ID_STONE);
            ExplodeAt(Vec3(0.5f, 0.5f, 9.5f), 4.0f, false);
            const int broken = before - CountBlocks(ID_STONE);
            if (mode == 0)
                CHECK(broken > 20 && CountDrops(ID_COBBLESTONE) > broken / 10 && CountDrops(ID_COBBLESTONE) < broken * 6 / 10,
                      "about a third of the %d broken blocks drop (%d did)", broken, CountDrops(ID_COBBLESTONE));
            else
                CHECK(broken > 20 && gDrops.empty(), "no drops from explosions in creative");
        }
        gGame.gameMode = MODE_SURVIVAL;
        CHECK(host.explosions == 2 && Heard(SND_EXPLODE) == 2, "a blast that is not ours has no fire ball of ours");

        BlockStage();
        gRules.explosionsBreakBlocks = false;
        ExplodeAt(Vec3(0.5f, 0.5f, 9.5f), 4.0f, false);
        CHECK(CountBlocks(ID_STONE) == 625, "the game rule keeps the blocks whole");
        gRules.explosionsBreakBlocks = true;

        SpawnDrop(Vec3(2.5f, 0.5f, 10.0f), Stack(ID_STICK, 1), still);
        PushLooseThings(Vec3(0.5f, 0.5f, 10.0f), 6.0f, 14.0f);
        CHECK(gDrops[0].vel.x > 5.0f && gDrops[0].vel.z > 1.0f && std::fabs(gDrops[0].vel.y) < 1e-3f, "a blast throws loose things away");

        // the chest the player is looking into blows up: the screen closes, the contents lie around
        BlockStage();
        gPlayed.clear();
        gGame.viewW = 800.0f;
        gGame.viewH = 600.0f;
        gWorld.SetRaw(1, 0, 10, MakeVox(ID_CHEST));
        OpenScreen(SCREEN_CHEST, { 1, 0, 10 });
        CHECK(gGame.screen == SCREEN_CHEST && gGame.openPos == (Int3{ 1, 0, 10 }) && gGame.cursorX == 400.0f && gGame.cursorY == 300.0f,
              "a chest opens with the cursor in the middle");
        CHECK(gWorld.chests.count(Int3{ 1, 0, 10 }) == 1 && Heard(SND_CHEST_OPEN) == 1, "and creaks");
        gWorld.chests[Int3{ 1, 0, 10 }].slots[0] = Stack(ID_DIAMOND, 2);
        ExplodeAt(Vec3(0.5f, 0.5f, 10.5f), 3.0f, false);
        CHECK(gGame.screen == SCREEN_NONE && Heard(SND_CHEST_CLOSE) == 1 && gWorld.GetBlock(1, 0, 10) == ID_AIR &&
                  CountDrops(ID_DIAMOND) == 2,
              "a chest that blows up closes its screen and spills");
    }

    // closing a screen: what was left on the crafting grid and on the cursor comes back
    {
        BlockStage();
        ResetSurvival();
        gGame = GameState();
        OpenScreen(SCREEN_CRAFTING, { 0, 0, 10 });
        gInv.craft3[4] = Stack(ID_STICK, 3);
        gInv.craft[0] = Stack(ID_COAL, 1);
        gInv.cursor = Stack(ID_DIAMOND, 5);
        CloseScreen();
        CHECK(gGame.screen == SCREEN_NONE && gInv.CountOf(ID_STICK) == 3 && gInv.CountOf(ID_COAL) == 1 && gInv.CountOf(ID_DIAMOND) == 5 &&
                  gInv.cursor.Empty() && gInv.craft3[4].Empty() && gInv.craft[0].Empty() && gDrops.empty(),
              "the crafting grid and the cursor are emptied into the inventory");
        for (auto& slot : gInv.slots)
            slot = Stack(ID_STONE, 64);
        gInv.cursor = Stack(ID_DIAMOND, 5);
        CloseScreen();
        CHECK(gInv.cursor.Empty() && CountDrops(ID_DIAMOND) == 5, "or at the player's feet when it is full");
        OpenScreen(SCREEN_FURNACE, { 2, 2, 10 });
        CHECK(gWorld.furnaces.count(Int3{ 2, 2, 10 }) == 1, "a furnace gets its inside when it is first opened");
        CloseScreen();
    }

    // particles
    {
        gParticles.clear();
        SpawnBreakParticles({ 0, 0, 10 }, ID_STONE);
        bool inside = gParticles.size() == 16;
        for (const Particle& p : gParticles)
            inside = inside && p.pos.x > 0.0f && p.pos.x < 1.0f && p.pos.y > 0.0f && p.pos.y < 1.0f && p.pos.z > 10.0f && p.pos.z < 11.0f;
        CHECK(inside, "a broken block leaves 16 bits where it was");
        const float z0 = gParticles[0].pos.z;
        ParticlesTick(0.05f);
        CHECK(gParticles.size() == 16 && gParticles[0].pos.z != z0, "they fly");
        for (int i = 0; i < 30; ++i)
            ParticlesTick(0.05f);
        CHECK(gParticles.empty(), "and are gone after a second");
        for (int i = 0; i < 2000; ++i)
            SpawnParticle(Particle());
        CHECK(gParticles.size() == 1500, "never more than 1500 at once");
    }

    SetHost(nullptr);
    BlockStage();
    gWorld.Clear();
    ResetSurvival();
    gGame = GameState();
}

// ---------------------------------------------------------------- arm's length (Interact.h)
// the player's eyes are at `eye` and look along `dir`
static void LookFrom(const Vec3& eye, const Vec3& dir) {
    gGame.eyePos = gGame.rayOrigin = eye;
    gGame.lookDir = dir;
    UpdateTarget();
}

// holds the attack button for a while
static void Mine(float seconds, bool held = true) {
    const float dt = 0.05f;
    const int ticks = (int)(seconds / dt + 0.5f);
    for (int i = 0; i < ticks; ++i) {
        InteractTick(dt);
        MineTick(dt, held);
    }
}

static void TestInteract() {
    TestHost host;
    SetHost(&host);
    srand(99);
    const Vec3 eye(0.5f, 0.5f, 11.62f), down(0, 0, -1);

    // how long a block takes, and whether the tool gets its drops
    {
        bool harvest = true;
        CHECK(Near(BreakSeconds(ID_STONE, ItemStack(), &harvest), 7.5f) && !harvest, "stone by hand: 7.5 s, and nothing to show for it");
        CHECK(Near(BreakSeconds(ID_STONE, Stack(ID_WOODEN_PICKAXE), &harvest), 1.125f, 1e-3f) && harvest, "stone with a wooden pickaxe: %.3f s",
              BreakSeconds(ID_STONE, Stack(ID_WOODEN_PICKAXE), nullptr));
        CHECK(Near(BreakSeconds(ID_STONE, Stack(ID_DIAMOND_PICKAXE), nullptr), 0.28125f, 1e-3f), "a diamond pickaxe is four times as fast");
        CHECK(Near(BreakSeconds(ID_STONE, Stack(ID_WOODEN_SHOVEL), &harvest), 7.5f) && !harvest, "the wrong tool is no better than a hand");
        BreakSeconds(ID_DIAMOND_ORE, Stack(ID_STONE_PICKAXE), &harvest);
        const bool withStone = harvest;
        BreakSeconds(ID_DIAMOND_ORE, Stack(ID_IRON_PICKAXE), &harvest);
        CHECK(!withStone && harvest, "diamond ore wants an iron pickaxe");
        CHECK(BreakSeconds(ID_BEDROCK, Stack(ID_DIAMOND_PICKAXE), &harvest) > 1e8f && !harvest, "bedrock does not break");
        CHECK(BreakSeconds(ID_POPPY, ItemStack(), &harvest) == 0.0f && harvest, "a flower breaks at once");
        CHECK(Near(BreakSecondsFor(ID_STONE, 4.0f, Stack(ID_WOODEN_PICKAXE), nullptr), 3.0f, 1e-3f), "a thing of the host's has its own hardness");
    }

    // what the player looks at
    {
        BlockStage();
        gGame = GameState();
        LookFrom(eye, down);
        CHECK(gTarget.valid && gTarget.voxel && gTarget.pos == (Int3{ 0, 0, 9 }) && gTarget.key == gTarget.pos && gTarget.face == FACE_TOP &&
                  Near(gTarget.point.z, 10.0f) && gTarget.normal.z == 1.0f,
              "looking down: the floor block, from above");
        LookFrom(Vec3(0.5f, 0.5f, 15.2f), down);
        CHECK(!gTarget.valid, "survival does not reach 5.2 blocks");
        gGame.gameMode = MODE_CREATIVE;
        LookFrom(Vec3(0.5f, 0.5f, 15.2f), down);
        CHECK(gTarget.valid, "creative does");
        gGame.gameMode = MODE_SURVIVAL;
        LookFrom(eye, Vec3(0, 0, 1));
        CHECK(!gTarget.valid, "the sky is no target");
        LookFrom(eye, Vec3());
        CHECK(!gTarget.valid, "no direction, no target");
        gWorld.SetRaw(3, 0, 10, MakeVox(ID_STONE));
        LookFrom(Vec3(0.5f, 0.5f, 10.5f), Vec3(1, 0, 0));
        CHECK(gTarget.valid && gTarget.pos == (Int3{ 3, 0, 10 }) && gTarget.face == FACE_WEST && gTarget.normal.x == -1.0f &&
                  Near(gTarget.point.x, 3.0f),
              "looking east: the west face of the block in the way");
        host.thing = true;
        LookFrom(Vec3(0.5f, 0.5f, 10.5f), Vec3(1, 0, 0));
        CHECK(gTarget.valid && !gTarget.voxel && gTarget.virtualBlock == ID_DIRT && Near(gTarget.point.x, 2.5f), "the host's thing in front of it");
        host.thing = false;
    }

    // mining
    {
        BlockStage();
        ResetSurvival();
        gGame = GameState();
        gPlayed.clear();
        gInv.Held() = Stack(ID_WOODEN_PICKAXE);
        LookFrom(eye, down);
        Mine(1.0f);
        CHECK(gWorld.GetBlock(0, 0, 9) == ID_STONE && MiningProgress() > 0.8f && MiningProgress() < 0.95f, "after a second the stone is cracked (%.2f)",
              MiningProgress());
        CHECK(Heard(HitSound(ID_STONE)) >= 3 && Heard(HitSound(ID_STONE)) <= 4 && gGame.swing >= 0.0f, "with a knock four times a second (%d)",
              Heard(HitSound(ID_STONE)));
        Mine(0.05f, false);
        CHECK(MiningProgress() == 0.0f, "letting go starts over");
        Mine(1.15f);
        CHECK(gWorld.GetBlock(0, 0, 9) == ID_AIR && CountDrops(ID_COBBLESTONE) == 1 && Heard(DigSound(ID_STONE)) == 1 && !gParticles.empty(),
              "1.125 s with a wooden pickaxe: it breaks and drops");
        CHECK(gInv.Held().damage == 1 && Near(gSurvival.exhaustion, kExhaustMine) && MiningProgress() == 0.0f, "the pickaxe wears, the miner tires");
        gWorld.SetRaw(0, 0, 9, MakeVox(ID_STONE));
        LookFrom(eye, down);
        Mine(0.2f);
        CHECK(MiningProgress() == 0.0f, "a short pause before the next block");
        Mine(0.2f);
        CHECK(MiningProgress() > 0.0f, "then on");

        gInv.Held().Clear();
        gDrops.clear();
        Mine(0.05f, false); // (the cracks made with the pickaxe would count)
        Mine(7.0f);
        CHECK(gWorld.GetBlock(0, 0, 9) == ID_STONE, "by hand it takes long");
        Mine(1.0f);
        CHECK(gWorld.GetBlock(0, 0, 9) == ID_AIR && gDrops.empty(), "and leaves nothing");

        // ores give experience, to the right tool only
        gWorld.SetRaw(0, 0, 9, MakeVox(ID_DIAMOND_ORE));
        gInv.Held() = Stack(ID_IRON_PICKAXE);
        BreakVoxel({ 0, 0, 9 }, true);
        int xp = 0;
        for (const XpOrb& o : gXpOrbs)
            xp += o.value;
        CHECK(CountDrops(ID_DIAMOND) == 1 && xp >= 3 && xp <= 7, "diamond ore: a diamond and %d experience", xp);
        gDrops.clear();
        gXpOrbs.clear();
        gWorld.SetRaw(0, 0, 9, MakeVox(ID_DIAMOND_ORE));
        gInv.Held() = Stack(ID_STONE_PICKAXE);
        BreakVoxel({ 0, 0, 9 }, true);
        CHECK(gWorld.GetBlock(0, 0, 9) == ID_AIR && gDrops.empty() && gXpOrbs.empty(), "with a stone pickaxe: nothing");
        gWorld.SetRaw(0, 0, 9, MakeVox(ID_STONE));
        gPlayed.clear();
        gParticles.clear();
        CHECK(PlaceFire({ 0, 0, 10 }), "a fire");
        BreakVoxel({ 0, 0, 10 }, true);
        CHECK(gWorld.GetBlock(0, 0, 10) == ID_AIR && Heard(SND_FIRE_EXTINGUISH) == 1 && gParticles.empty(), "a fire is punched out");

        // creative: at once, without drops, one block every quarter second, and never with a sword
        gGame.gameMode = MODE_CREATIVE;
        gInv.Held().Clear();
        gDrops.clear();
        LookFrom(eye, down);
        Mine(0.3f, false);
        Mine(0.05f);
        CHECK(gWorld.GetBlock(0, 0, 9) == ID_AIR && gDrops.empty() && MiningProgress() == 0.0f, "creative breaks at once and takes nothing");
        gWorld.SetRaw(0, 0, 9, MakeVox(ID_STONE));
        Mine(0.05f);
        CHECK(gWorld.GetBlock(0, 0, 9) == ID_STONE, "but not two blocks in the same quarter second");
        gInv.Held() = Stack(ID_DIAMOND_SWORD);
        Mine(1.0f);
        CHECK(gWorld.GetBlock(0, 0, 9) == ID_STONE, "and a sword in creative breaks nothing");
        gGame.gameMode = MODE_SURVIVAL;

        // a thing of the host's: mined by its own hardness, broken by the host
        gInv.Held().Clear();
        host.thing = true;
        host.thingsBroken = 0;
        LookFrom(eye, down);
        Mine(0.3f, false);
        Mine(0.6f);
        CHECK(host.thingsBroken == 0 && MiningProgress() > 0.5f, "the host's dirt is being dug (%.2f)", MiningProgress());
        Mine(0.3f);
        CHECK(host.thingsBroken == 1, "and broken by the host after 0.75 s");
        host.thing = false;
    }

    // placing
    {
        BlockStage();
        ResetSurvival();
        gGame = GameState();
        gPlayed.clear();
        gInv.Held() = Stack(ID_STONE, 2);
        LookFrom(eye, down);
        PlaceHeldBlock();
        CHECK(gWorld.GetBlock(0, 0, 10) == ID_STONE && gInv.Held().count == 1 && Heard(PlaceSound(ID_STONE)) == 1 && gGame.swing == 0.0f,
              "a block is put on the face looked at");
        CHECK(!PlaceReady(), "the next one has to wait a moment");
        Mine(0.3f, false);
        CHECK(PlaceReady(), "a quarter of a second");
        gGame.gameMode = MODE_CREATIVE;
        LookFrom(Vec3(4.5f, 4.5f, 11.62f), down);
        PlaceHeldBlock();
        CHECK(gWorld.GetBlock(4, 4, 10) == ID_STONE && gInv.Held().count == 1, "creative keeps the block");
        gGame.gameMode = MODE_SURVIVAL;

        gInv.Held() = Stack(ID_FURNACE);
        LookFrom(Vec3(5.5f, 5.5f, 11.62f), Vec3(0.6f, 0.0f, -0.8f));
        PlaceHeldBlock();
        CHECK(gWorld.GetBlock(6, 5, 10) == ID_FURNACE && VoxMeta(gWorld.Get(6, 5, 10)) == 3 && gWorld.furnaces.count(Int3{ 6, 5, 10 }) == 1 &&
                  gInv.Held().Empty(),
              "a furnace faces the way the player looks, and has an inside");
        gWorld.SetRaw(3, 0, 10, MakeVox(ID_STONE));
        gInv.Held() = Stack(ID_OAK_LOG);
        LookFrom(Vec3(0.5f, 0.5f, 10.5f), Vec3(1, 0, 0));
        PlaceHeldBlock();
        CHECK(gWorld.GetBlock(2, 0, 10) == ID_OAK_LOG && VoxMeta(gWorld.Get(2, 0, 10)) == 1, "a log lies along the face it is put on");

        gInv.Held() = Stack(ID_STONE);
        host.blocking = true;
        host.blocked = Int3{ -3, -3, 10 };
        LookFrom(Vec3(-2.5f, -2.5f, 11.62f), down);
        PlaceHeldBlock();
        CHECK(gWorld.GetBlock(-3, -3, 10) == ID_AIR && gInv.Held().count == 1, "no block where somebody stands");
        host.blocking = false;
        gWorld.SetRaw(-3, -3, 10, MakeVox(ID_WATER));
        PlaceHeldBlock();
        CHECK(gWorld.GetBlock(-3, -3, 10) == ID_STONE, "water makes room for a block");

        gInv.Held() = Stack(ID_POPPY);
        LookFrom(Vec3(-6.5f, -6.5f, 11.62f), down);
        PlaceHeldBlock();
        CHECK(gWorld.GetBlock(-7, -7, 10) == ID_AIR, "no flower on bare stone");
        gWorld.SetRaw(-7, -7, 9, MakeVox(ID_GRASS_BLOCK));
        LookFrom(Vec3(-6.5f, -6.5f, 11.62f), down);
        PlaceHeldBlock();
        CHECK(gWorld.GetBlock(-7, -7, 10) == ID_POPPY, "but on grass");
        gInv.Held() = Stack(ID_WATER_BUCKET);
        PlaceHeldBlock();
        CHECK(gInv.Held().id == ID_WATER_BUCKET, "a bucket is not a block");
    }

    // planting and bone meal
    {
        BlockStage();
        gWorld.SetRaw(0, 0, 9, MakeVox(ID_GRASS_BLOCK));
        gWorld.SetRaw(1, 0, 9, MakeVox(ID_SAND));
        CHECK(CanPlantAt(ID_POPPY, { 0, 0, 10 }, false) && !CanPlantAt(ID_POPPY, { 2, 0, 10 }, false) && CanPlantAt(ID_POPPY, { 2, 0, 10 }, true),
              "flowers want soil (creative plants on anything solid)");
        CHECK(CanPlantAt(ID_DEAD_BUSH, { 1, 0, 10 }, false) && !CanPlantAt(ID_POPPY, { 1, 0, 10 }, false) &&
                  CanPlantAt(ID_RED_MUSHROOM, { 2, 0, 10 }, false),
              "dead bushes grow on sand, mushrooms anywhere");
        CHECK(!CanPlantAt(ID_POPPY, { 0, 0, 9 }, true) && !CanPlantAt(ID_POPPY, { 0, 0, 15 }, true), "not into a block, not in the air");
        host.soil = true;
        CHECK(CanPlantAt(ID_POPPY, { 0, 0, 15 }, false), "the host's earth is soil too");
        host.soil = false;

        for (int x = -4; x <= 4; ++x)
            for (int y = -4; y <= 4; ++y)
                gWorld.SetRaw(x, y, 9, MakeVox(ID_GRASS_BLOCK));
        gParticles.clear();
        CHECK(ApplyBoneMeal(true, { 0, 0, 9 }, Vec3(0.5f, 0.5f, 10.0f), Vec3(0, 0, 1), false), "bone meal works on a grass block");
        int plants = 0;
        for (int x = -4; x <= 4; ++x)
            for (int y = -4; y <= 4; ++y)
                plants += IsPlantBlock(gWorld.GetBlock(x, y, 10));
        CHECK(plants >= 3 && plants <= 10 && !gParticles.empty(), "grass and flowers come up around it (%d)", plants);
        CHECK(!ApplyBoneMeal(true, { 8, 8, 9 }, Vec3(8.5f, 8.5f, 10.0f), Vec3(0, 0, 1), false), "not on stone");
        CHECK(!ApplyBoneMeal(true, { 0, 0, 9 }, Vec3(0.0f, 0.5f, 9.5f), Vec3(-1, 0, 0), false), "nor on the side of a grass block");
        gWorld.SetRaw(6, 6, 10, MakeVox(ID_OAK_SAPLING));
        CHECK(ApplyBoneMeal(true, { 6, 6, 10 }, Vec3(6.5f, 6.5f, 10.5f), Vec3(0, 0, 1), false), "and on a sapling");
    }

    // buckets, bone meal and armour in the hand
    {
        BlockStage();
        ResetSurvival();
        gGame = GameState();
        gPlayed.clear();
        host.doused = 0;
        gInv.Held() = Stack(ID_WATER_BUCKET);
        LookFrom(Vec3(8.5f, 8.5f, 11.62f), down);
        CHECK(UseWorldItem() && gWorld.GetBlock(8, 8, 10) == ID_WATER && gInv.Held().id == ID_BUCKET && host.doused == 1 &&
                  Heard(SND_BUCKET_EMPTY) == 1,
              "a bucket of water is emptied onto the block looked at");
        CHECK(UseWorldItem() && gWorld.GetBlock(8, 8, 10) == ID_AIR && gInv.Held().id == ID_WATER_BUCKET && Heard(SND_BUCKET_FILL) == 1,
              "and scooped up again");
        gGame.gameMode = MODE_CREATIVE;
        CHECK(UseWorldItem() && gWorld.GetBlock(8, 8, 10) == ID_WATER && gInv.Held().id == ID_WATER_BUCKET, "creative keeps the water");
        gGame.gameMode = MODE_SURVIVAL;
        gInv.Held() = Stack(ID_BUCKET, 3);
        CHECK(UseWorldItem() && gInv.Held().id == ID_BUCKET && gInv.Held().count == 2 && gInv.CountOf(ID_WATER_BUCKET) == 1,
              "from a stack of buckets one is filled");
        gInv.Held() = Stack(ID_BUCKET);
        LookFrom(Vec3(8.5f, 8.5f, 11.62f), Vec3(0, 0, 1));
        CHECK(!UseWorldItem(), "nothing to scoop from the sky");

        gWorld.SetRaw(0, 0, 10, MakeVox(ID_OAK_SAPLING));
        gInv.Held() = Stack(ID_BONE_MEAL, 3);
        LookFrom(eye, down);
        CHECK(gTarget.valid && gTarget.pos == (Int3{ 0, 0, 10 }) && UseWorldItem() && gInv.Held().count == 2 && Heard(SND_BONE_MEAL) == 1,
              "bone meal on a sapling is used up");

        gInv.Held() = Stack(ID_IRON_HELMET);
        CHECK(UseWorldItem() && gInv.armor[ARMOR_HEAD].id == ID_IRON_HELMET && gInv.Held().Empty() && Heard(SND_EQUIP_IRON) == 1,
              "a use puts a helmet on");
        gInv.Held() = Stack(ID_DIAMOND_HELMET);
        CHECK(!UseWorldItem() && gInv.Held().id == ID_DIAMOND_HELMET, "not a second one on top");
        gInv.Held() = Stack(ID_STICK);
        CHECK(!UseWorldItem(), "a stick does nothing");

        CHECK(HasRightClickUse(Stack(ID_STONE)) && HasRightClickUse(Stack(ID_BREAD)) && HasRightClickUse(Stack(ID_BOW)) &&
                  HasRightClickUse(Stack(ID_IRON_HELMET)),
              "blocks, food, bows and armour are used");
        CHECK(!HasRightClickUse(ItemStack()) && !HasRightClickUse(Stack(ID_DIAMOND_SWORD)) && !HasRightClickUse(Stack(ID_ARROW)) &&
                  !HasRightClickUse(Stack(ID_TOTEM_OF_UNDYING)),
              "swords, arrows and the totem are not (the off hand gets the use)");
    }

    // containers
    {
        BlockStage();
        gGame = GameState();
        CHECK(IsContainer(ID_CHEST) && IsContainer(ID_FURNACE) && IsContainer(ID_CRAFTING_TABLE) && !IsContainer(ID_STONE), "containers");
        LookFrom(eye, down);
        CHECK(!TargetIsContainer(), "the floor is none");
        gWorld.SetRaw(0, 0, 10, MakeVox(ID_CRAFTING_TABLE));
        LookFrom(Vec3(0.5f, 0.5f, 12.62f), down);
        CHECK(TargetIsContainer(), "a crafting table is");
        OpenTargetContainer();
        CHECK(gGame.screen == SCREEN_CRAFTING && gGame.openPos == (Int3{ 0, 0, 10 }), "and opens its screen");
        CloseScreen();
        gWorld.SetRaw(0, 0, 10, MakeVox(ID_CHEST));
        OpenTargetContainer();
        CHECK(gGame.screen == SCREEN_CHEST, "a chest opens the chest screen");
        CloseScreen();
        gWorld.SetRaw(0, 0, 10, MakeVox(ID_FURNACE));
        OpenTargetContainer();
        CHECK(gGame.screen == SCREEN_FURNACE, "a furnace the furnace screen");
        CloseScreen();
    }

    // the hands: hotbar, off hand, throwing, picking
    {
        BlockStage();
        ResetSurvival();
        gGame = GameState();
        gControls = Controls();
        gPlayed.clear();
        gInv.slots[0] = Stack(ID_STONE, 5);
        gInv.slots[2] = Stack(ID_DIRT);
        gGame.attackTimer = 5.0f;
        HotbarTick();
        CHECK(gInv.selected == 0 && gGame.selectedNameTimer == 0.0f && gGame.attackTimer == 5.0f, "no key, no change");
        gControls.Set(ACT_HOTBAR_3, true, true);
        HotbarTick();
        CHECK(gInv.selected == 2 && gGame.selectedNameTimer == 2.0f && gGame.attackTimer == 0.0f, "3 picks the third slot; the attack recharges");
        gControls = Controls();
        gControls.scrollDown = true;
        HotbarTick();
        CHECK(gInv.selected == 3, "the wheel goes to the next slot");
        gControls.scrollDown = false;
        gControls.scrollUp = true;
        for (int i = 0; i < 4; ++i)
            HotbarTick();
        CHECK(gInv.selected == 8, "and around the end");
        gControls = Controls();

        gInv.selected = 0;
        gInv.offhand = Stack(ID_TOTEM_OF_UNDYING);
        SwapHands();
        CHECK(gInv.Held().id == ID_TOTEM_OF_UNDYING && gInv.offhand.id == ID_STONE && gInv.offhand.count == 5 && Heard(SND_EQUIP_GENERIC) == 1,
              "the hands swap what they hold");
        SwapHands();

        host.hasPlayer = true;
        host.player = Vec3(0.5f, 0.5f, 11.0f);
        gGame.lookDir = Vec3(1, 0, 0);
        DropHeldItem(false);
        CHECK(gInv.Held().count == 4 && CountDrops(ID_STONE) == 1 && gGame.swing == 0.0f, "one item is thrown");
        DropHeldItem(true);
        CHECK(gInv.Held().Empty() && CountDrops(ID_STONE) == 5, "or the whole stack");
        DropHeldItem(true);
        CHECK(CountDrops(ID_STONE) == 5, "an empty hand throws nothing");

        LookFrom(eye, down);
        PickBlock();
        CHECK(gInv.Held().Empty(), "survival cannot pick blocks");
        gGame.gameMode = MODE_CREATIVE;
        LookFrom(eye, down);
        PickBlock();
        CHECK(gInv.Held().id == ID_STONE && gInv.Held().count == 1, "creative takes the block looked at into the hand");
        gGame.gameMode = MODE_SURVIVAL;
    }

    // experience orbs
    {
        BlockStage();
        ResetSurvival();
        gPlayed.clear();
        SpawnXp(Vec3(0.5f, 0.5f, 11.0f), 100);
        CHECK(gXpOrbs.size() == 4, "100 experience are four orbs (%d)", (int)gXpOrbs.size());
        const Vec3 away(0.5f, 0.5f, 60.0f);
        for (int i = 0; i < 60; ++i)
            XpTick(0.05f, &away);
        bool resting = gXpOrbs.size() == 4;
        for (const XpOrb& o : gXpOrbs)
            resting = resting && o.pos.z > 10.0f && o.pos.z < 10.6f;
        CHECK(resting && gSurvival.xpTotal == 0, "they fall and bounce on the floor");
        const Vec3 here(0.5f, 0.5f, 11.0f);
        for (int i = 0; i < 100 && !gXpOrbs.empty(); ++i)
            XpTick(0.05f, &here);
        CHECK(gXpOrbs.empty() && gSurvival.xpTotal == 100 && Heard(SND_XP_ORB) == 4, "and fly to the player who comes near (%d left)",
              (int)gXpOrbs.size());
        SpawnXp(Vec3(0.5f, 0.5f, 11.0f), 3);
        gXpOrbs[0].age = 299.99f;
        XpTick(0.05f, nullptr);
        CHECK(gXpOrbs.empty(), "an orb nobody takes is gone after five minutes");
    }

    SetHost(nullptr);
    BlockStage();
    gWorld.Clear();
    ResetSurvival();
    gGame = GameState();
    gControls = Controls();
}

// ---------------------------------------------------------------- the world file (Save.h)
// lets the animals live for a while; the player stands at `player`
static void RunMobs(float seconds, const Vec3& player, uint16_t held = 0, bool onFoot = true) {
    const float dt = 0.05f;
    for (float t = 0.0f; t < seconds; t += dt)
        MobsTick(dt, player, held, onFoot);
}

// the block stage with a wall around it (animals wander), no animals, a fresh player
static void MobStage() {
    BlockStage();
    for (int i = -12; i <= 12; ++i)
        for (int z = 10; z <= 11; ++z) {
            gWorld.SetRaw(i, -12, z, MakeVox(ID_STONE));
            gWorld.SetRaw(i, 12, z, MakeVox(ID_STONE));
            gWorld.SetRaw(-12, i, z, MakeVox(ID_STONE));
            gWorld.SetRaw(12, i, z, MakeVox(ID_STONE));
        }
    MobsClear();
    ResetSurvival();
    gGame = GameState();
    gRules = GameRules();
    gPlayed.clear();
}

static int CountParticles(uint16_t tile) {
    int n = 0;
    for (const Particle& p : gParticles)
        if (p.tile == tile)
            ++n;
    return n;
}

static void TestMobs() {
    TestHost host;
    SetHost(&host);
    srand(4242);
    const Vec3 player(0.5f, 0.5f, 10.0f);

    // what they are
    {
        MobStage();
        const int cow = SpawnMob(MOB_COW, Vec3(3.5f, 0.5f, 10.0f), true);
        CHECK(cow == 0 && gMobs.size() == 1 && gMobs[0].health == 10.0f && gMobs[0].kind == MOB_COW, "a cow has ten half hearts");
        CHECK(MobIdAt(cow) != 0 && MobIndexById(MobIdAt(cow)) == cow && MobIndexById(0) == -1 && MobIdAt(5) == 0, "and a number of its own");
        CHECK(Near(MobCentre(cow).z, 10.7f) && Near(MobWidth(gMobs[cow]), 0.9f), "it is 0.9 wide and 1.4 high");
        CHECK(SpawnMob(MOB_KIND_COUNT, player, true) == -1 && SpawnMob(-1, player, true) == -1 && gMobs.size() == 1, "no such animal");
        const int chick = SpawnMob(MOB_CHICKEN, Vec3(-3.5f, 0.5f, 10.0f), true, true);
        CHECK(gMobs[chick].health == 4.0f && MobScale(gMobs[chick]) == 0.5f && Near(MobHeight(gMobs[chick]), 0.35f), "a chick is half a chicken");
        CHECK(MobIdAt(chick) != MobIdAt(cow), "everybody has another number");

        // they wander about and make themselves heard
        const Vec3 start = gMobs[cow].pos;
        float furthest = 0.0f;
        for (int i = 0; i < 600; ++i) {
            MobsTick(0.05f, player, 0, true);
            furthest = std::max(furthest, (gMobs[cow].pos - start).Length());
        }
        CHECK(gMobs.size() == 2 && furthest > 1.0f, "the cow wanders (%.1f m)", furthest);
        CHECK(gMobs[cow].onGround && Near(gMobs[cow].pos.z, 10.0f, 0.01f) && gMobs[chick].onGround, "on the floor");
        CHECK(Heard(SND_COW_SAY) >= 1 && Heard(SND_CHICKEN_SAY) >= 1, "it moos now and then");
        CHECK(gMobs[cow].limbSwing > 0.0f, "and its legs moved");
    }

    // hits
    {
        MobStage();
        const int pig = SpawnMob(MOB_PIG, Vec3(3.5f, 0.5f, 10.0f), true);
        RunMobs(0.1f, player);
        gPlayed.clear();
        MobHurt(pig, 3.0f, player, 1.0f);
        const Mob& m = gMobs[pig];
        CHECK(m.health == 7.0f && Heard(SND_PIG_HURT) == 1 && m.hurt > 0.0f, "a hit hurts");
        CHECK(m.vel.x > 6.0f && m.vel.z > 5.0f && !m.onGround && m.panic > 0.0f, "and throws it back");
        MobHurt(pig, 3.0f, player, 1.0f);
        CHECK(m.health == 7.0f && Heard(SND_PIG_HURT) == 1, "the next hit has to wait a moment");
        RunMobs(0.3f, player);
        MobHurt(pig, 3.0f, player, 0.0f);
        CHECK(m.health == 4.0f, "then it counts again");
        RunMobs(1.0f, player);
        CHECK(m.moving && m.panic > 0.0f && m.pos.x > 4.0f, "it runs about in panic");
        RunMobs(7.0f, player);
        CHECK(m.panic <= 0.0f && m.onGround, "and calms down");
        MobHurt(7, 3.0f, player, 1.0f); // (no such animal: nothing happens)
        MobPush(pig, Vec3(0, 0, 5.0f));
        CHECK(m.vel.z == 5.0f && !m.onGround && m.panic >= 3.0f, "a push lifts it off its feet");
    }

    // death and what it leaves
    {
        MobStage();
        const int cow = SpawnMob(MOB_COW, Vec3(3.5f, 0.5f, 10.0f), true);
        const Vec3 eye(0.5f, 0.5f, 11.0f);
        CHECK(MobsRaycast(eye, Vec3(1, 0, 0), 5.0f).index == cow, "the cow can be aimed at");
        MobHurt(cow, 20.0f, player, 0.0f);
        CHECK(gMobs[cow].death >= 0.0f && Heard(SND_COW_DEATH) == 1 && Heard(SND_COW_HURT) == 0, "a hard hit kills");
        CHECK(MobsRaycast(eye, Vec3(1, 0, 0), 5.0f).index == -1, "the dead are not in the way");
        Vec3 seat;
        float yaw = 0.0f;
        CHECK(!MobRide(MobIdAt(cow), Vec3(1, 0, 0), &seat, &yaw), "nor to be ridden");
        RunMobs(0.5f, player);
        CHECK(gMobs.size() == 1 && gDrops.empty(), "it lies there for a second");
        RunMobs(0.7f, player);
        int xp = 0;
        for (const XpOrb& o : gXpOrbs)
            xp += o.value;
        CHECK(gMobs.empty() && CountDrops(ID_BEEF) >= 1 && CountDrops(ID_BEEF) <= 3 && CountDrops(ID_LEATHER) <= 2, "then it leaves beef (%d)",
              CountDrops(ID_BEEF));
        CHECK(xp >= 1 && xp <= 3 && CountParticles(TILE_P_GENERIC_0) > 0, "experience (%d) and a puff of smoke", xp);

        // burning ones leave cooked meat, babies nothing, sheared sheep no wool
        MobStage();
        const int pig = SpawnMob(MOB_PIG, Vec3(3.5f, 0.5f, 10.0f), true);
        gMobs[pig].burn = 5.0f;
        MobHurt(pig, 20.0f, player, 0.0f);
        RunMobs(1.2f, player);
        CHECK(CountDrops(ID_COOKED_PORKCHOP) >= 1 && CountDrops(ID_PORKCHOP) == 0, "a pig that died on fire leaves cooked pork");
        MobStage();
        const int lamb = SpawnMob(MOB_SHEEP, Vec3(3.5f, 0.5f, 10.0f), true, true);
        MobHurt(lamb, 20.0f, player, 0.0f);
        RunMobs(1.2f, player);
        CHECK(gMobs.empty() && gDrops.empty() && gXpOrbs.empty(), "a lamb leaves nothing");
        MobStage();
        const int bare = SpawnMob(MOB_SHEEP, Vec3(3.5f, 0.5f, 10.0f), true);
        gMobs[bare].sheared = true;
        gMobs[bare].woolTimer = 100.0f;
        const uint32_t woolly = MobIdAt(SpawnMob(MOB_SHEEP, Vec3(-3.5f, 0.5f, 10.0f), true));
        MobHurt(bare, 20.0f, player, 0.0f);
        RunMobs(1.2f, player);
        CHECK(CountDrops(ID_WHITE_WOOL) == 0 && CountDrops(ID_MUTTON) >= 1, "a sheared sheep leaves mutton only");
        MobHurt(MobIndexById(woolly), 20.0f, player, 0.0f); // (its index changed when the other one went)
        RunMobs(1.2f, player);
        CHECK(gMobs.empty() && CountDrops(ID_WHITE_WOOL) == 1, "a woolly one its wool too");
    }

    // falling, burning, swimming
    {
        MobStage();
        const int cow = SpawnMob(MOB_COW, Vec3(3.5f, 0.5f, 18.0f), true);
        const int hen = SpawnMob(MOB_CHICKEN, Vec3(-3.5f, 0.5f, 18.0f), true);
        RunMobs(1.0f, player);
        CHECK(gMobs[cow].onGround && gMobs[cow].health > 4.7f && gMobs[cow].health < 5.3f, "a cow that falls 8 m loses 5 (%.2f left)",
              gMobs[cow].health);
        CHECK(!gMobs[hen].onGround && gMobs[hen].pos.z > 14.0f, "a chicken flutters down slowly");
        RunMobs(4.0f, player);
        CHECK(gMobs[hen].onGround && gMobs[hen].health == 4.0f, "and lands unhurt");

        MobStage();
        const int burning = SpawnMob(MOB_COW, Vec3(3.5f, 0.5f, 10.0f), true);
        gMobs[burning].burn = 3.0f;
        RunMobs(4.0f, player);
        CHECK(gMobs[burning].health >= 6.9f && gMobs[burning].health <= 8.1f && gMobs[burning].burn <= 0.0f, "fire takes one a second (%.1f left)",
              gMobs[burning].health);
        CHECK(CountParticles(TILE_P_FLAME) > 0, "with flames all over");
        gMobs[burning].burn = 30.0f;
        host.sea = 12.0f;
        RunMobs(3.0f, player);
        CHECK(gMobs[burning].burn == 0.0f, "water puts it out");
        CHECK(Near(gMobs[burning].pos.z, 12.0f - 1.4f * 0.55f, 0.15f), "and it swims with the head out (%.2f)", gMobs[burning].pos.z);
        host.sea = -1000.0f;
    }

    // food: they follow it, and it makes them breed
    {
        MobStage();
        const int cow = SpawnMob(MOB_COW, Vec3(8.5f, 0.5f, 10.0f), true);
        auto standStill = [&] {
            gMobs[cow].moving = false;
            gMobs[cow].aiTimer = 100.0f;
            gMobs[cow].sayTimer = 100.0f;
        };
        standStill();
        RunMobs(6.0f, player, ID_CARROT);
        CHECK(Near(gMobs[cow].pos.x, 8.5f, 0.05f), "a cow does not care for carrots");
        RunMobs(6.0f, player, ID_WHEAT, false);
        CHECK(Near(gMobs[cow].pos.x, 8.5f, 0.05f), "nor for wheat held out of a car");
        RunMobs(8.0f, player, ID_WHEAT);
        const float dist = (gMobs[cow].pos - player).Length();
        CHECK(dist > 1.5f && dist < 2.7f, "but it comes for wheat, and stops in front of the player (%.2f)", dist);

        MobStage();
        const int a = SpawnMob(MOB_SHEEP, Vec3(2.5f, 0.5f, 10.0f), false);
        const int b = SpawnMob(MOB_SHEEP, Vec3(4.5f, 0.5f, 10.0f), true);
        gInv.Held() = Stack(ID_CARROT, 2);
        CHECK(!MobInteract(a, true) && gInv.Held().count == 2, "sheep do not eat carrots");
        gInv.Held() = Stack(ID_WHEAT, 2);
        CHECK(MobInteract(a, true) && gMobs[a].love > 0.0f && gInv.Held().count == 1 && Heard(SND_EAT) == 1, "wheat puts a sheep in the mood");
        CHECK(CountParticles(TILE_P_HEART) > 0, "(hearts)");
        CHECK(!MobInteract(a, true) && gInv.Held().count == 1, "once is enough");
        CHECK(MobInteract(b, true) && gInv.Held().Empty(), "the other one eats the last wheat");
        RunMobs(0.1f, player);
        CHECK(gMobs.size() == 3 && gMobs[2].kind == MOB_SHEEP && gMobs[2].baby > 0.0f && gMobs[2].persistent, "a lamb is born");
        CHECK(gMobs[a].love == 0.0f && gMobs[a].breedCooldown > 0.0f && !gXpOrbs.empty(), "the parents rest, the player gets experience");
        gInv.Held() = Stack(ID_WHEAT, 2);
        CHECK(!MobInteract(a, true) && !MobInteract(2, true) && gInv.Held().count == 2, "no more for the parents, and the lamb is too young");
        gMobs[2].baby = 0.2f;
        RunMobs(0.3f, player);
        CHECK(MobScale(gMobs[2]) == 1.0f, "it grows up");
        gGame.gameMode = MODE_CREATIVE;
        CHECK(MobInteract(2, true) && gInv.Held().count == 2, "in creative the food is not used up");
        gMobs[2].love = 20.0f;
        MobHurt(2, 1.0f, player, 0.0f);
        CHECK(gMobs[2].love == 0.0f, "a hit ends the mood");
    }

    // shears, bucket, saddle, eggs
    {
        MobStage();
        const int sheep = SpawnMob(MOB_SHEEP, Vec3(2.5f, 0.5f, 10.0f), true);
        const int cow = SpawnMob(MOB_COW, Vec3(4.5f, 0.5f, 10.0f), true);
        const int pig = SpawnMob(MOB_PIG, Vec3(-8.5f, 0.5f, 10.0f), false);
        for (Mob& m : gMobs) {
            m.aiTimer = 100.0f; // (they stand still)
            m.sayTimer = 100.0f;
        }
        gInv.Held() = Stack(ID_SHEARS);
        CHECK(!MobInteract(cow, true), "cows have no wool");
        CHECK(MobInteract(sheep, true) && gMobs[sheep].sheared && Heard(SND_SHEEP_SHEAR) == 1, "shears take the wool off a sheep");
        CHECK(CountDrops(ID_WHITE_WOOL) >= 1 && CountDrops(ID_WHITE_WOOL) <= 3 && gInv.Held().damage == 1, "one to three pieces, and the shears wear");
        CHECK(!MobInteract(sheep, true), "nothing left to shear");
        gMobs[sheep].woolTimer = 0.2f;
        RunMobs(0.3f, player);
        CHECK(!gMobs[sheep].sheared, "the wool grows back");

        gInv.Held() = Stack(ID_BUCKET);
        CHECK(!MobInteract(sheep, true), "sheep give no milk");
        CHECK(MobInteract(cow, true) && gInv.Held().id == ID_MILK_BUCKET && Heard(SND_COW_MILK) == 1, "a bucket becomes a bucket of milk");
        gInv.Held() = Stack(ID_BUCKET, 3);
        CHECK(MobInteract(cow, true) && gInv.Held().id == ID_BUCKET && gInv.Held().count == 2 && gInv.CountOf(ID_MILK_BUCKET) == 1,
              "from a stack of buckets one is filled");

        gInv.Held() = ItemStack();
        CHECK(!MobInteract(pig, true) && gGame.ridingMob == 0, "a pig without a saddle is not ridden");
        gInv.Held() = Stack(ID_SADDLE);
        CHECK(!MobInteract(cow, true), "a saddle is for pigs");
        CHECK(MobInteract(pig, true) && gMobs[pig].saddled && gMobs[pig].persistent && gInv.Held().Empty() && Heard(SND_SADDLE) == 1,
              "the saddle goes on the pig, which stays for good");
        CHECK(!MobInteract(pig, false) && gGame.ridingMob == 0, "nobody gets on from a car");
        CHECK(MobInteract(pig, true) && gGame.ridingMob == MobIdAt(pig), "on foot the player gets on");
        Vec3 seat = gMobs[pig].pos;
        float yaw = 0.0f;
        bool there = true;
        for (int i = 0; i < 60; ++i) {
            there = there && MobRide(gGame.ridingMob, Vec3(4.0f, 0, 0), &seat, &yaw);
            MobsTick(0.05f, seat, 0, true);
        }
        CHECK(there && gMobs[pig].pos.x > -0.5f && Near(gMobs[pig].pos.y, 0.5f, 0.05f), "it walks where it is steered (x %.1f)", gMobs[pig].pos.x);
        CHECK(Near(yaw, -kPi * 0.5f, 0.1f) && Near(seat.z, 10.0f + 0.9f + 0.25f, 0.05f), "facing that way, the seat on its back");
        MobsTick(0.05f, Vec3(900.0f, 0.5f, 10.0f), 0, true);
        CHECK(gMobs.size() == 1 && gMobs[0].id == gGame.ridingMob, "the pig the player rides never goes, wherever the host thinks he is");
        gGame.ridingMob = 0;

        MobStage();
        const int layer = SpawnMob(MOB_CHICKEN, Vec3(-3.5f, 3.5f, 10.0f), true);
        gMobs[layer].eggTimer = 0.2f;
        RunMobs(0.3f, player);
        CHECK(CountDrops(ID_EGG) == 1 && Heard(SND_CHICKEN_EGG) == 1 && gMobs[layer].eggTimer >= 120.0f, "a hen lays an egg now and then");
    }

    // aiming at them
    {
        MobStage();
        const int cow = SpawnMob(MOB_COW, Vec3(3.5f, 0.5f, 10.0f), true);
        const int pig = SpawnMob(MOB_PIG, Vec3(6.5f, 0.5f, 10.0f), true);
        const Vec3 eye(0.5f, 0.5f, 10.5f);
        MobHit h = MobsRaycast(eye, Vec3(1, 0, 0), 10.0f);
        CHECK(h.index == cow && Near(h.dist, 2.5f, 1e-3f) && Near(h.point.x, 3.0f, 1e-3f), "the nearer one is hit, at its side (%.2f)", h.dist);
        CHECK(MobsRaycast(eye, Vec3(1, 0, 0), 2.0f).index == -1, "not from too far");
        CHECK(MobsRaycast(eye, Vec3(0, 0, 1), 10.0f).index == -1 && MobsRaycast(eye, Vec3(-1, 0, 0), 10.0f).index == -1, "nor looking elsewhere");
        h = MobsRaycast(Vec3(0.5f, 0.5f, 11.2f), Vec3(1, 0, 0), 10.0f);
        CHECK(h.index == cow, "over the pig's back there is still the cow");
        h = MobsRaycast(Vec3(9.5f, 0.5f, 10.5f), Vec3(-1, 0, 0), 10.0f);
        CHECK(h.index == pig, "from the other side the pig comes first");
    }

    // explosions
    {
        MobStage();
        const int cow = SpawnMob(MOB_COW, Vec3(3.5f, 0.5f, 10.0f), true);
        const int far = SpawnMob(MOB_COW, Vec3(10.5f, 0.5f, 10.0f), true);
        const Vec3 at(0.5f, 0.5f, 10.7f);
        MobsRadial(at, 10.0f, 10.0f, 10.0f);
        CHECK(Near(gMobs[cow].health, 3.0f, 0.01f) && gMobs[far].health == 10.0f, "a blast hurts less the further away (%.2f)", gMobs[cow].health);
        CHECK(Near(gMobs[cow].vel.x, 7.0f, 0.01f) && Near(gMobs[cow].vel.z, 3.5f, 0.01f) && gMobs[far].vel.x == 0.0f, "and throws them away from it");
        MobsRadial(at, 10.0f, 10.0f, 10.0f);
        CHECK(gMobs[cow].death >= 0.0f, "it does not wait for the last hit to wear off");

        MobStage();
        gRules.explosionsBreakBlocks = false;
        const int near = SpawnMob(MOB_PIG, Vec3(2.5f, 0.5f, 10.0f), true);
        const int away = SpawnMob(MOB_PIG, Vec3(10.5f, 0.5f, 10.0f), true);
        ExplodeAt(Vec3(0.5f, 0.5f, 10.5f), 4.0f, true, BLAST_TNT);
        CHECK(gMobs[near].death >= 0.0f && gMobs[near].vel.x > 3.0f && gMobs[away].health == 10.0f, "TNT kills the pig next to it");
    }

    // herds appear, and go again
    {
        MobStage();
        host.herdGround = true;
        gRules.maxAnimals = 6;
        for (int i = 0; i < 200; ++i)
            MobsSpawnTick(0.5f, player);
        bool wild = true, ring = true;
        for (const Mob& m : gMobs) {
            wild = wild && !m.persistent && m.baby <= 0.0f;
            const float d = (Vec3(m.pos.x, m.pos.y, player.z) - player).Length();
            ring = ring && d > 24.0f && d < 81.0f && m.pos.z == 10.0f;
        }
        CHECK(gMobs.size() == 6 && wild, "herds appear until there are enough (%d)", (int)gMobs.size());
        CHECK(ring, "30 to 75 m from the player, on the ground the host showed");
        for (Mob& m : gMobs)
            m.persistent = true;
        for (int i = 0; i < 200; ++i)
            MobsSpawnTick(0.5f, player);
        CHECK(gMobs.size() == 12, "the player's own animals do not count (%d)", (int)gMobs.size());

        MobsClear();
        gRules.animals = false;
        for (int i = 0; i < 50; ++i)
            MobsSpawnTick(0.5f, player);
        CHECK(gMobs.empty(), "none when the rules say so");
        gRules.animals = true;
        host.outdoors = false;
        for (int i = 0; i < 50; ++i)
            MobsSpawnTick(0.5f, player);
        CHECK(gMobs.empty(), "none indoors");
        host.outdoors = true;
        host.herdGround = false;
        for (int i = 0; i < 50; ++i)
            MobsSpawnTick(0.5f, player);
        CHECK(gMobs.empty(), "none where the host has no ground for them");

        MobStage();
        SpawnMob(MOB_COW, Vec3(3.5f, 0.5f, 10.0f), false);
        SpawnMob(MOB_PIG, Vec3(-3.5f, 0.5f, 10.0f), true);
        RunMobs(0.1f, Vec3(200.0f, 0.5f, 10.0f));
        CHECK(gMobs.size() == 1 && gMobs[0].kind == MOB_PIG, "wild animals go when the player is far away");
        RunMobs(0.1f, Vec3(400.0f, 0.5f, 10.0f));
        CHECK(gMobs.empty(), "the player's own a good deal later");
        SpawnMob(MOB_COW, Vec3(40.5f, 0.5f, 10.0f), true); // (no floor there)
        RunMobs(6.0f, Vec3(40.5f, 0.5f, 10.0f));
        CHECK(gMobs.empty(), "what falls out of the world is gone");
    }
    MobsClear();
    gRules = GameRules();
}

// the animals' stage with a fresh host and a player at its middle who looks east, in first person
static void CombatStage(TestHost& host) {
    MobStage();
    CombatClear();
    host = TestHost();
    host.hasPlayer = true;
    host.player = Vec3(0.5f, 0.5f, 11.0f);
    gGame.eyePos = gGame.rayOrigin = Vec3(0.5f, 0.5f, 10.9f);
    gGame.lookDir = Vec3(1, 0, 0);
    gGame.cameraMode = CAM_FIRST;
    gGame.attackTimer = 10.0f;
    gTarget = Target();
}

static void RunShots(float seconds) {
    const float dt = 0.05f;
    for (float t = 0.0f; t < seconds; t += dt)
        ProjectilesTick(dt);
}

// pulls the held bow (crossbow, trident) for so long and lets go
static void PullAndRelease(float seconds) {
    ChargedItemsTick(seconds, true, true);
    ChargedItemsTick(0.05f, false, false);
}

static uint16_t ItemWith(int special) {
    for (int id = FIRST_ITEM; id < ITEM_END; ++id)
        if (Item((uint16_t)id).special == special)
            return (uint16_t)id;
    return 0;
}

// the player looks at the top of the floor block (3, 0, 9)
static void TargetFloor() {
    gTarget = Target();
    gTarget.valid = gTarget.voxel = true;
    gTarget.pos = Int3{ 3, 0, 9 };
    gTarget.point = Vec3(3.5f, 0.5f, 10.0f);
    gTarget.normal = Vec3(0, 0, 1);
}

static int XpOnTheGround() {
    int xp = 0;
    for (const XpOrb& o : gXpOrbs)
        xp += o.value;
    return xp;
}

static void TestCombat() {
    TestHost host;
    SetHost(&host);
    srand(2468);

    // the bow
    {
        CombatStage(host);
        gInv.Held() = Stack(ID_BOW);
        gInv.slots[1] = Stack(ID_ARROW, 2);
        CHECK(ChargedItemsTick(0.5f, true, true) && gGame.bowDraw == 0.5f && gProjectiles.empty(), "holding the button draws the bow");
        ChargedItemsTick(0.6f, true, false);
        CHECK(!ChargedItemsTick(0.05f, false, false) && gProjectiles.size() == 1 && gGame.bowDraw < 0.0f, "letting go shoots");
        const Projectile a = gProjectiles[0];
        CHECK(a.type == PJ_ARROW && a.crit && a.pickup && !a.hostile && Near(a.vel.x, 60.0f, 0.01f) && Near(a.pos.x, 1.1f),
              "fully drawn: a critical arrow at 60 m/s (%.1f)", a.vel.x);
        CHECK(gInv.CountOf(ID_ARROW) == 1 && gInv.Held().damage == 1 && Heard(SND_BOW_SHOOT) == 1, "an arrow is used up and the bow wears");
        CombatClear();
        PullAndRelease(0.2f);
        CHECK(gProjectiles.size() == 1 && !gProjectiles[0].crit && Near(gProjectiles[0].vel.x, 60.0f * 0.44f / 3.0f, 0.01f),
              "a short pull makes a slow arrow");
        CombatClear();
        gInv.slots[1] = Stack(ID_ARROW, 1);
        PullAndRelease(0.1f);
        CHECK(gProjectiles.empty() && gInv.CountOf(ID_ARROW) == 1, "barely pulled: no shot");
        gInv.slots[1] = ItemStack();
        CHECK(!ChargedItemsTick(0.5f, true, true) && gGame.bowDraw < 0.0f, "no arrows, no drawing");
        gGame.gameMode = MODE_CREATIVE;
        PullAndRelease(1.2f);
        CHECK(gProjectiles.size() == 1 && !gProjectiles[0].pickup, "creative needs no arrows (and leaves none to pick up)");
    }

    // an arrow's flight
    {
        CombatStage(host);
        gInv.Held() = Stack(ID_BOW);
        gInv.slots[1] = Stack(ID_ARROW, 1);
        PullAndRelease(1.2f);
        gPlayed.clear();
        RunShots(0.1f);
        CHECK(gProjectiles.size() == 1 && !gProjectiles[0].stuck && gProjectiles[0].pos.x > 5.0f && gProjectiles[0].vel.z < 0.0f,
              "it flies and begins to fall");
        RunShots(0.5f);
        CHECK(gProjectiles.size() == 1 && gProjectiles[0].stuck && Near(gProjectiles[0].pos.x, 11.95f, 0.02f) && Heard(SND_ARROW_HIT) == 1,
              "and sticks in the wall");
        const float unaimed = gProjectiles[0].pos.z;
        CHECK(unaimed < 10.8f && unaimed > 10.3f, "a little lower than it was shot (%.2f)", unaimed);
        RunShots(5.0f);
        CHECK(gProjectiles.size() == 1 && gInv.CountOf(ID_ARROW) == 0, "there it stays");
        host.player = gProjectiles[0].pos;
        RunShots(0.05f);
        CHECK(gProjectiles.empty() && gInv.CountOf(ID_ARROW) == 1 && Heard(SND_PICKUP) == 1, "until the player comes and takes it");

        // flying (or in third person, or at the wheel) the shot is aimed at the spot under the crosshair
        CombatStage(host);
        gGame.flying = true;
        host.velocity = Vec3(0, 6.0f, 0);
        gInv.Held() = Stack(ID_BOW);
        gInv.slots[1] = Stack(ID_ARROW, 1);
        PullAndRelease(1.2f);
        RunShots(0.6f);
        CHECK(gProjectiles.size() == 1 && gProjectiles[0].stuck && Near(gProjectiles[0].pos.y, 0.5f, 0.25f) &&
                  Near(gProjectiles[0].pos.z, 10.9f, 0.25f) && gProjectiles[0].pos.z > unaimed + 0.1f,
              "flying sideways, the arrow still lands under the crosshair (y %.2f, z %.2f)", gProjectiles[0].pos.y, gProjectiles[0].pos.z);

        // on foot in first person a throw goes straight ahead, with the player's own motion added
        CombatStage(host);
        host.velocity = Vec3(0, 5.0f, 3.0f);
        gInv.Held() = Stack(ID_SNOWBALL, 8);
        UseHeldItem();
        CHECK(Near(gProjectiles[0].vel.x, 30.0f) && Near(gProjectiles[0].vel.y, 5.0f) && gProjectiles[0].vel.z == 0.0f,
              "standing, only the sideways motion goes with a throw");
        host.onGround = false;
        CombatClear();
        UseHeldItem();
        CHECK(Near(gProjectiles[0].vel.z, 3.0f), "in the air all of it");
    }

    // what an arrow meets
    {
        CombatStage(host);
        const int cow = SpawnMob(MOB_COW, Vec3(6.5f, 0.5f, 10.0f), true);
        gInv.Held() = Stack(ID_BOW);
        gInv.slots[1] = Stack(ID_ARROW, 8);
        PullAndRelease(0.9f); // (not quite fully drawn: no critical extra)
        RunShots(0.3f);
        CHECK(gProjectiles.empty() && gMobs[cow].health == 4.0f && Heard(SND_COW_HURT) == 1 && gMobs[cow].vel.x > 3.0f,
              "an arrow at 52 m/s takes 6 off a cow and pushes it (%.1f left)", gMobs[cow].health);

        CombatStage(host);
        gInv.Held() = Stack(ID_BOW);
        gInv.slots[1] = Stack(ID_ARROW, 8);
        host.people.push_back({ Vec3(8.5f, 0.5f, 10.9f) });
        PullAndRelease(0.9f);
        RunShots(0.3f);
        const TestHost::Person& man = host.people[0];
        CHECK(gProjectiles.empty() && man.hurt == 6.0f && man.how == HURT_SHOT && !man.byHostile && Near(man.push.x, 2.0f, 0.05f),
              "the same for somebody of the host's (%.1f)", man.hurt);
        const int inFront = SpawnMob(MOB_COW, Vec3(4.5f, 0.5f, 10.0f), true);
        PullAndRelease(0.9f);
        RunShots(0.3f);
        CHECK(gMobs[inFront].health == 4.0f && host.people[0].hurt == 6.0f, "an animal in front of him takes the arrow");
        MobsClear();
        gWorld.SetRaw(4, 0, 10, MakeVox(ID_STONE));
        gWorld.SetRaw(4, 0, 11, MakeVox(ID_STONE));
        PullAndRelease(0.9f);
        RunShots(0.3f);
        CHECK(gProjectiles.size() == 1 && gProjectiles[0].stuck && gProjectiles[0].pos.x < 4.0f && host.people[0].hurt == 6.0f,
              "a block in front of him too");

        CombatStage(host);
        gInv.Held() = Stack(ID_BOW);
        gInv.slots[1] = Stack(ID_ARROW, 8);
        host.carX = 6.0f;
        PullAndRelease(0.9f);
        RunShots(0.3f);
        CHECK(gProjectiles.empty() && host.carHurt == 6.0f && host.shotVehicle == -1, "arrows do not stick in a vehicle, they wear it down");
        host.carX = 1e9f;
        host.vehicle = 5;
        PullAndRelease(0.9f);
        CHECK(gProjectiles.size() == 1 && Near(gProjectiles[0].pos.x, 1.7f) && gProjectiles[0].vehicle == 5, "from a vehicle the shot starts outside it");
        RunShots(0.05f);
        CHECK(host.shotVehicle == 5, "and the host is told which vehicle to let it through");

        // somebody of the host's shoots at the player
        CombatStage(host);
        ShootArrowAt(Vec3(8.5f, 0.5f, 11.0f), host.player, 3, -1);
        CHECK(gProjectiles.size() == 1 && gProjectiles[0].hostile && gProjectiles[0].shooter == 3 && !gProjectiles[0].pickup &&
                  Near(gProjectiles[0].vel.Length(), 48.0f, 0.01f) && Heard(SND_CROSSBOW_SHOOT) == 1,
              "a pillager's arrow");
        RunShots(0.5f);
        CHECK(gProjectiles.empty() && host.playerHurt >= 4.0f && host.playerHurt <= 6.0f && host.lastShooter == 3,
              "hits the player for 4 to 6 (%.1f)", host.playerHurt);
        ShootArrowAt(Vec3(8.5f, 0.5f, 11.0f), Vec3(8.5f, 0.5f, 11.2f), 3, -1);
        CHECK(gProjectiles.empty(), "nobody shoots at his own feet");
        ShootArrowAt(Vec3(0.5f, 0.5f, 11.0f), Vec3(8.5f, 0.5f, 10.5f), 3, -1);
        RunShots(1.0f);
        CHECK(gProjectiles.size() == 1 && gProjectiles[0].stuck, "one that misses sticks somewhere");
        RunShots(11.0f);
        CHECK(gProjectiles.empty() && gInv.CountOf(ID_ARROW) == 0, "and is gone after ten seconds; it cannot be picked up");
    }

    // the crossbow
    {
        CombatStage(host);
        gInv.Held() = Stack(ID_CROSSBOW);
        gInv.slots[1] = Stack(ID_ARROW, 1);
        CHECK(ChargedItemsTick(0.5f, true, true) && gGame.crossbowSlot < 0, "a crossbow takes a while to load");
        CHECK(ChargedItemsTick(0.8f, true, false) && gGame.crossbowSlot == gInv.selected && gInv.CountOf(ID_ARROW) == 0 && Heard(SND_CLICK) == 1,
              "after 1.25 s the arrow is in");
        ChargedItemsTick(0.05f, false, false);
        CHECK(gProjectiles.empty(), "it stays loaded");
        ChargedItemsTick(0.05f, true, true);
        CHECK(gProjectiles.size() == 1 && Near(gProjectiles[0].vel.x, 63.0f, 0.01f) && gProjectiles[0].damage == 9.0f && gGame.crossbowSlot < 0 &&
                  Heard(SND_CROSSBOW_SHOOT) == 1 && gInv.Held().damage == 1,
              "the next click shoots: 63 m/s, 9 damage");
        CombatClear();
        gInv.offhand = Stack(ID_FIREWORK_ROCKET);
        ChargedItemsTick(1.3f, true, true);
        CHECK(gGame.crossbowRocket && gInv.offhand.Empty(), "a firework from the other hand goes in instead of an arrow");
        ChargedItemsTick(0.05f, false, false);
        ChargedItemsTick(0.05f, true, true);
        CHECK(gProjectiles.size() == 1 && gProjectiles[0].type == PJ_ROCKET, "and is shot as a rocket");
        gPlayed.clear();
        RunShots(1.0f);
        CHECK(gProjectiles.empty() && Heard(SND_FIREWORK_BLAST) == 1 && host.explosions == 1 && host.lastBlast == BLAST_SMALL,
              "which bursts on the wall");
    }

    // the trident
    {
        CombatStage(host);
        gInv.Held() = Stack(ID_TRIDENT);
        PullAndRelease(0.3f);
        CHECK(gProjectiles.empty() && gInv.Held().id == ID_TRIDENT, "a short pull throws nothing");
        PullAndRelease(0.6f);
        CHECK(gProjectiles.size() == 1 && gProjectiles[0].type == PJ_TRIDENT && Near(gProjectiles[0].vel.x, 50.0f, 0.01f) && gInv.Held().Empty() &&
                  gProjectiles[0].stack.damage == 1 && Heard(SND_TRIDENT_THROW) == 1,
              "the trident is thrown, and wears");
        RunShots(0.4f);
        CHECK(gProjectiles.size() == 1 && gProjectiles[0].stuck && Heard(SND_TRIDENT_HIT) == 1 && gBolts.empty(), "it sticks in the wall; no storm, no lightning");
        RunShots(1.5f);
        CHECK(gProjectiles.empty() && gInv.slots[0].id == ID_TRIDENT && gInv.slots[0].damage == 1 && Heard(SND_PICKUP) == 1,
              "and flies back into the hand it left");

        CombatStage(host);
        const int cow = SpawnMob(MOB_COW, Vec3(6.5f, 0.5f, 10.0f), true);
        gInv.Held() = Stack(ID_TRIDENT);
        PullAndRelease(0.6f);
        RunShots(0.2f);
        CHECK(gMobs[cow].health == 2.0f && Heard(SND_TRIDENT_HIT) == 1, "a trident takes 8 off a cow");
        RunShots(2.0f);
        CHECK(gProjectiles.empty() && gInv.slots[0].id == ID_TRIDENT, "and comes back from there too");

        CombatStage(host);
        host.storm = true;
        gInv.Held() = Stack(ID_TRIDENT);
        PullAndRelease(0.6f);
        RunShots(0.4f);
        CHECK(gBolts.size() == 1 && LightningFlashActive() && host.fires.size() == 1 && host.fires[0] == 6.0f && host.explosions == 1 &&
                  host.lastBlast == BLAST_SMALL && Heard(SND_THUNDER) == 1,
              "in a storm it calls down lightning: a fire and a small blast");
        LightningTick(0.3f);
        CHECK(gBolts.size() == 1 && !LightningFlashActive(), "the flash is short");
        LightningTick(0.3f);
        CHECK(gBolts.empty(), "the bolt lasts half a second");

        CombatStage(host);
        gGame.gameMode = MODE_CREATIVE;
        gInv.Held() = Stack(ID_TRIDENT);
        PullAndRelease(0.6f);
        CHECK(gInv.Held().id == ID_TRIDENT && gProjectiles.size() == 1 && gProjectiles[0].stack.Empty(), "creative keeps its trident");
        RunShots(0.4f);
        CHECK(gBolts.size() == 1, "and has lightning in any weather");
        RunShots(4.0f);
        CHECK(gProjectiles.empty() && gInv.CountOf(ID_TRIDENT) == 1, "the thrown one just disappears");
    }

    // things that are thrown
    {
        CombatStage(host);
        const int cow = SpawnMob(MOB_COW, Vec3(6.5f, 0.5f, 10.0f), true);
        gInv.Held() = Stack(ID_SNOWBALL, 3);
        CHECK(UseHeldItem() && gProjectiles.size() == 1 && gProjectiles[0].type == PJ_SNOWBALL && Near(gProjectiles[0].vel.x, 30.0f) &&
                  gInv.Held().count == 2 && Heard(SND_THROW) == 1 && gGame.swing >= 0.0f,
              "a snowball is thrown");
        RunShots(0.4f);
        CHECK(gProjectiles.empty() && gMobs[cow].health == 10.0f && gMobs[cow].hurt > 0.0f && gMobs[cow].vel.x > 2.0f,
              "it pushes a cow without hurting it");
        MobsClear();
        host.people.push_back({ Vec3(6.5f, 0.5f, 10.9f) });
        UseHeldItem();
        RunShots(0.4f);
        CHECK(gProjectiles.empty() && host.people[0].hurt == 0.0f && Near(host.people[0].push.x, 2.5f, 0.05f), "and somebody of the host's");

        CombatStage(host);
        gInv.Held() = Stack(ID_EGG, 64);
        for (int i = 0; i < 64; ++i) {
            UseHeldItem();
            RunShots(0.6f);
        }
        bool chicks = true;
        for (const Mob& m : gMobs)
            chicks = chicks && m.kind == MOB_CHICKEN && m.baby > 0.0f && m.persistent;
        CHECK(gInv.Held().Empty() && gProjectiles.empty() && !gMobs.empty() && gMobs.size() < 24 && chicks,
              "now and then a chick comes out of a thrown egg (%d of 64)", (int)gMobs.size());

        CombatStage(host);
        gInv.Held() = Stack(ID_ENDER_PEARL, 3);
        UseHeldItem();
        RunShots(0.6f);
        CHECK(Heard(SND_PEARL_THROW) == 1 && host.moves == 1 && host.player.x > 11.0f && Near(host.playerHurt, 5.0f) && Heard(SND_TELEPORT) == 1,
              "an ender pearl takes the player to where it lands, for two and a half hearts (x %.1f)", host.player.x);
        host.vehicle = 5;
        UseHeldItem();
        RunShots(0.8f);
        CHECK(host.moves == 1 && gProjectiles.empty(), "not out of a vehicle");
        host.vehicle = -1;
        gGame.gameMode = MODE_CREATIVE;
        UseHeldItem();
        RunShots(0.6f);
        CHECK(host.moves == 2 && Near(host.playerHurt, 5.0f) && gInv.Held().count == 1, "in creative it is free and painless");

        CombatStage(host);
        gInv.Held() = Stack(ID_EXPERIENCE_BOTTLE);
        CHECK(UseHeldItem() && gInv.Held().Empty() && gProjectiles[0].type == PJ_XPBOTTLE, "a bottle o' enchanting is thrown");
        RunShots(1.5f);
        CHECK(gProjectiles.empty() && XpOnTheGround() >= 3 && XpOnTheGround() <= 11 && Heard(SND_DIG_GLASS) == 1,
              "and breaks into 3 to 11 experience (%d)", XpOnTheGround());

        CombatStage(host);
        const int blown = SpawnMob(MOB_COW, Vec3(10.5f, 0.5f, 10.0f), true);
        gInv.Held() = Stack(ID_WIND_CHARGE);
        UseHeldItem();
        RunShots(0.5f);
        CHECK(Heard(SND_WIND_THROW) == 1 && gProjectiles.empty() && host.gusts == 1 && host.gustPlayerToo && Heard(SND_WIND_BURST) == 1,
              "a wind charge bursts where it lands: the host's things are blown away, the player too");
        CHECK(gMobs[blown].vel.z > 3.0f && gMobs[blown].health == 10.0f, "and the animals, unhurt");
    }

    // fire
    {
        CombatStage(host);
        gInv.Held() = Stack(ID_FIRE_CHARGE, 2);
        CHECK(UseHeldItem() && gProjectiles[0].type == PJ_FIREBALL && Near(gProjectiles[0].vel.x, 24.0f) && gInv.Held().count == 1 &&
                  Heard(SND_FIREBALL) == 1,
              "a fire charge is shot as a fireball");
        RunShots(0.6f);
        CHECK(gProjectiles.empty() && host.explosions == 1 && host.lastBlast == BLAST_FIRE && Heard(SND_EXPLODE) == 1,
              "which flies straight and blows up");
        gWorld.SetRaw(3, 0, 10, MakeVox(ID_TNT));
        gTarget.valid = gTarget.voxel = true;
        gTarget.pos = Int3{ 3, 0, 10 };
        gTarget.point = Vec3(3.0f, 0.5f, 10.5f);
        gTarget.normal = Vec3(-1, 0, 0);
        CHECK(UseHeldItem() && gPrimedTnt.size() == 1 && gProjectiles.empty() && gInv.Held().Empty() && Heard(SND_IGNITE) == 1,
              "on TNT it lights the fuse instead");

        CombatStage(host);
        gInv.Held() = Stack(ID_FLINT_AND_STEEL);
        CHECK(!UseHeldItem() && gInv.Held().damage == 0, "flint and steel need something to light");
        TargetFloor();
        CHECK(UseHeldItem() && gWorld.GetBlock(3, 0, 10) == ID_FIRE && gInv.Held().damage == 1 && host.fires.empty(), "on a block: fire");
        gTarget.voxel = false;
        gTarget.point = Vec3(3.5f, 0.5f, 20.0f);
        CHECK(UseHeldItem() && host.fires.size() == 1 && host.fires[0] == 10.0f && gInv.Held().damage == 2,
              "where our fire cannot burn, on something of the host's, the host lights its own");

        CombatStage(host);
        gInv.Held() = Stack(ID_LAVA_BUCKET);
        CHECK(!UseHeldItem() && gInv.Held().id == ID_LAVA_BUCKET, "lava needs a spot");
        TargetFloor();
        gTarget.voxel = false;
        CHECK(UseHeldItem() && host.fires.size() == 4 && host.fires[0] == 9.0f && host.fires[3] == 7.0f && gInv.Held().id == ID_BUCKET,
              "poured on the host's ground it burns there");
    }

    // fireworks
    {
        CombatStage(host);
        gInv.Held() = Stack(ID_FIREWORK_ROCKET, 4);
        CHECK(!UseHeldItem() && gInv.Held().count == 4, "a firework needs something to stand on");
        TargetFloor();
        CHECK(UseHeldItem() && gProjectiles.size() == 1 && gProjectiles[0].type == PJ_FIREWORK && gProjectiles[0].vel.z == 8.0f &&
                  gInv.Held().count == 3 && Heard(SND_FIREWORK_LAUNCH) == 1,
              "set on the ground it takes off");
        RunShots(1.0f);
        CHECK(gProjectiles.size() == 1 && gProjectiles[0].pos.z > 15.0f && Heard(SND_FIREWORK_BLAST) == 0, "climbs");
        RunShots(1.0f);
        CHECK(gProjectiles.empty() && Heard(SND_FIREWORK_BLAST) == 1 && Heard(SND_FIREWORK_TWINKLE) == 1 && CountParticles(TILE_P_SPARK_0) > 50,
              "and bursts");
        gGame.gliding = true;
        gPlayed.clear();
        CHECK(UseHeldItem() && gGame.boostTime >= 1.0f && gGame.boostTime <= 1.6f && gProjectiles.size() == 1 && gProjectiles[0].pickup &&
                  gInv.Held().count == 2 && Heard(SND_FIREWORK_LAUNCH) == 1,
              "under the elytra it pushes the player on");
        host.player = Vec3(5.5f, 0.5f, 30.0f);
        RunShots(0.05f);
        CHECK(Near(gProjectiles[0].pos.x, 4.9f) && Near(gProjectiles[0].pos.z, 30.0f), "and stays behind him");
        gGame.gliding = false;
        RunShots(0.05f);
        CHECK(gProjectiles.empty(), "until he lands");
        host.vehicle = 5;
        CHECK(UseHeldItem() && host.boost == 1.6f && gInv.Held().count == 1 && gProjectiles.empty(), "at the wheel it is strapped to the vehicle");
    }

    // spawn eggs, buckets, boats, the host's own items
    {
        CombatStage(host);
        gInv.Held() = Stack(ID_COW_SPAWN_EGG, 2);
        CHECK(!UseHeldItem() && gMobs.empty(), "a spawn egg needs a spot");
        TargetFloor();
        CHECK(UseHeldItem() && gMobs.size() == 1 && gMobs[0].kind == MOB_COW && gMobs[0].persistent && Near(gMobs[0].pos.z, 10.0f, 0.03f) &&
                  gInv.Held().count == 1 && Heard(SND_POP) == 1,
              "there the cow stands, and it stays for good");
        host.vehicle = 5;
        CHECK(!UseHeldItem() && gMobs.size() == 1, "not from a vehicle");

        CombatStage(host);
        gGame.lookDir = Vec3(0.6f, 0, -0.8f);
        gInv.Held() = Stack(ID_BUCKET);
        CHECK(!UseHeldItem() && gInv.Held().id == ID_BUCKET, "an empty bucket with no water in sight");
        host.sea = 10.5f;
        CHECK(UseHeldItem() && gInv.Held().id == ID_WATER_BUCKET && Heard(SND_SPLASH) == 1, "the host's water fills it");
        CHECK(UseHeldItem() && host.doused == 2 && gInv.Held().id == ID_BUCKET && Heard(SND_SPLASH) == 2,
              "poured out again, the host's fires there and around the player go out");

        const uint16_t boat = ItemWith(SP_BOAT), cart = ItemWith(SP_MINECART), wand = ItemWith(SP_WAND), rod = ItemWith(SP_FISHING_ROD);
        CHECK(boat && cart && wand && rod, "there is a boat, a minecart, a magic stick and a fishing rod");
        host.sea = -1000.0f;
        gInv.Held() = Stack(boat);
        CHECK(UseHeldItem() && host.lastPlaced == -1 && !gInv.Held().Empty() && gGame.messageTimer > 0.0f, "a boat wants water, the player is told");
        host.sea = 10.5f;
        host.vehiclesOk = false;
        CHECK(!UseHeldItem() && !gInv.Held().Empty(), "when the host cannot make one nothing happens");
        host.vehiclesOk = true;
        CHECK(UseHeldItem() && host.lastPlaced == HOST_BOAT && gInv.Held().Empty(), "on water the host puts a boat there");
        gInv.Held() = Stack(cart);
        TargetFloor();
        gTarget.normal = Vec3(-1, 0, 0);
        CHECK(!UseHeldItem() && host.lastPlaced == HOST_BOAT, "a minecart needs flat ground");
        gTarget.normal = Vec3(0, 0, 1);
        CHECK(UseHeldItem() && host.lastPlaced == HOST_MINECART && gInv.Held().Empty(), "there the host puts one");

        gInv.Held() = Stack(wand);
        CHECK(UseHeldItem() && host.itemUses == 1 && MeleeAttack() && host.itemAttacks == 1, "the magic stick is the host's own business");
        gInv.Held() = Stack(rod);
        CHECK(!UseHeldItem() && host.itemUses == 2, "the fishing rod too");
    }

    // blows
    {
        CombatStage(host);
        int cow = SpawnMob(MOB_COW, Vec3(2.5f, 0.5f, 10.0f), true);
        const float tired = gSurvival.exhaustion;
        CHECK(MeleeAttack() && gMobs[cow].health == 9.0f && Heard(SND_ATTACK_STRONG) == 1 && gGame.attackTimer == 0.0f &&
                  gSurvival.exhaustion > tired && gGame.swing >= 0.0f && gMobs[cow].vel.x > 6.0f,
              "a fist takes one off a cow and knocks it back");
        gMobs[cow].hurt = 0.0f;
        gGame.attackTimer = 0.05f;
        const float charge = AttackCharge();
        MeleeAttack();
        CHECK(charge > 0.1f && charge < 0.3f && Near(gMobs[cow].health, 9.0f - (0.2f + 0.8f * charge * charge), 1e-4f) && Heard(SND_ATTACK_WEAK) == 1,
              "hitting again at once does little (%.2f left)", gMobs[cow].health);

        const float sword = Item(ID_DIAMOND_SWORD).damage;
        CombatStage(host);
        cow = SpawnMob(MOB_COW, Vec3(2.5f, 0.5f, 10.0f), true);
        gInv.Held() = Stack(ID_DIAMOND_SWORD);
        MeleeAttack();
        CHECK(sword > 1.0f && sword < 10.0f && Near(gMobs[cow].health, 10.0f - sword) && Heard(SND_ATTACK_SWEEP) == 1 && gInv.Held().damage == 1,
              "a diamond sword takes %.0f and wears by one", sword);
        gInv.Held() = Stack(ID_IRON_PICKAXE);
        gMobs[cow].hurt = 0.0f;
        gMobs[cow].health = 50.0f;
        gGame.attackTimer = 10.0f;
        MeleeAttack();
        CHECK(gInv.Held().damage == 2 && gMobs[cow].health < 50.0f, "a tool that is no sword wears by two");

        CombatStage(host);
        cow = SpawnMob(MOB_COW, Vec3(2.5f, 0.5f, 10.0f), true);
        host.falling = true;
        MeleeAttack();
        CHECK(gMobs[cow].health == 8.5f && Heard(SND_ATTACK_CRIT) == 1 && CountParticles(TILE_P_CRITICAL_HIT) == 12, "coming down from a jump: half as much again");
        CombatStage(host);
        cow = SpawnMob(MOB_COW, Vec3(2.5f, 0.5f, 10.0f), true);
        host.falling = true;
        gGame.sprinting = gGame.sprintLatch = true;
        MeleeAttack();
        CHECK(gMobs[cow].health == 9.0f && Heard(SND_KNOCKBACK) == 1 && Near(gMobs[cow].vel.x, 11.2f, 0.05f) && !gGame.sprintLatch,
              "sprinting: no critical hit but a harder push, and the sprint is over");

        CombatStage(host);
        cow = SpawnMob(MOB_COW, Vec3(5.0f, 0.5f, 10.0f), true);
        CHECK(!MeleeAttack() && gMobs[cow].health == 10.0f && gGame.attackTimer == 10.0f, "four metres are out of reach");
        gMobs[cow].pos.x = 2.5f;
        gWorld.SetRaw(1, 0, 10, MakeVox(ID_GLASS));
        CHECK(!MeleeAttack(), "no blow through a block");
        gWorld.SetRaw(1, 0, 10, MakeVox(ID_AIR));
        host.carX = 1.2f;
        CHECK(!MeleeAttack(), "nor through something of the host's");
        host.carX = 1e9f;
        CHECK(MeleeAttack(), "with the way free it lands");

        CombatStage(host);
        host.people.push_back({ Vec3(2.5f, 0.5f, 10.9f) });
        CHECK(MeleeAttack() && host.people[0].hurt == 1.0f && host.people[0].how == HURT_FIST && Near(host.people[0].push.x, 6.0f) &&
                  Near(host.people[0].push.z, 2.5f),
              "somebody of the host's is hit and pushed the same way");
        gGame.attackTimer = 10.0f;
        gInv.Held() = Stack(ID_DIAMOND_SWORD);
        MeleeAttack();
        CHECK(host.people[0].how == HURT_SWORD && Near(host.people[0].hurt, 1.0f + sword), "with a sword the host is told so");
        cow = SpawnMob(MOB_COW, Vec3(1.7f, 0.5f, 10.0f), true);
        gGame.attackTimer = 10.0f;
        MeleeAttack();
        CHECK(gMobs[cow].health < 10.0f && Near(host.people[0].hurt, 1.0f + sword), "an animal in front of him takes the blow");
    }

    // what the dead of the host's leave
    {
        CombatStage(host);
        const Vec3 at(3.5f, 0.5f, 10.5f);
        DropVillagerLoot(at, false);
        CHECK(XpOnTheGround() >= 1 && XpOnTheGround() <= 5, "a villager leaves 1 to 5 experience (%d)", XpOnTheGround());
        for (int i = 0; i < 100; ++i)
            DropVillagerLoot(at, false);
        CHECK(CountDrops(ID_ROTTEN_FLESH) > 10 && CountDrops(ID_BREAD) > 5 && CountDrops(ID_CROSSBOW) == 0, "and now and then a few things");
        gDrops.clear();
        gXpOrbs.clear();
        DropVillagerLoot(at, true);
        CHECK(XpOnTheGround() >= 5 && XpOnTheGround() <= 7, "a pillager 5 to 7 (%d)", XpOnTheGround());
        for (int i = 0; i < 100; ++i)
            DropVillagerLoot(at, true);
        CHECK(CountDrops(ID_ARROW) > 50 && CountDrops(ID_CROSSBOW) >= 1 && CountDrops(ID_ROTTEN_FLESH) == 0, "arrows, sometimes his crossbow");
    }

    CombatStage(host);
    gInv.Held() = Stack(ID_SNOWBALL);
    UseHeldItem();
    StrikeLightning(Vec3(3.5f, 0.5f, 10.0f));
    CombatClear();
    CHECK(gProjectiles.empty() && gBolts.empty(), "a new game starts with nothing in the air");
    MobsClear();
    gGame = GameState();
    gRules = GameRules();
}

static void TestSave() {
    const char* kPath = "mc_tests_world.tmp";
    BlockStage();
    ResetSurvival();
    gGame = GameState();
    gGame.gameMode = MODE_CREATIVE;
    gGame.cameraMode = CAM_FIRST;
    gInv.selected = 4;
    gInv.slots[4] = Stack(ID_DIAMOND_PICKAXE);
    gInv.slots[4].damage = 12;
    gInv.slots[20] = Stack(ID_BREAD, 17);
    gInv.armor[ARMOR_FEET] = Stack(ID_IRON_BOOTS);
    gInv.offhand = Stack(ID_TOTEM_OF_UNDYING);
    gSurvival.food = 13.0f;
    gSurvival.saturation = 2.5f;
    gSurvival.xpLevel = 6;
    gSurvival.xpTotal = 80;
    gSurvival.xpProgress = 0.4f;
    gWorld.SetRaw(3, 4, 12, MakeVox(ID_FURNACE, 2));
    const int stones = CountBlocks(ID_STONE);

    FILE* f = fopen(kPath, "wb");
    CHECK(f != nullptr, "the test file can be written");
    if (!f)
        return;
    WriteWorldFile(f, 1);
    const uint32_t tail = 0xABCD1234; // a section of the host's own
    fwrite(&tail, 4, 1, f);
    fclose(f);

    BlockStage();
    gWorld.Clear();
    ResetSurvival();
    gGame = GameState();
    WorldFileInfo info;
    f = fopen(kPath, "rb");
    CHECK(f && ReadWorldFile(f, info) && info.version == 3 && info.hostValue == 1 && info.blocksOk && info.hostPart, "it reads back");
    uint32_t got = 0;
    CHECK(f && fread(&got, 4, 1, f) == 1 && got == tail, "and leaves the file where the host's own part begins");
    if (f)
        fclose(f);
    CHECK(gGame.gameMode == MODE_CREATIVE && gGame.cameraMode == CAM_FIRST && gInv.selected == 4, "game mode, view and hotbar slot");
    CHECK(gInv.slots[4].id == ID_DIAMOND_PICKAXE && gInv.slots[4].damage == 12 && gInv.slots[20].id == ID_BREAD &&
              gInv.slots[20].count == 17 && gInv.armor[ARMOR_FEET].id == ID_IRON_BOOTS && gInv.offhand.id == ID_TOTEM_OF_UNDYING,
          "the inventory, the armour and the off hand");
    CHECK(gSurvival.food == 13.0f && gSurvival.saturation == 2.5f && gSurvival.xpLevel == 6 && gSurvival.xpTotal == 80 &&
              Near(gSurvival.xpProgress, 0.4f),
          "hunger and experience");
    CHECK(CountBlocks(ID_STONE) == stones && gWorld.Get(3, 4, 12) == MakeVox(ID_FURNACE, 2), "and every block (%d stones)",
          CountBlocks(ID_STONE));

    // something that is not a world file changes nothing
    f = fopen(kPath, "wb");
    const uint32_t junk[2] = { 0x12345678, 3 };
    fwrite(junk, sizeof(junk), 1, f);
    fclose(f);
    gInv.selected = 7;
    f = fopen(kPath, "rb");
    CHECK(f && !ReadWorldFile(f, info) && gInv.selected == 7 && CountBlocks(ID_STONE) == stones, "a foreign file is refused");
    if (f)
        fclose(f);
    remove(kPath);
    BlockStage();
    gWorld.Clear();
    ResetSurvival();
    gGame = GameState();
}

// ---------------------------------------------------------------- the game being played (GameState.h)
static void TestGameState() {
    gGame = GameState();
    ResetSurvival();
    gPlayed.clear();
    ShowMessage("hello", 1.5f);
    CHECK(gGame.message == "hello" && gGame.messageTimer == 1.5f, "a message for the line above the hotbar");
    gGame.age = 12.0f;
    NoteDamage(STR_DEATH_FALL);
    CHECK(gGame.deathCause == STR_DEATH_FALL && gGame.deathCauseTime == 12.0f, "what hurt last is remembered for the death screen");

    // the attack cooldown: a fist is ready after a quarter of a second, a sword takes longer
    gGame.attackTimer = 0.125f;
    CHECK(Near(AttackCharge(), 0.5f), "a fist is half ready after an eighth of a second (%.2f)", AttackCharge());
    gGame.attackTimer = 10.0f;
    CHECK(AttackCharge() == 1.0f, "and fully ready later");
    gInv.Held() = Stack(ID_DIAMOND_SWORD);
    const float swordSpeed = Item(ID_DIAMOND_SWORD).attackSpeed;
    gGame.attackTimer = 0.5f / swordSpeed;
    CHECK(swordSpeed > 0.0f && swordSpeed < 4.0f && Near(AttackCharge(), 0.5f), "a sword recharges slower (%.2f attacks a second)",
          swordSpeed);

    // the arm swings, and does not start over in the first half of a swing
    gGame.swing = -1.0f;
    StartSwing();
    CHECK(gGame.swing == 0.0f, "a swing starts");
    gGame.swing = 0.3f;
    StartSwing();
    CHECK(gGame.swing == 0.3f, "but not again before it is half over");
    gGame.swing = 0.6f;
    StartSwing();
    CHECK(gGame.swing == 0.0f, "after that it does");
    gGame.swing = gGame.offSwing = -1.0f;
    gGame.offhandActive = true;
    StartSwing();
    CHECK(gGame.offSwing == 0.0f && gGame.swing == -1.0f, "the off hand swings when it is the one in use");
    gGame.offhandActive = false;

    // tools wear out and break; not in creative, and blocks never do
    gInv.Held() = Stack(ID_WOODEN_PICKAXE);
    const int durability = Item(ID_WOODEN_PICKAXE).durability;
    DamageHeldItem();
    CHECK(durability > 1 && gInv.Held().damage == 1, "using a tool wears it");
    gGame.gameMode = MODE_CREATIVE;
    DamageHeldItem(5);
    CHECK(gInv.Held().damage == 1, "not in creative");
    gGame.gameMode = MODE_SURVIVAL;
    gGame.message.clear();
    DamageHeldItem(durability);
    CHECK(gInv.Held().Empty() && Heard(SND_TOOL_BREAK) == 1 && !gGame.message.empty(), "a worn out tool breaks, and says so");
    gInv.Held() = Stack(ID_STONE);
    DamageHeldItem();
    CHECK(gInv.Held().damage == 0 && gInv.Held().count == 1, "a block in the hand does not wear");

    gGame = GameState();
    ResetSurvival();
}

// ---------------------------------------------------------------- controls (Controls.h)
static void TestControls() {
    // Minecraft's own default keys
    CHECK(kActions[ACT_ATTACK].key == KEY_MOUSE_LEFT && kActions[ACT_USE].key == KEY_MOUSE_RIGHT &&
              kActions[ACT_PICK_BLOCK].key == KEY_MOUSE_MIDDLE,
          "mouse buttons: attack, use, pick block");
    CHECK(kActions[ACT_JUMP].key == KEY_SPACE && kActions[ACT_SNEAK].key == KEY_LSHIFT && kActions[ACT_SPRINT].key == KEY_LCTRL,
          "Space jumps, Shift sneaks, Ctrl sprints");
    CHECK(kActions[ACT_INVENTORY].key == KEY_E && kActions[ACT_DROP].key == KEY_Q && kActions[ACT_SWAP_HANDS].key == KEY_F &&
              kActions[ACT_PERSPECTIVE].key == KEY_F5 && kActions[ACT_BACK].key == KEY_ESCAPE,
          "E inventory, Q drop, F swap hands, F5 perspective, Esc back");
    for (int i = 0; i < 9; ++i)
        CHECK(kActions[ACT_HOTBAR_1 + i].key == KEY_1 + i, "hotbar slot %d is key %d", i + 1, i + 1);
    for (int a = 0; a < ACT_COUNT; ++a) {
        CHECK(kActions[a].name && kActions[a].name[0] && kActions[a].key != KEY_NONE && kActions[a].key < KEY_COUNT,
              "action %d has a name and a key", a);
        for (int b = a + 1; b < ACT_COUNT; ++b)
            CHECK(kActions[a].key != kActions[b].key, "%s and %s share a key", kActions[a].name, kActions[b].name);
    }

    // what the host fills in and the game asks for
    gControls = Controls();
    CHECK(!ActionDown(ACT_JUMP) && !ActionPressed(ACT_JUMP), "nothing is pressed at first");
    gControls.Set(ACT_JUMP, true, true);
    CHECK(ActionDown(ACT_JUMP) && ActionPressed(ACT_JUMP) && !ActionDown(ACT_SNEAK), "a key goes down");
    gControls.Set(ACT_JUMP, true, false);
    CHECK(ActionDown(ACT_JUMP) && !ActionPressed(ACT_JUMP), "and is held");
    gControls.Set(ACT_JUMP, false, false);
    CHECK(!ActionDown(ACT_JUMP) && !ActionPressed(ACT_JUMP), "and comes up again");
    gControls = Controls();
}

// mc_tests <world file>: reads a world file the way the game does and says what is in it (the file is not changed)
static int InspectWorldFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("cannot open %s\n", path);
        return 1;
    }
    WorldFileInfo info;
    const bool ours = ReadWorldFile(f, info);
    fclose(f);
    int items = 0;
    for (const ItemStack& st : gInv.slots)
        items += st.count;
    printf("%s: %s, version %u, blocks %s, host part %s, %u chunks, %d items, food %.1f, level %d\n", path,
           ours ? "a world file" : "NOT a world file", info.version, info.blocksOk ? "ok" : "INCOMPLETE",
           info.hostPart ? "follows" : "none", (unsigned)gWorld.chunks.size(), items, gSurvival.food, gSurvival.xpLevel);
    return ours && info.blocksOk ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc > 1)
        return InspectWorldFile(argv[1]);
    printf("blocks %d, items %d, shaped %d, shapeless %d, cooking %d\n", NUM_BLOCKS - 1, ITEM_END - FIRST_ITEM, kNumShaped,
           kNumShapeless, kNumCooking);
    const uint16_t P = ID_OAK_PLANKS, S = ID_STICK, C = ID_COBBLESTONE, I = ID_IRON_INGOT, G = ID_GUNPOWDER;
    Expect("log->planks", Craft(2, 2, { ID_OAK_LOG, 0, 0, 0 }), ID_OAK_PLANKS, 4);
    Expect("birch log->planks", Craft(3, 3, { 0, 0, 0, 0, ID_BIRCH_LOG, 0, 0, 0, 0 }), ID_BIRCH_PLANKS, 4);
    Expect("sticks", Craft(2, 2, { P, 0, P, 0 }), ID_STICK, 4);
    Expect("sticks (right column)", Craft(2, 2, { 0, P, 0, P }), ID_STICK, 4);
    Expect("crafting table", Craft(2, 2, { P, P, P, P }), ID_CRAFTING_TABLE, 1);
    Expect("wooden pickaxe", Craft(3, 3, { P, P, P, 0, S, 0, 0, S, 0 }), ID_WOODEN_PICKAXE, 1);
    Expect("stone pickaxe", Craft(3, 3, { C, C, C, 0, S, 0, 0, S, 0 }), ID_STONE_PICKAXE, 1);
    Expect("stone axe", Craft(3, 3, { C, C, 0, C, S, 0, 0, S, 0 }), ID_STONE_AXE, 1);
    Expect("stone axe mirrored", Craft(3, 3, { 0, C, C, 0, S, C, 0, S, 0 }), ID_STONE_AXE, 1);
    Expect("iron sword", Craft(3, 3, { 0, I, 0, 0, I, 0, 0, S, 0 }), ID_IRON_SWORD, 1);
    Expect("diamond sword", Craft(3, 3, { ID_DIAMOND, 0, 0, ID_DIAMOND, 0, 0, S, 0, 0 }), ID_DIAMOND_SWORD, 1);
    Expect("furnace", Craft(3, 3, { C, C, C, C, 0, C, C, C, C }), ID_FURNACE, 1);
    Expect("chest", Craft(3, 3, { P, P, P, P, 0, P, P, P, P }), ID_CHEST, 1);
    Expect("bow", Craft(3, 3, { 0, S, ID_STRING, S, 0, ID_STRING, 0, S, ID_STRING }), ID_BOW, 1);
    Expect("arrow", Craft(3, 3, { ID_FLINT, 0, 0, S, 0, 0, ID_FEATHER, 0, 0 }), ID_ARROW, 4);
    Expect("tnt", Craft(3, 3, { G, ID_SAND, G, ID_SAND, G, ID_SAND, G, ID_SAND, G }), ID_TNT, 1);
    Expect("flint and steel", Craft(2, 2, { I, ID_FLINT, 0, 0 }), ID_FLINT_AND_STEEL, 1);
    Expect("firework rocket", Craft(2, 2, { G, ID_PAPER, 0, 0 }), ID_FIREWORK_ROCKET, 3);
    Expect("iron block", Craft(3, 3, { I, I, I, I, I, I, I, I, I }), ID_IRON_BLOCK, 1);
    Expect("iron block -> ingots", Craft(2, 2, { ID_IRON_BLOCK, 0, 0, 0 }), ID_IRON_INGOT, 9);
    Expect("bread", Craft(3, 3, { ID_WHEAT, ID_WHEAT, ID_WHEAT, 0, 0, 0, 0, 0, 0 }), ID_BREAD, 1);
    Expect("garbage", Craft(2, 2, { ID_DIRT, ID_STONE, 0, 0 }), 0, 0);
    Expect("empty", Craft(2, 2, { 0, 0, 0, 0 }), 0, 0);

    int t = 0;
    CHECK(CookResult(COOK_SMELTING, ID_RAW_IRON, &t) == ID_IRON_INGOT && t == 200, "smelt raw iron (t=%d)", t);
    CHECK(CookResult(COOK_BLASTING, ID_RAW_IRON, &t) == ID_IRON_INGOT && t == 100, "blast raw iron (t=%d)", t);
    CHECK(CookResult(COOK_SMELTING, ID_COBBLESTONE) == ID_STONE, "smelt cobblestone");
    CHECK(CookResult(COOK_SMELTING, ID_SAND) == ID_GLASS, "smelt sand");
    CHECK(CookResult(COOK_SMELTING, ID_OAK_LOG) == ID_CHARCOAL, "smelt log");
    CHECK(CookResult(COOK_SMOKING, ID_BEEF) == ID_COOKED_BEEF, "smoke beef");
    CHECK(CookResult(COOK_SMELTING, ID_DIRT) == 0, "dirt must not smelt");

    CHECK(FuelTicks(ID_COAL) == 1600, "coal fuel");
    CHECK(FuelTicks(ID_OAK_PLANKS) == 300, "planks fuel %d", FuelTicks(ID_OAK_PLANKS));
    CHECK(FuelTicks(ID_STONE) == 0, "stone fuel");
    CHECK(MaxStack(ID_DIAMOND_SWORD) == 1 && MaxStack(ID_ENDER_PEARL) == 16 && MaxStack(ID_STONE) == 64, "stack sizes");
    CHECK(Item(ID_DIAMOND_SWORD).tool == TOOL_SWORD && Item(ID_DIAMOND_SWORD).damage == 7.0f, "diamond sword stats");
    CHECK(Item(ID_ELYTRA).armorSlot == 2 && Item(ID_ELYTRA).special == SP_ELYTRA, "elytra");
    CHECK(Item(ID_BOW).special == SP_BOW && Item(ID_ARROW).special == SP_ARROW, "bow/arrow");
    CHECK(Item(ID_FLINT_AND_STEEL).special == SP_IGNITER && Item(ID_FIREWORK_ROCKET).special == SP_FIREWORK,
          "igniter/firework");
    CHECK(Block(ID_STONE).requiresTool && Block(ID_STONE).tool == TOOL_PICKAXE, "stone needs pickaxe");
    CHECK(!Block(ID_DIRT).requiresTool && Block(ID_DIRT).tool == TOOL_SHOVEL, "dirt");
    CHECK(Block(ID_DIAMOND_ORE).tier == TIER_IRON, "diamond ore tier %d", Block(ID_DIAMOND_ORE).tier);
    CHECK(Block(ID_OAK_LOG).shape == SHAPE_COLUMN && Block(ID_FURNACE).shape == SHAPE_FACING, "shapes");
    CHECK(Block(ID_GLASS).render == RENDER_CUTOUT && Block(ID_WHITE_STAINED_GLASS).render == RENDER_TRANSLUCENT &&
              Block(ID_STONE).render == RENDER_OPAQUE,
          "render types");
    // facing: the front tile follows the facing
    for (int f = 0; f < 4; ++f) {
        static const int faces[4] = { FACE_NORTH, FACE_EAST, FACE_SOUTH, FACE_WEST };
        int fronts = 0;
        for (int k = 0; k < 4; ++k)
            if (BlockFaceTile(ID_FURNACE, faces[k], f) == Block(ID_FURNACE).tex[FACE_NORTH])
                fronts += (k == f) ? 1 : 10;
        CHECK(fronts == 1, "furnace facing %d front count %d", f, fronts);
    }
    CHECK(BlockFaceTile(ID_FURNACE, FACE_NORTH, META_LIT) == Block(ID_FURNACE).texFrontLit, "lit furnace front");
    CHECK(BlockFaceTile(ID_OAK_LOG, FACE_EAST, 1) == Block(ID_OAK_LOG).tex[FACE_TOP], "log axis X end");
    CHECK(BlockFaceTile(ID_OAK_LOG, FACE_TOP, 1) == Block(ID_OAK_LOG).tex[FACE_EAST], "log axis X side on top");

    size_t total = 0;
    for (int c = 0; c < CAT_COUNT; ++c) {
        printf("creative tab %d: %d items\n", c, (int)CreativeItems(c).size());
        total += CreativeItems(c).size();
    }
    size_t fluids = 0; // water and lava only come from buckets
    for (int b = 1; b < NUM_BLOCKS; ++b)
        if (IsFluidBlock(b) || IsFireBlock(b))
            ++fluids;
    CHECK(total == (size_t)(NUM_BLOCKS - 1 - fluids + ITEM_END - FIRST_ITEM), "creative tabs cover everything (%d vs %d)",
          (int)total, (int)(NUM_BLOCKS - 1 - fluids + ITEM_END - FIRST_ITEM));

    // inventory behaviour
    PlayerInventory inv;
    ItemStack s;
    s.id = ID_STONE;
    s.count = 64;
    for (int i = 0; i < 36; ++i)
        CHECK(inv.Add(s) == 0, "add stack %d", i);
    CHECK(inv.Add(s) == 64, "full inventory must reject");
    CHECK(inv.CountOf(ID_STONE) == 36 * 64, "count");
    CHECK(inv.Remove(ID_STONE, 100) && inv.CountOf(ID_STONE) == 36 * 64 - 100, "remove");

    TestPhysics();
    TestBlockRules();
    TestSurvival();
    TestEntities();
    TestInteract();
    TestMobs();
    TestCombat();
    TestSave();
    TestControls();
    TestGameState();

    printf(gFail ? "\n%d CHECK(S) FAILED\n" : "\nALL CHECKS PASSED\n", gFail);
    return gFail;
}
