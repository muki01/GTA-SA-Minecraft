#include "Effects.h"

#include "CPlayerData.h"
#include "CPlayerPed.h"
#include "CWaterLevel.h"
#include "common.h"

#include "BlockRules.h"
#include "Game.h"

namespace mc {

namespace {
float gBreathSeen = 0.0f; // GTA's own lung capacity: kept full, our air supply decides

} // namespace

void FireResistanceCleared(CPlayerPed* ped, bool hadIt) {
    if (hadIt && ped && gGame.gameMode != MODE_CREATIVE)
        ped->bFireProof = false;
}

void ClearEffects(CPlayerPed* ped) {
    const bool fireResistance = HasEffect(EFFECT_FIRE_RESISTANCE);
    ClearEffects();
    FireResistanceCleared(ped, fireResistance);
}

void EffectsUpdate(float dt, CPlayerPed* ped) {
    const float maxH = ped->m_fMaxHealth > 1.0f ? ped->m_fMaxHealth : 100.0f;
    const bool alive = ped->m_fHealth > 0.0f;
    if (!alive) {
        ClearEffects(ped);
        gSurvival.air = kMaxAir;
        return;
    }
    SurvivalEvents ev;
    EffectsTick(dt, ped->m_fHealth, maxH, ev);
    if (HasEffect(EFFECT_FIRE_RESISTANCE))
        ped->bFireProof = true;
    else if (ev.expired & (1u << EFFECT_FIRE_RESISTANCE))
        FireResistanceCleared(ped, true); // it ran out

    // air: ten bubbles that run out under water, then drowning
    const CVector eye = gGame.eyePos;
    float wl;
    const bool survival = gGame.gameMode == MODE_SURVIVAL;
    const bool under = !ped->bInVehicle &&
                       ((CWaterLevel::GetWaterLevelNoWaves(eye.x, eye.y, eye.z, &wl) && wl > eye.z + 0.05f) ||
                        FluidAt(eye) == ID_WATER);
    BreathTick(dt, under, survival, ped->m_fHealth, maxH, ev);
    BreathEffects(dt, under && survival, ev.drowned);
    gGta.directDamage += ev.directDamage;
    // GTA counts the player's breath too and would drown him on its own clock: keep it full
    if (ped->m_pPlayerData) {
        float& breath = ped->m_pPlayerData->m_fBreath;
        gBreathSeen = std::max(gBreathSeen, breath);
        breath = gBreathSeen;
    }
}

} // namespace mc
