#pragma once
// Minecraft's farm animals (cow, pig, sheep, chicken): how they wander, flee, follow food, breed, lay eggs, get
// sheared, milked, saddled, ridden and hurt; and the creeper, that comes out at night and blows up next to the player. The host draws them (it reads gMobs) and says where its ground lets
// a herd appear (Host::SpawnGround).

#include "Core.h"

namespace mc {

enum MobKind { MOB_COW = 0, MOB_PIG, MOB_SHEEP, MOB_CHICKEN, MOB_CREEPER, MOB_WARDEN, MOB_KIND_COUNT };

struct Mob {
    int kind = 0;
    Vec3 pos, vel; // pos = feet
    float yaw = 0.0f;
    float health = 10.0f;
    float hurt = 0.0f;   // seconds of red flash left
    float death = -1.0f; // seconds since it died, < 0 alive
    float limbSwing = 0.0f, limbAmount = 0.0f;
    float headYaw = 0.0f, headPitch = 0.0f;
    float aiTimer = 0.0f;
    Vec3 goal{ 0, 1, 0 };
    bool moving = false;
    float panic = 0.0f;
    float sayTimer = 5.0f;
    float eggTimer = 120.0f;
    bool sheared = false;
    float woolTimer = 0.0f;
    float flap = 0.0f, flapSpeed = 0.0f, flapping = 1.0f;
    bool onGround = false;
    bool persistent = false;
    float groundZ = -1000.0f;
    int groundTick = 0;
    float fallFrom = -1000.0f;
    float love = 0.0f;  // seconds in love mode (was fed)
    float baby = 0.0f;  // seconds left as a baby
    float breedCooldown = 0.0f;
    float lavaTimer = 0.0f;
    float burn = 0.0f;      // seconds it keeps burning
    float hitByCar = 0.0f;  // seconds until a vehicle of the host's can hit it again
    uint32_t id = 0;
    bool saddled = false;
    Vec3 rideVel{ 0, 0, 0 };
    float swell = 0.0f; // creeper: 0..1 of its fuse (it blows up at 1)
    int swellDir = -1;  // creeper: +1 swelling, -1 calming down
    // warden
    float anger = 0.0f;        // 80 and up: it goes for the player
    bool roared = false;       // (it roars once when it gets angry)
    float roar = 0.0f;         // seconds of roaring left (it stands)
    float listen = 0.0f;       // seconds until it can hear the next vibration
    float attackCooldown = 0.0f, attackAnim = 0.0f; // its blows (attackAnim 1..0: the arms come down)
    float boom = -1.0f;        // seconds charging a sonic boom, < 0 none
    float boomCooldown = 0.0f;
    float calm = 0.0f;         // seconds without any anger (a minute: it digs back down)
    float heart = 1.0f, pulse = 0.0f; // until the next heartbeat; 1..0 after one
};
bool IsMonster(int kind);
extern std::vector<Mob> gMobs;

float MobScale(const Mob& m);  // babies are half the size
float MobWidth(const Mob& m);
float MobHeight(const Mob& m);

struct MobHit {
    int index = -1;
    float dist = 0.0f;
    Vec3 point;
};

// now and then a small herd appears somewhere around the player (when the game rules and the host allow it)
void MobsSpawnTick(float dt, const Vec3& playerPos);
// One frame of every animal. playerHeld: what the player holds out (food they follow), 0 for nothing.
void MobsTick(float dt, const Vec3& playerPos, uint16_t playerHeld, bool playerOnFoot);
void MobsClear();
// kind: MOB_*. Returns the index or -1.
int SpawnMob(int kind, const Vec3& feet, bool persistent, bool baby = false);
MobHit MobsRaycast(const Vec3& origin, const Vec3& dir, float maxDist);
// halfHearts of damage coming from `from`; knock = knockback strength (1 = a normal hit)
void MobHurt(int index, float halfHearts, const Vec3& from, float knock);
void MobPush(int index, const Vec3& velocity); // m/s added to the animal
// explosions and shock waves: damage and push fall off with the distance
void MobsRadial(const Vec3& at, float radius, float halfHearts, float push);
// use with the held item (shears, bucket, food, saddle) or getting on; true if the use was taken
bool MobInteract(int index, bool playerOnFoot);
Vec3 MobCentre(int index);
// stable ids (indices change when animals despawn)
uint32_t MobIdAt(int index);
int MobIndexById(uint32_t id);
// The player rides this animal: it walks with `velocity` (m/s). Returns false when it is gone.
bool MobRide(uint32_t id, const Vec3& velocity, Vec3* seat, float* yaw);

} // namespace mc
