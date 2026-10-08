#pragma once

#include "ModCommon.h"
#include "RenderWare.h"
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

void BuildChunkMesh(Chunk& c);
void Render3D(); // the mod's 3D world (called before GTA draws its vehicles)
bool Render3DHooked(); // false: the hook failed, Render3D runs after the scene instead
void Render3DInit(); // logs what the depth buffer can do

struct BlockTargetVisual {
    bool show = false;
    Int3 pos;
    float progress = 0.0f;
};
extern BlockTargetVisual gTargetVisual;

struct DropEntity {
    CVector pos, vel;
    ItemStack stack;
    float age = 0.0f;
    float pickupDelay = 0.5f;
    float spin = 0.0f;
    float groundZ = -1000.0f;
    int groundCheck = 0;
};

struct Particle {
    CVector pos, vel;
    float life = 1.0f, maxLife = 1.0f;
    uint16_t tile = 0;
    float u = 0, v = 0, sub = 1.0f; // sub-rectangle of the tile (block bits)
    float size = 0.1f;
    float gravity = 12.0f;
    RwUInt32 color = 0xFFFFFFFF;
    bool glow = false;
    uint8_t anim = 0; // 0 fixed tile, 1 firework spark frames, 2 smoke frames
};

struct PrimedTnt {
    CVector pos, vel;
    float fuse;
    bool onGround = false;
};

extern std::vector<DropEntity> gDrops;
extern std::vector<Particle> gParticles;
extern std::vector<PrimedTnt> gPrimedTnt;

// round blob shadow on the ground (drawn in the translucent pass of the same frame)
void AddShadow(const CVector& ground, float radius, float strength);

void SpawnBreakParticles(const Int3& p, int block);
// falling sand / gravel (Blocks.cpp)
void RenderFallingBlocks(float light);
void SpawnParticle(const Particle& p);
float DaylightFactor();

// item helpers shared with the GUI / hand renderer
void EmitItemCube(const CVector& center, float half, const CVector& r, const CVector& u, const CVector& f, int block,
                  float light);
void EmitItemSprite(const CVector& center, const CVector& axisU, const CVector& axisV, float half, uint16_t tile,
                    float light);

} // namespace mc
