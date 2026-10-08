#pragma once
// The game Minecraft runs in ("the host"): its own map, its water and weather, and what only it can do. The core
// asks through this interface; the host registers its answers once with SetHost. Every question has a "there is
// no such thing here" answer by default, so a host without a map of its own (the offline tests) says nothing.

#include "Core.h"

namespace mc {

// kinds of explosions, for the host's own fire ball and damage
enum BlastKind { BLAST_TNT = 0, BLAST_SMALL, BLAST_FIRE };

struct Target;   // Interact.h
struct PickRay;  // Interact.h
struct VoxelHit; // World.h

// a cell of the host's map that an explosion opened
struct BlastedCell {
    Int3 cell;
    int block;    // what it was made of
    bool surface; // it could be seen
};

// What a blow or a shot met among the host's own things.
struct HostHit {
    bool hit = false;       // a wall, a vehicle, somebody
    float dist = 1e9f;      // how far along the line
    Vec3 point;
    int vehicle = -1;       // what was hit is a vehicle (the host's number for it)
    int being = -1;         // somebody was hit (the host's number for him); he may sit behind what was hit, in a vehicle
    float beingDist = 1e9f;
    Vec3 beingPoint;
};

// Whose shot it is, in the host's own numbers.
struct ShotOwner {
    bool hostile = false; // not the player's: it can hit him
    int shooter = -1;     // who shot it when it is not the player's (-1: nobody in particular)
    int vehicle = -1;     // the vehicle it left from and passes through (-1: none)
};

enum HurtKind { HURT_FIST, HURT_SWORD, HURT_SHOT };
enum HostVehicle { HOST_BOAT, HOST_MINECART };

struct Host {
    virtual ~Host() = default;
    // ---- its map
    virtual bool SolidCell(const Int3& c) { return false; }                  // its ground fills the cell
    virtual bool WallBetween(const Int3& a, const Int3& b) { return false; } // one of its walls between two neighbours
    virtual bool Supports(const Int3& c) { return false; }                   // it carries a block standing in the cell
    virtual bool CapAt(const Int3& c, float* top) { return false; }          // a partly filled cell, solid up to *top
    virtual bool GroundBelow(const Vec3& from, float maxDrop, float* z) { return false; } // its own ground under a point
    virtual bool LineBlocked(const Vec3& a, const Vec3& b) { return false; } // a wall, a car, a prop between two points
    virtual bool WaterLevel(const Vec3& at, float* level) { return false; }  // its own water (sea, pools) at that spot
    virtual bool FlammableUnder(const Int3& c) { return false; }             // bushes, dry grass under the cell
    // earth of its ground under a point (sand too when asked); *z: how high it is
    virtual bool SoilBelow(const Vec3& from, bool sandToo, float* z) { return false; }
    virtual bool CellBlocked(const Int3& c) { return false; }                // someone or something of its own is in the cell
    virtual bool Raining() { return false; }                                 // rain puts fires out
    virtual bool Outdoors() { return true; }                                 // not inside one of its buildings
    virtual bool SpawnGround(const Vec3& from, Vec3* ground) { return false; } // a spot below `from` where animals may appear
    // ---- what the player looks at
    // Something of the host's along the ray (`blocks`: what the ray hits among our blocks). True when the host
    // decided: `out` is its thing, or stays invalid because nothing is within reach. False: the block it is, if any.
    virtual bool Pick(const PickRay& ray, const VoxelHit& blocks, Target& out) { return false; }
    virtual void BreakTarget() {}                                            // gTarget, a thing of the host's, was mined
    // ---- the player, as long as the host keeps him (feet + 1 m; false: there is no player)
    virtual bool PlayerPos(Vec3* pos) { return false; }
    virtual Vec3 PlayerVelocity() { return Vec3(); }                         // m/s (his vehicle's when he sits in one)
    virtual bool PlayerOnGround() { return true; }
    virtual bool PlayerFalling() { return false; }                           // coming down through the air: his blows are critical
    virtual int PlayerVehicle() { return -1; }                               // its number for the vehicle he sits in, -1: on foot
    virtual void MovePlayer(const Vec3& to) {}                               // an ender pearl took him there
    virtual void HurtPlayer(float halfHearts) {}
    // ---- its own people and vehicles in a fight
    // how far the first of them is along the ray; `nothing` when there is none within reach
    virtual float AimDistance(const Vec3& origin, const Vec3& dir, float reach, float nothing) { return nothing; }
    virtual HostHit BlowTrace(const Vec3& origin, const Vec3& dir, float reach) { return HostHit(); } // the player's blow
    virtual HostHit ShotTrace(const Vec3& from, const Vec3& dir, float len, const ShotOwner& by) { return HostHit(); }
    virtual void HurtBeing(int being, float halfHearts, int how, const ShotOwner* by) {} // HurtKind; by nullptr: the player
    virtual void PushBeing(int being, const Vec3& velocity) {}               // m/s added to somebody on foot
    virtual void HurtVehicle(int vehicle, float halfHearts) {}
    // a burst of wind throws its people, vehicles and loose things away from `centre` (the player too when asked)
    virtual void Gust(const Vec3& centre, float radius, float side, float up, bool playerToo) {}
    // ---- what only the host can do
    virtual void FluidPlaced(const Int3& c) {}                               // a bucket was emptied here
    virtual void Douse(const Vec3& at, float radius) {}                      // water was poured: its fires there go out
    virtual void Ignite(const Vec3& at, float seconds, int spread) {}        // a fire of its own (spread: how often it may jump)
    virtual bool Thunderstorm() { return false; }                            // a trident calls down lightning
    virtual bool PlaceVehicle(int kind, const Vec3& at, float headingDeg) { return false; } // HostVehicle
    virtual void BoostVehicle(float seconds) {}                              // a firework strapped to the player's vehicle
    virtual bool ItemAttack(int special) { return false; }                   // attack / use with an item that is the host's
    virtual bool ItemUse(int special) { return false; }                      // own business; true: the click was used
    virtual void Explosion(const Vec3& at, int kind) {}                      // its own fire ball and damage (BlastKind)
    virtual void BlastMap(const Vec3& at, float radius, std::vector<BlastedCell>& opened) {} // craters in its map
};
void SetHost(Host* host); // nullptr: no host
Host& TheHost();

// highest solid surface (the host's ground, its caps, blocks) below `from`; false if there is none within `maxDrop`
bool GroundBelow(const Vec3& from, float maxDrop, float* zOut);

} // namespace mc
