#include "Movement.h"

#include "CColModel.h"
#include "CColStore.h"
#include "CColPoint.h"
#include "CFireManager.h"
#include "CPad.h"
#include "CPlayerPed.h"
#include "CPools.h"
#include "CTask.h"
#include "CVehicle.h"
#include "CWaterLevel.h"
#include "CWeapon.h"
#include "CWorld.h"
#include "common.h"
#include "safetyhook.hpp"

#include "Beds.h"
#include "Carve.h"
#include "Collision.h"
#include "Config.h"
#include "Game.h"
#include "GtaWorld.h"
#include "Controls.h"
#include "Inventory.h"
#include "Mobs.h"
#include "Physics.h"
#include "Sound.h"
#include "Terrain.h"

// The GTA side of the player's movement: keys and pad, the GTA map as obstacles (collision rays), and putting the
// result on the ped. How a Minecraft body moves is the core's business (src/core/Physics.h).

namespace mc {

namespace {
float gLastSpaceTap = -10.0f;
float gTickAcc = 0.0f;
float gGlideTime = 0.0f;
bool gPhysicsOverridden = false;
float gDuckCooldown = 0.0f;

// ---- Minecraft player physics
bool gCtrl = false;          // our controller moves the player
Body gBody;                  // where the player is and how he moves (position = ped origin: feet + 1 m)
bool gForced = false;        // controller forced on until landing (launches)
float gCarHitCooldown = 0.0f;
bool gTaskWarned = false;

CVector Horizontal(const CVector& v) { return ToGta(HorizontalDir(v)); }

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

// Minecraft sprinting (the rule itself: UpdateSprintLatch)
bool UpdateSprint(bool forward, bool menuOpen) {
    const bool hungry = gGame.gameMode == MODE_SURVIVAL && TooHungryToSprint();
    return UpdateSprintLatch(gGame.sprintLatch, forward, ActionPressed(ACT_SPRINT) || ActionDown(ACT_SPRINT),
                             menuOpen || ActionDown(ACT_SNEAK) || hungry);
}

bool UsingItem() {
    return gSurvival.eatTimer > 0.0f || gGame.bowDraw >= 0.0f || gGame.crossbowCharge >= 0.0f || gGame.tridentCharge >= 0.0f ||
           gGame.spyglass;
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
                b.x += d + sign * kBodyHalfWidth;
            else
                b.y += d + sign * kBodyHalfWidth;
            CColPoint cp;
            CEntity* e = nullptr;
            if (!GtaRay(a, b, cp, e))
                continue;
            const float n = axis == 0 ? cp.m_vecNormal.x : cp.m_vecNormal.y;
            if (cp.m_vecNormal.z > 0.75f && h <= kStepHeight + 0.05f)
                continue; // a walkable slope
            (void)n;
            float dist = axis == 0 ? std::fabs(cp.m_vecPoint.x - a.x) : std::fabs(cp.m_vecPoint.y - a.y);
            float can = std::max(0.0f, dist - kBodyHalfWidth - 0.01f) * sign;
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
float GtaClipBox(const Aabb& b, int axis, float d) {
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

// What the GTA map puts in the way of the Minecraft body: the answers the core's movement rules ask for.
struct GtaMoveWorld : MoveWorld {
    // dug ground and broken buildings leave boxes of their own where there is no block
    void CellBoxes(int x, int y, int z, std::vector<Aabb>& out) override {
        float top;
        if (TerrainCapAt(x, y, z, &top)) {
            out.push_back({ (float)x, (float)y, (float)z, x + 1.0f, y + 1.0f, top });
            return;
        }
        float sb[16][6];
        const int n = CarveSkinBoxes(x, y, z, sb, 16);
        for (int i = 0; i < n; ++i)
            out.push_back({ sb[i][0], sb[i][1], sb[i][2], sb[i][3], sb[i][4], sb[i][5] });
    }
    bool GroundUnder(const Vec3& pos, float rise, float drop, float* z) override {
        return GtaGroundUnder(ToGta(pos), rise, drop, z);
    }
    float ClipHorizontal(const Vec3& pos, float height, int axis, float d) override {
        return GtaClipHorizontal(ToGta(pos), height, axis, d);
    }
    bool Ceiling(const Vec3& pos, float height, float up, float* limit) override {
        return GtaCeiling(ToGta(pos), height, up, limit);
    }
    float ClipBox(const Aabb& b, int axis, float d) override { return GtaClipBox(b, axis, d); }
    // the GTA ground under the feet as a block (stone, grass, wood...)
    bool GroundBlock(const Vec3& pos, int* block) override {
        const CVector p = ToGta(pos);
        CColPoint cp;
        CEntity* e = nullptr;
        if (!GtaRay(p, p - CVector(0, 0, 1.4f), cp, e))
            return false;
        *block = MaterialFor(cp, e).block;
        return true;
    }
};
GtaMoveWorld gMap;

// moves the player kinematically (creative flight, elytra): the whole body as a box against blocks and the GTA
// world. Returns true when the feet touched the ground.
bool MoveKinematic(CPlayerPed* ped, Vec3& vel, float dt, float* wallImpact) {
    Vec3 pos = ped->GetPosition();
    const bool ground = FlyStep(pos, vel, dt, gGame.gliding, gMap, wallImpact);
    ped->SetPosn(ToGta(pos));
    ped->m_vecMoveSpeed = CVector(0, 0, 0);
    return ground;
}

void HitByVehicles(CPlayerPed* ped, float dt) {
    gCarHitCooldown = std::max(0.0f, gCarHitCooldown - dt);
    auto* pool = CPools::ms_pVehiclePool;
    if (!pool || gCarHitCooldown > 0.0f)
        return;
    const CVector me = ToGta(gBody.pos);
    for (int i = 0; i < pool->m_nSize; ++i) {
        CVehicle* v = pool->GetAt(i);
        if (!v || (v->GetPosition() - me).Magnitude() > 8.0f)
            continue;
        const CVector vv = v->m_vecMoveSpeed * 50.0f;
        const float speed = vv.Magnitude();
        if (speed < 4.0f)
            continue;
        CColModel* col = v->GetColModel();
        if (!col)
            continue;
        const CMatrix& m = *v->m_matrix;
        const CVector d = me - m.pos;
        const CVector l(d.x * m.right.x + d.y * m.right.y + d.z * m.right.z, d.x * m.up.x + d.y * m.up.y + d.z * m.up.z,
                        d.x * m.at.x + d.y * m.at.y + d.z * m.at.z);
        const CVector& mn = col->m_boundBox.m_vecMin;
        const CVector& mx = col->m_boundBox.m_vecMax;
        const float e = 0.35f;
        if (l.x < mn.x - e || l.x > mx.x + e || l.y < mn.y - e || l.y > mx.y + e || l.z < mn.z - 1.0f || l.z > mx.z + 0.8f)
            continue;
        gBody.vel = vv * 0.9f + CVector(0, 0, 4.0f + speed * 0.15f);
        gBody.onGround = false;
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
    ped->m_vecMoveSpeed = keepVelocity ? ToGta(gBody.vel) * (1.0f / 50.0f) : CVector(0, 0, 0);
}

// the Minecraft player on foot: reads the keys, lets the core move the body (WalkStep) and puts the ped there
void Controller(float dt, CPlayerPed* ped, bool menuOpen) {
    if (!gCtrl) {
        gCtrl = true;
        gBody.Place(ped->GetPosition(), ped->m_vecMoveSpeed * 50.0f, ped->bIsStanding);
    } else if ((ped->GetPosition() - ToGta(gBody.pos)).Magnitude() > 1.5f) {
        gBody.pos = ped->GetPosition(); // moved by the game (script, teleport)
        gBody.vel = Vec3();
        gBody.fallTop = gBody.pos.z;
    }
    gDuckCooldown = std::max(0.0f, gDuckCooldown - dt);

    // the map's collision is still streaming in (after a door, a teleport, fast travel): wait, like GTA does,
    // instead of falling through the ground that is not there yet
    if (!CColStore::HasCollisionLoaded(ToGta(gBody.pos), ped->m_nAreaCode)) {
        gBody.vel = Vec3();
        gBody.fallTop = gBody.pos.z;
        OverridePhysics(ped, true);
        ped->SetPosn(ToGta(gBody.pos));
        ped->m_vecMoveSpeed = CVector(0, 0, 0);
        gGame.flyVel = gBody.vel;
        gGame.jumping = false;
        return;
    }

    // in bed: he lies still on it
    Vec3 bed;
    if (SleepSpot(&bed)) {
        gBody.pos = bed + Vec3(0, 0, 1.0f);
        gBody.vel = Vec3();
        gBody.fallTop = gBody.pos.z;
        OverridePhysics(ped, true);
        ped->SetPosn(ToGta(gBody.pos));
        ped->m_vecMoveSpeed = CVector(0, 0, 0);
        gGame.flyVel = gBody.vel;
        gGame.jumping = false;
        return;
    }

    WalkInput in;
    ReadMoveInput(in.fwd, in.strafe, false); // the stick stays for GTA's walking animation; we snap the position back
    if (menuOpen)
        in.fwd = in.strafe = 0.0f;
    CPad* pad = CPad::GetPad(0);
    const bool sneak = !menuOpen && ActionDown(ACT_SNEAK);
    const bool sprint = UpdateSprint(in.fwd > 0.1f, menuOpen) && !sneak;
    gGame.sprinting = sprint;
    gGame.sneaking = sneak;
    pad->NewState.ButtonCross = sprint ? 255 : 0; // GTA's sprint animation
    // GTA crouches with one key press, Minecraft holds the sneak key
    if (sneak != (bool)ped->bIsDucking && gDuckCooldown <= 0.0f && gBody.onGround) {
        pad->NewState.ShockButtonL = 255;
        pad->OldState.ShockButtonL = 0;
        gDuckCooldown = 0.4f;
    }
    in.look = gGame.lookDir;
    in.sprint = sprint;
    in.sneak = sneak;
    in.jump = !menuOpen && ActionDown(ACT_JUMP);
    in.usingItem = UsingItem();
    in.mortal = gGame.gameMode == MODE_SURVIVAL;

    WalkEvents ev;
    WalkStep(gBody, in, dt, gMap, ev);

    // what happened on the way: damage, hunger (the sounds were the core's to play)
    if (ev.landed) {
        if (ev.fallDamage > 0.0f) {
            const float maxH = ped->m_fMaxHealth > 1.0f ? ped->m_fMaxHealth : 100.0f;
            CWeapon::GenerateDamageEvent(ped, nullptr, WEAPONTYPE_FALL, (int)(ev.fallDamage * maxH / 20.0f),
                                         (ePedPieceTypes)3, 0);
            NoteDamage(STR_DEATH_FALL);
        }
        gForced = false;
    }
    if (ev.sprintStopped)
        gGame.sprintLatch = false;
    gSurvival.exhaustion += ev.exhaustion;

    HitByVehicles(ped, dt);
    OverridePhysics(ped, true);
    ped->SetPosn(ToGta(gBody.pos));
    ped->m_vecMoveSpeed = CVector(0, 0, 0);
    ped->bIsStanding = gBody.onGround;
    gGame.flyVel = gBody.vel;
    gGame.jumping = !gBody.onGround;
}

// ---------------------------------------------------------------- creative flight / elytra / riding
void Fly(float dt, CPlayerPed* ped, bool frozen) {
    float fwd, strafe;
    ReadMoveInput(fwd, strafe, true);
    if (frozen)
        fwd = strafe = 0.0f; // a menu is open: hover in place
    const bool fast = UpdateSprint(fwd > 0.1f, frozen);
    gGame.sprinting = fast;
    FlySteer(gGame.flyVel, gGame.lookDir, fwd, strafe, fast, !frozen && ActionDown(ACT_JUMP), !frozen && ActionDown(ACT_SNEAK), dt);
    OverridePhysics(ped, true);
    if (fwd != 0.0f || strafe != 0.0f)
        FaceDirection(ped, gGame.lookDir);
    const float sinking = gGame.flyVel.z;
    bool ground = MoveKinematic(ped, gGame.flyVel, dt, nullptr);
    if (ground && sinking < -0.5f)
        StopFlying(ped); // landed
}

void Glide(float dt, CPlayerPed* ped) {
    float fwd, strafe;
    ReadMoveInput(fwd, strafe, true);
    OverridePhysics(ped, true);
    const int ticks = ElytraAdvance(gGame.flyVel, gGame.lookDir, gGame.boostTime, gTickAcc, dt);
    for (int i = 0; i < ticks; ++i)
        if (gGame.gameMode == MODE_SURVIVAL && ++gGlideTime >= 20.0f) {
            gGlideTime = 0.0f;
            ItemStack& e = gInv.armor[ARMOR_CHEST];
            if (e.id == ID_ELYTRA)
                e.damage++;
        }
    FaceDirection(ped, gGame.flyVel);
    float impact = 0.0f;
    float before = std::sqrt(gGame.flyVel.x * gGame.flyVel.x + gGame.flyVel.y * gGame.flyVel.y);
    bool ground = MoveKinematic(ped, gGame.flyVel, dt, &impact);
    if (impact > 0.0f && gGame.gameMode == MODE_SURVIVAL) {
        float after = std::sqrt(gGame.flyVel.x * gGame.flyVel.x + gGame.flyVel.y * gGame.flyVel.y);
        float hearts = GlideImpactDamage(before, after);
        if (hearts > 0.0f) {
            float maxH = ped->m_fMaxHealth > 1.0f ? ped->m_fMaxHealth : 100.0f;
            CWeapon::GenerateDamageEvent(ped, nullptr, WEAPONTYPE_FALL, (int)(hearts * maxH / 20.0f),
                                         (ePedPieceTypes)3, 0);
            PlaySfx(SND_HURT);
        }
    }
    float speed = gGame.flyVel.Length();
    SetLoopSfx(SND_ELYTRA_FLYING, true, Clamp(speed / 30.0f, 0.1f, 1.0f));
    if (ground || !gInv.HasElytra() || InGtaWater(ped))
        StopFlying(ped);
}

void RideUpdate(CPlayerPed* ped, bool menuOpen) {
    float fwd, strafe;
    ReadMoveInput(fwd, strafe, true);
    if (menuOpen)
        fwd = strafe = 0.0f;
    if (!menuOpen && ActionPressed(ACT_SNEAK)) {
        StopRiding(ped);
        return;
    }
    const CVector wish = ToGta(WishDir(gGame.lookDir, fwd, strafe));
    const bool sprint = UpdateSprint(fwd > 0.1f, menuOpen);
    gGame.sprinting = sprint;
    gGame.sneaking = false;
    Vec3 seat;
    float yaw = 0.0f;
    if (!MobRide(gGame.ridingMob, wish * (sprint ? 6.5f : 4.0f), &seat, &yaw)) {
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

bool PlayerOnGround(CPlayerPed* ped) { return gCtrl ? gBody.onGround : ped->bIsStanding; }

bool MovementControllerActive() { return gCtrl; }

CVector PlayerVelocity(CPlayerPed* ped) {
    if (!ped)
        return CVector(0, 0, 0);
    if (ped->bInVehicle && ped->m_pVehicle)
        return ped->m_pVehicle->m_vecMoveSpeed * 50.0f; // GTA keeps it per 1/50 s
    if (gGame.flying || gGame.gliding)
        return gGame.flyVel;
    if (gCtrl)
        return ToGta(gBody.vel);
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
    gBody.noFallDamage = false;
    if (ped) {
        OverridePhysics(ped, false);
        ped->m_vecMoveSpeed = CVector(0, 0, 0);
    }
    gGame.flyVel = Vec3();
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
        gBody.pos = ped->GetPosition();
        gBody.vel = ped->m_vecMoveSpeed * 50.0f;
    }
    gForced = true;
    gBody.vel += vel;
    gBody.onGround = false;
    gBody.noFallDamage = true;
    gBody.fallTop = gBody.pos.z;
}

void UpdateMovement(float dt, CPlayerPed* ped) {
    bool onFoot = !ped->bInVehicle && ped->m_fHealth > 0.0f;
    gGta.swimming = ped->m_pIntelligence && ped->m_pIntelligence->GetTaskSwim() != nullptr;
    if (gGta.swimming) {
        const CVector p = ped->GetPosition();
        float wl;
        gGta.underwater = CWaterLevel::GetWaterLevelNoWaves(p.x, p.y, p.z, &wl) && wl > p.z + 0.55f;
    } else {
        gGta.underwater = false;
    }
    if (!onFoot || !gGta.enabled) {
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
        RideUpdate(ped, menuOpen);
        return;
    }

    const bool airborne = gCtrl ? (!gBody.onGround && gBody.airTime > 0.05f) : !ped->bIsStanding;
    if (!menuOpen && ActionPressed(ACT_JUMP)) {
        // creative: Space twice flies (in the water too, like Minecraft)
        if (gGame.gameMode == MODE_CREATIVE && !gGame.gliding) {
            if (gGame.age - gLastSpaceTap < 0.3f) {
                if (gGame.flying) {
                    StopFlying(ped);
                } else {
                    gGame.flyVel = gCtrl ? gBody.vel : Vec3();
                    gGame.flyVel.z = gGta.swimming || InGtaWater(ped) ? 7.5f : 3.0f; // out of the water first
                    EndController(ped, false);
                    gGame.flying = true;
                }
                gLastSpaceTap = -10.0f;
            } else {
                gLastSpaceTap = gGame.age;
            }
        }
        // elytra: press Space again while in the air
        if (!gGame.flying && !gGame.gliding && gInv.HasElytra() && airborne && !InGtaWater(ped) && !gGta.swimming) {
            CVector v = gCtrl ? ToGta(gBody.vel) : ped->m_vecMoveSpeed * 50.0f;
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
                      !gGta.swimming && PlayerInControl(ped);
    if (want) {
        Controller(dt, ped, menuOpen);
        return;
    }
    EndController(ped, true);
    CPad* pad = CPad::GetPad(0);
    if (gConfig.minecraftControls && (InGtaWater(ped) || gGta.swimming)) {
        // swimming: Ctrl swims fast like Minecraft's sprint (GTA's own sprint key is Space, which only rises here)
        gGame.sprinting = UpdateSprint(pad->NewState.LeftStickY < -20, menuOpen);
    } else if (gConfig.minecraftControls) {
        // GTA moves the player, but with Minecraft keys
        pad->NewState.ButtonSquare = 0;
        pad->OldState.ButtonSquare = 0;
        const bool forward = pad->NewState.LeftStickY < -20;
        const bool sprint = UpdateSprint(forward, menuOpen);
        gGame.sprinting = sprint;
        gGame.sneaking = false;
        pad->NewState.ButtonCross = sprint ? 255 : 0;
    } else {
        gGame.sprinting = pad->NewState.ButtonCross != 0 && (pad->NewState.LeftStickX != 0 || pad->NewState.LeftStickY != 0);
    }
}

namespace {
SafetyHookInline gWorldProcessHook;
SafetyHookInline gSlowForPedsHook;

// GTA's traffic slows down only for people with collision (CCarCtrl::SlowCarDownForPedsSectorList, as since GTA III).
// The Minecraft movement keeps the player's off, so cars ran him over: it is lent to him while a car looks ahead.
void __cdecl HookSlowCarDownForPeds(void* list, CVehicle* vehicle, float x0, float y0, float x1, float y1, float* speed,
                                    float curSpeed) {
    CPlayerPed* player = FindPlayerPed();
    const bool lend = player && !player->bUsesCollision && !player->bInVehicle && player->m_fHealth > 0.0f;
    if (lend)
        player->bUsesCollision = true;
    gSlowForPedsHook.ccall<void>(list, vehicle, x0, y0, x1, y1, speed, curSpeed);
    if (lend)
        player->bUsesCollision = false;
}

// CWorld::Process moves every entity; GTA's walking animation moved the player too. Our position counts, and it
// must be back before the doors (CEntryExitManager) and the camera run later in the same frame.
void AfterWorldProcess() {
    if (!gCtrl)
        return;
    CPlayerPed* ped = FindPlayerPed();
    if (!ped || ped->bInVehicle || ped->m_fHealth <= 0.0f)
        return;
    if ((ped->GetPosition() - ToGta(gBody.pos)).Magnitude() < 1.5f) {
        ped->SetPosn(ToGta(gBody.pos));
        ped->m_vecMoveSpeed = CVector(0, 0, 0);
    } else {
        gBody.pos = ped->GetPosition(); // moved by the game itself
        gBody.vel = Vec3();
        gBody.fallTop = gBody.pos.z;
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
    gSlowForPedsHook = safetyhook::create_inline(reinterpret_cast<void*>(0x425440), reinterpret_cast<void*>(&HookSlowCarDownForPeds));
    Log("Hooks: traffic sees the player %s", gSlowForPedsHook ? "ok" : "FAILED");
}

void MovementAfterProcess(CPlayerPed* ped) {
    if (!gCtrl || !ped)
        return;
    // doors, interiors, scripts and respawns put the player somewhere else: follow them, never pull back
    if (ped->bInVehicle || (ped->GetPosition() - ToGta(gBody.pos)).Magnitude() > 0.5f) {
        gBody.pos = ped->GetPosition();
        gBody.vel = Vec3();
        gBody.fallTop = gBody.pos.z;
        return;
    }
    if (!gWorldProcessHook) {
        ped->SetPosn(ToGta(gBody.pos));
        ped->m_vecMoveSpeed = CVector(0, 0, 0);
    }
}

void LateMovementInput(CPlayerPed* ped) {
    if (!gConfig.minecraftControls || !ped || ped->bInVehicle || gGame.screen != SCREEN_NONE)
        return;
    if (!gGta.swimming || gGame.flying || gGame.gliding || gGame.ridingMob)
        return;
    // GTA's swim task: jump = back to the surface, fire = dive, sprint = swim fast
    CPad* pad = CPad::GetPad(0);
    pad->NewState.ButtonCross = gGame.sprinting ? 255 : 0;
    pad->NewState.ButtonSquare = 0;
    pad->OldState.ButtonSquare = 0;
    if (ActionDown(ACT_JUMP) && gGta.underwater)
        pad->NewState.ButtonSquare = 255;
    if (ActionPressed(ACT_SNEAK) && !gGta.underwater) {
        pad->NewState.ButtonCircle = 255;
        pad->OldState.ButtonCircle = 0;
    }
}

} // namespace mc
