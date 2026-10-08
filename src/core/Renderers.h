#pragma once
// Minecraft's renderers for what is not a model: items on the ground, primed TNT, experience orbs, particles, falling
// blocks, arrows and other projectiles, lightning, the fishing line; the cracks of a block being mined; the camera's
// rules (view bobbing, the wider view, the distance in third person). They draw through the host's ModelSink
// (McModel.h); the host keeps its own culling, shadows, lights and blending.

#include "Combat.h"
#include "Core.h"
#include "Entities.h"
#include "Particles.h"

namespace mc {

constexpr float kThirdPersonDistance = 4.0f; // the camera behind (or in front of) the player

// a block cube / an item sprite in the world: centre, half size, its axes
void DrawCube(const Vec3& center, float half, const Vec3& r, const Vec3& u, const Vec3& f, int block, float light);
void DrawSprite3D(const Vec3& center, const Vec3& axisU, const Vec3& axisV, float half, uint16_t tile, float light);

bool DrawDrop(const DropEntity& d, float light); // false: nothing to draw
void DrawPrimedTnt(const PrimedTnt& t, float light);
void DrawXpOrb(const XpOrb& o, const Vec3& camRight, const Vec3& camUp);
void DrawParticle(const Particle& p, const Vec3& camRight, const Vec3& camUp, float light);
void DrawFallingBlocks(float light);
void DrawProjectile(const Projectile& pr, const Vec3& camRight, const Vec3& camUp, float light);
void DrawBolt(const Bolt& b, const Vec3& cam);
void DrawFishingHook(const Vec3& camRight, const Vec3& camUp, float light);
void DrawFishingLine(const Vec3& tip, const Vec3& cam);
int CrackTile(float progress); // the destroy stage's tile for mining progress 0..1

void BobView(const Vec3& front, Vec3& pos, Vec3& look); // the first person camera while walking
float FovWanted(float base);                            // sprinting, flying and the spyglass change the view

} // namespace mc
