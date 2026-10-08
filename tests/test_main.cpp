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
#include "BlockMesh.h"
#include "Combat.h"
#include "Fishing.h"
#include "Hands.h"
#include "Hud.h"
#include "Items.h"
#include "McModel.h"
#include "Mobs.h"
#include "Screens.h"
#include "Shapes.h"
#include "Beds.h"
#include "Villagers.h"
#include "Particles.h"
#include "PlayerAnim.h"
#include "Renderers.h"
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
        for (int i = 0; i < 180; ++i)
            WalkStep(b, in, dt, map, ev);
        CHECK(ev.fluid == ID_LAVA && ev.inFluid, "in lava (it burns: Survival BurnTick)");
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
    bool dark = false;
    bool herdGround = false; // animals may appear anywhere, at z = 10
    float sea = -1000.0f;    // its water stands this high everywhere
    bool soil = false;     // its ground is everywhere, and it is earth
    bool blocking = false; // somebody stands in `blocked`
    Int3 blocked;
    bool thing = false;    // something of its own is in front of the player, two metres away
    bool Raining() override { return rain; }
    bool Outdoors() override { return outdoors; }
    bool Dark() override { return dark; }
    bool ownGround = false; // the dug ground's cells are the host's, half as bright
    bool OwnGround(const Int3&, float* depthShade) override {
        *depthShade = 0.5f;
        return ownGround;
    }
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
    // the fishing hook: their vehicle tells where on it the hook sits
    int flung = 0;
    Vec3 flingVel;
    HostHit HookTrace(const Vec3& from, const Vec3& dir, float len) override {
        HostHit h = Trace(from, dir, len, false);
        if (h.vehicle >= 0)
            h.local = Vec3(0.0f, 0.0f, h.point.z - 10.0f);
        return h;
    }
    bool HookPoint(const HostHit& on, Vec3* at) override {
        if (on.being >= 0 && on.being < (int)people.size()) {
            *at = people[on.being].centre + Vec3(0, 0, 0.3f);
            return true;
        }
        if (on.vehicle == kCarNumber && carX < 1e8f) {
            *at = Vec3(carX, 0.5f, 10.0f) + on.local;
            return true;
        }
        return false;
    }
    int talks = 0;
    HostHit BeingTrace(const Vec3& origin, const Vec3& dir, float reach) override {
        HostHit h = Trace(origin, dir, reach, false);
        h.hit = false;
        h.vehicle = -1;
        return h;
    }
    bool UseOnBeing(int) override {
        ++talks;
        return true;
    }
    bool Fling(const HostHit& what, const Vec3& v) override {
        Vec3 at;
        if (!HookPoint(what, &at))
            return false;
        ++flung;
        flingVel = v;
        return true;
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
    float hours = 12.0f, clockSet = -1.0f;
    bool clockNextDay = false;
    float ClockHours() override { return hours; }
    void SetClock(float h, bool nextDay) override {
        clockSet = h;
        clockNextDay = nextDay;
    }
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
        CHECK(!TargetHasUse(), "the floor is none");
        gWorld.SetRaw(0, 0, 10, MakeVox(ID_CRAFTING_TABLE));
        LookFrom(Vec3(0.5f, 0.5f, 12.62f), down);
        CHECK(TargetHasUse(), "a crafting table is");
        UseTargetBlock();
        CHECK(gGame.screen == SCREEN_CRAFTING && gGame.openPos == (Int3{ 0, 0, 10 }), "and opens its screen");
        CloseScreen();
        gWorld.SetRaw(0, 0, 10, MakeVox(ID_CHEST));
        UseTargetBlock();
        CHECK(gGame.screen == SCREEN_CHEST, "a chest opens the chest screen");
        CloseScreen();
        gWorld.SetRaw(0, 0, 10, MakeVox(ID_FURNACE));
        UseTargetBlock();
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
        host.vehicle = -1;
        gInv.Held() = Stack(ID_CREEPER_SPAWN_EGG);
        CHECK(UseHeldItem() && gMobs.size() == 2 && gMobs[1].kind == MOB_CREEPER, "a creeper egg makes a creeper");

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
        CHECK(UseHeldItem() && host.itemUses == 1 && FishingIsCast(), "the fishing rod is no business of the host's: it casts");
        FishingClear();
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

static void RunFishing(float seconds) {
    const float dt = 0.05f;
    for (float t = 0.0f; t < seconds; t += dt)
        FishingTick(dt);
}

// casts the held rod and lets the bobber fly until it lands somewhere (or a second has gone)
static void Cast() {
    UseHeldItem();
    for (int i = 0; i < 20 && gBobber.state == BOB_FLYING; ++i)
        FishingTick(0.05f);
}

static void TestFishing() {
    TestHost host;
    SetHost(&host);
    srand(1357);

    // a catch
    {
        CombatStage(host);
        host.sea = 10.5f;
        gInv.Held() = Stack(ID_FISHING_ROD);
        CHECK(UseHeldItem() && FishingIsCast() && gBobber.state == BOB_FLYING && Heard(SND_BOBBER_THROW) == 1 && gGame.swing >= 0.0f,
              "the rod casts the bobber");
        CHECK(Near(gBobber.vel.x, 21.0f) && Near(gBobber.vel.z, 3.0f) && Near(gBobber.pos.x, 1.2f), "at 21 m/s, a little upwards");
        RunFishing(1.0f);
        CHECK(gBobber.state == BOB_FLOATING && gBobber.pos.x > 6.0f && gBobber.pos.x < 11.0f && Heard(SND_BOBBER_SPLASH) == 1,
              "it lands in the host's water (x %.1f)", gBobber.pos.x);
        float waited = 0.0f;
        while (gBobber.nibble <= 0.0f && waited < 40.0f) {
            FishingTick(0.05f);
            waited += 0.05f;
        }
        CHECK(gBobber.nibble > 0.0f && waited > 4.0f && Heard(SND_BOBBER_SPLASH) == 2, "a fish bites after a while (%.1f s)", waited);
        const float tired = gSurvival.exhaustion;
        gPlayed.clear();
        CHECK(UseHeldItem() && !FishingIsCast() && Heard(SND_BOBBER_RETRIEVE) == 1, "reeling in");
        int caught = 0;
        for (const DropEntity& d : gDrops)
            caught += d.stack.count;
        CHECK(caught == 1 && gDrops[0].vel.x < 0.0f && XpOnTheGround() >= 1 && XpOnTheGround() <= 6, "brings the catch and some experience");
        CHECK(gInv.Held().damage == 1 && Near(gSurvival.exhaustion - tired, 0.05f), "the rod wears by one");

        gDrops.clear();
        Cast();
        RunFishing(0.5f);
        CHECK(gBobber.state == BOB_FLOATING && gBobber.nibble <= 0.0f, "cast again");
        UseHeldItem();
        CHECK(gDrops.empty() && gInv.Held().damage == 1, "too early: nothing, and the rod does not wear");

        // what lies by the hook comes along
        Cast();
        SpawnDrop(gBobber.pos, Stack(ID_STICK), Vec3(), 0.5f);
        UseHeldItem();
        CHECK(gDrops.size() == 1 && gDrops[0].vel.x < -5.0f && gDrops[0].pickupDelay == 0.0f, "things lying by the hook are pulled in");
    }

    // the line breaks
    {
        CombatStage(host);
        host.sea = 10.5f;
        gInv.Held() = Stack(ID_FISHING_ROD);
        Cast();
        gInv.selected = 1;
        RunFishing(0.05f);
        CHECK(!FishingIsCast(), "when the rod is put away");
        gInv.selected = 0;
        Cast();
        host.vehicle = 5;
        RunFishing(0.05f);
        CHECK(!FishingIsCast(), "when the player gets into a vehicle");
        host.vehicle = -1;
        Cast();
        host.player = Vec3(60.5f, 0.5f, 11.0f);
        RunFishing(0.05f);
        CHECK(!FishingIsCast(), "when he is more than 40 m away");
        host.player = Vec3(0.5f, 0.5f, 11.0f);
        host.vehicle = 5;
        CHECK(!UseHeldItem() && !FishingIsCast(), "nobody fishes from a vehicle");
    }

    // what the hook can catch besides fish
    {
        CombatStage(host);
        gInv.Held() = Stack(ID_FISHING_ROD);
        Cast();
        RunFishing(1.0f);
        CHECK(gBobber.state == BOB_STUCK && gBobber.pos.z > 9.9f && gBobber.pos.z < 10.1f, "on the floor it just lies");
        UseHeldItem();
        CHECK(gInv.Held().damage == 2, "pulling it out of the ground wears the rod by two");

        CombatStage(host);
        gInv.Held() = Stack(ID_FISHING_ROD);
        const int cow = SpawnMob(MOB_COW, Vec3(4.5f, 0.5f, 10.0f), true);
        Cast();
        CHECK(gBobber.state == BOB_HOOKED_MOB && gBobber.mobId == MobIdAt(cow), "an animal is hooked");
        gMobs[cow].pos.y = 2.5f;
        RunFishing(0.05f);
        CHECK(Near(gBobber.pos.y, 2.5f) && Near(gBobber.pos.z, 10.9f), "the bobber goes along with it");
        UseHeldItem();
        CHECK(gMobs[cow].vel.x < -3.0f && gMobs[cow].vel.z > 5.0f && gInv.Held().damage == 3, "and reeling in pulls it over (the rod wears by three)");

        CombatStage(host);
        gInv.Held() = Stack(ID_FISHING_ROD);
        host.people.push_back({ Vec3(5.5f, 0.5f, 10.9f) });
        Cast();
        CHECK(gBobber.state == BOB_HOOKED_BEING && gBobber.hooked.being == 0, "somebody of the host's is hooked");
        RunFishing(0.05f);
        CHECK(Near(gBobber.pos.z, 11.2f), "the host says where the hook is");
        UseHeldItem();
        CHECK(host.flung == 1 && Near(host.flingVel.x, -7.5f, 0.05f) && Near(host.flingVel.z, 8.0f, 0.05f) && gInv.Held().damage == 3,
              "he is flung towards the player");

        CombatStage(host);
        gInv.Held() = Stack(ID_FISHING_ROD);
        host.carX = 6.0f;
        Cast();
        CHECK(gBobber.state == BOB_HOOKED_VEHICLE && gBobber.hooked.vehicle == TestHost::kCarNumber && Near(gBobber.pos.x, 6.0f), "a vehicle is hooked");
        host.carX = 7.0f;
        RunFishing(0.05f);
        CHECK(Near(gBobber.pos.x, 7.0f), "the hook moves with it");
        UseHeldItem();
        CHECK(host.flung == 1 && Near(host.flingVel.x, -5.85f, 0.05f) && gInv.Held().damage == 5, "a vehicle is pulled more gently and wears the rod by five");
        Cast();
        host.carX = 1e9f;
        RunFishing(0.05f);
        CHECK(gBobber.state == BOB_STUCK, "when it is gone the hook just lies there");
    }
    FishingClear();
}

static void TestVillagers() {
    TestHost host;
    SetHost(&host);
    srand(97531);
    const Vec3 player(0.5f, 0.5f, 11.0f);

    // how they act
    {
        CombatStage(host);
        NpcLook l = NpcStart(NPC_VILLAGER, 1, 100.0f, false);
        CHECK(l.kind == NPC_VILLAGER && l.variant == 1 && l.death < 0.0f && l.say >= 4.0f && l.say <= 44.0f && l.offer >= 0 && l.offer < 5,
              "a new villager");
        NpcFacts f;
        f.pos = Vec3(3.5f, 0.5f, 10.0f);
        f.forward = Vec3(0, 1, 0);
        f.speed = 4.0f;
        f.health = 100.0f;
        l.say = 100.0f;
        for (int i = 0; i < 20; ++i)
            NpcTick(l, 0.05f, f, player);
        CHECK(l.limbAmount > 0.5f && l.limbSwing > 5.0f, "walking swings his legs");
        CHECK(l.headYaw < -0.9f, "his head turns to the player next to him (%.2f)", l.headYaw);
        f.pos = Vec3(9.5f, 0.5f, 10.0f);
        for (int i = 0; i < 40; ++i)
            NpcTick(l, 0.05f, f, player);
        CHECK(std::fabs(l.headYaw) < 0.05f, "and back when he is further away");
        f.health = 90.0f;
        NpcTick(l, 0.05f, f, player);
        CHECK(Heard(SND_VILLAGER_HURT) == 1 && l.hurt == 0.45f, "a villager who is hurt cries out and flashes red");
        f.health = 80.0f;
        NpcTick(l, 0.05f, f, player);
        CHECK(Heard(SND_VILLAGER_HURT) == 1, "once per flash");
        l.say = 0.01f;
        NpcTick(l, 0.05f, f, player);
        CHECK(Heard(SND_VILLAGER_SAY) == 1 && l.say >= 15.0f, "now and then he says something");
        f.dead = true;
        NpcTick(l, 0.05f, f, player);
        NpcTick(l, 0.05f, f, player);
        CHECK(Heard(SND_VILLAGER_DEATH) == 1 && Near(l.death, 0.1f), "he dies once");
        NpcLook p = NpcStart(NPC_PILLAGER, 0, 100.0f, false);
        f.dead = false;
        f.health = 100.0f;
        NpcTick(p, 0.05f, f, player);
        f.health = 50.0f;
        NpcTick(p, 0.05f, f, player);
        CHECK(Heard(SND_PILLAGER_HURT) == 1, "pillagers sound like pillagers");
        CHECK(NpcStart(NPC_VILLAGER, 0, 0.0f, true).death == 10.0f, "somebody found dead is long dead");
    }

    // trading
    {
        CombatStage(host);
        const Vec3 head(3.5f, 0.5f, 11.7f);
        int offer = 0;
        VillagerTrade(0, offer, head);
        CHECK(Heard(SND_VILLAGER_NO) == 1 && gGame.messageTimer > 0.0f && gGame.swing >= 0.0f, "a villager without a profession has nothing");
        gInv.Held() = Stack(ID_WHEAT, 30);
        VillagerTrade(1, offer, head);
        CHECK(gInv.CountOf(ID_WHEAT) == 10 && gInv.CountOf(ID_EMERALD) == 1 && Heard(SND_VILLAGER_TRADE) == 1 &&
                  CountParticles(TILE_P_ENCHANTED_HIT) == 8,
              "a farmer buys 20 wheat for an emerald");
        VillagerTrade(1, offer, head);
        CHECK(gInv.CountOf(ID_WHEAT) == 10 && gInv.CountOf(ID_EMERALD) == 1 && Heard(SND_VILLAGER_NO) == 2, "ten are not enough");
        gInv = PlayerInventory();
        gInv.Held() = Stack(ID_EMERALD, 1);
        VillagerTrade(7, offer, head);
        CHECK(gInv.CountOf(ID_BREAD) == 6 && gInv.CountOf(ID_EMERALD) == 0, "he sells six bread for one (profession 7 is a farmer again)");
        gInv.Held() = Stack(ID_EMERALD, 1);
        offer = 3;
        VillagerTrade(1, offer, head);
        CHECK(gInv.CountOf(ID_GOLDEN_CARROT) == 0 && gInv.CountOf(ID_EMERALD) == 1 && Heard(SND_VILLAGER_NO) == 3, "golden carrots cost three");
        gGame.gameMode = MODE_CREATIVE;
        VillagerTrade(1, offer, head);
        CHECK(gInv.CountOf(ID_GOLDEN_CARROT) == 3 && gInv.CountOf(ID_EMERALD) == 1, "in creative one is enough, and it stays");
        gInv.Held() = Stack(ID_DIRT, 1);
        VillagerTrade(1, offer, head);
        CHECK(offer == 4 && Heard(SND_VILLAGER_YES) == 1 && gGame.message.find("Zümrüt") != std::string::npos, "anything else: the next offer");
        offer = 59;
        VillagerTrade(1, offer, head);
        CHECK(offer == 0, "the offers go round");
    }
}

static void TestPlayerHealth() {
    TestHost host;
    SetHost(&host);
    srand(8642);

    // damage
    {
        CombatStage(host);
        float health = 100.0f;
        gSurvival.healthSeen = -1.0f;
        CHECK(!HealthTick(0.05f, health, 100.0f, 0.0f) && gSurvival.healthSeen == 100.0f && gGame.deathTime < 0.0f,
              "the first look only remembers the health");
        health = 60.0f;
        HealthTick(0.05f, health, 100.0f, 0.0f);
        CHECK(health == 60.0f && gGame.hurtTimer == 0.5f && Heard(SND_HURT) == 1 && gSurvival.healthSeen == 60.0f, "without armour a hit takes it all");
        gInv.armor[0] = Stack(ID_IRON_HELMET);
        gInv.armor[1] = Stack(ID_IRON_CHESTPLATE);
        const float block = ArmorBlock();
        health = 20.0f;
        HealthTick(0.05f, health, 100.0f, 0.0f);
        CHECK(block > 0.2f && Near(health, 20.0f + 40.0f * block, 0.01f) && gInv.armor[0].damage == 2 && gInv.armor[1].damage == 2,
              "armour gives some of it back (%.1f) and wears", health);
        const float before = health;
        health -= 10.0f;
        HealthTick(0.05f, health, 100.0f, 10.0f);
        CHECK(Near(health, before - 10.0f) && gInv.armor[0].damage == 2, "poison and drowning go through armour");
        health -= 0.3f;
        gGame.hurtTimer = 0.0f;
        HealthTick(0.05f, health, 100.0f, 0.0f);
        CHECK(gGame.hurtTimer == 0.0f, "a scratch is nothing");
        gGame.gameMode = MODE_CREATIVE;
        gPlayed.clear();
        const float c = health;
        health -= 20.0f;
        HealthTick(0.05f, health, 100.0f, 0.0f);
        CHECK(Near(health, c - 20.0f) && Heard(SND_HURT) == 0 && gGame.hurtTimer == 0.5f, "in creative the host decides alone");

        // the host's own yellow hearts (body armour)
        gGame.gameMode = MODE_SURVIVAL;
        gInv.armor[0] = gInv.armor[1] = ItemStack();
        float pool = 15.0f;
        health = 80.0f;
        HealthTick(0.05f, health, 100.0f, 0.0f, &pool);
        health -= 10.0f;
        HealthTick(0.05f, health, 100.0f, 0.0f, &pool);
        CHECK(Near(health, 80.0f) && Near(pool, 5.0f), "a hit takes the host's yellow hearts first");
        health -= 10.0f;
        HealthTick(0.05f, health, 100.0f, 0.0f, &pool);
        CHECK(Near(health, 75.0f) && pool == 0.0f, "then the health");
        health -= 10.0f;
        pool = 50.0f;
        HealthTick(0.05f, health, 100.0f, 10.0f, &pool);
        CHECK(Near(health, 65.0f) && pool == 50.0f, "poison and drowning go past them");

        // the totem
        gInv.armor[0] = gInv.armor[1] = ItemStack();
        gInv.Held() = Stack(ID_TOTEM_OF_UNDYING);
        health = 10.0f;
        CHECK(HealthTick(0.05f, health, 100.0f, 0.0f) && health == 50.0f && gInv.Held().Empty() && Heard(SND_TOTEM) == 1, "a totem saves him");
        CHECK(CountParticles(TILE_P_SPARK_0) == 60 && gGame.messageTimer > 0.0f && HasEffect(EFFECT_REGENERATION) && gSurvival.healthSeen == 50.0f,
              "with sparks, a message and regeneration");
        health = 10.0f;
        CHECK(!HealthTick(0.05f, health, 100.0f, 0.0f) && Near(health, 50.0f) && !HasEffect(EFFECT_ABSORPTION), "only once; its yellow hearts soak up the next hit and are gone");

        // burning
        ClearEffects(); // (the totem gave fire resistance)
        gSurvival.burn = 0.0f;
        health = 100.0f;
        CHECK(!BurnTick(0.05f, ID_AIR, false, true, health, 100.0f) && health == 100.0f, "nothing burns");
        CHECK(BurnTick(0.05f, ID_FIRE, false, true, health, 100.0f) && Near(health, 95.0f) && Near(gSurvival.burn, 7.95f) &&
                  gGame.deathCause == STR_DEATH_FIRE,
              "stepping into fire: a hit, and he catches fire");
        for (int i = 0; i < 10; ++i)
            BurnTick(0.05f, ID_FIRE, false, true, health, 100.0f);
        CHECK(Near(health, 90.0f), "in the flames a hit every half second (%.1f)", health);
        float lowest = health;
        int frames = 0;
        while (BurnTick(0.05f, ID_AIR, false, true, health, 100.0f) && frames < 400)
            ++frames;
        lowest = health;
        CHECK(frames > 150 && frames < 165 && lowest >= 45.0f && lowest <= 55.0f, "out of them he burns on for 8 s, a point a second (%.1f left)", lowest);
        BurnTick(0.05f, ID_LAVA, false, true, health, 100.0f);
        CHECK(Near(health, lowest - 20.0f) && gSurvival.burn > 14.0f && gGame.deathCause == STR_DEATH_LAVA, "lava: four points, 15 s of fire");
        CHECK(!BurnTick(0.05f, ID_AIR, true, true, health, 100.0f) && gSurvival.burn == 0.0f, "water puts him out");
        AddEffect(EFFECT_FIRE_RESISTANCE, 60.0f, 0);
        const float unburnt = health;
        for (int i = 0; i < 40; ++i)
            BurnTick(0.05f, ID_LAVA, false, true, health, 100.0f);
        CHECK(health == unburnt && gSurvival.burn > 0.0f, "fire resistance: burning without pain");
        RemoveEffect(EFFECT_FIRE_RESISTANCE);
        CHECK(!BurnTick(0.05f, ID_FIRE, false, false, health, 100.0f) && health == unburnt && gSurvival.burn == 0.0f, "creative: nothing");
        health = 100.0f;
        HealthTick(0.05f, health, 100.0f, 0.0f);
        gGame.deathCauseTime = -100.0f; // (what hurt him last is long ago)

        // dead
        health = 0.0f;
        HealthTick(0.05f, health, 100.0f, 0.0f);
        HealthTick(0.05f, health, 100.0f, 0.0f);
        CHECK(Near(gGame.deathTime, 0.05f) && gGame.deathCause == STR_DEATH_GENERIC, "while he is dead the death screen's clock runs");
        health = 100.0f;
        HealthTick(0.05f, health, 100.0f, 0.0f);
        CHECK(gGame.deathTime < 0.0f && health == 100.0f, "back alive");

        // starving
        gSurvival.food = 0.0f;
        gSurvival.foodTimer = 3.99f;
        gGame.hurtTimer = 0.0f;
        SurvivalEvents ev;
        HungerTick(0.05f, health, 100.0f, ev);
        CHECK(health == 95.0f && gGame.hurtTimer == 0.5f && gSurvival.healthSeen == 95.0f, "starving flashes red; it is no hit for the armour");
    }

    // death
    {
        CombatStage(host);
        const Vec3 at(3.5f, 0.5f, 10.5f);
        gInv.slots[0] = Stack(ID_DIRT, 5);
        gInv.armor[0] = Stack(ID_IRON_HELMET);
        gGame.screen = SCREEN_INVENTORY;
        PlayerDied(at);
        CHECK(gGame.screen == SCREEN_NONE && Heard(SND_PLAYER_DEATH) == 1 && gDrops.empty() && gInv.CountOf(ID_DIRT) == 5,
              "the rules keep his things");
        gRules.keepInventory = false;
        PlayerDied(at);
        CHECK(CountDrops(ID_DIRT) == 5 && CountDrops(ID_IRON_HELMET) == 1 && gInv.CountOf(ID_DIRT) == 0 && gInv.armor[0].Empty() &&
                  gDrops[0].pickupDelay == 2.0f,
              "otherwise they fall out");
        gDrops.clear();
        gInv.slots[0] = Stack(ID_DIRT, 5);
        gGame.gameMode = MODE_CREATIVE;
        PlayerDied(at);
        CHECK(gDrops.empty() && gInv.CountOf(ID_DIRT) == 5, "not in creative");
    }

    // a fresh world
    {
        gWorld.Clear();
        gInv = PlayerInventory();
        GiveStarterKit();
        CHECK(gInv.CountOf(ID_CRAFTING_TABLE) == 1 && gInv.CountOf(ID_OAK_PLANKS) == 16 && gInv.CountOf(ID_APPLE) == 4, "a new world starts with a kit");
        GiveStarterKit();
        CHECK(gInv.CountOf(ID_CRAFTING_TABLE) == 1, "once");
        BlockStage();
        gInv = PlayerInventory();
        GiveStarterKit();
        CHECK(gInv.CountOf(ID_CRAFTING_TABLE) == 0, "a world with blocks in it gets none");
    }
    gGame = GameState();
    gRules = GameRules();
    ResetSurvival();
}

// one frame of the hands: `pressed` went down this frame, `down` are held
static HandsResult Hands(std::initializer_list<Action> pressed, std::initializer_list<Action> down = {}, HandsFacts f = HandsFacts()) {
    gControls = Controls();
    for (Action a : down)
        gControls.Set(a, true, false);
    for (Action a : pressed)
        gControls.Set(a, true, true);
    InteractTick(0.05f);
    return HandsTick(0.05f, f);
}

// holds a button for a while (pressed in the first frame)
static HandsResult HoldHands(Action a, float seconds, HandsFacts f = HandsFacts()) {
    HandsResult last, any;
    for (float t = 0.0f; t < seconds; t += 0.05f) {
        last = t == 0.0f ? Hands({ a }, { a }, f) : Hands({}, { a }, f);
        if (last.finished)
            any = last;
    }
    if (any.finished)
        last.finished = any.finished;
    return last;
}

static void TestHands() {
    TestHost host;
    SetHost(&host);
    srand(1122);
    const Vec3 down45 = Vec3(1, 0, -1) * (1.0f / std::sqrt(2.0f));

    // screens
    {
        CombatStage(host);
        HandsResult r = Hands({ ACT_INVENTORY });
        CHECK(r.blockPad && gGame.screen == SCREEN_INVENTORY, "the inventory key opens the inventory");
        r = Hands({ ACT_HOTBAR_3 });
        CHECK(r.blockPad && !r.aimed && gInv.selected == 0, "while it is open the hands rest and GTA gets no buttons");
        r = Hands({ ACT_INVENTORY });
        CHECK(r.blockPad && gGame.screen == SCREEN_NONE, "the same key closes it");
        Hands({ ACT_INVENTORY });
        Hands({ ACT_BACK });
        CHECK(gGame.screen == SCREEN_NONE, "so does Escape");
        gGame.gameMode = MODE_CREATIVE;
        Hands({ ACT_INVENTORY });
        CHECK(gGame.screen == SCREEN_CREATIVE, "in creative the item tabs open");
        HandsFacts dead;
        dead.alive = false;
        Hands({}, {}, dead);
        CHECK(gGame.screen == SCREEN_NONE, "dying closes it");
    }

    // dead hands
    {
        CombatStage(host);
        gGame.bowDraw = 0.5f;
        gGame.spyglass = true;
        gSurvival.eatTimer = 1.0f;
        HandsFacts dead;
        dead.alive = false;
        const HandsResult r = Hands({ ACT_HOTBAR_3, ACT_INVENTORY }, {}, dead);
        CHECK(!r.aimed && !r.blockPad && gInv.selected == 0 && gGame.screen == SCREEN_NONE, "the dead do nothing");
        CHECK(gGame.bowDraw < 0.0f && !gGame.spyglass && gSurvival.eatTimer == 0.0f, "and let go of what they were doing");
    }

    // the hotbar, the hands, dropping
    {
        CombatStage(host);
        HandsResult r = Hands({ ACT_HOTBAR_3 });
        CHECK(r.aimed && !r.blockPad && gInv.selected == 2, "the hotbar keys");
        gInv.Held() = Stack(ID_DIRT, 5);
        gInv.offhand = Stack(ID_BREAD, 1);
        HandsFacts nearCar;
        nearCar.swapKeyFree = false;
        Hands({ ACT_SWAP_HANDS }, {}, nearCar);
        CHECK(gInv.Held().id == ID_DIRT, "next to a car the swap key is the host's");
        Hands({ ACT_SWAP_HANDS });
        CHECK(gInv.Held().id == ID_BREAD && gInv.offhand.id == ID_DIRT, "otherwise the hands swap");
        HandsFacts car;
        car.inVehicle = true;
        Hands({ ACT_DROP }, {}, car);
        CHECK(gDrops.empty() && gInv.Held().count == 1, "at the wheel the drop key is the host's");
        Hands({ ACT_DROP });
        CHECK(gDrops.size() == 1 && gInv.Held().Empty(), "on foot it drops the held item");
        gInv.Held() = Stack(ID_DIRT, 5);
        Hands({ ACT_DROP }, { ACT_DROP_STACK });
        CHECK(gDrops.size() == 2 && gInv.Held().Empty() && gDrops[1].stack.count == 5, "with the stack key all of it");
    }

    // fighting and mining
    {
        CombatStage(host);
        const int cow = SpawnMob(MOB_COW, Vec3(2.5f, 0.5f, 10.0f), true);
        HandsFacts guns;
        guns.vehicleGuns = true;
        Hands({ ACT_ATTACK }, { ACT_ATTACK }, guns);
        CHECK(gMobs[cow].health == 10.0f && gGame.swing < 0.0f, "when the button fires the vehicle's guns the hands do nothing");
        Hands({ ACT_ATTACK }, { ACT_ATTACK });
        CHECK(gMobs[cow].health == 9.0f, "otherwise they hit");

        CombatStage(host);
        gWorld.SetRaw(1, 0, 9, MakeVox(ID_DIRT));
        gGame.lookDir = down45;
        const int pig = SpawnMob(MOB_PIG, Vec3(1.5f, 0.5f, 10.0f), true);
        Hands({ ACT_ATTACK }, { ACT_ATTACK });
        CHECK(gMobs[pig].health == 9.0f, "a pig in front of the dirt is hit");
        MobsClear();
        for (int i = 0; i < 30; ++i)
            Hands({}, { ACT_ATTACK });
        CHECK(gWorld.GetBlock(1, 0, 9) == ID_DIRT, "holding the button after a hit does not dig");
        Hands({});
        HoldHands(ACT_ATTACK, 1.5f);
        CHECK(gWorld.GetBlock(1, 0, 9) == ID_AIR && CountDrops(ID_DIRT) == 1, "pressed again it digs the dirt out");
        gGame.attackTimer = 5.0f;
        gGame.lookDir = Vec3(0, 0, 1);
        Hands({ ACT_ATTACK }, { ACT_ATTACK });
        CHECK(gGame.attackTimer == 0.0f && gGame.swing >= 0.0f, "a swing at the air starts the cooldown again");
    }

    // using
    {
        CombatStage(host);
        gGame.lookDir = down45;
        gInv.Held() = Stack(ID_OAK_PLANKS, 4);
        Hands({ ACT_USE }, { ACT_USE });
        CHECK(gWorld.GetBlock(1, 0, 10) == ID_OAK_PLANKS && gInv.Held().count == 3, "the use button places the held block");

        CombatStage(host);
        gWorld.SetRaw(2, 0, 10, MakeVox(ID_CRAFTING_TABLE));
        gInv.Held() = Stack(ID_OAK_PLANKS, 4);
        HandsResult r = Hands({ ACT_USE }, { ACT_USE });
        CHECK(r.blockPad && gGame.screen == SCREEN_CRAFTING && gInv.Held().count == 4, "on a crafting table it opens the table");
        CloseScreen();
        HandsFacts sneak;
        sneak.sneaking = true;
        Hands({ ACT_USE }, { ACT_USE }, sneak);
        CHECK(gGame.screen == SCREEN_NONE && gWorld.GetBlock(1, 0, 10) == ID_OAK_PLANKS && gInv.Held().count == 3,
              "sneaking, the block goes against it");

        CombatStage(host);
        host.people.push_back({ Vec3(2.5f, 0.5f, 10.9f) });
        Hands({ ACT_USE }, { ACT_USE });
        CHECK(host.talks == 1, "somebody of the host's in front: the host is asked (villagers trade)");
        HandsFacts car;
        car.inVehicle = true;
        Hands({ ACT_USE }, { ACT_USE }, car);
        CHECK(host.talks == 1, "not from a vehicle");
        host.people[0].centre.x = 5.5f;
        Hands({ ACT_USE }, { ACT_USE });
        CHECK(host.talks == 1, "nor further than 4 m");

        CombatStage(host);
        const int sheep = SpawnMob(MOB_SHEEP, Vec3(2.5f, 0.5f, 10.0f), true);
        gInv.Held() = Stack(ID_SHEARS);
        Hands({ ACT_USE }, { ACT_USE }, car);
        CHECK(!gMobs[sheep].sheared, "an animal is not sheared from a vehicle");
        Hands({ ACT_USE }, { ACT_USE });
        CHECK(gMobs[sheep].sheared, "on foot it is");

        CombatStage(host);
        gInv.Held() = Stack(ID_SNOWBALL, 2);
        Hands({ ACT_USE }, { ACT_USE }, car);
        CHECK(gProjectiles.size() == 1 && gInv.Held().count == 1, "from a vehicle things are still thrown");
    }

    // food, the off hand, the bow
    {
        CombatStage(host);
        gSurvival.food = 10.0f;
        gInv.Held() = Stack(ID_BREAD, 2);
        HandsResult r = HoldHands(ACT_USE, 2.0f);
        CHECK(r.finished == ID_BREAD && gInv.Held().count == 1 && gSurvival.food > 10.0f, "holding the button eats");
        gSurvival.food = 10.0f;
        gInv.Held() = ItemStack();
        gInv.offhand = Stack(ID_BREAD, 1);
        bool offhand = false;
        for (float t = 0.0f; t < 2.0f; t += 0.05f) {
            r = t == 0.0f ? Hands({ ACT_USE }, { ACT_USE }) : Hands({}, { ACT_USE });
            offhand = offhand || gGame.usingOffhand;
        }
        CHECK(offhand && gInv.offhand.Empty() && gInv.Held().Empty() && gSurvival.food > 10.0f, "with nothing in the main hand the off hand eats");
        gSurvival.food = 20.0f;
        gInv.Held() = Stack(ID_MILK_BUCKET);
        r = HoldHands(ACT_USE, 2.0f);
        CHECK(r.finished == ID_MILK_BUCKET && gInv.Held().id == ID_BUCKET, "milk is drunk even when full (the host is told)");

        CombatStage(host);
        gInv.Held() = Stack(ID_BOW);
        gInv.slots[1] = Stack(ID_ARROW, 2);
        HoldHands(ACT_USE, 1.2f);
        CHECK(gProjectiles.empty() && gGame.bowDraw > 1.0f, "the bow is drawn while the button is held");
        Hands({});
        CHECK(gProjectiles.size() == 1, "and shoots when it is let go");
    }

    // game mode and camera keys
    {
        CombatStage(host);
        gControls = Controls();
        gControls.Set(ACT_GAME_MODE, true, true);
        gControls.Set(ACT_PERSPECTIVE, true, true);
        GameKeysTick();
        CHECK(gGame.gameMode == MODE_CREATIVE && gGame.cameraMode == CAM_THIRD_BACK && gGame.messageTimer > 0.0f, "the game mode and the camera change");
        GameKeysTick();
        CHECK(gGame.gameMode == MODE_SURVIVAL && gGame.cameraMode == CAM_THIRD_FRONT, "back, and the camera goes round");
        GameKeysTick();
        CHECK(gGame.cameraMode == CAM_FIRST, "in Minecraft's order");
        const int mode = gGame.gameMode;
        gGame.screen = SCREEN_INVENTORY;
        GameKeysTick();
        CHECK(gGame.gameMode == mode && gGame.cameraMode == CAM_FIRST, "not while a screen is open");
    }
    gControls = Controls();
    gGame = GameState();
    MobsClear();
    CombatClear();
    ResetSurvival();
}

// a click in the middle of the slot at (gx, gy) of the open screen
static void ClickAt(int gx, int gy, bool right = false, bool shift = false) { ScreenClick(gx + 8.0f, gy + 8.0f, !right, right, shift); }

static void TestScreens() {
    TestHost host;
    SetHost(&host);
    srand(4711);

    // the inventory
    {
        CombatStage(host);
        OpenScreen(SCREEN_INVENTORY);
        BuildSlots();
        CHECK(gSlots.size() == 46 && gWinW == 176 && gWinH == 166, "36 slots, 4 armour, the off hand, 2x2 crafting and its result (%d)",
              (int)gSlots.size());
        CHECK(SlotAt(16, 150) && SlotAt(16, 150)->st == &gInv.slots[0] && SlotAt(16, 92)->st == &gInv.slots[9] && !SlotAt(3, 3),
              "where the slots are");
        CHECK(OverWindow(1, 1) && !OverWindow(-1, 5) && !OverWindow(5, 170) && TabAt(5, -10) == -1, "the window; no tabs here");
        CHECK(std::string(ScreenTitle()) == "\xC3\x9Cretim", "its title");

        gInv.slots[0] = Stack(ID_DIRT, 10);
        ClickAt(8, 142);
        CHECK(gInv.cursor.id == ID_DIRT && gInv.cursor.count == 10 && gInv.slots[0].Empty() && gWorld.dirty, "a left click picks the stack up");
        ClickAt(8 + 18, 142, true);
        CHECK(gInv.slots[1].count == 1 && gInv.cursor.count == 9, "a right click puts one down");
        ClickAt(8 + 18, 142, true);
        CHECK(gInv.slots[1].count == 2 && gInv.cursor.count == 8, "and another one");
        ClickAt(8 + 18, 142);
        CHECK(gInv.slots[1].count == 10 && gInv.cursor.Empty(), "a left click puts all of it down");
        ClickAt(8 + 18, 142, true);
        CHECK(gInv.slots[1].count == 5 && gInv.cursor.count == 5, "a right click on a stack takes half");
        gInv.slots[2] = Stack(ID_STONE, 3);
        ClickAt(8 + 36, 142);
        CHECK(gInv.slots[2].id == ID_DIRT && gInv.cursor.id == ID_STONE, "something else: they swap");
        ClickAt(8 + 36, 142);
        ClickAt(8 + 18, 142);
        CHECK(gInv.slots[1].count == 10 && gInv.cursor.Empty() && gInv.slots[2].id == ID_STONE, "the same kind is put together");
        gInv.cursor.Clear();

        gInv.slots[1] = Stack(ID_DIRT, 10);
        ClickAt(8 + 18, 142, false, true);
        CHECK(gInv.slots[1].Empty() && gInv.slots[9].id == ID_DIRT && gInv.slots[9].count == 10, "shift moves from the hotbar up");
        ClickAt(8, 84, false, true);
        CHECK(gInv.slots[9].Empty() && gInv.CountOf(ID_DIRT) == 10 && gInv.slots[0].id == ID_DIRT, "and back down into the first free hotbar slot");
        gInv.slots[3] = Stack(ID_IRON_HELMET);
        ClickAt(8 + 54, 142, false, true);
        CHECK(gInv.armor[0].id == ID_IRON_HELMET && gInv.slots[3].Empty() && Heard(SND_EQUIP_GENERIC) == 1, "armour goes on with shift");
        gInv.cursor = Stack(ID_IRON_BOOTS);
        ClickAt(8, 8);
        CHECK(gInv.armor[0].id == ID_IRON_HELMET && gInv.cursor.id == ID_IRON_BOOTS, "boots do not fit the helmet slot");
        ClickAt(8, 8 + 54);
        CHECK(gInv.armor[3].id == ID_IRON_BOOTS && gInv.cursor.Empty(), "they fit the boots slot");

        // crafting in the 2x2 grid
        gInv.cursor = Stack(ID_OAK_PLANKS, 8);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c)
                ClickAt(98 + c * 18, 18 + r * 18, true);
        BuildSlots();
        CHECK(gInv.craft[0].count == 1 && gInv.craft[3].count == 1 && gInv.cursor.count == 4, "four planks in the grid");
        ClickAt(154, 28);
        CHECK(gInv.cursor.id == ID_OAK_PLANKS && gInv.cursor.count == 4, "the result cannot go onto planks");
        gInv.cursor.Clear();
        ClickAt(154, 28);
        CHECK(gInv.cursor.id == ID_CRAFTING_TABLE && gInv.craft[0].Empty(), "a crafting table, the planks are used");
        gInv.cursor.Clear();
        for (int i = 0; i < 4; ++i)
            gInv.craft[i] = Stack(ID_OAK_PLANKS, 3);
        ClickAt(154, 28, false, true);
        CHECK(gInv.CountOf(ID_CRAFTING_TABLE) == 3 && gInv.craft[0].Empty() && gInv.slots[8].id == ID_CRAFTING_TABLE && gInv.slots[8].count == 3,
              "shift crafts as many as it can, into the hotbar from its right end");

        // throwing out
        gInv.cursor = Stack(ID_STONE, 5);
        ScreenClick(-20.0f, 50.0f, false, true, false);
        CHECK(gInv.cursor.count == 4 && CountDrops(ID_STONE) == 1, "a right click outside the window throws one out");
        ScreenClick(-20.0f, 50.0f, true, false, false);
        CHECK(gInv.cursor.Empty() && CountDrops(ID_STONE) == 5, "a left click all of it");
        ScreenClick(40.0f, 3.0f, true, false, false);
        CHECK(gInv.cursor.Empty(), "inside the window, between the slots, nothing happens");
        CloseScreen();
    }

    // a chest and a furnace
    {
        CombatStage(host);
        gWorld.SetRaw(2, 0, 10, MakeVox(ID_CHEST));
        OpenScreen(SCREEN_CHEST, Int3{ 2, 0, 10 });
        BuildSlots();
        CHECK(gSlots.size() == 63 && gWinH == 167, "a chest has 27 slots more");
        gInv.slots[0] = Stack(ID_DIRT, 64);
        gInv.slots[1] = Stack(ID_IRON_HELMET);
        ClickAt(8, 143, false, true);
        ClickAt(26, 143, false, true);
        CHECK(gWorld.chests[(Int3{ 2, 0, 10 })].slots[0].count == 64 && gWorld.chests[(Int3{ 2, 0, 10 })].slots[1].id == ID_IRON_HELMET &&
                  gInv.armor[0].Empty(),
              "shift puts things into the chest (armour too)");
        ClickAt(8, 18, false, true);
        CHECK(gInv.slots[8].count == 64 && gInv.slots[9].Empty(), "and takes them out into the hotbar, from its right end");
        CloseScreen();

        gWorld.SetRaw(3, 0, 10, MakeVox(ID_FURNACE));
        OpenScreen(SCREEN_FURNACE, Int3{ 3, 0, 10 });
        BuildSlots();
        gInv = PlayerInventory();
        gInv.slots[0] = Stack(ID_RAW_IRON, 3);
        gInv.slots[1] = Stack(ID_COAL, 2);
        gInv.slots[2] = Stack(ID_DIRT, 1);
        ClickAt(8, 142, false, true);
        ClickAt(26, 142, false, true);
        ClickAt(44, 142, false, true);
        FurnaceState* f = OpenFurnace();
        CHECK(f->input.id == ID_RAW_IRON && f->fuel.id == ID_COAL && gInv.slots[2].Empty() && gInv.slots[9].id == ID_DIRT,
              "shift sends ore to the input, coal to the fuel, the rest up");
        gInv.cursor = Stack(ID_DIRT, 1);
        ClickAt(56, 53);
        CHECK(f->fuel.id == ID_COAL && gInv.cursor.id == ID_DIRT, "dirt does not burn");
        gInv.cursor.Clear();
        f->output = Stack(ID_IRON_INGOT, 2);
        ClickAt(116, 35);
        CHECK(gInv.cursor.id == ID_IRON_INGOT && gInv.cursor.count == 2 && f->output.Empty(), "the output is taken");
        f->output = Stack(ID_IRON_INGOT, 1);
        ClickAt(116, 35, true);
        CHECK(gInv.cursor.count == 3, "onto what the cursor holds");
        gInv.cursor.Clear();
        f->output = Stack(ID_IRON_INGOT, 5);
        ClickAt(116, 35, false, true);
        CHECK(f->output.Empty() && gInv.slots[8].id == ID_IRON_INGOT && gInv.slots[8].count == 5, "shift sends the output to the hotbar's right end");
        f->input = Stack(ID_RAW_IRON, 2);
        ClickAt(56, 17, false, true);
        CHECK(f->input.Empty() && gInv.CountOf(ID_RAW_IRON) == 2 && gInv.slots[10].id == ID_RAW_IRON, "and the input back to the inventory's top");
        gInv.cursor = Stack(ID_DIRT, 1);
        ClickAt(116, 35);
        CHECK(f->output.Empty() && gInv.cursor.id == ID_DIRT, "nothing goes into the output");
        gInv.cursor.Clear();
        CloseScreen();
    }

    // creative
    {
        CombatStage(host);
        gGame.gameMode = MODE_CREATIVE;
        OpenScreen(SCREEN_CREATIVE);
        gGame.creativeTab = 0;
        BuildSlots();
        CHECK(gWinW == 195 && gSlots.size() == 54 && SlotAt(17, 26)->kind == SK_PALETTE && !SlotAt(17, 26)->st->Empty(),
              "the item tabs: a palette of 45 and the hotbar");
        const uint16_t first = SlotAt(17, 26)->st->id;
        ScreenClick(17, 26, true, false, false);
        CHECK(gInv.cursor.id == first && gInv.cursor.count == MaxStack(first), "a left click takes a full stack");
        ScreenClick(17, 26, true, false, false);
        CHECK(gInv.cursor.Empty(), "clicking the palette again puts it away");
        ScreenClick(17, 26, false, true, false);
        CHECK(gInv.cursor.count == 1, "a right click takes one");
        gInv.cursor.Clear();
        ScreenClick(17, 26, true, false, true);
        CHECK(gInv.CountOf(first) == MaxStack(first), "shift puts a stack into the inventory");
        ScreenScroll(3);
        BuildSlots();
        CHECK(gGame.creativeScroll == std::min(3, std::max(0, PaletteRows() - 5)), "the wheel scrolls the palette (%d rows)", PaletteRows());
        ScreenScroll(1000);
        BuildSlots();
        CHECK(gGame.creativeScroll == std::max(0, PaletteRows() - 5), "not past its end");
        float x, y, w, h;
        TabBox(2, x, y, w, h);
        CHECK(TabAt(x + 3, y + 3) == 2 && x == 54.0f && y == -28.0f, "the tabs above the window");
        TabBox(CAT_COUNT, x, y, w, h);
        CHECK(x == 195.0f - 26.0f && y == 136.0f - 4.0f, "the inventory tab at the bottom right");
        ScreenClick(x + 3, y + 3, true, false, false);
        CHECK(gGame.creativeTab == CAT_COUNT && gGame.creativeScroll == 0 && Heard(SND_CLICK) == 1 && CreativeInventoryTab(), "a click picks a tab");
        BuildSlots();
        CHECK(gSlots.size() == 27 + 4 + 1 + 1 + 9 && std::string(ScreenTitle()) == kTabNames[CAT_COUNT], "there the whole inventory, and the trash");
        gGame.creativeScroll = 0;
        ScreenScroll(2);
        CHECK(gGame.creativeScroll == 0, "the wheel does nothing there");
        gInv.cursor = Stack(ID_DIRT, 5);
        ClickAt(173, 112);
        CHECK(gInv.cursor.Empty() && gInv.CountOf(first) == MaxStack(first), "the trash takes what the cursor holds");
        ClickAt(173, 112, false, true);
        CHECK(gInv.CountOf(first) == 0, "shift on the trash empties the inventory");
        CloseScreen();
    }
    gGame = GameState();
    ResetSurvival();
}

