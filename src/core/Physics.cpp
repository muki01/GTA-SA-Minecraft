#include "Physics.h"

#include "Audio.h"
#include "BlockRules.h"
#include "Items.h"
#include "World.h"
#include "Shapes.h"

namespace mc {

Vec3 HorizontalDir(Vec3 v) {
    v.z = 0;
    float m = v.Length();
    return m > 1e-4f ? v * (1.0f / m) : Vec3(0, 1, 0);
}

Vec3 WishDir(const Vec3& look, float fwd, float strafe) {
    const Vec3 f = HorizontalDir(look);
    const Vec3 r(f.y, -f.x, 0.0f);
    Vec3 wish = f * fwd + r * strafe;
    float m = wish.Length();
    if (m > 1.0f)
        wish = wish * (1.0f / m);
    return wish;
}

bool UpdateSprintLatch(bool& latch, bool forward, bool sprintKey, bool cannot) {
    if (forward && sprintKey)
        latch = true;
    if (!forward || cannot)
        latch = false;
    return latch;
}

// ---------------------------------------------------------------- collision against boxes (MC style)
void GatherBoxes(MoveWorld& world, const Aabb& b, std::vector<Aabb>& out) {
    out.clear();
    for (int z = FloorI(b.z0) - 1; z <= FloorI(b.z1); ++z)
        for (int y = FloorI(b.y0); y <= FloorI(b.y1); ++y)
            for (int x = FloorI(b.x0); x <= FloorI(b.x1); ++x) {
                const Voxel v = gWorld.Get(x, y, z);
                if (IsShapedBlock(VoxBlock(v))) {
                    ShapeBox sb[kMaxShapeBoxes];
                    const int n = BlockShapeBoxes(VoxBlock(v), VoxMeta(v), sb, ShapeConnectionsAt(x, y, z), true);
                    for (int i = 0; i < n; ++i)
                        out.push_back({ x + sb[i].x0, y + sb[i].y0, z + sb[i].z0, x + sb[i].x1, y + sb[i].y1, z + sb[i].z1 });
                    continue;
                }
                if (IsSolidBlock(VoxBlock(v))) {
                    out.push_back({ (float)x, (float)y, (float)z, x + 1.0f, y + 1.0f, z + 1.0f });
                    continue;
                }
                world.CellBoxes(x, y, z, out);
            }
}

namespace {
bool Overlap2(float a0, float a1, float b0, float b1) { return a1 > b0 + 1e-4f && a0 < b1 - 1e-4f; }
} // namespace

float ClipZ(const Aabb& p, float dz, const std::vector<Aabb>& boxes) {
    for (const Aabb& b : boxes) {
        if (!Overlap2(p.x0, p.x1, b.x0, b.x1) || !Overlap2(p.y0, p.y1, b.y0, b.y1))
            continue;
        if (dz < 0.0f && p.z0 >= b.z1 - 1e-3f)
            dz = std::max(dz, b.z1 - p.z0);
        else if (dz > 0.0f && p.z1 <= b.z0 + 1e-3f)
            dz = std::min(dz, b.z0 - p.z1);
    }
    return dz;
}

float ClipX(const Aabb& p, float dx, const std::vector<Aabb>& boxes) {
    for (const Aabb& b : boxes) {
        if (!Overlap2(p.y0, p.y1, b.y0, b.y1) || !Overlap2(p.z0, p.z1, b.z0, b.z1))
            continue;
        if (dx < 0.0f && p.x0 >= b.x1 - 1e-3f)
            dx = std::max(dx, b.x1 - p.x0);
        else if (dx > 0.0f && p.x1 <= b.x0 + 1e-3f)
            dx = std::min(dx, b.x0 - p.x1);
    }
    return dx;
}

float ClipY(const Aabb& p, float dy, const std::vector<Aabb>& boxes) {
    for (const Aabb& b : boxes) {
        if (!Overlap2(p.x0, p.x1, b.x0, b.x1) || !Overlap2(p.z0, p.z1, b.z0, b.z1))
            continue;
        if (dy < 0.0f && p.y0 >= b.y1 - 1e-3f)
            dy = std::max(dy, b.y1 - p.y0);
        else if (dy > 0.0f && p.y1 <= b.y0 + 1e-3f)
            dy = std::min(dy, b.y0 - p.y1);
    }
    return dy;
}

Aabb BodyBox(const Vec3& pos, float height) {
    const float feet = pos.z - 1.0f;
    return { pos.x - kBodyHalfWidth, pos.y - kBodyHalfWidth, feet, pos.x + kBodyHalfWidth, pos.y + kBodyHalfWidth,
             feet + height };
}

namespace {
// ground (blocks or the host's map) within `drop` below the feet at `pos`
bool AnyGround(MoveWorld& world, const Vec3& pos, float height, float drop, std::vector<Aabb>& tmp) {
    Aabb b = BodyBox(pos, height);
    Aabb probe = b;
    probe.z0 -= drop;
    GatherBoxes(world, probe, tmp);
    if (ClipZ(b, -drop, tmp) > -drop + 1e-3f)
        return true;
    float gz;
    return world.GroundUnder(pos, 0.05f, drop, &gz);
}

// footstep sound of what is under the feet
void Footstep(MoveWorld& world, const Vec3& pos, bool quiet) {
    const int x = FloorI(pos.x), y = FloorI(pos.y), z = FloorI(pos.z - 1.05f);
    int block = gWorld.GetBlock(x, y, z);
    if (!IsSolidBlock(block)) {
        world.GroundBlock(pos, &block);
        if (block == ID_AIR)
            block = ID_STONE;
    }
    const Vec3 at = pos - Vec3(0, 0, 1.0f);
    PlaySfx((SoundEvent)(SND_STEP_STONE + Block(block).sound), &at, quiet ? 0.08f : 0.18f, 1.0f);
}
} // namespace

// ---------------------------------------------------------------- on foot
// the Minecraft player: walking, sprinting, sneaking, jumping, swimming in block water, falling
void WalkStep(Body& body, const WalkInput& in, float dt, MoveWorld& world, WalkEvents& ev) {
    ev = WalkEvents();
    Vec3& vel = body.vel;
    body.jumpCooldown = std::max(0.0f, body.jumpCooldown - dt);
    const bool sprint = in.sprint, sneak = in.sneak;
    const float height = sneak ? kSneakHeight : kBodyHeight;

    const Vec3 f = HorizontalDir(in.look);
    const Vec3 wish = WishDir(in.look, in.fwd, in.strafe);

    int fluid = ID_AIR;
    const float depth = FluidDepth(body.pos - Vec3(0, 0, 1.0f), height, &fluid);
    const bool inFluid = depth > 0.0f;
    ev.inFluid = inFluid;
    ev.fluid = fluid;

    float speed = sprint ? kSprintSpeed : (sneak ? kSneakSpeed : kWalkSpeed);
    if (in.usingItem)
        speed *= 0.2f;
    if (inFluid)
        speed = fluid == ID_LAVA ? 1.2f : (sprint ? 3.0f : 2.0f);

    // horizontal: Minecraft friction (0.546 per tick on the ground, 0.91 in the air, 0.8 in water)
    const float base = inFluid ? (fluid == ID_LAVA ? 0.5f : 0.8f) : (body.onGround ? 0.546f : 0.91f);
    const float k = 1.0f - std::pow(base, dt * 20.0f);
    vel.x += (wish.x * speed - vel.x) * k;
    vel.y += (wish.y * speed - vel.y) * k;

    // vertical
    const bool space = in.jump;
    bool inAir = false;          // gravity acted on the body this frame
    float airSpeedBefore = 0.0f; // its vertical speed before it did
    if (inFluid) {
        float az = -8.0f + (space ? 24.0f : 0.0f) - (sneak ? 8.0f : 0.0f);
        vel.z = vel.z * std::pow(base, dt * 20.0f) + az * dt;
        // climb out at the surface next to a wall
        if (space && depth < 0.6f) {
            std::vector<Aabb> tmp;
            Aabb b = BodyBox(body.pos, height);
            b.x0 += wish.x * 0.3f;
            b.x1 += wish.x * 0.3f;
            b.y0 += wish.y * 0.3f;
            b.y1 += wish.y * 0.3f;
            GatherBoxes(world, b, tmp);
            if (!tmp.empty())
                vel.z = std::max(vel.z, 6.0f);
        }
        body.fallTop = body.pos.z; // water stops falls
    } else {
        if (body.onGround) {
            vel.z = 0.0f;
            if (space && body.jumpCooldown <= 0.0f) {
                vel.z = kJumpSpeed;
                if (sprint) // sprint jump boost (0.2 blocks/tick in the facing direction)
                    vel += f * 4.0f;
                body.onGround = false;
                body.jumpCooldown = 0.1f;
                ev.jumped = true;
                ev.exhaustion += sprint ? 0.2f : 0.05f;
            }
        }
        if (!body.onGround) {
            // gravity and drag (from the first frame of a jump on)
            airSpeedBefore = vel.z;
            vel.z += (-32.0f - 0.4f * vel.z) * dt;
            vel.z = std::max(vel.z, -78.0f);
            inAir = true;
        }
    }
    if (inFluid && fluid == ID_WATER) {
        // the current pushes (Entity.updateFluidHeightAndDoFluidPushing: 0.014 blocks/tick^2)
        const Vec3 flow = FluidFlowAt({ FloorI(body.pos.x), FloorI(body.pos.y), FloorI(body.pos.z - 0.9f) });
        vel.x += flow.x * 5.6f * dt;
        vel.y += flow.y * 5.6f * dt;
    }
    if (inFluid && !body.wasInFluid && vel.z < -6.0f)
        PlaySfx(SND_SPLASH, nullptr, Clamp(-vel.z / 20.0f, 0.2f, 1.0f));
    body.wasInFluid = inFluid;

    // ---- move: Z, then X, then Y
    std::vector<Aabb> boxes;
    Vec3 delta = vel * dt;
    if (inAir) // the mean of the speed before and after: a jump is as high at 30 frames a second as at 144
        delta.z = (airSpeedBefore + vel.z) * 0.5f * dt;
    Aabb pb = BodyBox(body.pos, height);
    Aabb sweep = pb;
    sweep.x0 -= std::fabs(delta.x) + 1.0f;
    sweep.x1 += std::fabs(delta.x) + 1.0f;
    sweep.y0 -= std::fabs(delta.y) + 1.0f;
    sweep.y1 += std::fabs(delta.y) + 1.0f;
    sweep.z0 -= std::fabs(delta.z) + kStepHeight + 1.0f;
    sweep.z1 += std::fabs(delta.z) + kStepHeight;
    GatherBoxes(world, sweep, boxes);

    const bool wasOnGround = body.onGround;
    Vec3 pos = body.pos;

    // vertical
    float dz = ClipZ(pb, delta.z, boxes);
    bool landed = delta.z < 0.0f && dz > delta.z + 1e-4f;
    if (delta.z > 0.0f) {
        float lim;
        if (world.Ceiling(pos, height, dz, &lim) && lim < dz)
            dz = lim;
        if (dz < delta.z - 1e-4f)
            vel.z = 0.0f; // bumped the head
    }
    float gz;
    // the host's ground: from a bit above the feet, to catch slopes and kerbs we walked into
    if (delta.z <= 0.0f && world.GroundUnder(pos, kStepHeight * 0.9f, -dz + 0.02f, &gz)) {
        const float feet = pos.z - 1.0f;
        if (gz >= feet + dz - 1e-3f) {
            dz = gz - feet;
            landed = true;
        }
    }
    pos.z += dz;
    pb = BodyBox(pos, height);

    // horizontal (boxes with Minecraft's step-up, then the host's walls)
    auto moveAxis = [&](int axis, float d) {
        if (std::fabs(d) < 1e-6f)
            return;
        float v = axis == 0 ? ClipX(pb, d, boxes) : ClipY(pb, d, boxes);
        if (std::fabs(v) < std::fabs(d) - 1e-4f && (wasOnGround || landed)) {
            // try stepping up onto a block
            Aabb up = pb;
            float rise = ClipZ(up, kStepHeight, boxes);
            up.z0 += rise;
            up.z1 += rise;
            float v2 = axis == 0 ? ClipX(up, d, boxes) : ClipY(up, d, boxes);
            if (std::fabs(v2) > std::fabs(v) + 1e-3f) {
                if (axis == 0) {
                    up.x0 += v2;
                    up.x1 += v2;
                } else {
                    up.y0 += v2;
                    up.y1 += v2;
                }
                float down = ClipZ(up, -rise, boxes);
                pos.z += rise + down;
                v = v2;
            }
        }
        float g = world.ClipHorizontal(pos, height, axis, v);
        if (std::fabs(g) < std::fabs(v))
            v = g;
        // sneaking: never walk off an edge
        if (sneak && (wasOnGround || landed)) {
            Vec3 test = pos;
            if (axis == 0)
                test.x += v;
            else
                test.y += v;
            std::vector<Aabb> tmp;
            if (!AnyGround(world, test, height, kStepHeight, tmp))
                v = 0.0f;
        }
        if (axis == 0) {
            pos.x += v;
            if (std::fabs(v) < std::fabs(d) - 1e-4f) {
                vel.x = 0.0f;
                if (sprint && in.fwd > 0.1f)
                    ev.sprintStopped = true; // running into a wall stops the sprint
            }
        } else {
            pos.y += v;
            if (std::fabs(v) < std::fabs(d) - 1e-4f)
                vel.y = 0.0f;
        }
        pb = BodyBox(pos, height);
    };
    moveAxis(0, delta.x);
    moveAxis(1, delta.y);

    // stay on slopes and stairs going down
    if (!landed && wasOnGround && vel.z <= 0.0f && !inFluid) {
        std::vector<Aabb> tmp;
        Aabb probe = pb;
        probe.z0 -= kStepHeight;
        GatherBoxes(world, probe, tmp);
        float drop = ClipZ(pb, -kStepHeight, tmp);
        float best = drop;
        if (world.GroundUnder(pos, 0.05f, kStepHeight, &gz)) {
            const float feet = pos.z - 1.0f;
            best = std::max(drop, gz - feet);
        }
        if (best > -kStepHeight + 1e-3f) {
            pos.z += best;
            landed = true;
        }
    }

    // ground contact and falls
    body.onGround = landed;
    if (body.onGround) {
        const float fall = body.fallTop - pos.z;
        if (!wasOnGround) {
            vel.z = 0.0f;
            ev.landed = true;
            if (fall > 3.0f && !body.noFallDamage && !inFluid && in.mortal) {
                ev.fallDamage = fall - 3.0f;
                ev.fallHeight = fall;
                PlaySfx(fall > 7.0f ? SND_FALL_BIG : SND_FALL_SMALL);
            } else if (body.airTime > 0.3f) {
                Footstep(world, pos, sneak);
            }
            body.noFallDamage = false;
        }
        body.fallTop = pos.z;
        body.airTime = 0.0f;
    } else {
        body.fallTop = std::max(body.fallTop, pos.z);
        body.airTime += dt;
    }

    // footsteps
    const float moved =
        std::sqrt((pos.x - body.pos.x) * (pos.x - body.pos.x) + (pos.y - body.pos.y) * (pos.y - body.pos.y));
    if (body.onGround && !inFluid) {
        body.stepDist += moved;
        if (body.stepDist > 1.6f) {
            body.stepDist = 0.0f;
            Footstep(world, pos, sneak);
        }
    }
    if (sprint && moved > 0.0f)
        ev.exhaustion += 0.1f * moved;
    ev.moved = moved;

    body.pos = pos;
}

// ---------------------------------------------------------------- creative flight / elytra
void FlySteer(Vec3& vel, const Vec3& look, float fwd, float strafe, bool fast, bool up, bool down, float dt) {
    Vec3 target = WishDir(look, fwd, strafe) * (fast ? kFlySprintSpeed : kFlySpeed);
    target.z = (up ? kFlyClimbSpeed : 0.0f) - (down ? kFlyClimbSpeed : 0.0f);
    vel = vel + (target - vel) * Clamp(dt * 8.0f, 0.0f, 1.0f);
}

bool FlyStep(Vec3& pos, Vec3& vel, float dt, bool gliding, MoveWorld& world, float* wallImpact) {
    // Minecraft's elytra pose is a 0.6 m cube; standing: feet 1 m under the body's position, 1.8 m tall
    const float zLo = gliding ? -0.3f : -1.0f, zHi = gliding ? 0.3f : 0.8f;
    const Vec3 delta = vel * dt;
    const int steps = std::clamp((int)std::ceil(delta.Length() / 0.4f), 1, 12);
    float step[3] = { delta.x / steps, delta.y / steps, delta.z / steps };
    float* velAxis[3] = { &vel.x, &vel.y, &vel.z };
    float* posAxis[3] = { &pos.x, &pos.y, &pos.z };
    bool ground = false;
    std::vector<Aabb> boxes;
    auto bodyBox = [&]() {
        return Aabb{ pos.x - kBodyHalfWidth, pos.y - kBodyHalfWidth, pos.z + zLo,
                     pos.x + kBodyHalfWidth, pos.y + kBodyHalfWidth, pos.z + zHi };
    };
    static const int kOrder[3] = { 2, 0, 1 };
    for (int s = 0; s < steps; ++s)
        for (int axis : kOrder) {
            const float d = step[axis];
            if (std::fabs(d) < 1e-6f)
                continue;
            const Aabb b = bodyBox();
            Aabb sweep = b;
            sweep.x0 -= 1.0f; sweep.x1 += 1.0f;
            sweep.y0 -= 1.0f; sweep.y1 += 1.0f;
            sweep.z0 -= 1.0f; sweep.z1 += 1.0f;
            GatherBoxes(world, sweep, boxes);
            float v = axis == 0 ? ClipX(b, d, boxes) : (axis == 1 ? ClipY(b, d, boxes) : ClipZ(b, d, boxes));
            const float g = world.ClipBox(b, axis, v);
            if (std::fabs(g) < std::fabs(v))
                v = g;
            if (std::fabs(v) < std::fabs(d) - 1e-4f) {
                if (axis == 2) {
                    if (d < 0.0f)
                        ground = true;
                } else if (wallImpact) {
                    *wallImpact = std::max(*wallImpact, std::sqrt(vel.x * vel.x + vel.y * vel.y));
                }
                *velAxis[axis] = 0.0f;
                step[axis] = 0.0f;
            }
            *posAxis[axis] += v;
        }
    if (!ground && vel.z <= 0.0f) {
        // something right under the feet?
        const Aabb b = bodyBox();
        Aabb probe = b;
        probe.z0 -= 0.2f;
        GatherBoxes(world, probe, boxes);
        if (ClipZ(b, -0.12f, boxes) > -0.12f + 1e-3f || std::fabs(world.ClipBox(b, 2, -0.12f)) < 0.12f - 1e-3f)
            ground = true;
    }
    return ground;
}

void ElytraTick(Vec3& vel, const Vec3& look, float& boostTime) {
    Vec3 v = vel * (1.0f / 20.0f); // blocks per tick
    float pitch = -std::asin(Clamp(look.z, -1.0f, 1.0f)); // > 0 looking down
    float horizLook = std::sqrt(look.x * look.x + look.y * look.y);
    float horizVel = std::sqrt(v.x * v.x + v.y * v.y);
    float cp = std::cos(pitch);
    cp = cp * cp;
    v.z += 0.08f * (-1.0f + cp * 0.75f);
    if (v.z < 0.0f && horizLook > 0.0f) {
        float d = v.z * -0.1f * cp;
        v.x += look.x * d / horizLook;
        v.y += look.y * d / horizLook;
        v.z += d;
    }
    if (pitch < 0.0f && horizLook > 0.0f) {
        float d = horizVel * -std::sin(pitch) * 0.04f;
        v.x -= look.x * d / horizLook;
        v.y -= look.y * d / horizLook;
        v.z += d * 3.2f;
    }
    if (horizLook > 0.0f) {
        v.x += (look.x / horizLook * horizVel - v.x) * 0.1f;
        v.y += (look.y / horizLook * horizVel - v.y) * 0.1f;
    }
    v.x *= 0.99f;
    v.y *= 0.99f;
    v.z *= 0.98f;
    if (boostTime > 0.0f) {
        v.x += look.x * 0.1f + (look.x * 1.5f - v.x) * 0.5f;
        v.y += look.y * 0.1f + (look.y * 1.5f - v.y) * 0.5f;
        v.z += look.z * 0.1f + (look.z * 1.5f - v.z) * 0.5f;
        boostTime -= 0.05f;
    }
    vel = v * 20.0f;
}

int ElytraAdvance(Vec3& vel, const Vec3& look, float& boostTime, float& tickAcc, float dt) {
    int ticks = 0;
    tickAcc += dt * 20.0f;
    while (tickAcc >= 1.0f) {
        tickAcc -= 1.0f;
        ElytraTick(vel, look, boostTime);
        ++ticks;
    }
    return ticks;
}

float GlideImpactDamage(float speedBefore, float speedAfter) { return (speedBefore - speedAfter) / 20.0f * 10.0f - 3.0f; }

} // namespace mc
