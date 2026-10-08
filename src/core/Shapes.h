#pragma once
// Blocks that are not one full cube are made of boxes: stairs (a half slab and a half block on the side they rise to),
// slabs (the lower or upper half, or both), glass panes and iron bars, fences and walls (a post, and arms towards the
// neighbours they join), beds (a mattress on two legs, in two blocks: the foot and the head). They are drawn, walked on
// and collided with as these boxes.

#include "Core.h"

namespace mc {

struct ShapeBox {
    float x0, y0, z0, x1, y1, z1; // inside the cell, 0..1 (fences and walls reach 1.5 high when collided with)
    int tileFace = -1;            // >= 0: every face of the box shows the tile of that face (a bed's wooden legs)
};
constexpr int kMaxShapeBoxes = 12;

// which neighbours a pane, fence or wall joins: 1 east, 2 west, 4 north, 8 south (their own kind, or a full block)
int ShapeConnections(int block, int east, int west, int north, int south);
int ShapeConnectionsAt(int x, int y, int z); // from the world
// the boxes of a shaped block (IsShapedBlock) with this meta and these connections; 0 for anything else.
// forCollision: what bumps into it (fences and walls are 1.5 high, as in Minecraft, and cannot be jumped over)
int BlockShapeBoxes(int block, int meta, ShapeBox out[kMaxShapeBoxes], int connect = 0, bool forCollision = false);
// faces whose tile turns with the block (a bed's top): quarter turns of (h, v), the face's position in the tile
int ShapeUvTurns(int block, int meta, int face);
void TurnUv(int turns, float& h, float& v);
// faces against such a neighbour are not drawn (panes, fences, walls of a kind; the parts of beds)
bool FlushSides(int shape, int other);

// beds: the block of the other half (foot <-> head), the cell of the other half (meta: the side the head is on)
bool IsBedBlock(int block);
int BedOtherBlock(int block);
Int3 BedOtherCell(const Int3& c, int block, int meta);

// how a shaped block looks as an item (in the inventory, in the hand): its boxes in the unit cube, the block whose
// tiles each one shows, and the meta for them
int InventoryBoxes(int block, ShapeBox out[kMaxShapeBoxes], int tileBlock[kMaxShapeBoxes], int* meta);

} // namespace mc
