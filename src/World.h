#pragma once

#include <unordered_set>

#include "ModCommon.h"
#include "Items.h"

namespace mc {

constexpr int CS = 16; // chunk edge length in blocks
constexpr int CS3 = CS * CS * CS;

// A voxel stores the block id in the low 12 bits and metadata (facing / axis / lit) in the high 4 bits.
using Voxel = uint16_t;
inline int VoxBlock(Voxel v) { return v & 0x0FFF; }
inline int VoxMeta(Voxel v) { return v >> 12; }
inline Voxel MakeVox(int block, int meta = 0) { return (Voxel)((block & 0x0FFF) | ((meta & 0xF) << 12)); }

struct ChunkMesh;

struct Chunk {
    Int3 pos; // chunk coordinates
    Voxel data[CS3] = {};
    int nonAir = 0;
    bool meshDirty = true;
    std::unique_ptr<ChunkMesh> mesh;

    Chunk();
    ~Chunk();
    static int Index(int lx, int ly, int lz) { return lx + CS * (ly + CS * lz); }
    Voxel Get(int lx, int ly, int lz) const { return data[Index(lx, ly, lz)]; }
};

struct ItemStack {
    uint16_t id = 0;
    uint8_t count = 0;
    uint16_t damage = 0;
    bool Empty() const { return id == 0 || count == 0; }
    void Clear() { id = 0; count = 0; damage = 0; }
    bool SameItem(const ItemStack& o) const { return id == o.id && damage == o.damage; }
};

struct FurnaceState {
    ItemStack input, fuel, output;
    int burnTime = 0;      // ticks of fuel left
    int burnTimeTotal = 0; // ticks the current fuel lasts
    int cookTime = 0;      // ticks of progress (200 = done)
    float tickAccum = 0.0f;
};

struct ChestState {
    ItemStack slots[27];
};

class World {
public:
    Voxel Get(int x, int y, int z) const;
    int GetBlock(int x, int y, int z) const { return VoxBlock(Get(x, y, z)); }
    bool IsSolid(int x, int y, int z) const;
    // Set() also tells the block logic (falling sand, fluids, plants) that the neighbourhood changed;
    // SetRaw() is for generating terrain and trees.
    void Set(int x, int y, int z, Voxel v);
    void SetRaw(int x, int y, int z, Voxel v);
    Chunk* FindChunk(const Int3& cpos) const;
    void Clear();

    void Write(FILE* f) const;
    bool Read(FILE* f);

    std::unordered_map<Int3, std::unique_ptr<Chunk>, Int3Hash> chunks;
    std::unordered_map<Int3, FurnaceState, Int3Hash> furnaces;
    std::unordered_map<Int3, ChestState, Int3Hash> chests;
    // keyed by (cx, cy, 0): bumped whenever a block in that 16x16 column changes
    std::unordered_map<Int3, uint32_t, Int3Hash> columnVersion;
    // keyed by (cx, cy, 0): chunk z coordinates present in that column
    std::unordered_map<Int3, std::vector<int>, Int3Hash> columnChunks;
    bool dirty = false;
    std::vector<Int3> updates;                    // cells to re-check (block logic)
    std::unordered_set<Int3, Int3Hash> ticking;   // saplings, lava, fire (random ticks)

private:
    void MarkMeshDirty(int x, int y, int z);
    void SetImpl(int x, int y, int z, Voxel v, bool notify);
};

extern World gWorld;
// called when a natural (GTA ground) block disappears; set by the terrain code
extern void (*gNaturalRemovedHook)(int x, int y, int z, int block);

inline Int3 ChunkOf(int x, int y, int z) { return { FloorDiv(x, CS), FloorDiv(y, CS), FloorDiv(z, CS) }; }

struct VoxelHit {
    bool hit = false;
    Int3 pos;
    int face = 0;
    float dist = 0;
};
// DDA ray through the voxel grid; the block containing the origin is ignored. Fluids are passed through
// unless `fluids` is set.
VoxelHit RaycastVoxels(const CVector& origin, const CVector& dir, float maxDist, bool fluids = false);

void WriteStack(FILE* f, const ItemStack& s);
ItemStack ReadStack(FILE* f);
// item ids of saves written before items moved to their fixed range
void SetItemIdMigration(int oldFirstItem);
int MigrateItemId(int id);

} // namespace mc