static bool SameRect(const GuiRect& a, const GuiRect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

static int CountSprites(const std::vector<HudPiece>& v, const GuiRect& r) {
    int n = 0;
    for (const HudPiece& p : v)
        n += p.kind == HP_SPRITE && SameRect(p.src, r);
    return n;
}

static const HudPiece* FindSprite(const std::vector<HudPiece>& v, const GuiRect& r) {
    for (const HudPiece& p : v)
        if (p.kind == HP_SPRITE && SameRect(p.src, r))
            return &p;
    return nullptr;
}

static const HudPiece* FindText(const std::vector<HudPiece>& v, const std::string& part) {
    for (const HudPiece& p : v)
        if (p.kind == HP_TEXT && p.text.find(part) != std::string::npos)
            return &p;
    return nullptr;
}

static int CountKind(const std::vector<HudPiece>& v, int kind) {
    int n = 0;
    for (const HudPiece& p : v)
        n += p.kind == kind;
    return n;
}

static void TestHud() {
    TestHost host;
    SetHost(&host);
    srand(5150);
    std::vector<HudPiece> v;
    HudFacts f;
    f.camera = Vec3(0.5f, 0.5f, 30.0f);

    // hidden
    {
        CombatStage(host);
        f.shown = false;
        BuildHud(f, v);
        CHECK(v.empty(), "with the HUD off nothing is drawn");
        ShowMessage("hello");
        BuildHud(f, v);
        CHECK(v.size() == 1 && v[0].kind == HP_TEXT && v[0].text == "hello" && v[0].anchor == AT_TOP && v[0].y == 40.0f && v[0].align == 1,
              "but messages are");
        f.shown = true;
    }

    // the hotbar and the stats
    {
        CombatStage(host);
        gInv.selected = 2;
        gInv.slots[0] = Stack(ID_DIRT, 3);
        f.health = 0.55f;
        v.clear();
        BuildHud(f, v);
        const HudPiece* bar = FindSprite(v, GUI_HOTBAR);
        CHECK(bar && bar->anchor == AT_BOTTOM && bar->x == -91.0f && bar->y == -22.0f, "the hotbar at the bottom");
        const HudPiece* sel = FindSprite(v, GUI_SELECTION);
        CHECK(sel && sel->x == -91.0f + 40.0f - 1.0f && sel->y == -23.0f, "the selection on the third slot");
        CHECK(CountKind(v, HP_STACK) == 9 && v[0].kind != HP_STACK, "nine slots");
        CHECK(CountSprites(v, GUI_HEART_CONTAINER) == 10 && CountSprites(v, GUI_HEART_FULL) == 5 && CountSprites(v, GUI_HEART_HALF) == 1,
              "55%% health: five and a half hearts");
        CHECK(FindSprite(v, GUI_HEART_FULL)->y == -39.0f && CountSprites(v, GUI_ARMOR_EMPTY) == 0, "steady, no armour row");
        CHECK(CountSprites(v, GUI_FOOD_FULL) == 10 && FindSprite(v, GUI_FOOD_FULL)->x == -91.0f + 173.0f, "full food, from the right");
        CHECK(CountSprites(v, GUI_XP_BG) == 1 && CountSprites(v, GUI_AIR) == 0 && !FindText(v, "0"), "the experience bar, empty; no air");
        CHECK(FindSprite(v, GUI_CROSSHAIR) && FindSprite(v, GUI_CROSSHAIR)->invert && FindSprite(v, GUI_CROSSHAIR)->anchor == AT_CENTRE, "the crosshair");

        gSurvival.xpProgress = 0.5f;
        gSurvival.xpLevel = 7;
        gSurvival.food = 7.0f;
        gSurvival.air = kMaxAir * 0.5f;
        gInv.armor[0] = Stack(ID_IRON_HELMET);
        gInv.offhand = Stack(ID_BREAD);
        AddEffect(EFFECT_POISON, 30.0f, 0);
        AddEffect(EFFECT_ABSORPTION, 30.0f, 1);
        gGame.selectedNameTimer = 0.5f;
        gInv.selected = 0;
        v.clear();
        BuildHud(f, v);
        GuiRect xp = GUI_XP_PROGRESS;
        xp.w = 91;
        CHECK(CountSprites(v, xp) == 1, "half the experience bar");
        const HudPiece* lvl = FindText(v, "7");
        CHECK(lvl && lvl->outline && !lvl->shadow && lvl->y == -35.0f, "the level in green with a black outline");
        CHECK(CountSprites(v, GUI_HEART_POISONED_FULL) == 5 && CountSprites(v, GUI_HEART_FULL) == 0, "poison turns the hearts green");
        CHECK(CountSprites(v, GUI_HEART_ABSORBING_FULL) == 4 && FindSprite(v, GUI_HEART_ABSORBING_FULL)->y == -49.0f,
              "absorption: four yellow hearts above (%.1f points)", gSurvival.absorption);
        CHECK(CountSprites(v, GUI_ARMOR_FULL) == 1 && CountSprites(v, GUI_ARMOR_EMPTY) == 9 && FindSprite(v, GUI_ARMOR_FULL)->y == -59.0f,
              "an iron helmet: one armour point, the row moved up");
        CHECK(CountSprites(v, GUI_FOOD_FULL) == 3 && CountSprites(v, GUI_FOOD_HALF) == 1 && CountSprites(v, GUI_FOOD_EMPTY) == 10, "7 food");
        CHECK(CountSprites(v, GUI_AIR) + CountSprites(v, GUI_AIR_BURSTING) == 5 && FindSprite(v, GUI_AIR)->y == -49.0f, "half the air: five bubbles");
        CHECK(FindSprite(v, GUI_OFFHAND) && FindSprite(v, GUI_OFFHAND)->x == -120.0f && CountKind(v, HP_STACK) == 10, "the off hand slot");
        const HudPiece* name = FindText(v, ItemName(ID_DIRT));
        CHECK(name && name->y == -59.0f && name->align == 1 && (name->color >> 24) == 127, "the held item's name fades");
        const HudPiece* ebg = FindSprite(v, GUI_EFFECT_BG);
        CHECK(CountSprites(v, GUI_EFFECT_BG) == 2 && ebg->anchor == AT_TOP_RIGHT && ebg->x == -25.0f && ebg->y == 1.0f, "two effects, top right");
        const HudPiece* poison = FindText(v, kEffectNames[EFFECT_POISON]);
        CHECK(poison && poison->align == 2 && poison->y == 27.0f + 3.0f && FindText(v, "0:30") && FindText(v, "0:30")->y == 1.0f + 13.0f,
              "their names and time left");

        ClearEffects();
        gInv.armor[0] = ItemStack();
        f.hostAbsorb = 10.0f;
        f.health = 0.15f;
        gSurvival.food = 4.0f;
        gSurvival.saturation = 0.0f;
        AddEffect(EFFECT_HUNGER, 5.0f, 0);
        bool heartsShake = false, foodShakes = false;
        for (int i = 0; i < 10; ++i) {
            v.clear();
            BuildHud(f, v);
            for (const HudPiece& p : v) {
                if (p.kind == HP_SPRITE && SameRect(p.src, GUI_HEART_CONTAINER) && p.y != -39.0f)
                    heartsShake = true;
                if (p.kind == HP_SPRITE && SameRect(p.src, GUI_FOOD_EMPTY_HUNGER) && p.y != -39.0f)
                    foodShakes = true;
            }
        }
        CHECK(heartsShake && foodShakes, "low health and an empty stomach shake");
        CHECK(CountSprites(v, GUI_ARMOR_FULL) == 0 && CountSprites(v, GUI_HEART_ABSORBING_FULL) == 5 && CountSprites(v, GUI_FOOD_HALF_HUNGER) == 0 &&
                  CountSprites(v, GUI_FOOD_FULL_HUNGER) == 2,
              "the host's body armour shows as yellow hearts, not armour; hunger turns the food green");
        ClearEffects();

        gGame.gameMode = MODE_CREATIVE;
        v.clear();
        BuildHud(f, v);
        CHECK(CountSprites(v, GUI_HEART_CONTAINER) == 0 && CountSprites(v, GUI_XP_BG) == 0 && FindText(v, ItemName(ID_DIRT))->y == -45.0f,
              "creative: no hearts, no experience");
        gGame.cameraMode = CAM_THIRD_FRONT;
        v.clear();
        BuildHud(f, v);
        CHECK(!FindSprite(v, GUI_CROSSHAIR), "no crosshair looking at the player's face");
        gGame.cameraMode = CAM_FIRST;
        gSurvival.burn = 5.0f;
        v.clear();
        BuildHud(f, v);
        CHECK(CountKind(v, HP_OVERLAY) == 2 && v[0].kind == HP_OVERLAY && v[0].u0 > v[0].u1 && v[0].y > 0.1f && v[0].y + v[0].h > 1.0f,
              "burning in first person: two flames rise from the bottom of the screen");
        gGame.cameraMode = CAM_THIRD_BACK;
        v.clear();
        BuildHud(f, v);
        CHECK(CountKind(v, HP_OVERLAY) == 0, "not seen from behind");
        gGame.cameraMode = CAM_FIRST;
        gSurvival.burn = 0.0f;
        gWorld.SetRaw(0, 0, 30, MakeVox(ID_WATER));
        v.clear();
        BuildHud(f, v);
        CHECK(v[0].kind == HP_FILL && v[0].w == 0.0f && (v[0].color & 0xFFFFFF) == 0x2A50C8, "inside water the screen is blue");
        gWorld.SetRaw(0, 0, 30, MakeVox(ID_AIR));
    }

    // an open screen
    {
        CombatStage(host);
        f = HudFacts();
        f.camera = Vec3(0.5f, 0.5f, 30.0f);
        gInv.slots[0] = Stack(ID_DIRT, 3);
        OpenScreen(SCREEN_INVENTORY);
        BuildSlots();
        f.mouseX = 8 + 4;
        f.mouseY = 142 + 4;
        v.clear();
        BuildHud(f, v);
        CHECK(!FindSprite(v, GUI_CROSSHAIR) && FindSprite(v, GUI_WIN_INVENTORY) && FindSprite(v, GUI_WIN_INVENTORY)->anchor == AT_WINDOW,
              "the inventory window, no crosshair");
        CHECK(CountKind(v, HP_PLAYER) == 1 && FindText(v, ScreenTitle()) && FindText(v, ScreenTitle())->x == 97.0f, "the player's doll and the title");
        CHECK(CountSprites(v, GUI_SLOT_HELMET) == 1 && CountSprites(v, GUI_SLOT_SHIELD) == 1, "empty armour and off hand slots show their shapes");
        bool hover = false;
        for (const HudPiece& p : v)
            hover = hover || (p.kind == HP_FILL && p.anchor == AT_WINDOW && p.x == 8.0f && p.y == 142.0f && p.w == 16.0f);
        CHECK(hover && CountKind(v, HP_TOOLTIP) == 1 && v[v.size() - 1].text == ItemName(ID_DIRT), "the slot under the mouse lights up, with a tooltip");
        gInv.cursor = Stack(ID_STONE, 2);
        v.clear();
        BuildHud(f, v);
        CHECK(CountKind(v, HP_TOOLTIP) == 0 && v.back().kind == HP_STACK && v.back().anchor == AT_CURSOR && v.back().x == -8.0f,
              "what the cursor holds hangs on the mouse");
        gInv.cursor.Clear();
        CloseScreen();

        gWorld.SetRaw(2, 0, 10, MakeVox(ID_BLAST_FURNACE));
        OpenScreen(SCREEN_FURNACE, Int3{ 2, 0, 10 });
        FurnaceState* fs = OpenFurnace();
        fs->burnTime = 50;
        fs->burnTimeTotal = 100;
        fs->input = Stack(ID_RAW_IRON);
        fs->cookTime = 50;
        BuildSlots();
        v.clear();
        BuildHud(f, v);
        CHECK(FindSprite(v, GUI_WIN_BLAST) && FindText(v, "Envanter") && FindText(v, "Envanter")->y == 72.0f, "a blast furnace has its own window");
        const HudPiece* lit = nullptr;
        const HudPiece* arrow = nullptr;
        for (const HudPiece& p : v) {
            if (p.kind == HP_SPRITE && p.src.x == GUI_LIT.x)
                lit = &p;
            if (p.kind == HP_SPRITE && p.src.x == GUI_BURN.x && p.src.y == GUI_BURN.y)
                arrow = &p;
        }
        CHECK(lit && lit->src.h == 7 && lit->y == 43.0f && arrow && arrow->src.w == 12, "half the fire, half the arrow (blast furnace: 100 ticks)");
        CloseScreen();

        gGame.gameMode = MODE_CREATIVE;
        OpenScreen(SCREEN_CREATIVE);
        gGame.creativeTab = 0;
        BuildSlots();
        f.mouseX = 5;
        f.mouseY = -20;
        v.clear();
        BuildHud(f, v);
        CHECK(CountKind(v, HP_ICON) == CAT_COUNT + 1 && FindSprite(v, GUI_TAB_TOP_SELECTED_1) && CountSprites(v, GUI_TAB_TOP_UNSELECTED_1) == 0,
              "creative: every tab with its icon, the first one picked");
        CHECK(CountSprites(v, GUI_SCROLLER) == 1 && FindSprite(v, GUI_SCROLLER)->y == 18.0f, "the scroller at the top");
        CHECK(CountKind(v, HP_TOOLTIP) == 1 && v.back().text == kTabNames[0], "the tab under the mouse says its name");
        CloseScreen();
    }

    // you died
    {
        CombatStage(host);
        gGame.deathTime = 0.5f;
        gGame.deathCause = STR_DEATH_DROWN;
        gSurvival.xpTotal = 42;
        f = HudFacts();
        f.camera = Vec3(0.5f, 0.5f, 30.0f);
        f.playerName = "CJ";
        v.clear();
        BuildHud(f, v);
        const HudPiece* veil = nullptr;
        for (const HudPiece& p : v)
            if (p.kind == HP_GRADIENT)
                veil = &p;
        CHECK(veil && veil->scale == 0.5f && veil->color == 0x60500000u && veil->color2 == 0xA0803030u, "a red veil fades in");
        const HudPiece* title = FindText(v, kStr[STR_YOU_DIED]);
        CHECK(title && title->scale == 2.0f && title->y == 60.0f, "\"You died!\" twice as big");
        CHECK(FindText(v, "CJ") && FindText(v, "CJ")->y == 85.0f, "the cause, with the player's name");
        const HudPiece* score = FindText(v, std::string(kStr[STR_SCORE]).substr(0, 3));
        CHECK(score && score->text2 == "42" && score->text.find("%s") == std::string::npos, "the score");
        CHECK(!FindText(v, kStr[STR_RESPAWN]), "no button yet");
        gGame.deathTime = 1.5f;
        v.clear();
        BuildHud(f, v);
        const HudPiece* button = FindSprite(v, MENU_BUTTON_HI);
        CHECK(button && button->texture == HT_MENU && button->anchor == AT_QUARTER && FindText(v, kStr[STR_RESPAWN]), "after a second the button");
    }
    gGame = GameState();
    ResetSurvival();
}

// the mesh of the chunk around the origin, in a world with just these blocks
static BlockMesh MeshOf(std::initializer_list<std::pair<Int3, Voxel>> blocks) {
    gWorld.Clear();
    for (const auto& b : blocks)
        gWorld.SetRaw(b.first.x, b.first.y, b.first.z, b.second);
    BlockMesh m;
    MeshChunk(*gWorld.FindChunk({ 0, 0, 0 }), m);
    return m;
}

static void TestBlockMesh() {
    TestHost host;
    SetHost(&host);

    {
        BlockMesh m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_STONE) } });
        CHECK(m.verts.size() == 24 && m.shade.size() == 24 && m.emissive.size() == 24 && m.tverts.empty() && m.nverts.empty(),
              "a lone stone block: six faces");
        int top = 0, bottom = 0, side = 0;
        for (size_t i = 0; i < m.verts.size(); ++i) {
            if (m.verts[i].n == (Int3{ 0, 0, 1 }))
                top += m.shade[i] == 255;
            if (m.verts[i].n == (Int3{ 0, 0, -1 }))
                bottom += m.shade[i] == 127;
            if (m.verts[i].n == (Int3{ 1, 0, 0 }))
                side += m.shade[i] == 153;
        }
        CHECK(top == 4 && bottom == 4 && side == 4, "Minecraft's light per face: top 1, east 0.6, bottom 0.5");
        const TileUV uv = AtlasTileUV(BlockFaceTile(ID_STONE, FACE_TOP, 0));
        bool inTile = true;
        for (const MeshVertex& v : m.verts)
            inTile = inTile && v.u >= uv.u0 - 1e-6f && v.u <= uv.u1 + 1e-6f && v.v >= uv.v0 - 1e-6f && v.v <= uv.v1 + 1e-6f;
        CHECK(inTile && uv.u1 > uv.u0 && uv.u1 - uv.u0 < 16.0f / ATLAS_SIZE, "its texture: one tile of the atlas");

        m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_STONE) }, { { 4, 3, 3 }, MakeVox(ID_STONE) } });
        CHECK(m.verts.size() == 40, "two stones side by side: the faces between them are hidden");
        m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_STONE) }, { { 4, 3, 4 }, MakeVox(ID_STONE) } });
        int dark = 0;
        for (size_t i = 0; i < m.verts.size(); ++i)
            if (m.verts[i].n == (Int3{ 0, 0, 1 }) && m.verts[i].z == 4.0f && m.verts[i].x == 4.0f)
                dark += m.shade[i] < 255;
        CHECK(dark == 2, "a block above the edge darkens the top's two corners there (ambient occlusion)");
        m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_GLASS) }, { { 4, 3, 3 }, MakeVox(ID_GLASS) } });
        CHECK(m.verts.size() == 40, "glass next to glass shows no face between");
        m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_GLASS) }, { { 4, 3, 3 }, MakeVox(ID_STONE) } });
        CHECK(m.verts.size() == 44, "glass does not hide the stone behind it");
    }

    {
        BlockMesh m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_POPPY) } });
        CHECK(m.verts.size() == 8 && m.shade[0] == 230, "a flower: two crossed sheets");
        m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_WATER) } });
        CHECK(m.verts.empty() && m.tverts.size() == 24 && m.tanim.size() == 24, "water is translucent");
        bool still = false, flow = false;
        for (uint8_t a : m.tanim) {
            still = still || a == ANIM_WATER_STILL;
            flow = flow || a == ANIM_WATER_FLOW;
        }
        float topZ = 0.0f;
        for (const MeshVertex& v : m.tverts)
            if (v.n == (Int3{ 0, 0, 1 }))
                topZ = v.z;
        CHECK(still && flow && topZ > 3.5f && topZ < 4.0f, "still on top (a little lower than a block, %.2f), flowing on the sides", topZ);
        m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_WATER) }, { { 3, 3, 4 }, MakeVox(ID_WATER) } });
        int tops = 0;
        for (const MeshVertex& v : m.tverts)
            tops += v.n == (Int3{ 0, 0, 1 });
        CHECK(tops == 4, "water under water has no surface");
        m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_LAVA) } });
        CHECK(m.verts.size() == 24 && m.emissive[0] == 1 && m.anim[0] == ANIM_LAVA_STILL, "lava glows");
        m = MeshOf({ { { 3, 3, 2 }, MakeVox(ID_STONE) }, { { 3, 3, 3 }, MakeVox(ID_FIRE) } });
        int fire = 0;
        for (uint8_t a : m.anim)
            fire += a == ANIM_FIRE_0 || a == ANIM_FIRE_1;
        CHECK(fire == 16, "fire on the floor: four leaning flames");
    }

    {
        host.ownGround = false;
        BlockMesh m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_DIRT, META_NATURAL) } });
        CHECK(m.verts.size() == 24 && m.nverts.empty(), "ground of ours is drawn as usual");
        host.ownGround = true;
        m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_DIRT, META_NATURAL) } });
        CHECK(m.verts.empty() && m.nverts.size() == 24 && m.nshade[0] < 128, "the host's dug ground goes apart, darker deep down");
        m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_DIRT) } });
        CHECK(m.verts.size() == 24, "a placed block is never the host's");
        host.ownGround = false;
    }
    gWorld.Clear();
}

