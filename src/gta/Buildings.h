#pragma once
// What kind of GTA model something is, and the clean-up of version 0.7's building conversion.

#include <cstdio>

#include "ModCommon.h"

class CEntity;

namespace mc {

// a building (walls, roofs, floors), as opposed to a piece of the landscape (earth, grass, sand, roads)
bool IsStructureModel(CEntity* e);
// version 0.7 turned whole buildings into blocks: here they are turned back, one by one, as they stream in
void BuildingsUpdate(float dt);
void BuildingsClear();
void BuildingsWrite(FILE* f);
bool BuildingsRead(FILE* f);

} // namespace mc
