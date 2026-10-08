#pragma once
// The loose things of the world: items lying on the ground, experience orbs, primed TNT, and what an explosion does
// to blocks and to them. The core makes and moves them; drawing them is the host's job (it reads gDrops, gXpOrbs
// and gPrimedTnt).

#include "Core.h"
#include "Host.h"
#include "World.h"

namespace mc {

// ---- items on the ground (ItemEntity)
struct DropEntity {
    Vec3 pos, vel;
    ItemStack stack;
    float age = 0.0f;
    float pickupDelay = 0.5f;
    float spin = 0.0f;
    float groundZ = -1000.0f;
    int groundCheck = 0;
};
extern std::vector<DropEntity> gDrops;

// without a velocity the item hops up a little, in a random direction
void SpawnDrop(const Vec3& pos, const ItemStack& s, const Vec3& vel = Vec3(), float delay = 0.5f);
void SpawnDropItem(const Vec3& pos, uint16_t id, int count);
void SpawnBlockDrops(int block, const Vec3& at);   // what a broken block leaves behind
void DropContainerContents(const Int3& p);         // a furnace or chest that goes spills what is in it
// One frame of the items: they fall, float, burn in lava, merge, and jump into the inventory of the `collector`
// (the player's position, feet + 1 m; nullptr: nobody picks anything up) when he is within `reach`.
void DropsTick(float dt, const Vec3* collector, float reach);

// ---- experience orbs
struct XpOrb {
    Vec3 pos, vel;
    int value;
    float age = 0.0f;
};
extern std::vector<XpOrb> gXpOrbs;

void SpawnXp(const Vec3& at, int amount); // as orbs of the usual sizes
// One frame of the orbs: they fall, and fly to the `collector` (the player's position; nullptr: nobody) when he
// is near; touching him adds their experience.
void XpTick(float dt, const Vec3* collector);

// ---- primed TNT
struct PrimedTnt {
    Vec3 pos, vel;
    float fuse;
    bool onGround = false;
};
extern std::vector<PrimedTnt> gPrimedTnt;

// Turns a TNT block into a primed one; without a velocity it does Minecraft's little hop.
void IgniteTnt(const Int3& p, float fuse, const Vec3* velocity = nullptr);
void TntTick(float dt);

// ---- explosions
// Blocks break, TNT nearby is lit, loose things and creatures are thrown. own: the blast is ours (TNT, a charge),
// so the host adds its own fire ball and damage of that `kind` (BlastKind); otherwise it was the host's to begin with.
void ExplodeAt(const Vec3& at, float radius, bool own, int kind = BLAST_TNT);
// pushes dropped items and primed TNT away from a blast
void PushLooseThings(const Vec3& at, float radius, float speed);

} // namespace mc
