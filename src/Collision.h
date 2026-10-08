#pragma once

#include "ModCommon.h"

class CEntity;

namespace mc {

// Gives the voxel world real GTA collision: every 16x16 column near the player gets an
// invisible static CObject whose collision model is a set of boxes built from the blocks.
void CollisionUpdate();     // every frame (script phase)
void CollisionForgetAll();  // after the game deleted objects (new game / load)
void CollisionShutdown();   // remove our objects (mod disabled / exit)
bool IsCollisionObject(const CEntity* e);
int CollisionObjectCount();

} // namespace mc
