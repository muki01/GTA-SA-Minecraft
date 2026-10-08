#pragma once
// Minecraft's fighting: blows with what the player holds; everything that is shot or thrown (arrows, tridents,
// snowballs, eggs, ender pearls, fire and wind charges, fireworks, bottles o' enchanting); the items that do something
// on the use button; lightning. The host's own people and vehicles are met through Host (BlowTrace, ShotTrace,
// HurtBeing...), and the host draws what flies (it reads gProjectiles and gBolts).

#include "Core.h"
#include "World.h"

namespace mc {

enum ProjType { PJ_ARROW, PJ_SNOWBALL, PJ_EGG, PJ_PEARL, PJ_FIREWORK, PJ_FIREBALL, PJ_WIND, PJ_TRIDENT, PJ_ROCKET, PJ_XPBOTTLE };

struct Projectile {
    int type;
    Vec3 pos, vel;
    float life = 60.0f;
    bool stuck = false;
    bool crit = false;
    bool pickup = false;
    float fuse = 0.0f;
    bool hostile = false;    // shot by somebody of the host's: it can hit the player
    int shooter = -1;        // who that was (the host's number for him)
    int vehicle = -1;        // the vehicle the shot left from (the host's number for it)
    float ignoreTime = 0.5f; // seconds it still passes through that vehicle
    float damage = 0.0f;     // half hearts, 0 = from the speed
    ItemStack stack;         // a thrown trident
    int slot = -1;           // hotbar slot the trident came from
    bool returning = false;
    float stuckTime = 0.0f;
};
extern std::vector<Projectile> gProjectiles;

struct Bolt {
    Vec3 at;
    float age;
    uint32_t seed;
};
extern std::vector<Bolt> gBolts;

// A blow with the held item at what is under the crosshair: true if somebody was hit (the click is used up).
bool MeleeAttack();
// Items used by holding the use button: bow, crossbow, trident, spyglass. True while the button belongs to one of them.
bool ChargedItemsTick(float dt, bool useDown, bool usePressed);
// The use button with the held item on gTarget (throwing, lighting, spawn eggs, buckets, boats...): true if the item
// did something.
bool UseHeldItem();
void ProjectilesTick(float dt);
// somebody of the host's shoots an arrow at a spot (`shooter`, `vehicle`: the host's numbers, -1 for none)
void ShootArrowAt(const Vec3& from, const Vec3& to, int shooter, int vehicle);
// a burst of wind: everything loose within `radius` is thrown away from `centre` (the player too when asked)
void Gust(const Vec3& centre, float radius, float side, float up, bool playerToo);
void StrikeLightning(const Vec3& at);
void LightningTick(float dt);
bool LightningFlashActive();
void CombatClear();

} // namespace mc
