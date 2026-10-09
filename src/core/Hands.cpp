#include "Hands.h"

#include <utility>

#include "Combat.h"
#include "Controls.h"
#include "GameState.h"
#include "Host.h"
#include "Interact.h"
#include "Beds.h"
#include "Inventory.h"
#include "Items.h"
#include "Mobs.h"
#include "Survival.h"

namespace mc {

namespace {
bool gMeleeHeld = false; // the attack button was used up by a hit: no mining until it is let go
} // namespace

HandsResult HandsTick(float dt, const HandsFacts& f) {
    HandsResult r;
    // ------------------------------------------------ open screen
    if (gGame.screen != SCREEN_NONE) {
        if (ActionPressed(ACT_INVENTORY) || ActionPressed(ACT_BACK) || !f.alive)
            CloseScreen();
        r.blockPad = true;
        return r;
    }

    // ------------------------------------------------ in bed: nothing but getting up
    if (SleepTick(dt, !f.alive || f.inVehicle || ActionPressed(ACT_SNEAK) || ActionPressed(ACT_JUMP))) {
        r.blockPad = true;
        return r;
    }

    if (!f.alive) {
        StopMining();
        gGame.bowDraw = gGame.crossbowCharge = gGame.tridentCharge = -1.0f;
        gSurvival.eatTimer = 0.0f;
        gGame.spyglass = false;
        return r;
    }
    // ------------------------------------------------ hotbar (also while driving)
    HotbarTick();

    if (ActionPressed(ACT_INVENTORY)) {
        OpenScreen(gGame.gameMode == MODE_CREATIVE ? SCREEN_CREATIVE : SCREEN_INVENTORY);
        r.blockPad = true;
        return r;
    }
    if (ActionPressed(ACT_SWAP_HANDS) && f.swapKeyFree)
        SwapHands();
    if (ActionPressed(ACT_DROP) && !f.inVehicle) // (at the wheel the key looks to the side)
        DropHeldItem(ActionDown(ACT_DROP_STACK));

    // ------------------------------------------------ look at / fight / mine / place
    UpdateTarget();
    r.aimed = true;
    if (ActionPressed(ACT_PICK_BLOCK))
        PickBlock();

    const bool targetHasUse = TargetHasUse();

    // the off hand gets the right click when the main hand has no use for it
    const bool useOff = !HasRightClickUse(gInv.Held()) && !gInv.offhand.Empty();
    if (useOff) {
        std::swap(gInv.slots[gInv.selected], gInv.offhand);
        gGame.offhandActive = true;
    }
    struct SwapBack {
        bool on;
        ~SwapBack() {
            if (on) {
                std::swap(gInv.slots[gInv.selected], gInv.offhand);
                gGame.offhandActive = false;
            }
        }
    } swapBack{ useOff };
    ItemStack& useItem = gInv.Held();

    // bow, crossbow, trident, spyglass; then food
    const bool rmb = ActionDown(ACT_USE) && !targetHasUse;
    const bool charging = ChargedItemsTick(dt, rmb, ActionPressed(ACT_USE) && !targetHasUse);
    bool eating = false;
    if (!charging) {
        r.hadFireResistance = HasEffect(EFFECT_FIRE_RESISTANCE);
        const EatResult e = EatingTick(dt, rmb, gGame.gameMode == MODE_SURVIVAL);
        r.finished = e.finished;
        eating = e.eating;
    }

    // use
    if (ActionPressed(ACT_USE) && !charging) {
        bool used = false;
        if (targetHasUse && !f.sneaking) {
            UseTargetBlock();
            used = true;
        }
        const float targetDist = gTarget.valid ? (gTarget.point - gGame.rayOrigin).Length() : 1e9f;
        const float reach = (gGame.eyePos - gGame.rayOrigin).Length() + 4.0f;
        if (!used && !eating && !f.inVehicle) {
            // somebody of the host's under the crosshair (a villager: trade)
            const HostHit hb = TheHost().BeingTrace(gGame.rayOrigin, gGame.lookDir, reach);
            if (hb.being >= 0 && (hb.beingPoint - gGame.eyePos).Length() <= 4.0f && hb.beingDist <= targetDist)
                used = TheHost().UseOnBeing(hb.being);
        }
        if (!used && !f.inVehicle) {
            // an animal under the crosshair: shear, milk, feed, saddle or ride it
            MobHit mh = MobsRaycast(gGame.rayOrigin, gGame.lookDir, reach);
            if (mh.index >= 0 && (mh.point - gGame.eyePos).Length() <= 4.0f && mh.dist <= targetDist)
                used = MobInteract(mh.index, true);
        }
        if (!used && !eating)
            used = UseWorldItem();
        if (!used && !eating)
            used = UseHeldItem();
        if (gGame.screen != SCREEN_NONE) {
            r.blockPad = true;
            return r;
        }
        if (!used && !eating)
            PlaceHeldBlock();
    } else if (ActionDown(ACT_USE) && PlaceReady() && !eating && !charging && !targetHasUse &&
               IsBlockItem(useItem.id)) {
        PlaceHeldBlock();
    }
    gGame.usingOffhand = useOff && (eating || charging);
    if (useOff) {
        // back to the main hand for fighting and mining
        swapBack.on = false;
        std::swap(gInv.slots[gInv.selected], gInv.offhand);
        gGame.offhandActive = false;
    }

    // attack, otherwise mine (unless the button fires the guns of the vehicle)
    const bool attackHeld = ActionDown(ACT_ATTACK) && !f.vehicleGuns;
    if (ActionPressed(ACT_ATTACK) && !f.vehicleGuns) {
        gMeleeHeld = MeleeAttack();
        if (!gMeleeHeld) {
            StartSwing();
            if (!gTarget.valid)
                gGame.attackTimer = 0.0f; // swinging at the air also resets the cooldown
        }
    }
    if (!attackHeld)
        gMeleeHeld = false;
    MineTick(dt, attackHeld && !gMeleeHeld);
    return r;
}

void GameKeysTick() {
    if (gGame.screen != SCREEN_NONE)
        return;
    if (ActionPressed(ACT_GAME_MODE)) {
        gGame.gameMode = gGame.gameMode == MODE_SURVIVAL ? MODE_CREATIVE : MODE_SURVIVAL;
        std::string msg = LangStr(STR_GAMEMODE_SET); // "Set own game mode to %s"
        const size_t at = msg.find("%s");
        if (at != std::string::npos)
            msg.replace(at, 2, LangStr(gGame.gameMode == MODE_CREATIVE ? STR_CREATIVE : STR_SURVIVAL));
        ShowMessage(msg);
        gWorld.dirty = true;
    }
    if (ActionPressed(ACT_PERSPECTIVE)) {
        // Minecraft order: first person -> third person back -> third person front
        gGame.cameraMode = gGame.cameraMode == CAM_FIRST ? CAM_THIRD_BACK
                         : gGame.cameraMode == CAM_THIRD_BACK ? CAM_THIRD_FRONT
                                                              : CAM_FIRST;
    }
}

} // namespace mc
