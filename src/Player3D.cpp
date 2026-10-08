#include "Player3D.h"

#include <d3d9.h>

#include "CCamera.h"
#include "CPlayerPed.h"
#include "CScene.h"
#include "CVehicle.h"
#include "CWorld.h"
#include "common.h"
#include "ePedBones.h"

#include "Draw3D.h"
#include "Fishing.h"
#include "Game.h"
#include "Inventory.h"
#include "Items.h"
#include "McModel.h"
#include "Movement.h"
#include "PedSkins.h"
#include "Render3D.h"
#include "Textures.h"

namespace mc {

namespace {
constexpr float kPlayerScale = 0.9375f; // Minecraft draws the player at 15/16 of the model size
float gSwimAmount = 0.0f;               // 0..1 swimming pose

CVector Horizontal(CVector v) {
    v.z = 0;
    float m = v.Magnitude();
    return m > 1e-4f ? v * (1.0f / m) : CVector(0, 1, 0);
}

CVector BonePos(CPlayerPed* ped, unsigned int bone) {
    RwV3d p;
    ped->GetBonePosition(p, bone, false);
    return CVector(p.x, p.y, p.z);
}

float ElytraDive() {
    const CVector& v = gGame.flyVel;
    float m = v.Magnitude();
    if (gGame.gliding && v.z < 0.0f && m > 0.01f)
        return 1.0f - std::pow(-v.z / m, 1.5f);
    return 1.0f;
}
} // namespace

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

void UpdatePlayerAnimation(float dt) {
    gGame.age += dt;
    CPlayerPed* ped = FindPlayerPed();
    if (!ped)
        return;
    const bool onFoot = !ped->bInVehicle;
    const bool kinematic = gGame.flying || gGame.gliding || gGame.jumping;
    CVector v = ped->m_vecMoveSpeed * 50.0f;
    if (kinematic || MovementControllerActive())
        v = gGame.flyVel;
    float speed = std::sqrt(v.x * v.x + v.y * v.y);

    // limb swing (LivingEntity.walkAnimation)
    float target = Clamp(speed / 20.0f * 4.0f, 0.0f, 1.0f);
    if (!onFoot || gGame.gliding)
        target = 0.0f;
    gGame.walkAmount += (target - gGame.walkAmount) * Clamp(dt * 8.0f, 0.0f, 1.0f);
    gGame.walkPhase += gGame.walkAmount * 20.0f * dt;

    // view bobbing (Player.bob / walkDist)
    const bool grounded = onFoot && !kinematic && ped->bIsStanding;
    float bobTarget = grounded ? std::min(0.1f, speed / 20.0f) : 0.0f;
    gGame.bob += (bobTarget - gGame.bob) * Clamp(dt * 8.0f, 0.0f, 1.0f);
    if (grounded)
        gGame.walkDist += speed * dt * 0.6f;

    // swimming pose (only while actually swimming somewhere)
    float swimTarget = gGame.swimming && speed > 1.0f ? 1.0f : 0.0f;
    gSwimAmount += Clamp(swimTarget - gSwimAmount, -dt * 3.0f, dt * 3.0f);
    if (gGame.swimming)
        gGame.walkPhase += speed * dt * 2.0f; // the crawl stroke follows the speed

    if (gGame.swing >= 0.0f) {
        gGame.swing += dt / 0.3f;
        if (gGame.swing >= 1.0f)
            gGame.swing = -1.0f;
    }

    // ItemInHandRenderer.tick: the hand drops while the item changes or the attack recharges
    uint16_t held = ped->m_fHealth > 0.0f ? gInv.Held().id : 0;
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
    uint16_t off = ped->m_fHealth > 0.0f ? gInv.offhand.id : 0;
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

namespace {

int ArmPoseNow(uint16_t held, int special) {
    if (!held)
        return ARM_DEFAULT;
    if (gGame.eatTimer > 0.0f)
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

static void RenderPlayer(float light, bool vehiclePass) {
    CPlayerPed* ped = FindPlayerPed();
    if (!gGame.enabled || !gGame.inWorld || !ped || !gEntityTex.tex)
        return;
    if (gGame.cameraMode == CAM_FIRST)
        return;
    const bool inVehicle = ped->bInVehicle && ped->m_pVehicle;
    if (inVehicle != vehiclePass)
        return;
    const bool wings = gInv.armor[ARMOR_CHEST].id == ID_ELYTRA;
    const bool dead = ped->m_fHealth <= 0.0f;
    const uint16_t held = dead ? 0 : gInv.Held().id;
    const uint16_t offHeld = dead ? 0 : gInv.offhand.id;
    const int special = gGame.usingOffhand ? (offHeld ? Item(offHeld).special : 0) : (held ? Item(held).special : 0);
    const CVector look = gGame.lookDir;
    const CVector pos = ped->GetPosition();

    if (gGame.steve) {
        CVector fwd, up, right, feet;
        float scale = kPlayerScale;
        const bool swimPose = !inVehicle && gSwimAmount > 0.05f;
        if (inVehicle) {
            const CMatrix& vm = *ped->m_pVehicle->m_matrix;
            fwd = vm.up; // GTA: "up" is the forward axis
            up = vm.at;
            right = CVector::Cross(fwd, up);
            CVector hip;
            scale = SeatedFit(ped, up, 20.0f, &hip);
            feet = hip - up * (0.75f * scale);
        } else if (gGame.gliding || swimPose) {
            CVector dir = gGame.gliding ? gGame.flyVel : ped->m_vecMoveSpeed * 50.0f;
            float m = dir.Magnitude();
            dir = m > 0.5f ? dir * (1.0f / m) : look;
            if (swimPose && !gGame.underwater) {
                dir.z = 0.0f; // swimming on the surface: flat
                float hm = dir.Magnitude();
                dir = hm > 1e-3f ? dir * (1.0f / hm) : Horizontal(look);
            }
            // lerp between standing and lying (head first)
            const float t = gGame.gliding ? 1.0f : gSwimAmount;
            CVector stand(0, 0, 1);
            up = stand * (1.0f - t) + dir * t;
            float um = up.Magnitude();
            up = um > 1e-3f ? up * (1.0f / um) : stand;
            CVector down(0, 0, -1);
            fwd = down - up * (down.x * up.x + down.y * up.y + down.z * up.z);
            float fm = fwd.Magnitude();
            fwd = fm > 0.05f ? fwd * (1.0f / fm) : Horizontal(look);
            if (t < 0.5f && fm <= 0.05f)
                fwd = Horizontal(ped->GetForward());
            right = CVector::Cross(fwd, up);
            feet = pos - up * 0.9f;
        } else {
            fwd = Horizontal(ped->GetForward());
            up = CVector(0, 0, 1);
            right = CVector::Cross(fwd, up);
            feet = pos - CVector(0, 0, ped->bIsDucking ? 1.0f + 0.125f * kPlayerScale : 1.0f);
            if (gGame.ridingMob)
                fwd = Horizontal(ped->GetForward());
        }
        if (dead) {
            // LivingEntityRenderer: the dead player tips over onto his side
            float f = std::min(1.0f, std::sqrt(std::max(0.0f, gGame.deathTime) * 1.6f));
            float a = f * (kPi / 2.0f);
            CVector r2 = right * std::cos(a) + up * std::sin(a);
            CVector u2 = up * std::cos(a) - right * std::sin(a);
            right = r2;
            up = u2;
        }
        Pose base = EntityPose(feet, right, up, fwd, scale);

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
        in.anim.crouch = !inVehicle && ped->bIsDucking && !gGame.gliding && !swimPose;
        in.anim.gliding = gGame.gliding;
        in.anim.swim = inVehicle ? 0.0f : gSwimAmount;
        in.anim.armPose = ArmPoseNow(gGame.usingOffhand ? offHeld : held, special);
        in.anim.useTicks = in.anim.armPose == ARM_EAT ? gGame.eatTimer * 20.0f
                         : in.anim.armPose == ARM_CROSSBOW_CHARGE ? gGame.crossbowCharge * 20.0f
                                                                  : 0.0f;
        in.held = held;
        in.heldTile = HeldItemTile(held);
        in.offHeld = offHeld;
        in.offTile = HeldItemTile(offHeld);
        const bool raising = gGame.tridentCharge >= 0.0f;
        in.heldUsing = raising && !gGame.usingOffhand;
        in.offUsing = raising && gGame.usingOffhand;
        in.elytra = wings;
        in.dive = ElytraDive();
        in.light = light;
        HurtTint(&in.r, &in.g, &in.b);
        for (int i = 0; i < 4; ++i)
            in.armor[i] = gInv.armor[i].id;
        DrawPlayerFull(base, in);

        if (!inVehicle && !swimPose) {
            float gz;
            if (GroundBelow(pos, 8.0f, &gz))
                AddShadow(CVector(pos.x, pos.y, gz), 0.5f, Clamp(1.0f - (pos.z - 1.0f - gz) / 6.0f, 0.0f, 1.0f));
        }
    } else if (ped->m_pRwObject && ped->bIsVisible && !inVehicle) {
        // CJ: Minecraft item in his hand and the elytra on his back, following his bones
        CVector fwd = Horizontal(ped->GetForward());
        CVector right(fwd.y, -fwd.x, 0.0f);
        const bool raising = gGame.tridentCharge >= 0.0f;
        if (held) {
            Pose arm = ArmPoseFromBones(BonePos(ped, BONE_RIGHTELBOW), BonePos(ped, BONE_RIGHTHAND), right, kPlayerScale);
            DrawHeldItem(arm, held, light, HeldItemTile(held), false, raising && !gGame.usingOffhand);
        }
        if (offHeld) {
            Pose arm = ArmPoseFromBones(BonePos(ped, BONE_LEFTELBOW), BonePos(ped, BONE_LEFTHAND), right, kPlayerScale, true);
            DrawHeldItem(arm, offHeld, light, HeldItemTile(offHeld), true, raising && gGame.usingOffhand);
        }
        if (wings) {
            CVector neck = BonePos(ped, BONE_NECK), pelvis = BonePos(ped, BONE_PELVIS);
            CVector up = neck - pelvis;
            float m = up.Magnitude();
            up = m > 0.05f ? up * (1.0f / m) : CVector(0, 0, 1);
            CVector f2 = fwd - up * (fwd.x * up.x + fwd.y * up.y + fwd.z * up.z);
            float fm = f2.Magnitude();
            f2 = fm > 1e-3f ? f2 * (1.0f / fm) : fwd;
            CVector r2 = CVector::Cross(f2, up);
            Pose body;
            body.X = r2 * -kPlayerScale;
            body.Y = up * -kPlayerScale;
            body.Z = f2 * -kPlayerScale;
            body.o = neck - up * 0.04f;
            ModelStyle st;
            st.tex = ENT_ELYTRA;
            st.light = light;
            DrawElytra(body, ped->bIsDucking, gGame.gliding, ElytraDive(), st);
        }
    }
    d3::Flush();
}

void RenderPlayerModel(float light) { RenderPlayer(light, false); }
void RenderPlayerInVehicle(float light) { RenderPlayer(light, true); }

void RenderFirstPersonHand() {
    CPlayerPed* ped = FindPlayerPed();
    if (!gGame.enabled || !gGame.inWorld || !ped || gGame.cameraMode != CAM_FIRST || !gEntityTex.tex ||
        ped->m_fHealth <= 0.0f || gGame.spyglass)
        return;
    // clear the depth buffer so the hand never pokes into walls (the world is already drawn)
    if (auto* dev = reinterpret_cast<IDirect3DDevice9*>(GetD3DDevice()))
        dev->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);

    d3::StateGuard guard;
    d3::StateOpaque();
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    float light = std::max(0.45f, DaylightFactor());

    const CMatrix& cm = TheCamera.m_mCameraMatrix;
    // Minecraft always draws the hand with a 70 degree view; stretch view space so it looks the same here
    float f = 1.0f;
    if (Scene.m_pCamera) {
        float vy = Scene.m_pCamera->viewWindow.y;
        if (vy > 0.05f && vy < 5.0f)
            f = vy / std::tan(Rad(35.0f));
    }
    Pose view = ViewPose(cm.pos, cm.right * -f, cm.at * f, cm.up);

    FirstPersonInput in;
    {
        // the hand lags a little behind quick camera turns
        static float sYaw = 0.0f, sPitch = 0.0f;
        static bool sInit = false;
        auto wrap = [](float a) {
            while (a > 180.0f) a -= 360.0f;
            while (a < -180.0f) a += 360.0f;
            return a;
        };
        const CVector& F = cm.up;
        float yaw = std::atan2(F.x, F.y) * (180.0f / kPi);
        float pitch = -std::asin(Clamp(F.z, -1.0f, 1.0f)) * (180.0f / kPi);
        if (!sInit) {
            sYaw = yaw;
            sPitch = pitch;
            sInit = true;
        }
        float k = 1.0f - std::pow(0.5f, Clamp(FrameDelta(), 0.0f, 0.1f) * 20.0f);
        sYaw = wrap(sYaw + wrap(yaw - sYaw) * k);
        sPitch += (pitch - sPitch) * k;
        in.swayYaw = Clamp(wrap(yaw - sYaw) * 0.1f, -6.0f, 6.0f);
        in.swayPitch = Clamp((pitch - sPitch) * 0.1f, -6.0f, 6.0f);
    }
    in.item = gGame.handItem;
    in.tileOverride = HeldItemTile(in.item);
    in.swing = gGame.swing >= 0.0f ? gGame.swing : 0.0f;
    in.lowered = 1.0f - Clamp(gGame.handHeight, 0.0f, 1.0f);
    const bool current = gGame.handItem == gInv.Held().id && !gGame.usingOffhand;
    if (current && gGame.eatTimer > 0.0f)
        in.eat = gGame.eatTimer / 1.6f;
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
    in.steveArm = gGame.steve;
    in.light = light;
    DrawFirstPerson(view, in);

    // the off hand (only when it holds something)
    FirstPersonInput off;
    off.leftHand = true;
    off.item = gGame.offItem;
    off.tileOverride = HeldItemTile(off.item);
    off.swing = gGame.offSwing >= 0.0f ? gGame.offSwing : 0.0f;
    off.lowered = 1.0f - Clamp(gGame.offHeight, 0.0f, 1.0f);
    const bool offCurrent = gGame.usingOffhand && gGame.offItem == gInv.offhand.id && IsValidItem(off.item);
    if (offCurrent && gGame.eatTimer > 0.0f)
        off.eat = gGame.eatTimer / 1.6f;
    if (offCurrent && gGame.bowDraw >= 0.0f && Item(off.item).special == SP_BOW)
        off.bowTicks = gGame.bowDraw * 20.0f;
    if (offCurrent && gGame.crossbowCharge >= 0.0f && Item(off.item).special == SP_CROSSBOW)
        off.crossbowTicks = gGame.crossbowCharge * 20.0f;
    if (offCurrent && gGame.tridentCharge >= 0.0f && Item(off.item).special == SP_TRIDENT)
        off.tridentTicks = gGame.tridentCharge * 20.0f;
    off.walkDist = in.walkDist;
    off.bob = in.bob;
    off.age = in.age;
    off.swayPitch = in.swayPitch;
    off.swayYaw = in.swayYaw;
    off.steveArm = in.steveArm;
    off.light = light;
    DrawFirstPerson(view, off);
    d3::Flush();
}

} // namespace mc
