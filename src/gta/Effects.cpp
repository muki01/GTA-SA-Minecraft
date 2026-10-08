#include "Effects.h"

#include "CPlayerData.h"
#include "CPlayerPed.h"
#include "CWaterLevel.h"
#include "common.h"

#include "BlockRules.h"
#include "Game.h"
#include "Items.h"
#include "Render3D.h"

namespace mc {

namespace {
float gBubbleTimer = 0.0f;
float gBreathSeen = 0.0f; // GTA's own lung capacity: kept full, our air supply decides

void Bubbles(const CVector& at, int n) {
    for (int i = 0; i < n; ++i) {
        Particle p;
        p.pos = at + CVector((Rand01() - 0.5f) * 0.4f, (Rand01() - 0.5f) * 0.4f, (Rand01() - 0.5f) * 0.3f);
        p.vel = CVector((Rand01() - 0.5f) * 0.4f, (Rand01() - 0.5f) * 0.4f, 0.8f + Rand01() * 0.8f);
        p.maxLife = p.life = 0.6f + Rand01() * 0.6f;
        p.tile = TILE_P_BUBBLE;
        p.size = 0.04f + Rand01() * 0.03f;
        p.gravity = -1.0f;
        SpawnParticle(p);
    }
}
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
    if (ev.drowned) {
        NoteDamage(STR_DEATH_DROWN);
        Bubbles(eye, 8);
    }
    if (under && survival) {
        gBubbleTimer -= dt;
        if (gBubbleTimer <= 0.0f) {
            gBubbleTimer = 0.9f + Rand01() * 0.8f;
            Bubbles(eye + gGame.lookDir * 0.25f - CVector(0, 0, 0.1f), 2);
        }
    }
    gGta.directDamage += ev.directDamage;
    // GTA counts the player's breath too and would drown him on its own clock: keep it full
    if (ped->m_pPlayerData) {
        float& breath = ped->m_pPlayerData->m_fBreath;
        gBreathSeen = std::max(gBreathSeen, breath);
        breath = gBreathSeen;
    }
}

} // namespace mc
