#pragma once
// Minecraft entity models and item rendering, ported from the game's own model definitions.
// All coordinates are Minecraft model coordinates (pixels; x = model's left, y = down, z = back).

#include "GameTables.h"
#include "Pose.h"
#include "generated/Assets.h"

namespace mc {

struct ModelStyle {
    GuiRect tex{ 0, 0, 64, 64 }; // where the entity's texture sits inside entity.png
    float light = 1.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f; // tint (hurt flash, CJ's skin colour...)
    bool untextured = false;
};

// ModelPart.Cube
void McCube(const Pose& pose, float x, float y, float z, float w, float h, float d, float texU, float texV,
            const ModelStyle& st, float inflate = 0.0f, bool mirror = false);

// ModelPart.translateAndRotate
inline Pose Part(const Pose& base, float px, float py, float pz, float rx = 0.0f, float ry = 0.0f, float rz = 0.0f) {
    Pose p = base;
    p.Translate(px / 16.0f, py / 16.0f, pz / 16.0f);
    p.RotZYX(rz, ry, rx);
    return p;
}

// ---------------------------------------------------------------- player
struct PartState {
    float x = 0, y = 0, z = 0;    // pivot
    float rx = 0, ry = 0, rz = 0; // rotation
};
struct HumanoidAnim {
    PartState head, body, rArm, lArm, rLeg, lLeg;
};
enum ArmPose { ARM_DEFAULT = 0, ARM_EAT, ARM_SPYGLASS, ARM_CROSSBOW_HOLD, ARM_CROSSBOW_CHARGE, ARM_THROW_SPEAR };
struct PlayerAnimInput {
    float limbSwing = 0, limbAmount = 0, age = 0; // age in ticks
    float headYaw = 0, headPitch = 0;             // radians
    float attack = 0;                             // 0..1 swing progress (0 = none)
    bool holding = false, bow = false, riding = false, crouch = false, gliding = false;
    float swim = 0;          // 0..1 swimming pose (body lies flat, crawl stroke)
    int armPose = ARM_DEFAULT;
    float useTicks = 0;      // ticks the item has been used (eating bob, crossbow charge)
    bool holdingLeft = false; // something in the off hand
    bool attackLeft = false;  // the swing is done with the off hand
    bool useLeft = false;     // eating with the off hand
};
void ComputePlayerAnim(HumanoidAnim& a, const PlayerAnimInput& in);
void DrawPlayerModel(const Pose& base, const HumanoidAnim& a, const ModelStyle& st);
Pose RightArmPose(const Pose& base, const HumanoidAnim& a);
Pose LeftArmPose(const Pose& base, const HumanoidAnim& a);
void DrawRightArm(const Pose& pose, const ModelStyle& st, bool sleeve); // arm cube at the current pose (first person)
void DrawElytra(const Pose& base, bool crouch, bool gliding, float dive, const ModelStyle& st);
// worn armour (armor[0] helmet .. armor[3] boots), HumanoidArmorLayer
void DrawArmor(const Pose& base, const HumanoidAnim& a, const uint16_t armor[4], float light, float r = 1.0f,
               float g = 1.0f, float b = 1.0f);

// ---------------------------------------------------------------- items
enum DisplayCtx { CTX_GROUND, CTX_FIRST_PERSON, CTX_THIRD_PERSON };
void ApplyDisplay(Pose& p, uint16_t id, DisplayCtx ctx);
// Draws the item model (block cube or extruded sprite); `p` already includes the display transform.
void DrawItemModel(Pose p, uint16_t id, float light, int tileOverride = -1, bool inHand = false);
// TridentModel at `p` (model space: the spikes point to -y); thrown tridents
void DrawTridentModel(const Pose& p, float light);
// ItemInHandLayer: item held in the right (or left) hand of a humanoid model
void DrawHeldItem(const Pose& armPose, uint16_t id, float light, int tileOverride = -1, bool left = false,
                  bool usingItem = false);

// Steve + held item + elytra in one call
struct PlayerDrawInput {
    PlayerAnimInput anim;
    uint16_t held = 0;
    int heldTile = -1;
    uint16_t offHeld = 0;    // off hand
    int offTile = -1;
    bool heldUsing = false, offUsing = false; // the item in that hand is being used (raised trident)
    bool elytra = false;
    float dive = 1.0f; // elytra spread factor while gliding
    float light = 1.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f; // hurt flash
    uint16_t armor[4] = {};
};
void DrawPlayerFull(const Pose& base, const PlayerDrawInput& in);

// Arm pose for a model that is not ours (CJ): built from the elbow and hand positions.
Pose ArmPoseFromBones(const CVector& elbow, const CVector& hand, const CVector& bodyRight, float scale, bool left = false);

// First-person view model (ItemInHandRenderer). `view` = ViewPose(eye, right, up, forward).
struct FirstPersonInput {
    uint16_t item = 0;
    int tileOverride = -1;
    float swing = 0.0f;     // 0..1, 0 = no swing
    float lowered = 0.0f;   // 0 = up, 1 = fully lowered (switching items)
    float eat = -1.0f;      // 0..1 eating progress, < 0 = not eating
    float bowTicks = -1.0f; // ticks the bow has been drawn, < 0 = not drawing
    float crossbowTicks = -1.0f; // ticks the crossbow has been loading, < 0 = not loading
    bool crossbowLoaded = false;
    float tridentTicks = -1.0f;  // ticks the trident has been raised, < 0 = not throwing
    float walkDist = 0.0f, bob = 0.0f; // view bobbing
    float age = 0.0f;       // ticks, idle arm sway
    float swayPitch = 0.0f, swayYaw = 0.0f; // degrees the hand lags behind the camera
    bool steveArm = true;   // false: plain skin-coloured arm (CJ)
    bool leftHand = false;  // off hand: everything mirrored (Minecraft's arm side factor)
    float light = 1.0f;
};
void DrawFirstPerson(const Pose& view, const FirstPersonInput& in);

// ---------------------------------------------------------------- mobs
enum MobKind { MOB_COW = 0, MOB_PIG, MOB_SHEEP, MOB_CHICKEN, MOB_KIND_COUNT };
struct MobAnim {
    float limbSwing = 0, limbAmount = 0;
    float headYaw = 0, headPitch = 0;
    float wingFlap = 0;  // chicken
    bool sheared = false;
    bool saddled = false;  // pig
};
void DrawMob(int kind, const Pose& base, const MobAnim& a, float light, float r, float g, float b);

enum NpcKind { NPC_VILLAGER = 0, NPC_PILLAGER, NPC_VINDICATOR };
struct NpcAnim {
    float limbSwing = 0, limbAmount = 0;
    float headYaw = 0, headPitch = 0;
    bool riding = false, armed = false; // armed: melee weapon
    bool crossbow = false;                // holding a gun = a crossbow
    int variant = 0; // villager profession
};
void DrawNpc(int kind, const Pose& base, const NpcAnim& a, float light, float r, float g, float b);

} // namespace mc
