#include "PlayerAnim.h"

#include <algorithm>
#include <cmath>

#include "Fishing.h"
#include "GameState.h"
#include "Inventory.h"
#include "Items.h"
#include "Survival.h"

namespace mc {

namespace {
float gSwimAmount = 0.0f; // 0..1 swimming pose

int ArmPoseNow(uint16_t held, int special) {
    if (!held)
        return ARM_DEFAULT;
    if (gSurvival.eatTimer > 0.0f)
        return ARM_EAT;
    if (gGame.spyglass)
        return ARM_SPYGLASS;
    if (special == SP_CROSSBOW) {
        if (gGame.crossbowCharge >= 0.0f)
            return ARM_CROSSBOW_CHARGE;
        if (gGame.crossbowSlot == gInv.selected)
            return ARM_CROSSBOW_HOLD;
    }
    if (special == SP_TRIDENT && gGame.tridentCharge >= 0.0f)
        return ARM_THROW_SPEAR;
    return ARM_DEFAULT;
}

void HurtTint(float* r, float* g, float* b) {
    *r = *g = *b = 1.0f;
    if (gGame.hurtTimer > 0.0f || gGame.deathTime >= 0.0f) {
        *g = 0.45f;
        *b = 0.45f;
    }
}
} // namespace

float SwimPose() { return gSwimAmount; }

float ElytraDive() {
    const Vec3& v = gGame.flyVel;
    float m = v.Length();
    if (gGame.gliding && v.z < 0.0f && m > 0.01f)
        return 1.0f - std::pow(-v.z / m, 1.5f);
    return 1.0f;
}
int HeldItemTile(uint16_t id) {
    if (!IsValidItem(id) || IsBlockItem(id))
        return -1;
    const ItemDef& d = Item(id);
    if (d.special == SP_BOW && gGame.bowDraw >= 0.0f) {
        float p = gGame.bowDraw;
        return p >= 0.9f ? TILE_BOW_PULLING_2 : (p >= 0.65f ? TILE_BOW_PULLING_1 : TILE_BOW_PULLING_0);
    }
    if (d.special == SP_FISHING_ROD && FishingIsCast())
        return TILE_FISHING_ROD_CAST;
    if (d.special == SP_CROSSBOW && id == gInv.Held().id) {
        if (gGame.crossbowCharge >= 0.0f) {
            float p = gGame.crossbowCharge / 1.25f;
            return p >= 1.0f ? TILE_CROSSBOW_PULLING_2 : (p >= 0.58f ? TILE_CROSSBOW_PULLING_1 : TILE_CROSSBOW_PULLING_0);
        }
        if (gGame.crossbowSlot == gInv.selected)
            return gGame.crossbowRocket ? TILE_CROSSBOW_FIREWORK : TILE_CROSSBOW_ARROW;
    }
    return -1;
}

void PlayerAnimTick(float dt, const PlayerMotion* m) {
    gGame.age += dt;
    if (!m)
        return;
    const bool onFoot = m->onFoot;
    const bool kinematic = gGame.flying || gGame.gliding || gGame.jumping;
    const Vec3 v = m->velocity;
    float speed = std::sqrt(v.x * v.x + v.y * v.y);

    // limb swing (LivingEntity.walkAnimation)
    float target = Clamp(speed / 20.0f * 4.0f, 0.0f, 1.0f);
    if (!onFoot || gGame.gliding)
        target = 0.0f;
    gGame.walkAmount += (target - gGame.walkAmount) * Clamp(dt * 8.0f, 0.0f, 1.0f);
    gGame.walkPhase += gGame.walkAmount * 20.0f * dt;

    // view bobbing (Player.bob / walkDist)
    const bool grounded = onFoot && !kinematic && m->standing;
    float bobTarget = grounded ? std::min(0.1f, speed / 20.0f) : 0.0f;
    gGame.bob += (bobTarget - gGame.bob) * Clamp(dt * 8.0f, 0.0f, 1.0f);
    if (grounded)
        gGame.walkDist += speed * dt * 0.6f;

    // swimming pose (only while actually swimming somewhere)
    float swimTarget = m->swimming && speed > 1.0f ? 1.0f : 0.0f;
    gSwimAmount += Clamp(swimTarget - gSwimAmount, -dt * 3.0f, dt * 3.0f);
    if (m->swimming)
        gGame.walkPhase += speed * dt * 2.0f; // the crawl stroke follows the speed

    if (gGame.swing >= 0.0f) {
        gGame.swing += dt / 0.3f;
        if (gGame.swing >= 1.0f)
            gGame.swing = -1.0f;
    }

    // ItemInHandRenderer.tick: the hand drops while the item changes or the attack recharges
    uint16_t held = m->alive ? gInv.Held().id : 0;
    float want = gGame.handItem == held ? std::pow(AttackCharge(), 3.0f) : 0.0f;
    float step = 0.4f * 20.0f * dt;
    gGame.handHeight += Clamp(want - gGame.handHeight, -step, step);
    if (gGame.handHeight < 0.1f)
        gGame.handItem = held;

    // off hand: same, without the attack recharge
    if (gGame.offSwing >= 0.0f) {
        gGame.offSwing += dt / 0.3f;
        if (gGame.offSwing >= 1.0f)
            gGame.offSwing = -1.0f;
    }
    uint16_t off = m->alive ? gInv.offhand.id : 0;
    float wantOff = gGame.offItem == off ? 1.0f : 0.0f;
    gGame.offHeight += Clamp(wantOff - gGame.offHeight, -step, step);
    if (gGame.offHeight < 0.1f)
        gGame.offItem = off;

    // sprinting and flying widen the view a little
    float fovTarget = 1.0f;
    if (gGame.flying)
        fovTarget *= 1.1f;
    if (gGame.sprinting)
        fovTarget *= 1.15f;
    gGame.fovMod += (fovTarget - gGame.fovMod) * Clamp(dt * 10.0f, 0.0f, 1.0f);
}

void DeathTilt(float seconds, Vec3& right, Vec3& up) {
    float f = std::min(1.0f, std::sqrt(std::max(0.0f, seconds) * 1.6f));
    float a = f * (kPi / 2.0f);
    Vec3 r2 = right * std::cos(a) + up * std::sin(a);
    Vec3 u2 = up * std::cos(a) - right * std::sin(a);
    right = r2;
    up = u2;
}

PlayerDrawInput PlayerLook(const Vec3& right, const Vec3& fwd, bool inVehicle, bool crouching, bool dead, float light) {
    const uint16_t held = dead ? 0 : gInv.Held().id;
    const uint16_t offHeld = dead ? 0 : gInv.offhand.id;
    const int special = gGame.usingOffhand ? (offHeld ? Item(offHeld).special : 0) : (held ? Item(held).special : 0);
    const Vec3 look = gGame.lookDir;
    PlayerDrawInput in;
    in.anim.limbSwing = gGame.walkPhase;
    in.anim.limbAmount = dead ? 0.0f : gGame.walkAmount;
    in.anim.age = gGame.age * 20.0f;
    if (!gGame.gliding && !dead) {
        float yaw = std::atan2(look.x * right.x + look.y * right.y + look.z * right.z,
                               look.x * fwd.x + look.y * fwd.y + look.z * fwd.z);
        in.anim.headYaw = Clamp(yaw, -1.3f, 1.3f);
        in.anim.headPitch = -std::asin(Clamp(look.z, -1.0f, 1.0f));
    }
    in.anim.attack = gGame.swing >= 0.0f ? gGame.swing : 0.0f;
    if (in.anim.attack <= 0.0f && gGame.offSwing >= 0.0f) {
        in.anim.attack = gGame.offSwing;
        in.anim.attackLeft = true;
    }
    in.anim.holding = held != 0;
    in.anim.holdingLeft = offHeld != 0;
    in.anim.useLeft = gGame.usingOffhand;
    in.anim.bow = gGame.bowDraw >= 0.0f;
    in.anim.riding = inVehicle || gGame.ridingMob != 0;
    in.anim.crouch = crouching;
    in.anim.gliding = gGame.gliding;
    in.anim.swim = inVehicle ? 0.0f : gSwimAmount;
    in.anim.armPose = ArmPoseNow(gGame.usingOffhand ? offHeld : held, special);
    in.anim.useTicks = in.anim.armPose == ARM_EAT ? gSurvival.eatTimer * 20.0f
                     : in.anim.armPose == ARM_CROSSBOW_CHARGE ? gGame.crossbowCharge * 20.0f
                                                              : 0.0f;
    in.held = held;
    in.heldTile = HeldItemTile(held);
    in.offHeld = offHeld;
    in.offTile = HeldItemTile(offHeld);
    const bool raising = gGame.tridentCharge >= 0.0f;
    in.heldUsing = raising && !gGame.usingOffhand;
    in.offUsing = raising && gGame.usingOffhand;
    in.elytra = gInv.armor[ARMOR_CHEST].id == ID_ELYTRA;
    in.dive = ElytraDive();
    in.light = light;
    HurtTint(&in.r, &in.g, &in.b);
    for (int i = 0; i < 4; ++i)
        in.armor[i] = gInv.armor[i].id;
    return in;
}

FirstPersonInput HandInput(bool left) {
    FirstPersonInput in;
    if (!left) {
        in.item = gGame.handItem;
        in.tileOverride = HeldItemTile(in.item);
        in.swing = gGame.swing >= 0.0f ? gGame.swing : 0.0f;
        in.lowered = 1.0f - Clamp(gGame.handHeight, 0.0f, 1.0f);
        const bool current = gGame.handItem == gInv.Held().id && !gGame.usingOffhand;
        if (current && gSurvival.eatTimer > 0.0f)
            in.eat = gSurvival.eatTimer / 1.6f;
        if (current && gGame.bowDraw >= 0.0f && IsValidItem(in.item) && Item(in.item).special == SP_BOW)
            in.bowTicks = gGame.bowDraw * 20.0f;
        if (current && IsValidItem(in.item) && Item(in.item).special == SP_CROSSBOW) {
            if (gGame.crossbowCharge >= 0.0f)
                in.crossbowTicks = gGame.crossbowCharge * 20.0f;
            in.crossbowLoaded = gGame.crossbowSlot == gInv.selected;
        }
        if (current && gGame.tridentCharge >= 0.0f && IsValidItem(in.item) && Item(in.item).special == SP_TRIDENT)
            in.tridentTicks = gGame.tridentCharge * 20.0f;
        in.walkDist = gGame.walkDist;
        in.bob = gGame.bob;
        in.age = gGame.age * 20.0f;
        return in;
    }
        in.leftHand = true;
        in.item = gGame.offItem;
        in.tileOverride = HeldItemTile(in.item);
        in.swing = gGame.offSwing >= 0.0f ? gGame.offSwing : 0.0f;
        in.lowered = 1.0f - Clamp(gGame.offHeight, 0.0f, 1.0f);
        const bool offCurrent = gGame.usingOffhand && gGame.offItem == gInv.offhand.id && IsValidItem(in.item);
        if (offCurrent && gSurvival.eatTimer > 0.0f)
            in.eat = gSurvival.eatTimer / 1.6f;
        if (offCurrent && gGame.bowDraw >= 0.0f && Item(in.item).special == SP_BOW)
            in.bowTicks = gGame.bowDraw * 20.0f;
        if (offCurrent && gGame.crossbowCharge >= 0.0f && Item(in.item).special == SP_CROSSBOW)
            in.crossbowTicks = gGame.crossbowCharge * 20.0f;
        if (offCurrent && gGame.tridentCharge >= 0.0f && Item(in.item).special == SP_TRIDENT)
            in.tridentTicks = gGame.tridentCharge * 20.0f;
    in.walkDist = gGame.walkDist;
    in.bob = gGame.bob;
    in.age = gGame.age * 20.0f;
    return in;
}

} // namespace mc
