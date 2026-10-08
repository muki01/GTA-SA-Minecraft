#pragma once
// What Minecraft's blocks look like: the faces of a chunk that can be seen, with their textures (atlas UVs) and the
// shade of every corner (light per face direction, ambient occlusion); plants as crossed sheets, fluids with sloping
// surfaces and flowing textures, fire as leaning flames. The host turns the vertices into its own and lights them.

#include "Core.h"
#include "World.h"

namespace mc {

// corner offsets per face: bottom-left, bottom-right, top-right, top-left (seen from outside)
extern const int kFaceCorners[6][4][3];
extern const float kFaceShade[6]; // how bright each face direction is

// a tile of the block / item atlas
struct TileUV {
    float u0, v0, u1, v1;
};
TileUV AtlasTileUV(int tile);

struct MeshVertex {
    float x, y, z;
    float u, v;
    Int3 n; // the face's direction
};

struct BlockMesh {
    std::vector<MeshVertex> verts;  // opaque + cutout, 4 per quad
    std::vector<uint8_t> shade;     // per vertex, 0..255
    std::vector<uint8_t> emissive;
    std::vector<uint8_t> anim;      // animated tiles (fluids, fire): AnimId, 0 = static
    std::vector<MeshVertex> nverts; // the host's own dug ground (Host::OwnGround), seen only through its holes
    std::vector<uint8_t> nshade;
    std::vector<MeshVertex> tverts; // translucent (stained glass, ice, water...)
    std::vector<uint8_t> tshade;
    std::vector<uint8_t> tanim;
    void Clear();
};

void MeshChunk(const Chunk& c, BlockMesh& m);

} // namespace mc
