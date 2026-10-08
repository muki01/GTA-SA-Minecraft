#pragma once
// The survival side of a Minecraft player: hunger, status effects, the air supply, armour, the totem, eating and
// experience. Health itself belongs to the host (GTA keeps a person's health and takes damage from it by its own
// rules): these rules are handed the health and the host's full health, and change the health in place. One
// Minecraft health point (half a heart) is a twentieth of the host's full health.

#include "Core.h"

namespace mc {

struct EffectState {
    float time = 0.0f; // seconds left, <= 0 = not active
    int amp = 0;       // amplifier: 0 = level I
    float tick = 0.0f; // time since the effect last did something
};

constexpr float kMaxAir = 300.0f;   // ticks of air, like Minecraft (ten bubbles)
constexpr float kEatSeconds = 1.6f; // 32 ticks
// what tires the player (exhaustion; 4 of it cost one point of saturation, then of food)
constexpr float kExhaustAttack = 0.1f, kExhaustMine = 0.005f;

struct Survival {
    float food = 20.0f;
    float saturation = 5.0f;
    float exhaustion = 0.0f;
    float foodTimer = 0.0f;
    float absorption = 0.0f;  // yellow hearts, in Minecraft health points (half hearts)
    float air = kMaxAir;      // ticks of air left under water
    float eatTimer = 0.0f;    // seconds the player has been eating what is in the hand
    float eatSoundTimer = 0.0f;
    EffectState effects[EFFECT_COUNT];
    int xpLevel = 0;
    float xpProgress = 0.0f;  // 0..1 towards the next level
    int xpTotal = 0;

    void Respawn();  // a full stomach after dying
    void NewGame();  // a new world: a full stomach and no experience
};
extern Survival gSurvival;

// what the rules below did to the player this frame, for the host to show (sounds, messages, particles)
struct SurvivalEvents {
    unsigned expired = 0;       // effects that ran out (bit per EFFECT_*)
    float directDamage = 0.0f;  // health taken by poison and drowning: armour does not block it
    bool drowned = false;       // a drowning hit
    bool starved = false;       // a starvation hit
};

// ---- status effects (golden apples, bad food, totem)
// a stronger effect replaces a weaker one, the same strength keeps the longer time
void AddEffect(int effect, float seconds, int amplifier = 0);
inline bool HasEffect(int effect) { return gSurvival.effects[effect].time > 0.0f; }
void RemoveEffect(int effect);
void ClearEffects();
// one frame of the running effects: regeneration heals, poison hurts (never kills), hunger tires
void EffectsTick(float dt, float& health, float maxHealth, SurvivalEvents& ev);
// what is left of `loss` (host health) after resistance and the yellow absorption hearts
float AbsorbDamage(float loss, float maxHealth);

// ---- air: ten bubbles that run out under water, then a drowning hit every second
void BreathTick(float dt, bool underWater, bool mortal, float& health, float maxHealth, SurvivalEvents& ev);

// ---- hunger
// Exhaustion eats saturation, then food; every four seconds a full stomach heals and an empty one starves (down
// to a tenth of full health). True when that four-second check was made.
bool HungerTick(float dt, float& health, float maxHealth, SurvivalEvents& ev);
void CreativeTick(float& health, float maxHealth); // creative: never hungry, never hurt for long
inline bool TooHungryToSprint() { return gSurvival.food <= 6.0f; }

// ---- eating
// Food needs room in the stomach (and survival); golden apples and milk always go down.
bool CanEat(int item, bool mortal);
struct EatResult {
    bool eating = false; // the player is eating or drinking right now
    int finished = 0;    // the item that was just eaten or drunk up (0 = none)
};
// One frame of holding "use" with something to eat or drink in the hand: chewing sounds, and after kEatSeconds
// the meal is finished.
EatResult EatingTick(float dt, bool useHeld, bool mortal);
// The player finished eating / drinking what is in the hand: it fills the stomach, starts or cures effects, and in
// survival is used up (milk leaves the bucket). Burp.
void FinishEating(bool mortal);

// ---- armour and the totem
int ArmorPoints();           // of the pieces worn
float ArmorBlock();          // share of a hit they block: 4% a point, up to 80%
int WearArmor(int amount);   // a hit wears every piece; returns how many broke (each with its sound)
// Totem of undying in either hand, health under 15%: it is used up, health goes to half and every effect makes
// way for regeneration, absorption and fire resistance. True if it saved the player.
bool UseTotem(float& health, float maxHealth);

// ---- experience
int XpToNextLevel(int level);
bool AddXp(int points);      // true when a level worth its fanfare was reached (every fifth)
int XpForBlock(int block);   // ores give experience when mined
int XpOrbSize(int amount);   // the biggest orb that fits into `amount` (ExperienceOrb.getExperienceValue)

} // namespace mc
