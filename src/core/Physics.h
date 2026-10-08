#pragma once
// How a Minecraft player moves: walking, sprinting, sneaking, jumping, swimming and falling, creative flight and
// the elytra. Only the rules: the host says what the player wants (WalkInput) and what its own map puts in the
// way (MoveWorld), and gets back where the body ends up and what happened on the way (WalkEvents).
// Metres and seconds, z is up; one block is one metre, a Minecraft tick is 1/20 s.

#include "Core.h"

namespace mc {

constexpr float kBodyHalfWidth = 0.3f;
constexpr float kBodyHeight = 1.8f, kSneakHeight = 1.5f;
constexpr float kStepHeight = 0.6f;
// The jump speed that gives Minecraft's 1.25 block jump with our gravity and drag
// (Minecraft: 0.42 blocks/tick, gravity 0.08, drag 0.98 per tick).
constexpr float kJumpSpeed = 9.29f;
constexpr float kWalkSpeed = 4.317f, kSprintSpeed = 5.612f, kSneakSpeed = 1.295f;
constexpr float kFlySpeed = 10.9f, kFlySprintSpeed = 21.6f, kFlyClimbSpeed = 7.5f;

struct Aabb {
    float x0, y0, z0, x1, y1, z1;
};

// how far the box `p` can move along one axis before it touches one of `boxes`
float ClipX(const Aabb& p, float dx, const std::vector<Aabb>& boxes);
float ClipY(const Aabb& p, float dy, const std::vector<Aabb>& boxes);
float ClipZ(const Aabb& p, float dz, const std::vector<Aabb>& boxes);

// The body's position is 1 m above its feet (where GTA keeps a person's origin).
Aabb BodyBox(const Vec3& pos, float height);

Vec3 HorizontalDir(Vec3 v);                                 // unit vector along the ground (north if v has no length)
Vec3 WishDir(const Vec3& look, float fwd, float strafe);    // where the stick points, at most 1 long

// What is in the body's way. The block world answers by itself (blocks, water and lava); the rest is the host's own
// map. A host without a map of its own (the offline tests) leaves everything as it is.
struct MoveWorld {
    virtual ~MoveWorld() = default;
    // the host's boxes in a cell that holds no solid block (thin layers of ground, rims of holes)
    virtual void CellBoxes(int x, int y, int z, std::vector<Aabb>& out) {}
    // highest walkable ground under the feet within [feet - drop, feet + rise]
    virtual bool GroundUnder(const Vec3& pos, float rise, float drop, float* z) { return false; }
    // how far the body can move along `axis` (0 x, 1 y) before a wall
    virtual float ClipHorizontal(const Vec3& pos, float height, int axis, float d) { return d; }
    // room above the head, when there is less of it than `up`
    virtual bool Ceiling(const Vec3& pos, float height, float up, float* limit) { return false; }
    // how far a box can move along one axis (0 x, 1 y, 2 z)
    virtual float ClipBox(const Aabb& b, int axis, float d) { return d; }
    // what its ground under the feet is like, as a block (for the footsteps)
    virtual bool GroundBlock(const Vec3& pos, int* block) { return false; }
};

// solid boxes touching the region: blocks, and the host's own boxes where there is no block
void GatherBoxes(MoveWorld& world, const Aabb& region, std::vector<Aabb>& out);

// ---------------------------------------------------------------- on foot
struct Body {
    Vec3 pos;                  // feet + 1 m
    Vec3 vel;                  // m/s
    bool onGround = false;
    float airTime = 0.0f;
    float fallTop = 0.0f;      // highest point since leaving the ground
    bool noFallDamage = false; // the next landing does not hurt (wind charge flights)
    float jumpCooldown = 0.0f;
    float stepDist = 0.0f;
    bool wasInFluid = false;

    // the body starts being moved by these rules from here
    void Place(const Vec3& p, const Vec3& v, bool ground) {
        pos = p;
        vel = v;
        onGround = ground;
        fallTop = p.z;
        airTime = 0.0f;
    }
};

struct WalkInput {
    float fwd = 0.0f, strafe = 0.0f; // -1..1
    Vec3 look;                       // where the player looks
    bool sprint = false, sneak = false, jump = false;
    bool usingItem = false;          // eating, drawing a bow...: slow
    bool mortal = false;             // falls hurt (survival)
};

struct WalkEvents {
    bool inFluid = false;
    int fluid = 0;               // block id of the fluid the body is in
    bool jumped = false;
    bool sprintStopped = false;  // ran into a wall
    bool landed = false;         // touched the ground after being in the air
    float fallDamage = 0.0f;     // Minecraft health points (20 = full health); 0 = the landing did not hurt
    float fallHeight = 0.0f;     // of a landing that hurt
    float moved = 0.0f;          // horizontal distance
    float exhaustion = 0.0f;     // hunger cost of the jump and the sprinting
};

// one frame of walking / jumping / swimming / falling, with its sounds (splash, footsteps, the thud of a fall)
void WalkStep(Body& body, const WalkInput& in, float dt, MoveWorld& world, WalkEvents& ev);

// Minecraft sprinting: tap (or hold) the sprint key while walking forward; it lasts until the player stops or
// cannot sprint any more (a menu is open, sneaking, too hungry). Returns the latch.
bool UpdateSprintLatch(bool& latch, bool forward, bool sprintKey, bool cannot);

// ---------------------------------------------------------------- creative flight / elytra
// creative flight: the velocity follows the stick, `up` / `down` climb and sink
void FlySteer(Vec3& vel, const Vec3& look, float fwd, float strafe, bool fast, bool up, bool down, float dt);

// Moves a flying body (creative flight, elytra) as a box against the blocks and the host's map, in short steps so
// nothing is skipped at speed. Stops `vel` along the axes that hit something; `wallImpact` (optional) gets the
// horizontal speed of the hardest hit. Returns true when the feet touched the ground.
bool FlyStep(Vec3& pos, Vec3& vel, float dt, bool gliding, MoveWorld& world, float* wallImpact);

// one Minecraft tick of elytra flight (LivingEntity.travel while fall-flying); `boostTime` is what is left of a
// firework rocket's push
void ElytraTick(Vec3& vel, const Vec3& look, float& boostTime);
// runs the elytra ticks that are due after `dt` more seconds (`tickAcc` carries the remainder); returns how many
int ElytraAdvance(Vec3& vel, const Vec3& look, float& boostTime, float& tickAcc, float dt);
// Minecraft's fly-into-wall damage (health points) from the horizontal speed before and after the hit
float GlideImpactDamage(float speedBefore, float speedAfter);

} // namespace mc
