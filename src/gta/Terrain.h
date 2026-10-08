#pragma once
// Digging into the GTA map. The ground around a dig spot turns into Minecraft blocks under the
// surface ("natural" blocks: grass/dirt/stone/ores...). A dug column is "opened": its GTA ground is
// ignored by our physics and the hole renderer draws through it.
//
// Column model (cell z = [z, z+1)):  GTA surface at gz, "cap" cell = ceil(gz)-1 (the part of the ground
// between the blocks and the GTA surface), natural blocks from the cap cell - 1 downwards.

#include <cstdio>

#include "Carve.h"
#include "GtaWorld.h"
#include "ModCommon.h"

class CEntity;
class CColPoint;

namespace mc {

// GTA ground that can be dug into (outdoor terrain: grass, dirt, sand, rock, roads...)
// grass, earth, sand, roads and pavements: ground whatever model it belongs to
bool TerrainNaturalSurface(int surface);
bool TerrainDiggable(const CColPoint& cp, CEntity* ent, const GtaMaterial& m);
// Converts the ground around the hit point and opens its column. Returns the block that was dug out.
int TerrainDig(const CColPoint& cp, CEntity* ent, const GtaMaterial& m);
// Opens an already converted column (mining a cap from the side, explosions).
int TerrainOpenColumn(int x, int y, bool removeTop);
// TNT next to the ground: blast a crater into the GTA map (call before the blocks are destroyed)
void TerrainExplode(const CVector& at, float radius);

bool TerrainIsConverted(int x, int y);
bool TerrainIsOpened(int x, int y);
float TerrainSurface(int x, int y);  // GTA ground height of a converted column (very low if not converted)
bool TerrainCapAt(int x, int y, int z, float* top); // the cap cell of a closed column (solid from z up to *top)
int TerrainCapBlock(int x, int y);   // what mining a cap gives
// A GTA ray hit on ground that has been dug away (pass through it)
bool TerrainIgnoreHit(const CVector& point, const CEntity* e);
// any hole at all (dug into the ground or broken into a building)
bool TerrainAnyHole();
// Any dug column near `p` (the player then needs our own physics)
bool TerrainNear(const CVector& p, float radius);
bool TerrainCameraUnderground(const CVector& cam);
// DDA ray against the cap cells of closed columns
bool RaycastCaps(const CVector& o, const CVector& d, float maxDist, Int3* cell, int* face, float* dist);
float TerrainDepthShade(int x, int y, int z); // underground gets darker

// The open columns are cut out of the GTA ground models (GeoCut), from a little under the surface to a little
// over it: kerbs and slopes in the column go with it.
uint32_t TerrainVersion(); // changes whenever a column opens
void TerrainBoxesIn(const float lo[3], const float hi[3], std::vector<CutBox>& out); // appended
void TerrainAreas(const CVector& cam, float radius, std::vector<CutBox>& out);      // appended
// a natural block of the dug ground (seen only through the holes) rather than one inside a building
bool TerrainOwnsCell(int x, int y, int z);

// rendering (Render3D): the open columns' space marks the stencil (where the blocks under the ground can be seen),
// skirts fill the gap between the blocks and the GTA surface
bool TerrainAnyOpening(const CVector& cam, float radius);
void TerrainEmitHoleVolume(const CVector& cam, float radius);
void TerrainEmitSkirts(const CVector& cam, float radius, float light);
// the underside of the thin GTA ground layer where the block under it was mined
void TerrainEmitCapBottoms(const CVector& cam, float radius, float light);

void TerrainClear();
void TerrainWrite(FILE* f);
bool TerrainRead(FILE* f);

} // namespace mc
