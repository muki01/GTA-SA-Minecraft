#include "Particles.h"

#include <cstdlib>

#include "Items.h"

namespace mc {

std::vector<Particle> gParticles;

void SpawnParticle(const Particle& p) {
    if (gParticles.size() < 1500)
        gParticles.push_back(p);
}

void SpawnBreakParticles(const Int3& p, int block) {
    uint16_t tile = BlockFaceTile(block, FACE_NORTH, 0);
    for (int i = 0; i < 16; ++i) {
        Particle pt;
        float rx = Rand01(), ry = Rand01(), rz = Rand01();
        pt.pos = Vec3(p.x + 0.15f + rx * 0.7f, p.y + 0.15f + ry * 0.7f, p.z + 0.15f + rz * 0.7f);
        pt.vel = Vec3((rx - 0.5f) * 3.0f, (ry - 0.5f) * 3.0f, 1.5f + rz * 2.5f);
        pt.maxLife = pt.life = 0.5f + Rand01() * 0.5f;
        pt.tile = tile;
        pt.u = (rand() % 4) * 0.25f;
        pt.v = (rand() % 4) * 0.25f;
        pt.sub = 0.25f;
        pt.size = 0.05f;
        SpawnParticle(pt);
    }
}

void ParticlesTick(float dt) {
    for (size_t i = 0; i < gParticles.size();) {
        Particle& p = gParticles[i];
        p.life -= dt;
        p.vel.z -= p.gravity * dt;
        p.vel = p.vel * (1.0f - Clamp(dt * 0.8f, 0.0f, 1.0f));
        p.pos += p.vel * dt;
        if (p.life <= 0) {
            gParticles[i] = gParticles.back();
            gParticles.pop_back();
        } else {
            ++i;
        }
    }
}

} // namespace mc