// counts the models' quads and where they go
struct RecordSink : ModelSink {
    int tex = -1, quads[2] = { 0, 0 };
    Vec3 lo, hi;
    uint32_t color = 0;
    void Reset() {
        tex = -1;
        quads[0] = quads[1] = 0;
        lo = Vec3(1e9f, 1e9f, 1e9f);
        hi = Vec3(-1e9f, -1e9f, -1e9f);
    }
    void Texture(int t) override { tex = t; }
    void Quad(const Vec3* p, const float*, const float*, uint32_t c) override {
        ++quads[tex == MT_ATLAS ? 1 : 0];
        color = c;
        for (int k = 0; k < 4; ++k) {
            lo = Vec3(std::min(lo.x, p[k].x), std::min(lo.y, p[k].y), std::min(lo.z, p[k].z));
            hi = Vec3(std::max(hi.x, p[k].x), std::max(hi.y, p[k].y), std::max(hi.z, p[k].z));
        }
    }
    // every tile is an 8x8 square in the middle
    bool Opaque(int, int px, int py) override { return px >= 4 && px < 12 && py >= 4 && py < 12; }
};

static void TestModels() {
    RecordSink sink;
    SetModelSink(&sink);
    const Pose stand = EntityPose(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 0, 1), Vec3(0, 1, 0), 1.0f);

    sink.Reset();
    DrawMob(MOB_COW, stand, MobAnim(), 1.0f, 1.0f, 1.0f, 1.0f);
    const float cowH = sink.hi.z;
    CHECK(sink.quads[0] > 30 && sink.quads[1] == 0 && sink.lo.z > -0.01f && cowH > 1.2f && cowH < 1.6f,
          "a cow: %d quads of entity.png, %.2f m high", sink.quads[0], cowH);
    CHECK(sink.hi.y - sink.lo.y > 1.0f && sink.hi.x - sink.lo.x < 1.0f, "longer than it is wide");
    sink.Reset();
    DrawMob(MOB_CHICKEN, stand, MobAnim(), 1.0f, 1.0f, 1.0f, 1.0f);
    CHECK(sink.quads[0] > 20 && sink.hi.z < cowH * 0.75f, "a chicken is small (%.2f m)", sink.hi.z);
    sink.Reset();
    DrawMob(MOB_COW, stand, MobAnim(), 1.0f, 1.0f, 0.45f, 0.45f);
    CHECK(((sink.color >> 8) & 255) * 2 < ((sink.color >> 16) & 255) + 2 && (sink.color & 255) * 2 < ((sink.color >> 16) & 255) + 2,
          "a hurt animal flashes red");

    sink.Reset();
    DrawMob(MOB_CREEPER, stand, MobAnim(), 1.0f, 1.0f, 1.0f, 1.0f);
    const float creeperW = sink.hi.x - sink.lo.x;
    CHECK(sink.quads[0] == 36 && sink.hi.z > 1.6f && sink.hi.z < 1.75f && sink.lo.z > -0.01f, "a creeper: six cubes, 26 px high (%.2f)", sink.hi.z);
    MobAnim swelling;
    swelling.swell = 0.95f;
    sink.Reset();
    DrawMob(MOB_CREEPER, stand, swelling, 1.0f, 1.0f, 1.0f, 1.0f);
    CHECK(sink.hi.x - sink.lo.x > creeperW * 1.25f && sink.lo.z > -0.01f, "about to blow up it is a third wider, still on its feet");

    sink.Reset();
    HumanoidAnim a;
    PlayerAnimInput in;
    ComputePlayerAnim(a, in);
    DrawPlayerModel(stand, a, ModelStyle());
    CHECK(sink.quads[0] == 72 && sink.hi.z > 1.95f && sink.hi.z < 2.1f, "Steve: 12 cubes with their outer layer, 32 px = 2 blocks (%.2f)", sink.hi.z);
    CHECK(a.rArm.rx == 0.0f && a.lLeg.rx == 0.0f, "standing still");
    in.limbSwing = 1.0f;
    in.limbAmount = 1.0f;
    ComputePlayerAnim(a, in);
    CHECK(a.rArm.rx != 0.0f && Near(a.rArm.rx, -a.lArm.rx, 1e-3f) && Near(a.rLeg.rx, -a.lLeg.rx, 1e-3f) && a.rArm.rx * a.rLeg.rx < 0.0f,
          "walking swings arms and legs against each other");
    in = PlayerAnimInput();
    in.crouch = true;
    ComputePlayerAnim(a, in);
    CHECK(a.body.rx > 0.4f, "sneaking bends the body forward");
    in = PlayerAnimInput();
    in.attack = 0.5f;
    HumanoidAnim still;
    ComputePlayerAnim(still, PlayerAnimInput());
    ComputePlayerAnim(a, in);
    CHECK(a.rArm.rx < still.rArm.rx - 0.5f, "a swing raises the right arm");

    sink.Reset();
    Pose item;
    DrawItemModel(item, ID_STONE, 1.0f);
    CHECK(sink.quads[1] == 6 && sink.quads[0] == 0, "a block in the hand: a cube from the atlas");
    sink.Reset();
    DrawItemModel(item, ID_STICK, 1.0f);
    CHECK(sink.quads[1] == 2 + 32, "a flat item: front, back and one edge per border pixel (%d)", sink.quads[1]);
    SetModelSink(nullptr);
}

