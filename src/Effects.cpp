#include "Effects.h"

#include "CFireManager.h"
#include "CPlayerData.h"
#include "CPlayerPed.h"
#include "CWaterLevel.h"
#include "common.h"

#include "Blocks.h"
#include "Game.h"
#include "Items.h"
#include "Render3D.h"
#include "Sound.h"

namespace mc {

EffectState gEffects[EFFECT_COUNT];

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

void AddEffect(int effect, float seconds, int amplifier) {
    if (effect < 0 || effect >= EFFECT_COUNT || seconds <= 0.0f)
        return;
    EffectState& e = gEffects[effect];
    if (e.time > 0.0f && (e.amp > amplifier || (e.amp == amplifier && e.time >= seconds)))
        return;
    e.time = seconds;
    e.amp = amplifier;
    e.tick = 0.0f;
    if (effect == EFFECT_ABSORPTION)
        gGame.absorption = std::max(gGame.absorption, 4.0f * (amplifier + 1)); // two yellow hearts per level
}

void RemoveEffect(int effect, CPlayerPed* ped) {
    if (effect < 0 || effect >= EFFECT_COUNT)
        return;
    const bool was = gEffects[effect].time > 0.0f;
    gEffects[effect] = EffectState();
    if (effect == EFFECT_ABSORPTION)
        gGame.absorption = 0.0f;
    if (effect == EFFECT_FIRE_RESISTANCE && was && ped && gGame.gameMode != MODE_CREATIVE)
        ped->bFireProof = false;
}

void ClearEffects(CPlayerPed* ped) {
    for (int i = 0; i < EFFECT_COUNT; ++i)
        RemoveEffect(i, ped);
}

float EffectsAbsorbDamage(float loss, float maxHealth) {
    if (loss <= 0.0f)
        return 0.0f;
    if (HasEffect(EFFECT_RESISTANCE))
        loss *= std::max(0.0f, 1.0f - 0.2f * (gEffects[EFFECT_RESISTANCE].amp + 1));
    if (gGame.absorption > 0.0f) {
        const float unit = maxHealth / 20.0f;
        const float absorbed = std::min(loss, gGame.absorption * unit);
        gGame.absorption -= absorbed / unit;
        loss -= absorbed;
        if (gGame.absorption < 0.05f) {
            gGame.absorption = 0.0f;
            gEffects[EFFECT_ABSORPTION] = EffectState(); // the yellow hearts are gone, so is the effect
        }
    }
    return loss;
}

void EffectsUpdate(float dt, CPlayerPed* ped) {
    const float maxH = ped->m_fMaxHealth > 1.0f ? ped->m_fMaxHealth : 100.0f;
    const float unit = maxH / 20.0f; // one Minecraft health point (half a heart)
    const bool alive = ped->m_fHealth > 0.0f;
    if (!alive) {
        ClearEffects(ped);
        gGame.air = kMaxAir;
        return;
    }
    for (int i = 0; i < EFFECT_COUNT; ++i) {
        EffectState& e = gEffects[i];
        if (e.time <= 0.0f)
            continue;
        e.time -= dt;
        e.tick += dt;
        if (e.time <= 0.0f) {
            RemoveEffect(i, ped);
            continue;
        }
        switch (i) {
        case EFFECT_REGENERATION: {
            const float period = 2.5f / (float)(1 << std::min(e.amp, 5)); // 50 ticks >> amplifier
            while (e.tick >= period) {
                e.tick -= period;
                if (ped->m_fHealth < maxH)
                    ped->m_fHealth = std::min(maxH, ped->m_fHealth + unit);
            }
            break;
        }
        case EFFECT_POISON: {
            const float period = 1.25f / (float)(1 << std::min(e.amp, 4)); // 25 ticks >> amplifier
            while (e.tick >= period) {
                e.tick -= period;
                if (ped->m_fHealth > unit * 1.5f) { // poison never kills
                    ped->m_fHealth -= unit;
                    gGame.directDamage += unit;
                }
            }
            break;
        }
        case EFFECT_HUNGER:
            gGame.exhaustion += 0.1f * (e.amp + 1) * dt; // 0.005 per tick and level
            break;
        case EFFECT_FIRE_RESISTANCE:
            ped->bFireProof = true;
            break;
        default:
            break;
        }
    }

    // air: ten bubbles that run out under water, then drowning
    const CVector eye = gGame.eyePos;
    float wl;
    const bool survival = gGame.gameMode == MODE_SURVIVAL;
    const bool under = !ped->bInVehicle &&
                       ((CWaterLevel::GetWaterLevelNoWaves(eye.x, eye.y, eye.z, &wl) && wl > eye.z + 0.05f) ||
                        FluidAt(eye) == ID_WATER);
    if (under && survival) {
        gGame.air -= 20.0f * dt;
        if (gGame.air <= -20.0f) {
            gGame.air = 0.0f;
            ped->m_fHealth = std::max(0.0f, ped->m_fHealth - 2.0f * unit); // drowning ignores armour
            gGame.directDamage += 2.0f * unit;
            NoteDamage(STR_DEATH_DROWN);
            Bubbles(eye, 8);
        }
        gBubbleTimer -= dt;
        if (gBubbleTimer <= 0.0f) {
            gBubbleTimer = 0.9f + Rand01() * 0.8f;
            Bubbles(eye + gGame.lookDir * 0.25f - CVector(0, 0, 0.1f), 2);
        }
    } else {
        gGame.air = under ? kMaxAir : std::min(kMaxAir, gGame.air + 80.0f * dt);
    }
    // GTA counts the player's breath too and would drown him on its own clock: keep it full
    if (ped->m_pPlayerData) {
        float& breath = ped->m_pPlayerData->m_fBreath;
        gBreathSeen = std::max(gBreathSeen, breath);
        breath = gBreathSeen;
    }
}

} // namespace mc
