#include "Movement.h"

#include "CColModel.h"
#include "CColStore.h"
#include "CColPoint.h"
#include "CFireManager.h"
#include "CPad.h"
#include "CPedIntelligence.h"
#include "CPlayerPed.h"
#include "CPools.h"
#include "CTask.h"
#include "CTaskSimpleSwim.h"
#include "CVehicle.h"
#include "CWaterLevel.h"
#include "CWeapon.h"
#include "CWorld.h"
#include "common.h"
#include "safetyhook.hpp"

#include "Blocks.h"
#include "Carve.h"
#include "Collision.h"
#include "Config.h"
#include "Game.h"
#include "GtaWorld.h"
#include "Input.h"
#include "Inventory.h"
#include "Items.h"
#include "Mobs.h"
#include "Sound.h"
#include "Terrain.h"

namespace mc {

namespace {
float gLastSpaceTap = -10.0f;
float gTickAcc = 0.0f;
float gGlideTime = 0.0f;
bool gPhysicsOverridden = false;
float gDuckCooldown = 0.0f;

// ---- Minecraft player physics
bool gCtrl = false;          // our controller moves the player
CVector gPos;                // authoritative position (ped origin: feet + 1 m)
CVector gVel;                // m/s
bool gOnGround = false;
float gAirTime = 0.0f;
float gFallTop = 0.0f;       // highest point since leaving the ground
bool gNoFallDamage = false;  // wind charge flights
bool gForced = false;        // controller forced on until landing (launches)
float gJumpCooldown = 0.0f;
float gStepDist = 0.0f;
float gCarHitCooldown = 0.0f;
float gLavaTimer = 0.0f;
bool gWasInFluid = false;
bool gTaskWarned = false;

constexpr float kHalfWidth = 0.3f;
constexpr float kStepHeight = 0.6f;
// The jump speed that gives Minecraft's 1.25 block jump with our continuous gravity and drag
// (Minecraft: 0.42 blocks/tick, gravity 0.08, drag 0.98 per tick).
constexpr float kJumpSpeed = 9.2f;
constexpr float kWalk = 4.317f, kSprint = 5.612f, kSneak = 1.295f;

CVector Horizontal(CVector v) {
    v.z = 0;
    float m = v.Magnitude();
    return m > 1e-4f ? v * (1.0f / m) : CVector(0, 1, 0);
}

void OverridePhysics(CPlayerPed* ped, bool on) {
    if (on) {
        ped->bUsesCollision = false;
        ped->bApplyGravity = false;
        ped->m_vecMoveSpeed = CVector(0, 0, 0);
        gPhysicsOverridden = true;
    } else if (gPhysicsOverridden) {
        ped->bUsesCollision = true;
        ped->bApplyGravity = true;
        gPhysicsOverridden = false;
    }
}

void FaceDirection(CPlayerPed* ped, const CVector& dir) {
    CVector h = Horizontal(dir);
    float heading = std::atan2(-h.x, h.y);
    ped->m_fHeadingCurrent = heading;
    ped->m_fHeadingGoal = heading;
    ped->SetHeading(heading);
}

// GTA ray that skips our own collision boxes and ground that has been dug away
bool GtaRay(const CVector& a, const CVector& b, CColPoint& cp, CEntity*& e) {
    CVector start = a;
    const CVector d = b - a;
    const float len = d.Magnitude();
    if (len < 1e-5f)
        return false;
    const CVector dir = d * (1.0f / len);
    for (int i = 0; i < 4; ++i) {
        e = nullptr;
        if (!CWorld::ProcessLineOfSight(start, b, cp, e, true, true, false, true, false, false, false, false))
            return false;
        if (IsCollisionObject(e) || TerrainIgnoreHit(cp.m_vecPoint, e)) {
            start = cp.m_vecPoint + dir * 0.05f;
            if ((start - a).Magnitude() >= len)
                return false;
            continue;
        }
        return true;
    }
    return false;
}

void ReadMoveInput(float& fwd, float& strafe, bool consume) {
    CPad* pad = CPad::GetPad(0);
    fwd = -pad->NewState.LeftStickY / 128.0f;
    strafe = pad->NewState.LeftStickX / 128.0f;
    if (consume) {
        pad->NewState.LeftStickX = 0;
        pad->NewState.LeftStickY = 0;
    }
    pad->NewState.ButtonSquare = 0;
    pad->OldState.ButtonSquare = 0;
    pad->NewState.ButtonCross = 0;
    pad->NewState.ShockButtonL = 0;
}

bool InGtaWater(CPlayerPed* ped) {
    if (ped->bSubmergedInWater)
        return true;
    const CVector p = ped->GetPosition();
    float wl;
    return CWaterLevel::GetWaterLevelNoWaves(p.x, p.y, p.z, &wl) && wl > p.z - 0.45f;
}

// Minecraft sprinting: tap (or hold) Ctrl while walking forward, it lasts until the player stops
bool UpdateSprint(CPlayerPed* ped, bool forward, bool menuOpen) {
    if (!menuOpen && forward && (KeyPressed(VK_LCONTROL) || KeyDown(VK_LCONTROL)))
        gGame.sprintLatch = true;
    const bool hungry = gGame.gameMode == MODE_SURVIVAL && gGame.food <= 6.0f;
    if (!forward || menuOpen || KeyDown(VK_LSHIFT) || hungry)
        gGame.sprintLatch = false;
    (void)ped;
    return gGame.sprintLatch;
}

bool UsingItem() {
    return gGame.eatTimer > 0.0f || gGame.bowDraw >= 0.0f || gGame.crossbowCharge >= 0.0f || gGame.tridentCharge >= 0.0f ||
           gGame.spyglass;
}

// ---------------------------------------------------------------- collision against blocks (MC style)
struct Box {
    float x0, y0, z0, x1, y1, z1;
};

void GatherBoxes(const Box& b, std::vector<Box>& out) {
    out.clear();
    for (int z = FloorI(b.z0) - 1; z <= FloorI(b.z1); ++z)
        for (int y = FloorI(b.y0); y <= FloorI(b.y1); ++y)
            for (int x = FloorI(b.x0); x <= FloorI(b.x1); ++x) {
                if (gWorld.IsSolid(x, y, z)) {
                    out.push_back({ (float)x, (float)y, (float)z, x + 1.0f, y + 1.0f, z + 1.0f });
                    continue;
                }
                float top;
                if (TerrainCapAt(x, y, z, &top)) {
                    out.push_back({ (float)x, (float)y, (float)z, x + 1.0f, y + 1.0f, top });
                    continue;
                }
                float sb[16][6];
                const int n = CarveSkinBoxes(x, y, z, sb, 16);
                for (int i = 0; i < n; ++i)
                    out.push_back({ sb[i][0], sb[i][1], sb[i][2], sb[i][3], sb[i][4], sb[i][5] });
            }
}

bool Overlap2(float a0, float a1, float b0, float b1) { return a1 > b0 + 1e-4f && a0 < b1 - 1e-4f; }

float ClipZ(const Box& p, float dz, const std::vector<Box>& boxes) {
    for (const Box& b : boxes) {
        if (!Overlap2(p.x0, p.x1, b.x0, b.x1) || !Overlap2(p.y0, p.y1, b.y0, b.y1))
            continue;
        if (dz < 0.0f && p.z0 >= b.z1 - 1e-3f)
            dz = std::max(dz, b.z1 - p.z0);
        else if (dz > 0.0f && p.z1 <= b.z0 + 1e-3f)
            dz = std::min(dz, b.z0 - p.z1);
    }
    return dz;
}

float ClipX(const Box& p, float dx, const std::vector<Box>& boxes) {
    for (const Box& b : boxes) {
        if (!Overlap2(p.y0, p.y1, b.y0, b.y1) || !Overlap2(p.z0, p.z1, b.z0, b.z1))
            continue;
        if (dx < 0.0f && p.x0 >= b.x1 - 1e-3f)
            dx = std::max(dx, b.x1 - p.x0);
        else if (dx > 0.0f && p.x1 <= b.x0 + 1e-3f)
            dx = std::min(dx, b.x0 - p.x1);
    }
    return dx;
}

float ClipY(const Box& p, float dy, const std::vector<Box>& boxes) {
    for (const Box& b : boxes) {
        if (!Overlap2(p.x0, p.x1, b.x0, b.x1) || !Overlap2(p.z0, p.z1, b.z0, b.z1))
            continue;
        if (dy < 0.0f && p.y0 >= b.y1 - 1e-3f)
            dy = std::max(dy, b.y1 - p.y0);
        else if (dy > 0.0f && p.y1 <= b.y0 + 1e-3f)
            dy = std::min(dy, b.y0 - p.y1);
    }
    return dy;
}

Box PlayerBox(const CVector& pos, float height) {
    const float feet = pos.z - 1.0f;
    return { pos.x - kHalfWidth, pos.y - kHalfWidth, feet, pos.x + kHalfWidth, pos.y + kHalfWidth, feet + height };
}

// ---------------------------------------------------------------- collision against the GTA map (rays)
// highest GTA ground under the feet within [feet - drop, feet + rise]
bool GtaGroundUnder(const CVector& pos, float rise, float drop, float* gz) {
    static const float kOff[5][2] = { { 0, 0 }, { 0.24f, 0.24f }, { -0.24f, 0.24f }, { 0.24f, -0.24f }, { -0.24f, -0.24f } };
    const float feet = pos.z - 1.0f;
    bool found = false;
    float best = -1e9f;
    for (auto& o : kOff) {
        CColPoint cp;
        CEntity* e = nullptr;
        CVector a(pos.x + o[0], pos.y + o[1], feet + rise), b(pos.x + o[0], pos.y + o[1], feet - drop);
        if (GtaRay(a, b, cp, e) && cp.m_vecNormal.z > 0.35f && cp.m_vecPoint.z > best) {
            best = cp.m_vecPoint.z;
            found = true;
        }
    }
    if (found)
        *gz = best;
    return found;
}

// how far the body can move horizontally along `axis` (0 x, 1 y) before a GTA wall
float GtaClipHorizontal(const CVector& pos, float height, int axis, float d) {
    if (std::fabs(d) < 1e-5f)
        return d;
    const float sign = d > 0.0f ? 1.0f : -1.0f;
    const float feet = pos.z - 1.0f;
    const float heights[3] = { kStepHeight + 0.02f, std::min(1.0f, height - 0.15f), height - 0.1f };
    const float lat[3] = { -0.25f, 0.0f, 0.25f };
    float allowed = d;
    for (float h : heights)
        for (float l : lat) {
            CVector a = pos;
            a.z = feet + h;
            if (axis == 0)
                a.y += l;
            else
                a.x += l;
            CVector b = a;
            if (axis == 0)
                b.x += d + sign * kHalfWidth;
            else
                b.y += d + sign * kHalfWidth;
            CColPoint cp;
            CEntity* e = nullptr;
            if (!GtaRay(a, b, cp, e))
                continue;
            const float n = axis == 0 ? cp.m_vecNormal.x : cp.m_vecNormal.y;
            if (cp.m_vecNormal.z > 0.75f && h <= kStepHeight + 0.05f)
                continue; // a walkable slope
            (void)n;
            float dist = axis == 0 ? std::fabs(cp.m_vecPoint.x - a.x) : std::fabs(cp.m_vecPoint.y - a.y);
            float can = std::max(0.0f, dist - kHalfWidth - 0.01f) * sign;
            if (std::fabs(can) < std::fabs(allowed))
                allowed = can;
        }
    return allowed;
}

bool GtaCeiling(const CVector& pos, float height, float up, float* limit) {
    const float head = pos.z - 1.0f + height;
    CColPoint cp;
    CEntity* e = nullptr;
    if (GtaRay(CVector(pos.x, pos.y, head - 0.1f), CVector(pos.x, pos.y, head + up + 0.02f), cp, e) &&
        cp.m_vecNormal.z < -0.3f) {
        *limit = std::max(0.0f, cp.m_vecPoint.z - head - 0.01f);
        return true;
    }
    return false;
}

// how far a box can move `d` along one axis (0 x, 1 y, 2 z) before the GTA map, a car or a prop is in the way:
// rays from the middle of the box through a 3x3 grid of its leading face
float GtaClipBox(const Box& b, int axis, float d) {
    if (std::fabs(d) < 1e-6f)
        return d;
    const float sign = d > 0.0f ? 1.0f : -1.0f;
    const float lo[3] = { b.x0, b.y0, b.z0 }, hi[3] = { b.x1, b.y1, b.z1 };
    const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
    const float mid = (lo[axis] + hi[axis]) * 0.5f, half = (hi[axis] - lo[axis]) * 0.5f;
    const float in = 0.04f;
    float allowed = d;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float p[3];
            p[axis] = mid;
            p[a1] = i == 0 ? lo[a1] + in : (i == 1 ? (lo[a1] + hi[a1]) * 0.5f : hi[a1] - in);
            p[a2] = j == 0 ? lo[a2] + in : (j == 1 ? (lo[a2] + hi[a2]) * 0.5f : hi[a2] - in);
            float q[3] = { p[0], p[1], p[2] };
            q[axis] += sign * (half + std::fabs(d));
            CColPoint cp;
            CEntity* e = nullptr;
            if (!GtaRay(CVector(p[0], p[1], p[2]), CVector(q[0], q[1], q[2]), cp, e))
                continue;
            const float hit = axis == 0 ? cp.m_vecPoint.x : (axis == 1 ? cp.m_vecPoint.y : cp.m_vecPoint.z);
            const float can = std::max(0.0f, std::fabs(hit - mid) - half - 0.01f) * sign;
            if (std::fabs(can) < std::fabs(allowed))
                allowed = can;
        }
    return allowed;
}

// moves the player kinematically (creative flight, elytra): the whole body as a box against blocks and the GTA
// world, in short steps so nothing is skipped at speed. Returns true when the feet touched the ground.
bool MoveKinematic(CPlayerPed* ped, CVector& vel, float dt, float* wallImpact) {
    CVector pos = ped->GetPosition();
    // Minecraft's elytra pose is a 0.6 m cube; standing: feet 1 m under the ped's origin, 1.8 m tall
    const float zLo = gGame.gliding ? -0.3f : -1.0f, zHi = gGame.gliding ? 0.3f : 0.8f;
    const CVector delta = vel * dt;
    const int steps = std::clamp((int)std::ceil(delta.Magnitude() / 0.4f), 1, 12);
    float step[3] = { delta.x / steps, delta.y / steps, delta.z / steps };
    float* velAxis[3] = { &vel.x, &vel.y, &vel.z };
    float* posAxis[3] = { &pos.x, &pos.y, &pos.z };
    bool ground = false;
    std::vector<Box> boxes;
    auto bodyBox = [&]() {
        return Box{ pos.x - kHalfWidth, pos.y - kHalfWidth, pos.z + zLo, pos.x + kHalfWidth, pos.y + kHalfWidth, pos.z + zHi };
    };
    static const int kOrder[3] = { 2, 0, 1 };
    for (int s = 0; s < steps; ++s)
        for (int axis : kOrder) {
            const float d = step[axis];
            if (std::fabs(d) < 1e-6f)
                continue;
            const Box b = bodyBox();
            Box sweep = b;
            sweep.x0 -= 1.0f; sweep.x1 += 1.0f;
            sweep.y0 -= 1.0f; sweep.y1 += 1.0f;
            sweep.z0 -= 1.0f; sweep.z1 += 1.0f;
            GatherBoxes(sweep, boxes);
            float v = axis == 0 ? ClipX(b, d, boxes) : (axis == 1 ? ClipY(b, d, boxes) : ClipZ(b, d, boxes));
            const float g = GtaClipBox(b, axis, v);
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
        const Box b = bodyBox();
        Box probe = b;
        probe.z0 -= 0.2f;
        GatherBoxes(probe, boxes);
        if (ClipZ(b, -0.12f, boxes) > -0.12f + 1e-3f || std::fabs(GtaClipBox(b, 2, -0.12f)) < 0.12f - 1e-3f)
            ground = true;
    }
    ped->SetPosn(pos);
    ped->m_vecMoveSpeed = CVector(0, 0, 0);
    return ground;
}

// ground (blocks or GTA map) within `drop` below the feet at `pos`
bool AnyGround(const CVector& pos, float height, float drop, std::vector<Box>& tmp) {
    Box b = PlayerBox(pos, height);
    Box probe = b;
    probe.z0 -= drop;
    GatherBoxes(probe, tmp);
    if (ClipZ(b, -drop, tmp) > -drop + 1e-3f)
        return true;
    float gz;
    return GtaGroundUnder(pos, 0.05f, drop, &gz);
}

// footstep sound of what is under the feet
void Footstep(const CVector& pos, bool quiet) {
    const int x = FloorI(pos.x), y = FloorI(pos.y), z = FloorI(pos.z - 1.05f);
    int block = gWorld.GetBlock(x, y, z);
    if (!IsSolidBlock(block)) {
        CColPoint cp;
        CEntity* e = nullptr;
        if (GtaRay(pos, pos - CVector(0, 0, 1.4f), cp, e))
            block = MaterialFor(cp, e).block;
        if (block == ID_AIR)
            block = ID_STONE;
    }
    const CVector at = pos - CVector(0, 0, 1.0f);
    PlaySfx((SoundEvent)(SND_STEP_STONE + Block(block).sound), &at, quiet ? 0.08f : 0.18f, 1.0f);
}

void HitByVehicles(CPlayerPed* ped, float dt) {
    gCarHitCooldown = std::max(0.0f, gCarHitCooldown - dt);
    auto* pool = CPools::ms_pVehiclePool;
    if (!pool || gCarHitCooldown > 0.0f)
        return;
    for (int i = 0; i < pool->m_nSize; ++i) {
        CVehicle* v = pool->GetAt(i);
        if (!v || (v->GetPosition() - gPos).Magnitude() > 8.0f)
            continue;
        const CVector vv = v->m_vecMoveSpeed * 50.0f;
        const float speed = vv.Magnitude();
        if (speed < 4.0f)
            continue;
        CColModel* col = v->GetColModel();
        if (!col)
            continue;
        const CMatrix& m = *v->m_matrix;
        const CVector d = gPos - m.pos;
        const CVector l(d.x * m.right.x + d.y * m.right.y + d.z * m.right.z, d.x * m.up.x + d.y * m.up.y + d.z * m.up.z,
                        d.x * m.at.x + d.y * m.at.y + d.z * m.at.z);
        const CVector& mn = col->m_boundBox.m_vecMin;
        const CVector& mx = col->m_boundBox.m_vecMax;
        const float e = 0.35f;
        if (l.x < mn.x - e || l.x > mx.x + e || l.y < mn.y - e || l.y > mx.y + e || l.z < mn.z - 1.0f || l.z > mx.z + 0.8f)
            continue;
        gVel = vv * 0.9f + CVector(0, 0, 4.0f + speed * 0.15f);
        gOnGround = false;
        gCarHitCooldown = 0.6f;
        if (gGame.gameMode == MODE_SURVIVAL)
            CWeapon::GenerateDamageEvent(ped, v, WEAPONTYPE_RAMMEDBYCAR, (int)(speed * 2.5f), (ePedPieceTypes)3, 0);
        PlaySfx(SND_HURT);
        return;
    }
}

bool PlayerInControl(CPlayerPed* ped) {
    if (ped->m_fHealth <= 0.0f || ped->bInVehicle)
        return false;
    CPad* pad = CPad::GetPad(0);
    if (pad->DisablePlayerControls)
        return false;
    if (!ped->m_pIntelligence)
        return false;
    CTask* t = ped->m_pIntelligence->m_TaskMgr.GetSimplestActiveTask();
    if (!t)
        return true;
    const int id = t->GetId();
    if (id != TASK_SIMPLE_PLAYER_ON_FOOT && !gTaskWarned && !gCtrl) {
        gTaskWarned = true;
        Log("Movement: GTA task %d is in charge of the player", id);
    }
    return id == TASK_SIMPLE_PLAYER_ON_FOOT;
}

void EndController(CPlayerPed* ped, bool keepVelocity) {
    if (!gCtrl)
        return;
    gCtrl = false;
    gForced = false;
    gGame.jumping = false;
    OverridePhysics(ped, false);
    ped->m_vecMoveSpeed = keepVelocity ? gVel * (1.0f / 50.0f) : CVector(0, 0, 0);
}

// the Minecraft player: walking, sprinting, sneaking, jumping, swimming in block water, falling
void Controller(float dt, CPlayerPed* ped, bool menuOpen) {
    if (!gCtrl) {
        gCtrl = true;
        gPos = ped->GetPosition();
        gVel = ped->m_vecMoveSpeed * 50.0f;
        gOnGround = ped->bIsStanding;
        gFallTop = gPos.z;
        gAirTime = 0.0f;
    } else if ((ped->GetPosition() - gPos).Magnitude() > 1.5f) {
        gPos = ped->GetPosition(); // moved by the game (script, teleport)
        gVel = CVector(0, 0, 0);
        gFallTop = gPos.z;
    }
    gJumpCooldown = std::max(0.0f, gJumpCooldown - dt);
    gDuckCooldown = std::max(0.0f, gDuckCooldown - dt);

    // the map's collision is still streaming in (after a door, a teleport, fast travel): wait, like GTA does,
    // instead of falling through the ground that is not there yet
    if (!CColStore::HasCollisionLoaded(gPos, ped->m_nAreaCode)) {
        gVel = CVector(0, 0, 0);
        gFallTop = gPos.z;
        OverridePhysics(ped, true);
        ped->SetPosn(gPos);
        ped->m_vecMoveSpeed = CVector(0, 0, 0);
        gGame.flyVel = gVel;
        gGame.jumping = false;
        return;
    }

    float fwd, strafe;
    ReadMoveInput(fwd, strafe, false); // the stick stays for GTA's walking animation; we snap the position back
    if (menuOpen)
        fwd = strafe = 0.0f;
    CPad* pad = CPad::GetPad(0);
    const bool sneak = !menuOpen && KeyDown(VK_LSHIFT);
    const bool sprint = UpdateSprint(ped, fwd > 0.1f, menuOpen) && !sneak;
    gGame.sprinting = sprint;
    pad->NewState.ButtonCross = sprint ? 255 : 0; // GTA's sprint animation
    // GTA crouches with one key press, Minecraft holds Shift
    if (sneak != (bool)ped->bIsDucking && gDuckCooldown <= 0.0f && gOnGround) {
        pad->NewState.ShockButtonL = 255;
        pad->OldState.ShockButtonL = 0;
        gDuckCooldown = 0.4f;
    }
    const float height = sneak ? 1.5f : 1.8f;

    CVector f = Horizontal(gGame.lookDir);
    CVector r(f.y, -f.x, 0.0f);
    CVector wish = f * fwd + r * strafe;
    float wm = wish.Magnitude();
    if (wm > 1.0f)
        wish = wish * (1.0f / wm);

    int fluid = ID_AIR;
    const float depth = FluidDepth(gPos - CVector(0, 0, 1.0f), height, &fluid);
    const bool inFluid = depth > 0.0f;

    float speed = sprint ? kSprint : (sneak ? kSneak : kWalk);
    if (UsingItem())
        speed *= 0.2f;
    if (inFluid)
        speed = fluid == ID_LAVA ? 1.2f : (sprint ? 3.0f : 2.0f);

    // horizontal: Minecraft friction (0.546 per tick on the ground, 0.91 in the air, 0.8 in water)
    const float base = inFluid ? (fluid == ID_LAVA ? 0.5f : 0.8f) : (gOnGround ? 0.546f : 0.91f);
    const float k = 1.0f - std::pow(base, dt * 20.0f);
    gVel.x += (wish.x * speed - gVel.x) * k;
    gVel.y += (wish.y * speed - gVel.y) * k;

    // vertical
    const bool space = !menuOpen && KeyDown(VK_SPACE);
    if (inFluid) {
        float az = -8.0f + (space ? 24.0f : 0.0f) - (sneak ? 8.0f : 0.0f);
        gVel.z = gVel.z * std::pow(base, dt * 20.0f) + az * dt;
        // climb out at the surface next to a wall
        if (space && depth < 0.6f) {
            std::vector<Box> tmp;
            Box b = PlayerBox(gPos, height);
            b.x0 += wish.x * 0.3f;
            b.x1 += wish.x * 0.3f;
            b.y0 += wish.y * 0.3f;
            b.y1 += wish.y * 0.3f;
            GatherBoxes(b, tmp);
            if (!tmp.empty())
                gVel.z = std::max(gVel.z, 6.0f);
        }
        gFallTop = gPos.z; // water stops falls
    } else if (!gOnGround) {
        gVel.z += (-32.0f - 0.4f * gVel.z) * dt;
        gVel.z = std::max(gVel.z, -78.0f);
    } else {
        gVel.z = 0.0f;
        if (space && gJumpCooldown <= 0.0f) {
            gVel.z = kJumpSpeed;
            if (sprint) // sprint jump boost (0.2 blocks/tick in the facing direction)
                gVel += f * 4.0f;
            gOnGround = false;
            gJumpCooldown = 0.1f;
            gGame.exhaustion += sprint ? 0.2f : 0.05f;
        }
    }
    if (inFluid && fluid == ID_WATER) {
        // the current pushes (Entity.updateFluidHeightAndDoFluidPushing: 0.014 blocks/tick^2)
        const CVector flow = FluidFlowAt({ FloorI(gPos.x), FloorI(gPos.y), FloorI(gPos.z - 0.9f) });
        gVel.x += flow.x * 5.6f * dt;
        gVel.y += flow.y * 5.6f * dt;
    }
    if (inFluid && !gWasInFluid && gVel.z < -6.0f)
        PlaySfx(SND_SPLASH, nullptr, Clamp(-gVel.z / 20.0f, 0.2f, 1.0f));
    gWasInFluid = inFluid;

    // ---- move: Z, then X, then Y
    std::vector<Box> boxes;
    const CVector delta = gVel * dt;
    Box pb = PlayerBox(gPos, height);
    Box sweep = pb;
    sweep.x0 -= std::fabs(delta.x) + 1.0f;
    sweep.x1 += std::fabs(delta.x) + 1.0f;
    sweep.y0 -= std::fabs(delta.y) + 1.0f;
    sweep.y1 += std::fabs(delta.y) + 1.0f;
    sweep.z0 -= std::fabs(delta.z) + kStepHeight + 1.0f;
    sweep.z1 += std::fabs(delta.z) + kStepHeight;
    GatherBoxes(sweep, boxes);

    const bool wasOnGround = gOnGround;
    CVector pos = gPos;

    // vertical
    float dz = ClipZ(pb, delta.z, boxes);
    bool landed = delta.z < 0.0f && dz > delta.z + 1e-4f;
    if (delta.z > 0.0f) {
        float lim;
        if (GtaCeiling(pos, height, dz, &lim) && lim < dz)
            dz = lim;
        if (dz < delta.z - 1e-4f)
            gVel.z = 0.0f; // bumped the head
    }
    float gz;
    // GTA ground: rays from a bit above the feet catch slopes and kerbs we walked into
    if (delta.z <= 0.0f && GtaGroundUnder(pos, kStepHeight * 0.9f, -dz + 0.02f, &gz)) {
        const float feet = pos.z - 1.0f;
        if (gz >= feet + dz - 1e-3f) {
            dz = gz - feet;
            landed = true;
        }
    }
    pos.z += dz;
    pb = PlayerBox(pos, height);

    // horizontal (voxels with Minecraft's step-up, GTA walls with rays)
    auto moveAxis = [&](int axis, float d) {
        if (std::fabs(d) < 1e-6f)
            return;
        float v = axis == 0 ? ClipX(pb, d, boxes) : ClipY(pb, d, boxes);
        if (std::fabs(v) < std::fabs(d) - 1e-4f && (wasOnGround || landed)) {
            // try stepping up onto a block
            Box up = pb;
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
        float g = GtaClipHorizontal(pos, height, axis, v);
        if (std::fabs(g) < std::fabs(v))
            v = g;
        // sneaking: never walk off an edge
        if (sneak && (wasOnGround || landed)) {
            CVector test = pos;
            if (axis == 0)
                test.x += v;
            else
                test.y += v;
            std::vector<Box> tmp;
            if (!AnyGround(test, height, kStepHeight, tmp))
                v = 0.0f;
        }
        if (axis == 0) {
            pos.x += v;
            if (std::fabs(v) < std::fabs(d) - 1e-4f) {
                gVel.x = 0.0f;
                if (sprint && fwd > 0.1f)
                    gGame.sprintLatch = false; // running into a wall stops the sprint
            }
        } else {
            pos.y += v;
            if (std::fabs(v) < std::fabs(d) - 1e-4f)
                gVel.y = 0.0f;
        }
        pb = PlayerBox(pos, height);
    };
    moveAxis(0, delta.x);
    moveAxis(1, delta.y);

    // stay on slopes and stairs going down
    if (!landed && wasOnGround && gVel.z <= 0.0f && !inFluid) {
        std::vector<Box> tmp;
        Box probe = pb;
        probe.z0 -= kStepHeight;
        GatherBoxes(probe, tmp);
        float drop = ClipZ(pb, -kStepHeight, tmp);
        float best = drop;
        if (GtaGroundUnder(pos, 0.05f, kStepHeight, &gz)) {
            const float feet = pos.z - 1.0f;
            best = std::max(drop, gz - feet);
        }
        if (best > -kStepHeight + 1e-3f) {
            pos.z += best;
            landed = true;
        }
    }

    // ground contact and falls
    gOnGround = landed;
    if (gOnGround) {
        const float fall = gFallTop - pos.z;
        if (!wasOnGround) {
            gVel.z = 0.0f;
            if (fall > 3.0f && !gNoFallDamage && !inFluid && gGame.gameMode == MODE_SURVIVAL) {
                const float maxH = ped->m_fMaxHealth > 1.0f ? ped->m_fMaxHealth : 100.0f;
                CWeapon::GenerateDamageEvent(ped, nullptr, WEAPONTYPE_FALL, (int)((fall - 3.0f) * maxH / 20.0f),
                                             (ePedPieceTypes)3, 0);
                NoteDamage(STR_DEATH_FALL);
                PlaySfx(fall > 7.0f ? SND_FALL_BIG : SND_FALL_SMALL);
            } else if (gAirTime > 0.3f) {
                Footstep(pos, sneak);
            }
            gNoFallDamage = false;
            gForced = false;
        }
        gFallTop = pos.z;
        gAirTime = 0.0f;
    } else {
        gFallTop = std::max(gFallTop, pos.z);
        gAirTime += dt;
    }

    // footsteps
    const float moved = std::sqrt((pos.x - gPos.x) * (pos.x - gPos.x) + (pos.y - gPos.y) * (pos.y - gPos.y));
    if (gOnGround && !inFluid) {
        gStepDist += moved;
        if (gStepDist > 1.6f) {
            gStepDist = 0.0f;
            Footstep(pos, sneak);
        }
    }
    if (gGame.sprinting && moved > 0.0f)
        gGame.exhaustion += 0.1f * moved;

    // lava burns
    if (inFluid && fluid == ID_LAVA) {
        gLavaTimer -= dt;
        if (gLavaTimer <= 0.0f) {
            gLavaTimer = 0.5f;
            if (gGame.gameMode == MODE_SURVIVAL && !ped->bFireProof) {
                CWeapon::GenerateDamageEvent(ped, nullptr, WEAPONTYPE_FTHROWER, 20, (ePedPieceTypes)3, 0);
                NoteDamage(STR_DEATH_LAVA);
                gFireManager.StartFire(ped, nullptr, 1.0f, 1, 7000, 1);
            }
        }
    }

    gPos = pos;
    HitByVehicles(ped, dt);
    OverridePhysics(ped, true);
    ped->SetPosn(gPos);
    ped->m_vecMoveSpeed = CVector(0, 0, 0);
    ped->bIsStanding = gOnGround;
    gGame.flyVel = gVel;
    gGame.jumping = !gOnGround;
}

// ---------------------------------------------------------------- creative flight / elytra / riding
void Fly(float dt, CPlayerPed* ped, bool frozen) {
    float fwd, strafe;
    ReadMoveInput(fwd, strafe, true);
    if (frozen)
        fwd = strafe = 0.0f; // a menu is open: hover in place
    CVector f = Horizontal(gGame.lookDir);
    CVector r(f.y, -f.x, 0.0f);
    const bool fast = UpdateSprint(ped, fwd > 0.1f, frozen);
    gGame.sprinting = fast;
    float speed = fast ? 21.6f : 10.9f;
    CVector target = f * fwd + r * strafe;
    float m = target.Magnitude();
    if (m > 1.0f)
        target = target * (1.0f / m);
    target = target * speed;
    target.z = frozen ? 0.0f : (KeyDown(VK_SPACE) ? 7.5f : 0.0f) - (KeyDown(VK_LSHIFT) ? 7.5f : 0.0f);
    gGame.flyVel = gGame.flyVel + (target - gGame.flyVel) * Clamp(dt * 8.0f, 0.0f, 1.0f);
    OverridePhysics(ped, true);
    if (fwd != 0.0f || strafe != 0.0f)
        FaceDirection(ped, gGame.lookDir);
    const float sinking = gGame.flyVel.z;
    bool ground = MoveKinematic(ped, gGame.flyVel, dt, nullptr);
    if (ground && sinking < -0.5f)
        StopFlying(ped); // landed
}

void ElytraTick() {
    CVector look = gGame.lookDir;
    CVector v = gGame.flyVel * (1.0f / 20.0f); // blocks per tick
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
    if (gGame.boostTime > 0.0f) {
        v.x += look.x * 0.1f + (look.x * 1.5f - v.x) * 0.5f;
        v.y += look.y * 0.1f + (look.y * 1.5f - v.y) * 0.5f;
        v.z += look.z * 0.1f + (look.z * 1.5f - v.z) * 0.5f;
        gGame.boostTime -= 0.05f;
    }
    gGame.flyVel = v * 20.0f;
}

void Glide(float dt, CPlayerPed* ped) {
    float fwd, strafe;
    ReadMoveInput(fwd, strafe, true);
    OverridePhysics(ped, true);
    gTickAcc += dt * 20.0f;
    while (gTickAcc >= 1.0f) {
        gTickAcc -= 1.0f;
        ElytraTick();
        if (gGame.gameMode == MODE_SURVIVAL && ++gGlideTime >= 20.0f) {
            gGlideTime = 0.0f;
            ItemStack& e = gInv.armor[ARMOR_CHEST];
            if (e.id == ID_ELYTRA)
                e.damage++;
        }
    }
    FaceDirection(ped, gGame.flyVel);
    float impact = 0.0f;
    float before = std::sqrt(gGame.flyVel.x * gGame.flyVel.x + gGame.flyVel.y * gGame.flyVel.y);
    bool ground = MoveKinematic(ped, gGame.flyVel, dt, &impact);
    if (impact > 0.0f && gGame.gameMode == MODE_SURVIVAL) {
        float after = std::sqrt(gGame.flyVel.x * gGame.flyVel.x + gGame.flyVel.y * gGame.flyVel.y);
        float hearts = (before - after) / 20.0f * 10.0f - 3.0f; // Minecraft's fly-into-wall damage
        if (hearts > 0.0f) {
            float maxH = ped->m_fMaxHealth > 1.0f ? ped->m_fMaxHealth : 100.0f;
            CWeapon::GenerateDamageEvent(ped, nullptr, WEAPONTYPE_FALL, (int)(hearts * maxH / 20.0f),
                                         (ePedPieceTypes)3, 0);
            PlaySfx(SND_HURT);
        }
    }
    float speed = gGame.flyVel.Magnitude();
    SetLoopSfx(SND_ELYTRA_FLYING, true, Clamp(speed / 30.0f, 0.1f, 1.0f));
    if (ground || !gInv.HasElytra() || InGtaWater(ped))
        StopFlying(ped);
}

void RideUpdate(float dt, CPlayerPed* ped, bool menuOpen) {
    float fwd, strafe;
    ReadMoveInput(fwd, strafe, true);
    if (menuOpen)
        fwd = strafe = 0.0f;
    if (!menuOpen && KeyPressed(VK_LSHIFT)) {
        StopRiding(ped);
        return;
    }
    CVector f = Horizontal(gGame.lookDir);
    CVector r(f.y, -f.x, 0.0f);
    CVector wish = f * fwd + r * strafe;
    float m = wish.Magnitude();
    if (m > 1.0f)
        wish = wish * (1.0f / m);
    const bool sprint = UpdateSprint(ped, fwd > 0.1f, menuOpen);
    gGame.sprinting = sprint;
    CVector seat;
    float yaw = 0.0f;
    if (!MobRide(gGame.ridingMob, wish * (sprint ? 6.5f : 4.0f), dt, &seat, &yaw)) {
        StopRiding(ped);
        return;
    }
    OverridePhysics(ped, true);
    ped->SetPosn(seat);
    ped->m_vecMoveSpeed = CVector(0, 0, 0);
    ped->m_fHeadingCurrent = ped->m_fHeadingGoal = yaw;
    ped->SetHeading(yaw);
}
} // namespace

bool PlayerOnGround(CPlayerPed* ped) { return gCtrl ? gOnGround : ped->bIsStanding; }

bool MovementControllerActive() { return gCtrl; }

CVector PlayerVelocity(CPlayerPed* ped) {
    if (!ped)
        return CVector(0, 0, 0);
    if (ped->bInVehicle && ped->m_pVehicle)
        return ped->m_pVehicle->m_vecMoveSpeed * 50.0f; // GTA keeps it per 1/50 s
    if (gGame.flying || gGame.gliding)
        return gGame.flyVel;
    if (gCtrl)
        return gVel;
    return ped->m_vecMoveSpeed * 50.0f;
}

void StopFlying(CPlayerPed* ped) {
    if (gGame.gliding)
        SetLoopSfx(SND_ELYTRA_FLYING, false);
    gGame.flying = false;
    gGame.gliding = false;
    gGame.boostTime = 0.0f;
    if (ped)
        EndController(ped, false);
    gGame.jumping = false;
    gNoFallDamage = false;
    if (ped) {
        OverridePhysics(ped, false);
        ped->m_vecMoveSpeed = CVector(0, 0, 0);
    }
    gGame.flyVel = CVector(0, 0, 0);
}

void StopRiding(CPlayerPed* ped) {
    if (!gGame.ridingMob)
        return;
    gGame.ridingMob = 0;
    if (ped) {
        OverridePhysics(ped, false);
        CVector p = ped->GetPosition();
        CVector side = Horizontal(CVector(gGame.lookDir.y, -gGame.lookDir.x, 0.0f));
        ped->SetPosn(p + side * 0.9f + CVector(0, 0, 0.2f));
        ped->m_vecMoveSpeed = CVector(0, 0, 0);
    }
}

void LaunchPlayer(CPlayerPed* ped, const CVector& vel) {
    if (!ped || ped->bInVehicle || ped->m_fHealth <= 0.0f)
        return;
    if (gGame.ridingMob)
        StopRiding(ped);
    if (gGame.flying || gGame.gliding) {
        gGame.flyVel += vel;
        return;
    }
    if (!gCtrl) {
        gCtrl = true;
        gPos = ped->GetPosition();
        gVel = ped->m_vecMoveSpeed * 50.0f;
    }
    gForced = true;
    gVel += vel;
    gOnGround = false;
    gNoFallDamage = true;
    gFallTop = gPos.z;
}

bool FireworkBoost() {
    if (!gGame.gliding)
        return false;
    gGame.boostTime = 1.0f + Rand01() * 0.6f;
    PlaySfx(SND_FIREWORK_LAUNCH);
    return true;
}

void UpdateMovement(float dt, CPlayerPed* ped) {
    bool onFoot = !ped->bInVehicle && ped->m_fHealth > 0.0f;
    gGame.swimming = ped->m_pIntelligence && ped->m_pIntelligence->GetTaskSwim() != nullptr;
    if (gGame.swimming) {
        const CVector p = ped->GetPosition();
        float wl;
        gGame.underwater = CWaterLevel::GetWaterLevelNoWaves(p.x, p.y, p.z, &wl) && wl > p.z + 0.55f;
    } else {
        gGame.underwater = false;
    }
    if (!onFoot || !gGame.enabled) {
        if (gGame.flying || gGame.gliding || gCtrl)
            StopFlying(ped);
        if (gGame.ridingMob)
            StopRiding(ped);
        gGame.sprinting = false;
        gGame.sprintLatch = false;
        return;
    }
    if (gGame.gameMode != MODE_CREATIVE && gGame.flying)
        StopFlying(ped);
    const bool menuOpen = gGame.screen != SCREEN_NONE;

    if (gGame.ridingMob) {
        EndController(ped, false);
        RideUpdate(dt, ped, menuOpen);
        return;
    }

    const bool airborne = gCtrl ? (!gOnGround && gAirTime > 0.05f) : !ped->bIsStanding;
    if (!menuOpen && KeyPressed(VK_SPACE)) {
        // creative: Space twice flies (in the water too, like Minecraft)
        if (gGame.gameMode == MODE_CREATIVE && !gGame.gliding) {
            if (gGame.age - gLastSpaceTap < 0.3f) {
                if (gGame.flying) {
                    StopFlying(ped);
                } else {
                    gGame.flyVel = gCtrl ? gVel : CVector(0, 0, 0);
                    gGame.flyVel.z = gGame.swimming || InGtaWater(ped) ? 7.5f : 3.0f; // out of the water first
                    EndController(ped, false);
                    gGame.flying = true;
                }
                gLastSpaceTap = -10.0f;
            } else {
                gLastSpaceTap = gGame.age;
            }
        }
        // elytra: press Space again while in the air
        if (!gGame.flying && !gGame.gliding && gInv.HasElytra() && airborne && !InGtaWater(ped) && !gGame.swimming) {
            CVector v = gCtrl ? gVel : ped->m_vecMoveSpeed * 50.0f;
            EndController(ped, false);
            gGame.gliding = true;
            gGame.flyVel = v;
            gTickAcc = 0.0f;
            PlaySfx(SND_EQUIP_ELYTRA);
        }
    }
    if (gGame.flying) {
        Fly(dt, ped, menuOpen);
        return;
    }
    if (gGame.gliding) {
        gGame.sprinting = false;
        Glide(dt, ped);
        return;
    }

    // Minecraft physics: always (setting), near dug holes, or while thrown through the air
    const CVector p = ped->GetPosition();
    const bool underground = TerrainCameraUnderground(p);
    const bool want = (gConfig.minecraftPhysics || gForced || TerrainNear(p, 3.0f) || underground) && !InGtaWater(ped) &&
                      !gGame.swimming && PlayerInControl(ped);
    if (want) {
        Controller(dt, ped, menuOpen);
        return;
    }
    EndController(ped, true);
    CPad* pad = CPad::GetPad(0);
    if (gConfig.minecraftControls && (InGtaWater(ped) || gGame.swimming)) {
        // swimming: Ctrl swims fast like Minecraft's sprint (GTA's own sprint key is Space, which only rises here)
        gGame.sprinting = UpdateSprint(ped, pad->NewState.LeftStickY < -20, menuOpen);
    } else if (gConfig.minecraftControls) {
        // GTA moves the player, but with Minecraft keys
        pad->NewState.ButtonSquare = 0;
        pad->OldState.ButtonSquare = 0;
        const bool forward = pad->NewState.LeftStickY < -20;
        const bool sprint = UpdateSprint(ped, forward, menuOpen);
        gGame.sprinting = sprint;
        pad->NewState.ButtonCross = sprint ? 255 : 0;
    } else {
        gGame.sprinting = pad->NewState.ButtonCross != 0 && (pad->NewState.LeftStickX != 0 || pad->NewState.LeftStickY != 0);
    }
}

namespace {
SafetyHookInline gWorldProcessHook;

// CWorld::Process moves every entity; GTA's walking animation moved the player too. Our position counts, and it
// must be back before the doors (CEntryExitManager) and the camera run later in the same frame.
void AfterWorldProcess() {
    if (!gCtrl)
        return;
    CPlayerPed* ped = FindPlayerPed();
    if (!ped || ped->bInVehicle || ped->m_fHealth <= 0.0f)
        return;
    if ((ped->GetPosition() - gPos).Magnitude() < 1.5f) {
        ped->SetPosn(gPos);
        ped->m_vecMoveSpeed = CVector(0, 0, 0);
    } else {
        gPos = ped->GetPosition(); // moved by the game itself
        gVel = CVector(0, 0, 0);
        gFallTop = gPos.z;
    }
}

void __cdecl HookWorldProcess() {
    gWorldProcessHook.ccall<void>();
    AfterWorldProcess();
}
} // namespace

void InstallMovementHooks() {
    static bool done = false;
    if (done)
        return;
    done = true;
    gWorldProcessHook = safetyhook::create_inline(reinterpret_cast<void*>(0x5684A0), reinterpret_cast<void*>(&HookWorldProcess));
    Log("Hooks: world process %s", gWorldProcessHook ? "ok" : "FAILED");
}

void MovementAfterProcess(CPlayerPed* ped) {
    if (!gCtrl || !ped)
        return;
    // doors, interiors, scripts and respawns put the player somewhere else: follow them, never pull back
    if (ped->bInVehicle || (ped->GetPosition() - gPos).Magnitude() > 0.5f) {
        gPos = ped->GetPosition();
        gVel = CVector(0, 0, 0);
        gFallTop = gPos.z;
        return;
    }
    if (!gWorldProcessHook) {
        ped->SetPosn(gPos);
        ped->m_vecMoveSpeed = CVector(0, 0, 0);
    }
}

void LateMovementInput(CPlayerPed* ped) {
    if (!gConfig.minecraftControls || !ped || ped->bInVehicle || gGame.screen != SCREEN_NONE)
        return;
    if (!gGame.swimming || gGame.flying || gGame.gliding || gGame.ridingMob)
        return;
    // GTA's swim task: jump = back to the surface, fire = dive, sprint = swim fast
    CPad* pad = CPad::GetPad(0);
    pad->NewState.ButtonCross = gGame.sprinting ? 255 : 0;
    pad->NewState.ButtonSquare = 0;
    pad->OldState.ButtonSquare = 0;
    if (KeyDown(VK_SPACE) && gGame.underwater)
        pad->NewState.ButtonSquare = 255;
    if (KeyPressed(VK_LSHIFT) && !gGame.underwater) {
        pad->NewState.ButtonCircle = 255;
        pad->OldState.ButtonCircle = 0;
    }
}

} // namespace mc