static void RunAnim(float seconds, const PlayerMotion& m) {
    for (float t = 0.0f; t < seconds; t += 0.05f)
        PlayerAnimTick(0.05f, &m);
}

static void TestPlayerAnim() {
    TestHost host;
    SetHost(&host);
    srand(31337);

    // the body's clocks
    {
        CombatStage(host);
        PlayerAnimTick(0.5f, nullptr);
        CHECK(Near(gGame.age, 0.5f), "without a player only the clock runs");
        PlayerMotion m;
        m.velocity = Vec3(4.3f, 0, 0);
        RunAnim(1.0f, m);
        CHECK(gGame.walkAmount > 0.6f && gGame.walkPhase > 5.0f && gGame.bob > 0.05f && gGame.walkDist > 2.0f, "walking swings the legs and bobs the view");
        m.standing = false;
        RunAnim(1.0f, m);
        CHECK(gGame.bob < 0.01f, "in the air the view stops bobbing");
        m.onFoot = false;
        RunAnim(1.0f, m);
        CHECK(gGame.walkAmount < 0.01f, "in a vehicle the legs rest");
        m = PlayerMotion();
        m.swimming = true;
        m.velocity = Vec3(2.0f, 0, 0);
        RunAnim(0.5f, m);
        CHECK(SwimPose() > 0.99f, "swimming lies down");
        m.velocity = Vec3();
        RunAnim(0.5f, m);
        CHECK(SwimPose() < 0.01f, "floating still stands up again");
        StartSwing();
        RunAnim(0.1f, PlayerMotion());
        CHECK(gGame.swing > 0.2f && gGame.swing < 0.5f, "a swing takes 0.3 s");
        RunAnim(0.3f, PlayerMotion());
        CHECK(gGame.swing < 0.0f, "and ends");
        gGame.sprinting = true;
        RunAnim(1.0f, PlayerMotion());
        CHECK(Near(gGame.fovMod, 1.15f, 0.01f), "sprinting widens the view");
        gGame.sprinting = false;

        gInv.Held() = Stack(ID_DIRT);
        gGame.handItem = ID_DIRT;
        gGame.handHeight = 1.0f;
        gGame.attackTimer = 10.0f;
        gInv.Held() = Stack(ID_STONE);
        RunAnim(0.1f, PlayerMotion());
        CHECK(gGame.handHeight < 0.5f && gGame.handItem == ID_DIRT, "a new item: the hand goes down first");
        RunAnim(0.5f, PlayerMotion());
        CHECK(gGame.handItem == ID_STONE && gGame.handHeight > 0.9f, "and comes up with the new one");
        m = PlayerMotion();
        m.alive = false;
        RunAnim(0.5f, m);
        CHECK(gGame.handItem == 0, "the dead hold nothing");
    }

    // what the items look like while used
    {
        CombatStage(host);
        CHECK(HeldItemTile(ID_BOW) == -1 && HeldItemTile(ID_STONE) == -1, "nothing special");
        gGame.bowDraw = 0.3f;
        CHECK(HeldItemTile(ID_BOW) == TILE_BOW_PULLING_0, "a bow being drawn");
        gGame.bowDraw = 1.0f;
        CHECK(HeldItemTile(ID_BOW) == TILE_BOW_PULLING_2, "fully drawn");
        gInv.Held() = Stack(ID_CROSSBOW);
        gGame.crossbowSlot = gInv.selected;
        gGame.crossbowRocket = true;
        CHECK(HeldItemTile(ID_CROSSBOW) == TILE_CROSSBOW_FIREWORK, "a crossbow loaded with a firework");
        gInv.Held() = Stack(ID_FISHING_ROD);
        UseHeldItem();
        CHECK(HeldItemTile(ID_FISHING_ROD) == TILE_FISHING_ROD_CAST, "a cast rod");
        FishingClear();
    }

    // the third person look and the first person hands
    {
        CombatStage(host);
        gInv.Held() = Stack(ID_DIAMOND_SWORD);
        gInv.offhand = Stack(ID_BREAD);
        gInv.armor[ARMOR_CHEST] = Stack(ID_ELYTRA);
        gInv.armor[0] = Stack(ID_IRON_HELMET);
        gGame.lookDir = Vec3(1, 0, 0);
        gGame.hurtTimer = 0.3f;
        PlayerDrawInput in = PlayerLook(Vec3(0, -1, 0), Vec3(1, 0, 0), false, true, false, 0.8f);
        CHECK(in.held == ID_DIAMOND_SWORD && in.offHeld == ID_BREAD && in.anim.holding && in.anim.holdingLeft && in.elytra && in.armor[0] == ID_IRON_HELMET,
              "the items, the elytra and the armour");
        CHECK(in.anim.crouch && in.g < 0.5f && in.r == 1.0f && in.light == 0.8f && Near(in.anim.headYaw, 0.0f) && Near(in.anim.headPitch, 0.0f),
              "sneaking, hurt, looking straight ahead");
        gGame.lookDir = Vec3(0, -1, 0);
        in = PlayerLook(Vec3(0, -1, 0), Vec3(1, 0, 0), false, false, false, 1.0f);
        CHECK(Near(in.anim.headYaw, 1.3f), "the head turns, but not all the way (%.2f)", in.anim.headYaw);
        in = PlayerLook(Vec3(0, -1, 0), Vec3(1, 0, 0), false, false, true, 1.0f);
        CHECK(in.held == 0 && in.anim.limbAmount == 0.0f, "the dead hold nothing");
        gSurvival.eatTimer = 0.8f;
        in = PlayerLook(Vec3(0, -1, 0), Vec3(1, 0, 0), false, false, false, 1.0f);
        CHECK(in.anim.armPose == ARM_EAT && Near(in.anim.useTicks, 16.0f), "eating raises the arm");

        gGame.handItem = ID_DIAMOND_SWORD;
        gGame.handHeight = 1.0f;
        FirstPersonInput h = HandInput(false);
        CHECK(h.item == ID_DIAMOND_SWORD && !h.leftHand && Near(h.eat, 0.5f) && h.lowered == 0.0f, "the main hand eats half way");
        gGame.offItem = ID_BREAD;
        gGame.usingOffhand = true;
        h = HandInput(true);
        gGame.offHeight = 0.25f;
        h = HandInput(true);
        CHECK(h.leftHand && h.item == ID_BREAD && Near(h.eat, 0.5f) && Near(h.lowered, 0.75f), "the off hand eats too, while coming up");
        gGame.usingOffhand = false;
        gSurvival.eatTimer = 0.0f;
    }

    // the dead tip over
    {
        Vec3 r(1, 0, 0), u(0, 0, 1);
        DeathTilt(0.0f, r, u);
        CHECK(Near(u.z, 1.0f), "at first upright");
        DeathTilt(1.0f, r, u);
        CHECK(Near(u.x, -1.0f, 1e-3f) && Near(r.z, 1.0f, 1e-3f), "after a second on the side");
    }

    // lava and fire near the player
    {
        CombatStage(host);
        gWorld.SetRaw(3, 0, 10, MakeVox(ID_LAVA));
        gWorld.SetRaw(-3, 0, 10, MakeVox(ID_FIRE));
        gWorld.ticking.insert(Int3{ 3, 0, 10 });
        gWorld.ticking.insert(Int3{ -3, 0, 10 });
        HeatTick(0.05f, Vec3(0.5f, 0.5f, 10.0f));
        CHECK(HotBlocks().size() == 2 && HotBlocks()[0].d2 <= HotBlocks()[1].d2, "the lava and the fire, nearest first");
        CHECK(HotBlockAt(Vec3(3.5f, 0.5f, 10.5f)) == ID_LAVA && HotBlockAt(Vec3(-2.5f, 0.5f, 10.2f)) == ID_FIRE &&
                  HotBlockAt(Vec3(0.5f, 0.5f, 10.5f)) == ID_AIR,
              "what burns where");
        gParticles.clear();
        for (int i = 0; i < 400; ++i)
            HeatTick(0.05f, Vec3(0.5f, 0.5f, 10.0f));
        CHECK(CountParticles(TILE_P_GENERIC_0) > 20 && CountParticles(TILE_P_LAVA) >= 1 && Heard(SND_FIRE_AMBIENT) > 2,
              "fire smokes and crackles, lava pops (%d drops)", CountParticles(TILE_P_LAVA));
        BlockRulesClear();
        CHECK(HotBlocks().empty(), "a new world forgets them");
        gWorld.ticking.clear();
    }

    // bubbles
    {
        CombatStage(host);
        gGame.eyePos = Vec3(0.5f, 0.5f, 11.6f);
        BreathEffects(0.05f, false, true);
        CHECK(CountParticles(TILE_P_BUBBLE) == 8 && gGame.deathCause == STR_DEATH_DROWN, "drowning: a burst of bubbles");
        gParticles.clear();
        for (int i = 0; i < 40; ++i)
            BreathEffects(0.05f, true, false);
        CHECK(CountParticles(TILE_P_BUBBLE) >= 2 && CountParticles(TILE_P_BUBBLE) <= 6, "under water a few now and then (%d in 2 s)",
              CountParticles(TILE_P_BUBBLE));
    }

    // items in the GUI
    {
        IconQuad q[kMaxIconQuads];
        CHECK(ItemIcon(ID_STONE, q) == 3 && q[0].color == 0xFFFFFFFF && q[1].color == 0xFFCCCCCC && q[2].color == 0xFF999999, "a block: a little cube");
        CHECK(ItemIcon(ID_POPPY, q) == 1 && q[0].x[1] == 16.0f, "a flower: flat");
        CHECK(ItemIcon(ID_STICK, q) == 1 && q[0].tile == Item(ID_STICK).tile, "an item: its tile");
        CHECK(ItemIcon(0, q) == 0, "nothing: nothing");
        int w = 0;
        uint32_t col = 0;
        CHECK(!DurabilityBar(Stack(ID_DIAMOND_SWORD), &w, &col) && !DurabilityBar(Stack(ID_DIRT), &w, &col), "new tools and blocks have no bar");
        ItemStack worn = Stack(ID_DIAMOND_SWORD);
        worn.damage = (uint16_t)(Item(ID_DIAMOND_SWORD).durability / 2);
        CHECK(DurabilityBar(worn, &w, &col) && (w == 6 || w == 7) && ((col >> 16) & 255) > 200 && ((col >> 8) & 255) > 200, "half worn: half a yellow bar");
        worn.damage = (uint16_t)(Item(ID_DIAMOND_SWORD).durability - 1);
        CHECK(DurabilityBar(worn, &w, &col) && w == 0 && ((col >> 16) & 255) == 255 && ((col >> 8) & 255) < 10, "nearly broken: red");
    }
    gGame = GameState();
    ResetSurvival();
}

