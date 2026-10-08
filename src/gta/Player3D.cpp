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
#include "Game.h"
#include "Inventory.h"
#include "McModel.h"
#include "Movement.h"
#include "PedSkins.h"
#include "PlayerAnim.h"
#include "Render3D.h"
#include "Textures.h"

namespace mc {

namespace {
constexpr float kPlayerScale = 0.9375f; // Minecraft draws the player at 15/16 of the model size

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

} // namespace

// the core's animation clocks, with what GTA knows about the player's body
void UpdatePlayerAnimation(float dt) {
    CPlayerPed* ped = FindPlayerPed();
    if (!ped) {
        PlayerAnimTick(dt, nullptr);
        return;
    }
    PlayerMotion m;
    m.alive = ped->m_fHealth > 0.0f;
    m.onFoot = !ped->bInVehicle;
    m.standing = ped->bIsStanding;
    m.swimming = gGta.swimming;
    const bool kinematic = gGame.flying || gGame.gliding || gGame.jumping;
    m.velocity = kinematic || MovementControllerActive() ? gGame.flyVel : Vec3(ped->m_vecMoveSpeed * 50.0f);
    PlayerAnimTick(dt, &m);
}

static void RenderPlayer(float light, bool vehiclePass) {
    CPlayerPed* ped = FindPlayerPed();
    if (!gGta.enabled || !gGta.inWorld || !ped || !gEntityTex.tex)
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
    const CVector look = gGame.lookDir;
    const CVector pos = ped->GetPosition();

    if (gGta.steve) {
        CVector fwd, up, right, feet;
        float scale = kPlayerScale;
        const bool swimPose = !inVehicle && SwimPose() > 0.05f;
        if (inVehicle) {
            const CMatrix& vm = *ped->m_pVehicle->m_matrix;
            fwd = vm.up; // GTA: "up" is the forward axis
            up = vm.at;
            right = CVector::Cross(fwd, up);
            CVector hip;
            scale = SeatedFit(ped, up, 20.0f, &hip);
            feet = hip - up * (0.75f * scale);
        } else if (gGame.gliding || swimPose) {
            CVector dir = gGame.gliding ? ToGta(gGame.flyVel) : ped->m_vecMoveSpeed * 50.0f;
            float m = dir.Magnitude();
            dir = m > 0.5f ? dir * (1.0f / m) : look;
            if (swimPose && !gGta.underwater) {
                dir.z = 0.0f; // swimming on the surface: flat
                float hm = dir.Magnitude();
                dir = hm > 1e-3f ? dir * (1.0f / hm) : Horizontal(look);
            }
            // lerp between standing and lying (head first)
            const float t = gGame.gliding ? 1.0f : SwimPose();
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
            Vec3 r2 = right, u2 = up;
            DeathTilt(gGame.deathTime, r2, u2); // the dead player tips over onto his side
            right = ToGta(r2);
            up = ToGta(u2);
        }
        Pose base = EntityPose(feet, right, up, fwd, scale);

        PlayerDrawInput in = PlayerLook(right, fwd, inVehicle, !inVehicle && ped->bIsDucking && !gGame.gliding && !swimPose, dead, light);
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
    if (!gGta.enabled || !gGta.inWorld || !ped || gGame.cameraMode != CAM_FIRST || !gEntityTex.tex ||
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

    float swayYaw, swayPitch;
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
        swayYaw = Clamp(wrap(yaw - sYaw) * 0.1f, -6.0f, 6.0f);
        swayPitch = Clamp((pitch - sPitch) * 0.1f, -6.0f, 6.0f);
    }
    FirstPersonInput in = HandInput(false);
    in.swayYaw = swayYaw;
    in.swayPitch = swayPitch;
    in.steveArm = gGta.steve;
    in.light = light;
    DrawFirstPerson(view, in);

    // the off hand (only when it holds something)
    FirstPersonInput off = HandInput(true);
    off.swayPitch = swayPitch;
    off.swayYaw = swayYaw;
    off.steveArm = in.steveArm;
    off.light = light;
    DrawFirstPerson(view, off);
    d3::Flush();
}

} // namespace mc
