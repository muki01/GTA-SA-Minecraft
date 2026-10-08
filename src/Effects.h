#pragma once
// Status effects (golden apples, bad food, totem) and the air supply under water.

#include "ModCommon.h"

class CPlayerPed;

namespace mc {

struct EffectState {
    float time = 0.0f; // seconds left, <= 0 = not active
    int amp = 0;       // amplifier: 0 = level I
    float tick = 0.0f; // time since the effect last did something
};
extern EffectState gEffects[EFFECT_COUNT];

constexpr float kMaxAir = 300.0f; // ticks of air, like Minecraft

// a stronger effect replaces a weaker one, the same strength keeps the longer time
void AddEffect(int effect, float seconds, int amplifier = 0);
inline bool HasEffect(int effect) { return gEffects[effect].time > 0.0f; }
void RemoveEffect(int effect, CPlayerPed* ped = nullptr);
void ClearEffects(CPlayerPed* ped = nullptr);
// regeneration, poison, hunger, fire resistance and drowning (script phase)
void EffectsUpdate(float dt, CPlayerPed* ped);
// what is left of `loss` (GTA health) after resistance and the yellow absorption hearts
float EffectsAbsorbDamage(float loss, float maxHealth);

} // namespace mc
