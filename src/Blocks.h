#pragma once
// Block logic: falling sand and gravel, flowing water and lava, fire, plants that need ground, growing saplings.

#include <cmath>

#include "ModCommon.h"
#include "Items.h"
#include "World.h"

class CPlayerPed;

namespace mc {

void BlocksUpdate(float dt); // script phase
void BlocksClear();

// fluids
bool PlaceFluid(int block, const Int3& cell);      // bucket: source block
bool TakeFluid(const Int3& cell, int* block);      // empty bucket on a source block
// fluid at a point (nullptr level check): returns ID_WATER, ID_LAVA or ID_AIR
int FluidAt(const CVector& p);
// how deep a box (feet at `feet`, `height` tall) is in a fluid: 0..1
float FluidDepth(const CVector& feet, float height, int* block);
// which way the fluid in that cell flows (unit, horizontal; zero for still water)
CVector FluidFlowAt(const Int3& cell);

// FlowingFluid.getOwnHeight: a source or falling fluid fills 8/9 of its cell
inline float FluidOwnHeight(Voxel v) {
    return (VoxMeta(v) & META_FLUID_FALLING) ? 8.0f / 9.0f : (8 - (VoxMeta(v) & META_FLUID_LEVEL)) / 9.0f;
}

// FlowingFluid.getFlow. get(dx, dy, dz) returns the voxel next to the cell.
template <class Get>
CVector FluidFlowT(int fluid, Voxel self, Get get) {
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
    return m > 1e-4f ? CVector(fx / m, fy / m, 0.0f) : CVector(0.0f, 0.0f, 0.0f);
}

// fire
bool PlaceFire(const Int3& cell);                  // flint and steel, lava; false if it cannot burn there
bool FireAt(const CVector& p);                     // a fire block at that point

// plants and trees
bool CanPlantAt(int plant, const Int3& cell, bool creative);
bool GrowSapling(const Int3& p);
// bone meal on a voxel (sapling / grass block) or on GTA grass ground. Returns true if it was used.
bool ApplyBoneMeal(bool voxel, const Int3& cell, const CVector& point, const CVector& normal, bool gtaGrass);
// cell a plant sits in when placed on GTA ground with its surface at `groundZ`
int PlantCellOnGround(float groundZ);

} // namespace mc
