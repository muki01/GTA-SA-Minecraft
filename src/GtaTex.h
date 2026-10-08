#pragma once
// What a GTA model is made of, judged by its textures: the texture's name and average colour pick the
// Minecraft block (a wooden house gives planks, a brick path gives bricks).

#include <functional>

#include "ModCommon.h"

class CEntity;
struct RwTexture;

namespace mc {

struct TexInfo {
    uint16_t block = 0;   // Minecraft block for this texture, 0 = unknown
    uint8_t r = 128, g = 128, b = 128;
    uint8_t alpha = 255;  // average opacity
    bool skip = false;    // decals, shadows, wire: not a solid surface
    bool hasColor = false;
};

// cached by texture name
const TexInfo& TextureInfo(RwTexture* tex);

// every triangle of the entity's own (render) mesh, in world space; false if the model is not loaded
using TriangleFn = std::function<void(const CVector& a, const CVector& b, const CVector& c, RwTexture* tex, uint32_t matColor)>;
bool ForEachTriangle(CEntity* e, const TriangleFn& fn);

// texture of the triangle a ray hits; `nearDist` = where the collision said the hit is (the closest triangle
// to that distance wins). nullptr if nothing was found.
RwTexture* TextureAtRay(CEntity* e, const CVector& origin, const CVector& dir, float maxDist, float nearDist);

// block of the list whose colour is closest
uint16_t NearestBlock(const uint16_t* blocks, int count, int r, int g, int b);
uint16_t ConcreteForColor(int r, int g, int b);
uint16_t PlanksForColor(int r, int g, int b);
uint16_t WoolForColor(int r, int g, int b);
// block for a plain colour (materials without a texture)
uint16_t BlockForColor(int r, int g, int b);

} // namespace mc
