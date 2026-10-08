#pragma once
// What the player does with the world at arm's length: looking at a block, mining it, placing one, using what is
// in the hand on it, choosing what is in the hand (MultiPlayerGameMode in Minecraft). The host may have things of
// its own within reach - in GTA the ground, the walls, the cars: it says so through Host::Pick and breaks them
// itself (Host::BreakTarget).

#include "Audio.h"
#include "Core.h"
#include "Items.h"
#include "World.h"

namespace mc {

// what the player looks at
struct Target {
    bool valid = false;
    bool voxel = false;        // one of our blocks; otherwise something of the host's
    Int3 pos;                  // its cell
    int face = FACE_TOP;
    Vec3 point, normal;
    // ---- when it is something of the host's
    int virtualBlock = ID_AIR; // the block it counts as (ID_AIR: it cannot be mined)
    float hardness = -2.0f;    // how hard it is (not above -1: as hard as that block)
    Int3 key;                  // which thing it is, for the mining progress (a block: its cell)
    bool outline = true;       // it gets the selection box
    bool grass = false;        // grass of the host's ground: bone meal grows things on it
};
extern Target gTarget;

// the line of sight a target is looked for along
struct PickRay {
    Vec3 origin, dir;
    Vec3 head;     // the player's eyes: a target counts when it is within `reach` of them
    float reach;
    float maxDist; // along the ray
};

void UpdateTarget(); // from gGame.rayOrigin / lookDir / eyePos: fills gTarget

// ---- the sounds of a block, by what it is made of
inline SoundEvent DigSound(int block) { return (SoundEvent)(SND_DIG_STONE + Block(block).sound); }
inline SoundEvent HitSound(int block) { return (SoundEvent)(SND_HIT_STONE + Block(block).sound); }
inline SoundEvent PlaceSound(int block) { return (SoundEvent)(SND_PLACE_STONE + Block(block).sound); }

// ---- mining
// Seconds it takes to break a block with that tool, and whether the tool gets its drops.
// hardnessOverride above -1 replaces the block's own hardness (things of the host's).
float BreakSecondsFor(int block, float hardnessOverride, const ItemStack& tool, bool* canHarvest);
float BreakSeconds(int block, const ItemStack& tool, bool* canHarvest);
// a block of ours breaks: bits, sound, its drops and experience (withDrops: when the tool in the hand gets them)
void BreakVoxel(const Int3& p, bool withDrops);
void InteractTick(float dt);             // the cooldowns between two blocks
// One frame of the attack button on the target (`mining`: it is held and was not used up by a hit): survival
// mines for as long as the block takes, creative breaks at once.
void MineTick(float dt, bool mining);
void StopMining();
float MiningProgress();                  // 0..1 for the cracks on the block (always 0 in creative)

// ---- the hands
void HotbarTick();                       // number keys and the wheel choose the slot
void SwapHands();                        // main hand <-> off hand
void DropHeldItem(bool wholeStack);      // throws what is in the hand
void PickBlock();                        // creative: the block looked at goes into the hand

// ---- using
bool IsContainer(int block);             // crafting table, furnaces, chest, barrel
bool TargetIsContainer();
void OpenTargetContainer();
// does the main hand do anything with a use? (if not, the off hand gets it)
bool HasRightClickUse(const ItemStack& s);
bool UseWorldItem();                     // buckets, bone meal, putting armour on: true if the item did something
void PlaceHeldBlock();
bool PlaceReady();                       // holding "use" places the next block after a short pause

} // namespace mc