static void TestRenderers() {
    TestHost host;
    SetHost(&host);
    RecordSink sink;
    SetModelSink(&sink);
    const Vec3 R(1, 0, 0), U(0, 0, 1);

    // things on the ground
    {
        DropEntity d;
        d.pos = Vec3(0.5f, 0.5f, 10.0f);
        d.stack = Stack(ID_STONE, 20);
        sink.Reset();
        CHECK(DrawDrop(d, 1.0f) && sink.quads[1] == 18 && sink.lo.z > 10.0f, "twenty stone: three little cubes, floating (%d)", sink.quads[1]);
        d.stack = Stack(ID_STICK, 1);
        sink.Reset();
        DrawDrop(d, 1.0f);
        CHECK(sink.quads[1] == 34, "a stick: one flat item");
        d.stack = ItemStack();
        CHECK(!DrawDrop(d, 1.0f), "nothing: no drawing, no shadow");

        PrimedTnt t;
        t.pos = Vec3(0.5f, 0.5f, 10.0f);
        t.fuse = 2.0f;
        sink.Reset();
        DrawPrimedTnt(t, 1.0f);
        CHECK(sink.quads[1] == 6 && sink.quads[0] == 6, "lit TNT blinks white");
        t.fuse = 0.3f;
        sink.Reset();
        DrawPrimedTnt(t, 1.0f);
        CHECK(sink.quads[1] == 6 && sink.quads[0] == 0, "and dark");
        t.fuse = 0.05f;
        sink.Reset();
        DrawPrimedTnt(t, 1.0f);
        CHECK(sink.hi.z - sink.lo.z > 1.1f, "it swells just before the blast (%.2f)", sink.hi.z - sink.lo.z);

        XpOrb o;
        o.pos = Vec3(0.5f, 0.5f, 10.0f);
        o.value = 20;
        sink.Reset();
        DrawXpOrb(o, R, U);
        const float small = sink.hi.x - sink.lo.x;
        o.value = 2500;
        sink.Reset();
        DrawXpOrb(o, R, U);
        CHECK(sink.quads[0] == 1 && sink.hi.x - sink.lo.x > small, "experience orbs: bigger for more");
    }

    // particles
    {
        Particle p;
        p.pos = Vec3(0.5f, 0.5f, 10.0f);
        p.size = 0.1f;
        p.tile = TILE_P_BUBBLE;
        p.color = 0xFFFFFFFF;
        sink.Reset();
        DrawParticle(p, R, U, 0.5f);
        CHECK(sink.quads[1] == 1 && ((sink.color >> 16) & 255) == 127 && (sink.color >> 24) == 255, "a particle in the dark is darker");
        p.glow = true;
        sink.Reset();
        DrawParticle(p, R, U, 0.5f);
        CHECK(((sink.color >> 16) & 255) == 255, "a glowing one is not");
    }

    // falling blocks
    {
        BlockStage();
        gWorld.Set(0, 0, 15, MakeVox(ID_SAND));
        RunBlocks(0.05f);
        sink.Reset();
        DrawFallingBlocks(1.0f);
        CHECK(!FallingBlocks().empty() && sink.quads[1] == 6 * (int)FallingBlocks().size(), "falling sand is drawn as a cube");
        BlockRulesClear();
    }

    // what flies
    {
        Projectile pr;
        pr.type = PJ_ARROW;
        pr.pos = Vec3(0.5f, 0.5f, 10.0f);
        pr.vel = Vec3(10, 0, 0);
        sink.Reset();
        DrawProjectile(pr, R, U, 1.0f);
        CHECK(sink.quads[0] == 2 && sink.hi.x - sink.lo.x > 0.85f, "an arrow: two crossed strips along its flight");
        pr.type = PJ_SNOWBALL;
        sink.Reset();
        DrawProjectile(pr, R, U, 1.0f);
        CHECK(sink.quads[1] == 1 && sink.quads[0] == 0, "a snowball: its sprite");
        pr.type = PJ_TRIDENT;
        sink.Reset();
        DrawProjectile(pr, R, U, 1.0f);
        CHECK(sink.quads[0] > 6, "a trident: its model");

        Bolt b{ Vec3(0.5f, 0.5f, 10.0f), 0.0f, 12345u };
        sink.Reset();
        DrawBolt(b, Vec3(20.5f, 0.5f, 12.0f));
        CHECK(sink.quads[0] == 2 * (16 + 6 + 6) && sink.hi.z > 100.0f, "lightning: a jagged column 110 m high");
        b.age = 0.07f;
        sink.Reset();
        DrawBolt(b, Vec3(20.5f, 0.5f, 12.0f));
        CHECK(sink.quads[0] == 0, "it flickers");

        gBobber = Bobber();
        gBobber.active = true;
        gBobber.pos = Vec3(5.5f, 0.5f, 10.0f);
        sink.Reset();
        DrawFishingHook(R, U, 1.0f);
        DrawFishingLine(Vec3(0.5f, 0.5f, 11.5f), Vec3(0.5f, -2.0f, 11.6f));
        CHECK(sink.quads[0] == 1 + 16, "the bobber and a line of sixteen pieces");
        FishingClear();
    }

    // cracks and the camera
    {
        CHECK(CrackTile(0.0f) == TILE_DESTROY_0 && CrackTile(0.55f) == TILE_DESTROY_0 + 5 && CrackTile(1.5f) == TILE_DESTROY_0 + 9, "ten crack stages");
        gGame = GameState();
        Vec3 pos(0, 0, 10), look(0, 1, 0);
        gGame.bob = 0.1f;
        gGame.walkDist = 0.25f;
        BobView(Vec3(0, 1, 0), pos, look);
        CHECK(pos.z > 10.0f && std::fabs(pos.x) > 0.01f && look.z < 0.0f && Near(look.Length(), 1.0f, 1e-3f), "walking bobs the view");
        gGame.fovMod = 1.15f;
        CHECK(Near(FovWanted(70.0f), 80.5f, 1e-3f), "sprinting widens it");
        gGame.spyglass = true;
        CHECK(Near(FovWanted(70.0f), 8.05f, 1e-3f), "the spyglass narrows it ten times");
        gGame = GameState();
    }
    SetModelSink(nullptr);
}

