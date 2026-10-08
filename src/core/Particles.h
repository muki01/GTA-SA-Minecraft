#pragma once
// Particles: bits of a broken block, smoke, sparks, bubbles. The core makes them and moves them; drawing them is
// the host's job (it reads gParticles).

#include "Core.h"

namespace mc {

struct Particle {
    Vec3 pos, vel;
    float life = 1.0f, maxLife = 1.0f;
    uint16_t tile = 0;
    float u = 0, v = 0, sub = 1.0f; // sub-rectangle of the tile (block bits)
    float size = 0.1f;
    float gravity = 12.0f;
    uint32_t color = 0xFFFFFFFF;
    bool glow = false;
    uint8_t anim = 0; // 0 fixed tile, 1 firework spark frames, 2 smoke frames
};

extern std::vector<Particle> gParticles;

void SpawnParticle(const Particle& p);
void SpawnBreakParticles(const Int3& p, int block); // the bits of a block that broke
void ParticlesTick(float dt);

} // namespace mc
