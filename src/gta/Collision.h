#pragma once

#include "ModCommon.h"

class CEntity;

namespace mc {

// Gives the voxel world real GTA collision: every 16x16 column near the player gets an
// invisible static CObject whose collision model is a set of boxes built from the blocks.
void CollisionUpdate();     // every frame (script phase)
void CollisionForgetAll();  // after the game deleted objects (new game / load)
bool IsCollisionObject(const CEntity* e);

} // namespace mc