static void TestCreeper() {
    TestHost host;
    SetHost(&host);
    srand(2718);
    const Vec3 player(0.5f, 0.5f, 11.0f); // (his middle; his feet are on the floor at z = 10)

    // it goes for a player in survival, swells and blows up
    {
        MobStage();
        host.explosions = 0;
        int c = SpawnMob(MOB_CREEPER, Vec3(8.5f, 0.5f, 10.0f), true);
        CHECK(c >= 0 && gMobs[c].health == 20.0f && Near(MobHeight(gMobs[c]), 1.7f), "a creeper: 10 hearts, 1.7 m");
        gGame.gameMode = MODE_CREATIVE;
        RunMobs(3.0f, player);
        CHECK(gMobs[c].swell == 0.0f && (gMobs[c].pos - Vec3(0.5f, 0.5f, 10.0f)).Length() > 3.0f, "a player in creative is left alone");
        gGame.gameMode = MODE_SURVIVAL;
        gMobs[c].pos = Vec3(8.5f, 0.5f, 10.0f);
        float t = 0.0f;
        while (gMobs[c].swell == 0.0f && t < 10.0f) {
            MobsTick(0.05f, player, 0, true);
            t += 0.05f;
        }
        const float d = (gMobs[c].pos - Vec3(0.5f, 0.5f, 10.0f)).Length();
        CHECK(t < 4.0f && d < 3.0f && d > 2.0f && Heard(SND_CREEPER_PRIMED) == 1, "it walks up to him and hisses at 3 m (%.1f s)", t);
        RunMobs(1.0f, player);
        CHECK(gMobs.size() == 1 && gMobs[c].swell > 0.6f && gMobs[c].vel.Length() < 0.5f, "it stands and swells");
        RunMobs(0.6f, player);
        CHECK(gMobs.empty() && host.explosions == 1 && host.lastBlast == BLAST_TNT && Heard(SND_EXPLODE) == 1, "after 1.5 s it blows up, and is gone");
        CHECK(gWorld.GetBlock(3, 0, 9) == ID_AIR && gDrops.size() > 0 && gGame.deathCause == STR_DEATH_EXPLOSION, "taking the floor with it");

        // running away
        MobStage();
        host.explosions = 0;
        c = SpawnMob(MOB_CREEPER, Vec3(2.5f, 0.5f, 10.0f), true);
        RunMobs(0.6f, player);
        CHECK(gMobs[c].swell > 0.2f, "it starts to swell");
        RunMobs(0.6f, Vec3(10.5f, 0.5f, 11.0f));
        CHECK(gMobs[c].swellDir < 0 && gMobs[c].swell < 0.5f, "8 m away it calms down");
        RunMobs(4.0f, Vec3(30.5f, 0.5f, 11.0f));
        CHECK(gMobs.size() == 1 && gMobs[c].swell == 0.0f && host.explosions == 0, "and does not blow up");

        // hit and killed
        MobStage();
        c = SpawnMob(MOB_CREEPER, Vec3(6.5f, 0.5f, 10.0f), true);
        MobHurt(c, 5.0f, player, 1.0f);
        CHECK(gMobs[c].panic == 0.0f && Heard(SND_CREEPER_HURT) == 1, "a hit does not scare it");
        MobHurt(c, 0.0f, player, 0.0f);
        gMobs[c].hurt = 0.0f;
        MobHurt(c, 30.0f, player, 0.0f);
        RunMobs(1.2f, Vec3(40.5f, 0.5f, 11.0f));
        int xp = 0;
        for (const XpOrb& o : gXpOrbs)
            xp += o.value;
        CHECK(gMobs.empty() && Heard(SND_CREEPER_DEATH) == 1 && CountDrops(ID_GUNPOWDER) <= 2 && xp == 5, "killed: gunpowder and 5 experience");
    }

    // only at night
    {
        MobStage();
        host.herdGround = true;
        gRules.animals = false;
        for (int i = 0; i < 100; ++i)
            MobsSpawnTick(0.5f, player);
        CHECK(gMobs.empty(), "no creepers by day");
        host.dark = true;
        for (int i = 0; i < 200; ++i)
            MobsSpawnTick(0.5f, player);
        bool creepers = !gMobs.empty();
        for (const Mob& m : gMobs)
            creepers = creepers && m.kind == MOB_CREEPER && !m.persistent;
        CHECK(creepers && gMobs.size() == 4, "at night they come out, four at most (%d)", (int)gMobs.size());
        gRules.monsters = false;
        MobsClear();
        for (int i = 0; i < 100; ++i)
            MobsSpawnTick(0.5f, player);
        CHECK(gMobs.empty(), "not when the rules say no");
        host.dark = false;
    }
    MobsClear();
    gRules = GameRules();
    gGame = GameState();
}

