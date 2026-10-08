#pragma once
// Breaking into GTA buildings, roofs and cliffs. The GTA model keeps its look; the cell that was broken becomes a
// hole in it (its triangles there are cut out of the model by GeoCut and stop colliding) and what lies behind is
// Minecraft blocks, which appear as the player digs on.

#include <cstdio>
#include <vector>

#include "ModCommon.h"

class CColPoint;
class CEntity;

namespace mc {

bool CarvedCell(int x, int y, int z);
// a point of GTA geometry that lies in a carved cell: not drawn, does not collide
bool CarvedAt(const CVector& p);
// the camera is in space that was dug out of a building
bool CarveInside(const CVector& cam);

// The player broke the GTA surface at `cp` (looking along `dir`). `block` is what the surface is made of.
// Returns the block to drop (ID_AIR if nothing happened).
int CarveBreak(const CColPoint& cp, CEntity* ent, const CVector& dir, int block);
// a natural block next to carved space was mined (from World's hook)
void CarveNaturalRemoved(int x, int y, int z, int block);

// The rim of a hole: cells the GTA model still runs through. Only what is between the model's faces is solid, as
// columns of a quarter block: up to 16 boxes { x0, y0, z0, x1, y1, z1 }. Returns how many.
int CarveSkinBoxes(int x, int y, int z, float boxes[][6], int max);
int CarveSkinBlock(int x, int y, int z);
bool CarveRaycastSkins(const CVector& o, const CVector& d, float maxDist, Int3* cell, int* face, float* dist);
int CarveBreakSkin(const Int3& cell); // returns the block to drop

// A blast (TNT, rockets) next to a GTA building blows a crater into it. Returns the cells it opened.
struct CarveOpened {
    Int3 cell;
    uint16_t block;
    bool surface; // a GTA surface was there (it gives drops), not only the inside of the building
};
std::vector<CarveOpened> CarveExplode(const CVector& at, float radius);
// per frame: works out the rims of craters bit by bit
void CarveUpdate();

// The space that is cut out of the GTA models: every carved cell, stretched along the GTA surface's axis so that
// the surface running just outside the cell goes too.
struct CutBox {
    float lo[3], hi[3];
};
uint32_t CarveVersion(); // changes whenever the carved space changes
// boxes that touch the world-space box lo..hi (appended to `out`)
void CarveBoxesIn(const float lo[3], const float hi[3], std::vector<CutBox>& out);
// bounds of the carved space per 16 m square around `cam` (to look for the GTA models it cuts; appended)
void CarveAreas(const CVector& cam, float radius, std::vector<CutBox>& out);

// rendering: the cross-section of the GTA models on the holes' boundaries (only seen from inside the holes)
void CarveEmitCaps(const CVector& cam, float radius, float light);

void CarveClear();
void CarveWrite(FILE* f);
bool CarveRead(FILE* f);

} // namespace mc
