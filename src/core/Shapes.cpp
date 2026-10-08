#include "Shapes.h"

#include "Items.h"
#include "World.h"

#include <cstring>

namespace mc {

int ShapeConnections(int block, int east, int west, int north, int south) {
    if (!IsShapedBlock(block))
        return 0;
    const int s = Block(block).shape;
    if (s != SHAPE_PANE && s != SHAPE_FENCE && s != SHAPE_WALL)
        return 0;
    auto joins = [&](int o) {
        if (o <= ID_AIR || o >= NUM_BLOCKS)
            return false;
        return Block(o).shape == s || (IsSolidBlock(o) && !IsShapedBlock(o));
    };
    return (joins(east) ? 1 : 0) | (joins(west) ? 2 : 0) | (joins(north) ? 4 : 0) | (joins(south) ? 8 : 0);
}

int ShapeConnectionsAt(int x, int y, int z) {
    return ShapeConnections(gWorld.GetBlock(x, y, z), gWorld.GetBlock(x + 1, y, z), gWorld.GetBlock(x - 1, y, z),
                            gWorld.GetBlock(x, y + 1, z), gWorld.GetBlock(x, y - 1, z));
}

bool IsBedBlock(int block) {
    if (block <= ID_AIR || block >= NUM_BLOCKS)
        return false;
    return Block(block).shape == SHAPE_BED || Block(block).shape == SHAPE_BED_HEAD;
}

int BedOtherBlock(int block) {
    static int other[NUM_BLOCKS] = {};
    static bool init = false;
    if (!init) {
        init = true;
        for (int b = 1; b < NUM_BLOCKS; ++b) {
            if (Block(b).shape != SHAPE_BED)
                continue;
            const size_t n = std::strlen(Block(b).key);
            for (int h = 1; h < NUM_BLOCKS; ++h)
                if (Block(h).shape == SHAPE_BED_HEAD && std::strncmp(Block(h).key, Block(b).key, n) == 0 &&
                    std::strcmp(Block(h).key + n, "_head") == 0) {
                    other[b] = h;
                    other[h] = b;
                }
        }
    }
    return IsBedBlock(block) ? other[block] : 0;
}

Int3 BedOtherCell(const Int3& c, int block, int meta) {
    const Int3& d = FACE_DIR[meta & 3];
    return Block(block).shape == SHAPE_BED ? c + d : Int3{ c.x - d.x, c.y - d.y, c.z - d.z };
}

int ShapeUvTurns(int block, int meta, int face) {
    if (!IsBedBlock(block) || (face != FACE_TOP && face != FACE_BOTTOM))
        return 0;
    // the tiles are drawn for a bed whose head is north; turned clockwise (seen from above) by its side
    static const int kTurns[4] = { 1, 3, 0, 2 }; // east, west, north, south
    const int t = kTurns[meta & 3];
    return face == FACE_TOP ? t : (4 - t) & 3;
}

void TurnUv(int turns, float& h, float& v) {
    for (int i = 0; i < (turns & 3); ++i) {
        const float nh = 1.0f - v;
        v = h;
        h = nh;
    }
}

bool FlushSides(int shape, int other) {
    if (shape >= SHAPE_PANE && shape <= SHAPE_WALL)
        return other == shape;
    return (shape == SHAPE_BED || shape == SHAPE_BED_HEAD) && (other == SHAPE_BED || other == SHAPE_BED_HEAD);
}

int InventoryBoxes(int block, ShapeBox out[kMaxShapeBoxes], int tileBlock[kMaxShapeBoxes], int* meta) {
    const int s = Block(block).shape;
    *meta = s == SHAPE_STAIRS ? FACE_WEST : s == SHAPE_BED ? FACE_NORTH : 0;
    const int connect = s == SHAPE_FENCE || s == SHAPE_WALL || s == SHAPE_PANE ? 1 | 2 : 0;
    int n = BlockShapeBoxes(block, *meta, out, connect);
    for (int i = 0; i < n; ++i)
        tileBlock[i] = block;
    if (s != SHAPE_BED)
        return n;
    // a bed: the foot and the head behind it (north), made small enough to fit
    const int head = BedOtherBlock(block);
    ShapeBox hb[kMaxShapeBoxes];
    const int nh = BlockShapeBoxes(head, *meta, hb);
    for (int i = 0; i < nh && n < kMaxShapeBoxes; ++i, ++n) {
        out[n] = hb[i];
        out[n].y0 += 1.0f;
        out[n].y1 += 1.0f;
        tileBlock[n] = head;
    }
    constexpr float k = 0.6f;
    for (int i = 0; i < n; ++i) {
        ShapeBox& b = out[i];
        b = { (b.x0 - 0.5f) * k + 0.5f, (b.y0 - 1.0f) * k + 0.5f, (b.z0 - 0.28f) * k + 0.45f,
              (b.x1 - 0.5f) * k + 0.5f, (b.y1 - 1.0f) * k + 0.5f, (b.z1 - 0.28f) * k + 0.45f, b.tileFace };
    }
    return n;
}

namespace {
// an arm from the middle (lo..hi wide) to the side `dir` (1 east, 2 west, 4 north, 8 south), z0..z1 high
ShapeBox Arm(int dir, float lo, float hi, float from, float z0, float z1) {
    switch (dir) {
    case 1: return { from, lo, z0, 1.0f, hi, z1 };
    case 2: return { 0.0f, lo, z0, 1.0f - from, hi, z1 };
    case 4: return { lo, from, z0, hi, 1.0f, z1 };
    default: return { lo, 0.0f, z0, hi, 1.0f - from, z1 };
    }
}
} // namespace

int BlockShapeBoxes(int block, int meta, ShapeBox out[kMaxShapeBoxes], int connect, bool forCollision) {
    if (!IsShapedBlock(block))
        return 0;
    constexpr float px = 1.0f / 16.0f;
    int n = 0;
    switch (Block(block).shape) {
    case SHAPE_STAIRS: {
        // a slab on the bottom (on top when upside down) and the half block on the side they rise to
        const bool up = (meta & META_UPSIDE) != 0;
        out[n++] = { 0, 0, up ? 0.5f : 0.0f, 1, 1, up ? 1.0f : 0.5f };
        ShapeBox b{ 0, 0, up ? 0.0f : 0.5f, 1, 1, up ? 0.5f : 1.0f };
        switch (meta & META_STAIRS_SIDE) {
        case FACE_EAST: b.x0 = 0.5f; break;
        case FACE_WEST: b.x1 = 0.5f; break;
        case FACE_NORTH: b.y0 = 0.5f; break;
        default: b.y1 = 0.5f; break;
        }
        out[n++] = b;
        return n;
    }
    case SHAPE_SLAB:
        if (meta & META_SLAB_DOUBLE)
            out[n++] = { 0, 0, 0, 1, 1, 1 };
        else if (meta & META_SLAB_TOP)
            out[n++] = { 0, 0, 0.5f, 1, 1, 1 };
        else
            out[n++] = { 0, 0, 0, 1, 1, 0.5f };
        return n;
    case SHAPE_PANE:
        // a post two pixels thick, and a sheet to every neighbour it joins
        out[n++] = { 7 * px, 7 * px, 0, 9 * px, 9 * px, 1 };
        for (int d = 1; d <= 8; d <<= 1)
            if (connect & d)
                out[n++] = Arm(d, 7 * px, 9 * px, 9 * px, 0, 1);
        return n;
    case SHAPE_FENCE: {
        // a post four pixels thick; two bars to every neighbour it joins (one tall arm for what bumps into it)
        const float h = forCollision ? 1.5f : 1.0f;
        out[n++] = { 6 * px, 6 * px, 0, 10 * px, 10 * px, h };
        for (int d = 1; d <= 8; d <<= 1) {
            if (!(connect & d))
                continue;
            if (forCollision) {
                out[n++] = Arm(d, 6 * px, 10 * px, 10 * px, 0, h);
            } else {
                out[n++] = Arm(d, 7 * px, 9 * px, 10 * px, 6 * px, 9 * px);
                out[n++] = Arm(d, 7 * px, 9 * px, 10 * px, 12 * px, 15 * px);
            }
        }
        return n;
    }
    case SHAPE_WALL: {
        // a post eight pixels thick; a wall six thick and fourteen high to every neighbour it joins
        out[n++] = { 4 * px, 4 * px, 0, 12 * px, 12 * px, forCollision ? 1.5f : 1.0f };
        for (int d = 1; d <= 8; d <<= 1)
            if (connect & d)
                out[n++] = Arm(d, 5 * px, 11 * px, 12 * px, 0, forCollision ? 1.5f : 14 * px);
        return n;
    }
    case SHAPE_BED:
    case SHAPE_BED_HEAD: {
        if (forCollision) {
            out[n++] = { 0, 0, 0, 1, 1, 9 * px };
            return n;
        }
        // the mattress, and two legs at the end of this half (the head's end, or the foot's)
        out[n++] = { 0, 0, 3 * px, 1, 1, 9 * px };
        const int side = Block(block).shape == SHAPE_BED ? (meta & 3) ^ 1 : meta & 3; // FACE_EAST^1 = FACE_WEST, ...
        const float lo = 0.0f, hi = 13 * px, w = 3 * px;
        for (int leg = 0; leg < 2; ++leg) {
            const float a = leg ? hi : lo; // along the end
            ShapeBox b;
            switch (side) {
            case FACE_EAST: b = { hi, a, 0, 1, a + w, w }; break;
            case FACE_WEST: b = { 0, a, 0, w, a + w, w }; break;
            case FACE_NORTH: b = { a, hi, 0, a + w, 1, w }; break;
            default: b = { a, 0, 0, a + w, w, w }; break;
            }
            b.tileFace = FACE_BOTTOM;
            out[n++] = b;
        }
        return n;
    }
    default:
        return 0;
    }
}

} // namespace mc