static void TestShapes() {
    TestHost host;
    SetHost(&host);
    srand(31415);

    // what they are made of
    {
        ShapeBox b[kMaxShapeBoxes];
        CHECK(IsShapedBlock(ID_OAK_STAIRS) && IsSolidBlock(ID_OAK_STAIRS) && !IsOpaqueBlock(ID_OAK_STAIRS) && !IsShapedBlock(ID_STONE),
              "stairs are solid, but not a full cube");
        CHECK(BlockShapeBoxes(ID_OAK_STAIRS, FACE_EAST, b) == 2 && b[0].z1 == 0.5f && b[1].x0 == 0.5f && b[1].z0 == 0.5f && b[1].z1 == 1.0f,
              "stairs: a lower slab and the half rising to the east");
        CHECK(BlockShapeBoxes(ID_OAK_STAIRS, FACE_SOUTH | META_UPSIDE, b) == 2 && b[0].z0 == 0.5f && b[1].y1 == 0.5f && b[1].z1 == 0.5f,
              "upside down: the slab on top");
        CHECK(BlockShapeBoxes(ID_STONE_SLAB, 0, b) == 1 && b[0].z1 == 0.5f, "a slab: the lower half");
        CHECK(BlockShapeBoxes(ID_STONE_SLAB, META_SLAB_TOP, b) == 1 && b[0].z0 == 0.5f, "or the upper one");
        CHECK(BlockShapeBoxes(ID_STONE_SLAB, META_SLAB_DOUBLE, b) == 1 && b[0].z0 == 0.0f && b[0].z1 == 1.0f, "or both");
        CHECK(Craft(3, 3, { ID_OAK_PLANKS, 0, 0, ID_OAK_PLANKS, ID_OAK_PLANKS, 0, ID_OAK_PLANKS, ID_OAK_PLANKS, ID_OAK_PLANKS }).id == ID_OAK_STAIRS &&
                  Craft(3, 3, { ID_STONE, ID_STONE, ID_STONE, 0, 0, 0, 0, 0, 0 }).id == ID_STONE_SLAB,
              "both are crafted as in Minecraft");
    }

    // drawn
    {
        BlockMesh m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_OAK_STAIRS, FACE_EAST) } });
        CHECK(m.verts.size() == 48, "lone stairs: two boxes, twelve faces (%d)", (int)m.verts.size() / 4);
        float top = 0.0f;
        for (const MeshVertex& v : m.verts)
            top = std::max(top, v.z);
        CHECK(top == 4.0f, "a block high");
        m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_OAK_STAIRS, FACE_EAST) }, { { 3, 3, 2 }, MakeVox(ID_STONE) } });
        CHECK(m.verts.size() == 44 + 24, "on stone: their bottom is hidden, the stone's top is not");
        m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_STONE_SLAB, 0) } });
        bool half = true;
        for (const MeshVertex& v : m.verts)
            half = half && v.z <= 3.5f;
        CHECK(m.verts.size() == 24 && half, "a slab: half a block high");
    }

    // walked on
    {
        TestMap map;
        BlockStage();
        gWorld.SetRaw(2, 0, 10, MakeVox(ID_OAK_STAIRS, FACE_EAST));
        gWorld.SetRaw(3, 0, 10, MakeVox(ID_STONE));
        std::vector<Aabb> boxes;
        GatherBoxes(map, Aabb{ 2.1f, 0.1f, 10.1f, 2.4f, 0.4f, 10.4f }, boxes);
        bool low = false;
        for (const Aabb& a : boxes)
            low = low || (a.x0 == 2.0f && a.z1 == 10.5f);
        CHECK(low, "the physics sees the stairs' boxes");
        Body b = Standing(0.5f, 0.5f);
        WalkInput in;
        in.fwd = 1.0f;
        in.look = Vec3(1, 0, 0); // east
        WalkEvents ev;
        float highest = 0.0f, highestX = 0.0f;
        for (int i = 0; i < 120; ++i) {
            WalkStep(b, in, 1.0f / 60.0f, map, ev);
            if (b.pos.z > highest) {
                highest = b.pos.z;
                highestX = b.pos.x;
            }
        }
        CHECK(highest > 11.95f && highestX < 4.0f, "walking east the player goes up the stairs onto the stone (%.2f at x %.2f)",
              highest, highestX);
    }

    // placed
    {
        CombatStage(host);
        gGame.lookDir = Vec3(1, 0, 0);
        gTarget = Target();
        gTarget.valid = gTarget.voxel = true;
        gTarget.pos = Int3{ 3, 0, 9 };
        gTarget.face = FACE_TOP;
        gTarget.point = Vec3(3.5f, 0.5f, 10.0f);
        gTarget.normal = Vec3(0, 0, 1);
        gInv.Held() = Stack(ID_OAK_STAIRS, 4);
        PlaceHeldBlock();
        const Voxel v = gWorld.Get(3, 0, 10);
        CHECK(VoxBlock(v) == ID_OAK_STAIRS && VoxMeta(v) == FACE_EAST, "stairs put down looking east rise to the east");
        gWorld.SetRaw(5, 0, 11, MakeVox(ID_STONE));
        gTarget.pos = Int3{ 5, 0, 11 };
        gTarget.face = FACE_WEST;
        gTarget.point = Vec3(5.0f, 0.5f, 11.8f);
        gTarget.normal = Vec3(-1, 0, 0);
        PlaceHeldBlock();
        CHECK(VoxMeta(gWorld.Get(4, 0, 11)) == (FACE_EAST | META_UPSIDE), "against the upper half of a side: upside down");

        gInv.Held() = Stack(ID_STONE_SLAB, 4);
        gWorld.SetRaw(3, 0, 10, MakeVox(ID_AIR));
        gTarget.pos = Int3{ 3, 0, 9 };
        gTarget.face = FACE_TOP;
        gTarget.point = Vec3(3.5f, 0.5f, 10.0f);
        gTarget.normal = Vec3(0, 0, 1);
        PlaceHeldBlock();
        CHECK(gWorld.Get(3, 0, 10) == MakeVox(ID_STONE_SLAB, 0) && gInv.Held().count == 3, "a slab on the floor: the lower half");
        gTarget.pos = Int3{ 3, 0, 10 };
        gTarget.point = Vec3(3.5f, 0.5f, 10.5f);
        PlaceHeldBlock();
        CHECK(gWorld.Get(3, 0, 10) == MakeVox(ID_STONE_SLAB, META_SLAB_DOUBLE) && gWorld.GetBlock(3, 0, 11) == ID_AIR && gInv.Held().count == 2,
              "another on top of it makes it double");
        gInv.Held() = Stack(ID_DIAMOND_PICKAXE);
        BreakVoxel(Int3{ 3, 0, 10 }, true);
        CHECK(CountDrops(ID_STONE_SLAB) == 2, "a double slab breaks into two (%d)", CountDrops(ID_STONE_SLAB));
    }

    // panes, bars, fences, walls: they join their neighbours
    {
        ShapeBox b[kMaxShapeBoxes];
        CHECK(ShapeConnections(ID_OAK_FENCE, ID_OAK_FENCE, ID_AIR, ID_STONE, ID_GLASS_PANE) == (1 | 4),
              "a fence joins a fence and a full block, not air or a pane");
        CHECK(ShapeConnections(ID_GLASS_PANE, ID_IRON_BARS, ID_GLASS, ID_OAK_STAIRS, ID_GLASS_PANE) == (1 | 2 | 8) &&
                  ShapeConnections(ID_STONE, ID_STONE, ID_STONE, ID_STONE, ID_STONE) == 0,
              "a pane joins bars, glass and panes, not stairs; a cube joins nothing");
        CHECK(BlockShapeBoxes(ID_GLASS_PANE, 0, b, 0) == 1 && b[0].x1 - b[0].x0 == 2.0f / 16.0f, "a lone pane: its post");
        CHECK(BlockShapeBoxes(ID_OAK_FENCE, 0, b, 1) == 3 && b[1].x0 == 10.0f / 16.0f && b[1].x1 == 1.0f, "a fence: its post and two bars east");
        CHECK(BlockShapeBoxes(ID_OAK_FENCE, 0, b, 1, true) == 2 && b[0].z1 == 1.5f && b[1].z1 == 1.5f, "bumped into: 1.5 high");
        CHECK(BlockShapeBoxes(ID_COBBLESTONE_WALL, 0, b, 4 | 8) == 3 && b[1].z1 == 14.0f / 16.0f && b[2].y0 == 0.0f, "a wall north and south");
        CHECK(Craft(3, 3, { ID_OAK_PLANKS, ID_STICK, ID_OAK_PLANKS, ID_OAK_PLANKS, ID_STICK, ID_OAK_PLANKS, 0, 0, 0 }).id == ID_OAK_FENCE &&
                  Craft(3, 3, { ID_GLASS, ID_GLASS, ID_GLASS, ID_GLASS, ID_GLASS, ID_GLASS, 0, 0, 0 }).count == 16 &&
                  Craft(3, 3, { ID_COBBLESTONE, ID_COBBLESTONE, ID_COBBLESTONE, ID_COBBLESTONE, ID_COBBLESTONE, ID_COBBLESTONE, 0, 0, 0 }).id ==
                      ID_COBBLESTONE_WALL,
              "crafted as in Minecraft");

        BlockMesh m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_OAK_FENCE) } });
        CHECK(m.verts.size() == 24, "a lone fence: its post");
        m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_OAK_FENCE) }, { { 4, 3, 3 }, MakeVox(ID_OAK_FENCE) } });
        CHECK(m.verts.size() == 2 * 4 * (6 + 5 + 5), "two fences: their bars meet, the faces between them are not drawn (%d)",
              (int)m.verts.size() / 4);

        TestMap map;
        BlockStage();
        gWorld.SetRaw(2, 0, 10, MakeVox(ID_OAK_FENCE));
        std::vector<Aabb> boxes;
        GatherBoxes(map, Aabb{ 2.1f, 0.1f, 10.1f, 2.9f, 0.9f, 11.4f }, boxes);
        bool tall = false;
        for (const Aabb& a : boxes)
            tall = tall || a.z1 == 11.5f;
        CHECK(tall, "the player cannot jump over a fence");
    }

    // as items
    {
        IconQuad q[kMaxIconQuads];
        CHECK(ItemIcon(ID_OAK_STAIRS, q) == 6 && ItemIcon(ID_STONE_SLAB, q) == 3 && q[0].y[0] > 4.0f, "stairs and slabs in the GUI: their shape");
        CHECK(ItemIcon(ID_OAK_FENCE, q) == 15 && ItemIcon(ID_GLASS_PANE, q) == 1, "a fence: post and bars; a pane: flat");
        RecordSink rec;
        SetModelSink(&rec);
        rec.Reset();
        Pose p;
        DrawItemModel(p, ID_OAK_FENCE, 1.0f);
        CHECK(rec.quads[1] == 5 * 6, "a fence in the hand: its boxes (%d)", rec.quads[1]);
        SetModelSink(nullptr);
    }
    gWorld.Clear();
}

