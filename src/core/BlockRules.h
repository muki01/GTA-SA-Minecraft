#pragma once
// What blocks do by themselves: water and lava flow, fire burns and spreads, sand and gravel fall, plants need
// ground under them, saplings grow into trees. The block world is the core's own; where the host has a map of
// its own (GTA's ground and walls) the rules ask it (Host.h).

#include "Core.h"
#include "Host.h"
#include "Items.h"
#include "World.h"

namespace mc {

// One frame of block logic: neighbour updates, flowing fluids, falling blocks and, once a second, the random
// ticks (saplings, lava, fire). `now` is the host's clock in seconds; fluids flow by it.
void BlocksTick(float dt, float now);
void BlockRulesClear(); // forget what is falling and what is about to flow

struct FallingBlock {
    Vec3 pos; // bottom centre
    Vec3 vel;
    int block;
    float age = 0.0f;
};
const std::vector<FallingBlock>& FallingBlocks();

// ---- fluids
bool PlaceFluid(int block, const Int3& cell);      // bucket: source block
bool TakeFluid(const Int3& cell, int* block);      // empty bucket on a source block
// fluid at a point: ID_WATER, ID_LAVA or ID_AIR
int FluidAt(const Vec3& p);
// how deep a box (feet at `feet`, `height` tall) is in a fluid: 0..1
float FluidDepth(const Vec3& feet, float height, int* block);
// which way the fluid in that cell flows (unit, horizontal; zero for still water)
Vec3 FluidFlowAt(const Int3& cell);

// FlowingFluid.getOwnHeight: a source or falling fluid fills 8/9 of its cell
inline float FluidOwnHeight(Voxel v) {
    return (VoxMeta(v) & META_FLUID_FALLING) ? 8.0f / 9.0f : (8 - (VoxMeta(v) & META_FLUID_LEVEL)) / 9.0f;
}

// FlowingFluid.getFlow. get(dx, dy, dz) returns the voxel next to the cell.
template <class Get>
Vec3 FluidFlowT(int fluid, Voxel self, Get get) {
    const float own = FluidOwnHeight(self);
    float fx = 0.0f, fy = 0.0f;
    for (int f = 0; f < 4; ++f) {
        const Int3& d = FACE_DIR[f];
        const Voxel nv = get(d.x, d.y, 0);
        const int nb = VoxBlock(nv);
        float diff = 0.0f;
        if (nb == fluid) {
            diff = own - FluidOwnHeight(nv);
        } else if (!IsFluidBlock(nb) && !IsSolidBlock(nb)) {
            const Voxel bv = get(d.x, d.y, -1);
            if (VoxBlock(bv) == fluid)
                diff = own - (FluidOwnHeight(bv) - 8.0f / 9.0f);
        }
        fx += d.x * diff;
        fy += d.y * diff;
    }
    const float m = std::sqrt(fx * fx + fy * fy);
    return m > 1e-4f ? Vec3(fx / m, fy / m, 0.0f) : Vec3(0.0f, 0.0f, 0.0f);
}

// ---- fire
bool PlaceFire(const Int3& cell);                  // flint and steel, lava; false if it cannot burn there
bool FireAt(const Vec3& p);                        // a fire block at that point

// ---- plants and trees
bool GrowSapling(const Int3& p);                   // the sapling in that cell becomes a tree, if there is room
void GrowthSparkles(const Vec3& at, int n);        // the green glints of something growing
// may that plant stand in the cell? (on soil - a block or the host's ground; creative plants anywhere solid)
bool CanPlantAt(int plant, const Int3& cell, bool creative);
// Bone meal on a block (sapling / grass block) or on grass of the host's ground. Returns true if it was used.
bool ApplyBoneMeal(bool voxel, const Int3& cell, const Vec3& point, const Vec3& normal, bool hostGrass);
bool IsSoil(int block);                            // what plants grow on
uint16_t RandomFlower();
// cell a plant sits in when placed on the host's ground with its surface at `groundZ`
int PlantCellOnGround(float groundZ);

} // namespace mc
