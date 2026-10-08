#pragma once

#include "ModCommon.h"
#include "RenderWare.h"

#include "Entities.h"
#include "Particles.h"
#include "World.h"

namespace mc {

// Pre-built geometry of one chunk. Colour is computed every frame from `shade` and daylight.
struct ChunkMesh {
    std::vector<RwIm3DVertex> verts; // opaque + cutout, 4 per quad
    std::vector<uint8_t> shade;
    std::vector<uint8_t> emissive;
    std::vector<uint8_t> anim;       // animated tiles (fluids, fire): AnimId, 0 = static
    std::vector<RwIm3DVertex> nverts; // natural ground under the GTA map (only seen through holes)
    std::vector<uint8_t> nshade;
    std::vector<RwIm3DVertex> tverts; // translucent (stained glass, ice, water...)
    std::vector<uint8_t> tshade;
    std::vector<uint8_t> tanim;
};

void Render3D(); // the mod's 3D world (called before GTA draws its vehicles)
bool Render3DHooked(); // false: the hook failed, Render3D runs after the scene instead
void Render3DInit(); // logs what the depth buffer can do

struct BlockTargetVisual {
    bool show = false;
    Int3 pos;
    float progress = 0.0f;
};
extern BlockTargetVisual gTargetVisual;

// round blob shadow on the ground (drawn in the translucent pass of the same frame)
void AddShadow(const CVector& ground, float radius, float strength);

// falling sand / gravel (Blocks.cpp)
void RenderFallingBlocks(float light);
float DaylightFactor();

} // namespace mc