static void TestBeds() {
    TestHost host;
    SetHost(&host);
    srand(2718);

    // what a bed is
    {
        ShapeBox b[kMaxShapeBoxes];
        CHECK(BedOtherBlock(ID_RED_BED) == ID_RED_BED_HEAD && BedOtherBlock(ID_RED_BED_HEAD) == ID_RED_BED && BedOtherBlock(ID_STONE) == 0 &&
                  (FACE_EAST ^ 1) == FACE_WEST && (FACE_NORTH ^ 1) == FACE_SOUTH,
              "a bed: its foot and its head");
        CHECK(BlockShapeBoxes(ID_RED_BED, FACE_NORTH, b) == 3 && b[0].z0 == 3.0f / 16.0f && b[0].z1 == 9.0f / 16.0f && b[1].y0 == 0.0f &&
                  b[1].y1 == 3.0f / 16.0f && b[1].tileFace == FACE_BOTTOM,
              "the mattress, and the legs at the foot's end");
        CHECK(BlockShapeBoxes(ID_RED_BED_HEAD, FACE_NORTH, b) == 3 && b[1].y1 == 1.0f, "the head's legs at its end");
        CHECK(BlockShapeBoxes(ID_RED_BED, FACE_EAST, b, 0, true) == 1 && b[0].z0 == 0.0f && b[0].z1 == 9.0f / 16.0f, "bumped into: one box");
        const BlockDef& d = Block(ID_RED_BED);
        CHECK(BlockFaceTile(ID_RED_BED, FACE_SOUTH, FACE_NORTH) == d.tex[FACE_SOUTH] &&
                  BlockFaceTile(ID_RED_BED, FACE_WEST, FACE_EAST) == d.tex[FACE_SOUTH] &&
                  BlockFaceTile(ID_RED_BED, FACE_NORTH, FACE_EAST) == d.tex[FACE_WEST],
              "turned with its head: the foot's end faces away from it");
        float h = 0.25f, v = 0.0f;
        TurnUv(ShapeUvTurns(ID_RED_BED, FACE_EAST, FACE_TOP), h, v);
        CHECK(ShapeUvTurns(ID_RED_BED, FACE_NORTH, FACE_TOP) == 0 && h == 1.0f && v == 0.25f && ShapeUvTurns(ID_STONE_SLAB, 0, FACE_TOP) == 0,
              "its top turns with it");
        CHECK(Craft(3, 3, { ID_RED_WOOL, ID_RED_WOOL, ID_RED_WOOL, ID_OAK_PLANKS, ID_OAK_PLANKS, ID_OAK_PLANKS, 0, 0, 0 }).id == ID_RED_BED,
              "crafted as in Minecraft");
        IconQuad q[kMaxIconQuads];
        CHECK(ItemIcon(ID_RED_BED, q) == 18, "in the GUI: both halves");
        BlockMesh m = MeshOf({ { { 3, 3, 3 }, MakeVox(ID_RED_BED, FACE_NORTH) }, { { 3, 4, 3 }, MakeVox(ID_RED_BED_HEAD, FACE_NORTH) } });
        CHECK(m.verts.size() == 4 * (2 * 18 - 2), "drawn: the faces between the halves are not (%d)", (int)m.verts.size() / 4);
    }

    // put down, broken
    {
        CombatStage(host);
        gTarget.valid = gTarget.voxel = true;
        gTarget.pos = Int3{ 3, 0, 9 };
        gTarget.face = FACE_TOP;
        gTarget.point = Vec3(3.5f, 0.5f, 10.0f);
        gTarget.normal = Vec3(0, 0, 1);
        gInv.Held() = Stack(ID_RED_BED, 2);
        PlaceHeldBlock();
        CHECK(gWorld.Get(3, 0, 10) == MakeVox(ID_RED_BED, FACE_EAST) && gWorld.Get(4, 0, 10) == MakeVox(ID_RED_BED_HEAD, FACE_EAST) &&
                  gInv.Held().count == 1,
              "looking east: the foot here, the head beyond");
        gWorld.SetRaw(4, 2, 10, MakeVox(ID_STONE));
        gTarget.pos = Int3{ 3, 2, 9 };
        gTarget.point = Vec3(3.5f, 2.5f, 10.0f);
        PlaceHeldBlock();
        CHECK(gWorld.GetBlock(3, 2, 10) == ID_AIR && gInv.Held().count == 1, "no room for the head: no bed");

        gInv.Held() = ItemStack();
        gGame.gameMode = MODE_SURVIVAL;
        BreakVoxel(Int3{ 4, 0, 10 }, true);
        CHECK(gWorld.GetBlock(3, 0, 10) == ID_AIR && gWorld.GetBlock(4, 0, 10) == ID_AIR && CountDrops(ID_RED_BED) == 1,
              "the head broken: the whole bed goes, one bed drops");
    }

    // slept in
    {
        CombatStage(host);
        gWorld.SetRaw(3, 0, 10, MakeVox(ID_RED_BED, FACE_EAST));
        gWorld.SetRaw(4, 0, 10, MakeVox(ID_RED_BED_HEAD, FACE_EAST));
        gSleep = SleepState();
        host.hours = 13.0f;
        CHECK(UseBed(Int3{ 4, 0, 10 }) && !gSleep.asleep && gSleep.spawnSet && gSleep.spawn == (Int3{ 3, 0, 10 }) &&
                  gGame.message.find("geceleri") != std::string::npos,
              "by day: no sleep, but the bed is the respawn point");
        CHECK(!UseBed(Int3{ 5, 0, 10 }), "no bed there");
        host.hours = 23.5f;
        gGame.gameMode = MODE_SURVIVAL;
        int c = SpawnMob(MOB_CREEPER, Vec3(8.5f, 0.5f, 10.0f), true);
        CHECK(UseBed(Int3{ 3, 0, 10 }) && !gSleep.asleep && gGame.message.find("canavar") != std::string::npos, "a creeper near: no rest");
        gMobs[c].pos = Vec3(30.5f, 0.5f, 10.0f);
        UseBed(Int3{ 3, 0, 10 });
        Vec3 spot, feet, head;
        CHECK(gSleep.asleep && SleepSpot(&spot) && spot.x == 4.0f && spot.z == 10.0f + 9.0f / 16.0f && SleepPose(&feet, &head) && head.x == 1.0f,
              "at night: asleep, lying on the bed, the head on the pillow");
        for (int i = 0; i < 40; ++i)
            SleepTick(0.05f, false);
        CHECK(gSleep.asleep && SleepFade() > 0.35f && SleepFade() < 0.45f && host.clockSet < 0.0f, "the screen goes dark");
        for (int i = 0; i < 70 && gSleep.asleep; ++i)
            SleepTick(0.05f, false);
        CHECK(!gSleep.asleep && host.clockSet == 6.0f && host.clockNextDay && SleepFade() > 0.9f, "five seconds: the morning");
        SleepTick(0.5f, false);
        CHECK(SleepFade() == 0.0f, "and the dark goes away");

        UseBed(Int3{ 3, 0, 10 });
        CHECK(!SleepTick(0.05f, true) && !gSleep.asleep && host.clockSet == 6.0f, "the sneak key: he gets up, the night goes on");

        host.moves = 0;
        BedRespawn();
        CHECK(host.moves == 1 && host.player.x == 4.0f && host.player.z == 11.0f + 9.0f / 16.0f, "dying, he comes back at his bed");
        gWorld.SetRaw(4, 0, 10, MakeVox(ID_AIR));
        BedRespawn();
        CHECK(host.moves == 1 && !gSleep.spawnSet && gGame.message.find("Yatağın yok") != std::string::npos, "unless it is gone");
    }
    gSleep = SleepState();
    gWorld.Clear();
}

static void TestWarden() {
    TestHost host;
    SetHost(&host);
    srand(1618);
    const Vec3 player(0.5f, 0.5f, 11.0f); // (his middle; his feet are on the floor at z = 10)

    MobStage();
    int w = SpawnMob(MOB_WARDEN, Vec3(6.5f, 0.5f, 10.0f), true);
    CHECK(w >= 0 && gMobs[w].health == 500.0f && Near(MobHeight(gMobs[w]), 2.9f) && IsMonster(MOB_WARDEN) && !IsMonster(MOB_COW) &&
              Item(ID_WARDEN_SPAWN_EGG).special == SP_EGG_WARDEN,
          "a warden: 250 hearts, 2.9 m, from its egg");
    // it hears him walk, not sneak; not at all in creative
    host.velocity = Vec3(3, 0, 0);
    gGame.gameMode = MODE_CREATIVE;
    RunMobs(3.0f, player);
    CHECK(gMobs[w].anger == 0.0f, "a player in creative is not heard");
    gGame.gameMode = MODE_SURVIVAL;
    gGame.sneaking = true;
    gMobs[w].pos = Vec3(11.0f, 0.5f, 10.0f);
    RunMobs(1.5f, player);
    CHECK(gMobs[w].anger == 0.0f, "nor one who sneaks (further than it smells)");
    gGame.sneaking = false;
    gMobs[w].pos = Vec3(9.5f, 0.5f, 10.0f);
    float t = 0.0f;
    while (gMobs[w].anger < 80.0f && t < 10.0f) {
        MobsTick(0.05f, player, 0, true);
        t += 0.05f;
    }
    CHECK(t > 3.5f && t < 4.5f && Heard(SND_WARDEN_LISTENING) >= 2, "walking near it: the third footstep makes it angry (%.1f s)", t);
    host.velocity = Vec3();
    RunMobs(1.0f, player);
    CHECK(gMobs[w].roared && gMobs[w].roar > 0.0f && Heard(SND_WARDEN_ROAR) == 1 && host.playerHurt == 0.0f, "it roars first");
    // from afar: the sonic boom
    gMobs[w].pos = Vec3(9.5f, 0.5f, 10.0f);
    RunMobs(5.5f, player);
    CHECK(Heard(SND_WARDEN_SONIC_CHARGE) == 1 && Heard(SND_WARDEN_SONIC_BOOM) == 1 && host.playerHurt == 10.0f && host.gusts == 1,
          "9 m away: a sonic boom, 5 hearts, and he is thrown back (%.1f)", host.playerHurt);
    // close by: its blows
    gMobs[w].pos = Vec3(2.0f, 0.5f, 10.0f);
    gMobs[w].boomCooldown = 10.0f;
    host.playerHurt = 0.0f;
    RunMobs(1.0f, player);
    CHECK(host.playerHurt == 60.0f && Heard(SND_WARDEN_ATTACK) == 2, "next to him: a blow of 15 hearts every 0.9 s (%.0f)", host.playerHurt);
    // hit: furious, not knocked back
    gMobs[w].anger = 0.0f;
    gMobs[w].vel = Vec3();
    MobHurt(w, 6.0f, Vec3(0.5f, 0.5f, 10.0f), 1.0f);
    CHECK(gMobs[w].anger == 100.0f && gMobs[w].vel.Length() == 0.0f && gMobs[w].health == 494.0f, "a hit makes it furious; it does not budge");
    // left alone: it digs back down
    gGame.gameMode = MODE_CREATIVE;
    gMobs[w].anger = 0.0f;
    RunMobs(65.0f, Vec3(40.5f, 0.5f, 11.0f));
    CHECK(gMobs.empty() && Heard(SND_WARDEN_DIG) == 1 && CountDrops(ID_SCULK_CATALYST) == 0, "a minute after its anger is gone it digs back down");
    gGame.gameMode = MODE_SURVIVAL;
    w = SpawnMob(MOB_WARDEN, Vec3(6.5f, 0.5f, 10.0f), true);
    gMobs[w].health = 1.0f;
    MobHurt(w, 5.0f, player, 0.0f);
    RunMobs(1.5f, Vec3(40.5f, 0.5f, 11.0f));
    CHECK(CountDrops(ID_SCULK_CATALYST) == 1, "killed: a sculk catalyst");

    RecordSink rec;
    SetModelSink(&rec);
    rec.Reset();
    DrawMob(MOB_WARDEN, EntityPose(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 0, 1), Vec3(0, 1, 0), 1.0f), MobAnim(), 1.0f, 1, 1, 1);
    CHECK(rec.quads[0] >= 40 && rec.hi.z > 3.0f && rec.hi.z < 3.8f, "its model: over 3 m with its tendrils (%.2f)", rec.hi.z);
    SetModelSink(nullptr);
    MobsClear();
    gWorld.Clear();
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
    gSleep.spawnSet = true;
    gSleep.spawn = Int3{ 7, -8, 9 };
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
    CHECK(f && ReadWorldFile(f, info) && info.version == 4 && info.hostValue == 1 && info.blocksOk && info.hostPart && gSleep.spawnSet && gSleep.spawn == (Int3{ 7, -8, 9 }), "it reads back");
    gSleep = SleepState();
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
    size_t fluids = 0; // water and lava only come from buckets; a bed's head only with the bed
    for (int b = 1; b < NUM_BLOCKS; ++b)
        if (IsFluidBlock(b) || IsFireBlock(b) || Block(b).shape == SHAPE_BED_HEAD)
            ++fluids;
    CHECK(total == (size_t)(NUM_BLOCKS - 1 - fluids + ITEM_END - FIRST_ITEM), "creative tabs cover everything (%d vs %d)",
          (int)total, (int)(NUM_BLOCKS - 1 - fluids + ITEM_END - FIRST_ITEM));
    {
        auto at = [](int tab, uint16_t id) {
            const std::vector<uint16_t>& v = CreativeItems(tab);
            return (int)(std::find(v.begin(), v.end(), id) - v.begin());
        };
        CHECK(CreativeItems(CAT_BUILDING)[0] == ID_OAK_LOG && at(CAT_BUILDING, ID_OAK_PLANKS) < at(CAT_BUILDING, ID_OAK_STAIRS) &&
                  at(CAT_BUILDING, ID_OAK_STAIRS) < at(CAT_BUILDING, ID_SPRUCE_LOG) && at(CAT_BUILDING, ID_STONE) < at(CAT_BUILDING, ID_STONE_STAIRS) &&
                  at(CAT_BUILDING, ID_STONE_STAIRS) < at(CAT_BUILDING, ID_COBBLESTONE),
              "the building blocks as in Minecraft: wood by wood type, then stone, each with its stairs and slabs");
        CHECK(at(CAT_COLORED, ID_WHITE_WOOL) + 1 == at(CAT_COLORED, ID_LIGHT_GRAY_WOOL) && at(CAT_NATURAL, ID_GRASS_BLOCK) == 0 &&
                  at(CAT_TOOLS, ID_WOODEN_SHOVEL) + 1 == at(CAT_TOOLS, ID_WOODEN_PICKAXE) && at(CAT_FOOD, ID_APPLE) == 0,
              "colours in Minecraft's order; the natural tab starts with grass; tools by material");
        CHECK(at(CAT_REDSTONE, ID_REDSTONE) < (int)CreativeItems(CAT_REDSTONE).size() &&
                  at(CAT_SPAWN_EGGS, ID_WARDEN_SPAWN_EGG) < (int)CreativeItems(CAT_SPAWN_EGGS).size(),
              "redstone and spawn eggs have their tabs");
    }

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
    TestCreeper();
    TestCombat();
    TestFishing();
    TestVillagers();
    TestPlayerHealth();
    TestHands();
    TestScreens();
    TestHud();
    TestBlockMesh();
    TestShapes();
    TestBeds();
    TestWarden();
    TestModels();
    TestPlayerAnim();
    TestRenderers();
    TestSave();
    TestControls();
    TestGameState();

    printf(gFail ? "\n%d CHECK(S) FAILED\n" : "\nALL CHECKS PASSED\n", gFail);
    return gFail;
}
