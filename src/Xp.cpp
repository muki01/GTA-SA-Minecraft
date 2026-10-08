#include "Xp.h"

#include "CCamera.h"
#include "CPlayerPed.h"
#include "common.h"

#include "Draw3D.h"
#include "Game.h"
#include "Items.h"
#include "Sound.h"
#include "Textures.h"

namespace mc {

namespace {
struct Orb {
    CVector pos, vel;
    int value;
    float age = 0.0f;
};
std::vector<Orb> gOrbs;

// ExperienceOrb.getExperienceValue: the sizes orbs are split into
int OrbSize(int amount) {
    static const int kSizes[] = { 2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3, 1 };
    for (int s : kSizes)
        if (amount >= s)
            return s;
    return 1;
}

int OrbIcon(int value) {
    static const int kSteps[] = { 2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3 };
    for (int i = 0; i < 10; ++i)
        if (value >= kSteps[i])
            return 10 - i;
    return 0;
}
} // namespace

int XpToNextLevel(int level) {
    if (level >= 30)
        return 112 + (level - 30) * 9;
    if (level >= 15)
        return 37 + (level - 15) * 5;
    return 7 + level * 2;
}

void AddXp(int points) {
    if (points <= 0)
        return;
    const int before = gGame.xpLevel;
    gGame.xpTotal += points;
    float p = gGame.xpProgress * XpToNextLevel(gGame.xpLevel) + points;
    while (p >= XpToNextLevel(gGame.xpLevel)) {
        p -= XpToNextLevel(gGame.xpLevel);
        gGame.xpLevel++;
    }
    gGame.xpProgress = p / XpToNextLevel(gGame.xpLevel);
    if (gGame.xpLevel > before && gGame.xpLevel % 5 == 0)
        PlaySfx(SND_LEVELUP, nullptr, 0.75f);
    gWorld.dirty = true;
}

void SpawnXp(const CVector& at, int amount) {
    while (amount > 0 && gOrbs.size() < 300) {
        const int v = OrbSize(amount);
        amount -= v;
        Orb o;
        o.pos = at;
        o.vel = CVector((Rand01() - 0.5f) * 4.0f, (Rand01() - 0.5f) * 4.0f, 2.0f + Rand01() * 3.0f);
        o.value = v;
        gOrbs.push_back(o);
    }
}

int XpForBlock(int block) {
    auto range = [](int lo, int hi) { return lo + rand() % (hi - lo + 1); };
    switch (block) {
    case ID_COAL_ORE: case ID_DEEPSLATE_COAL_ORE: return range(0, 2);
    case ID_DIAMOND_ORE: case ID_DEEPSLATE_DIAMOND_ORE: return range(3, 7);
    case ID_EMERALD_ORE: case ID_DEEPSLATE_EMERALD_ORE: return range(3, 7);
    case ID_LAPIS_ORE: case ID_DEEPSLATE_LAPIS_ORE: return range(2, 5);
    case ID_REDSTONE_ORE: case ID_DEEPSLATE_REDSTONE_ORE: return range(1, 5);
    case ID_NETHER_QUARTZ_ORE: return range(2, 5);
    default: return 0;
    }
}

void XpUpdate(float dt, CPlayerPed* ped) {
    const bool canCollect = ped && ped->m_fHealth > 0.0f && gGame.enabled;
    const CVector target = ped ? ped->GetPosition() : CVector(0, 0, -10000.0f);
    for (size_t i = 0; i < gOrbs.size();) {
        Orb& o = gOrbs[i];
        o.age += dt;
        CVector d = target - o.pos;
        const float dist = d.Magnitude();
        if (canCollect && dist < 8.0f && o.age > 0.5f) {
            // the orb is pulled towards the player, faster as it comes closer
            float pull = 1.0f - dist / 8.0f;
            o.vel += d * (1.0f / std::max(dist, 0.1f)) * (pull * pull * 40.0f * dt);
            o.vel = o.vel * std::pow(0.9f, dt * 20.0f);
        } else {
            o.vel.z -= 6.0f * dt;
            o.vel.x *= std::pow(0.98f, dt * 20.0f);
            o.vel.y *= std::pow(0.98f, dt * 20.0f);
        }
        CVector np = o.pos + o.vel * dt;
        float gz;
        if (o.vel.z < 0.0f && GroundBelow(CVector(np.x, np.y, o.pos.z + 0.2f), 1.0f, &gz) && np.z < gz + 0.1f) {
            np.z = gz + 0.1f;
            o.vel.z *= -0.4f;
            o.vel.x *= 0.7f;
            o.vel.y *= 0.7f;
        }
        o.pos = np;
        bool remove = o.age > 300.0f || o.pos.z < -200.0f;
        if (canCollect && dist < 1.1f && o.age > 0.5f) {
            AddXp(o.value);
            PlaySfx(SND_XP_ORB, nullptr, 0.1f, 0.55f + Rand01() * 0.9f);
            remove = true;
        }
        if (remove) {
            gOrbs[i] = gOrbs.back();
            gOrbs.pop_back();
        } else {
            ++i;
        }
    }
}

void XpRender(float light) {
    if (gOrbs.empty() || !gEntityTex.tex)
        return;
    (void)light;
    const CMatrix& cm = TheCamera.m_mCameraMatrix;
    const CVector R = cm.right * -1.0f, U = cm.at;
    d3::SetRaster(gEntityTex.Raster());
    for (auto& o : gOrbs) {
        const int icon = OrbIcon(o.value);
        const float u0 = (ENT_XP_ORB.x + (icon % 4) * 16.0f) / ENT_TEX_W, v0 = (ENT_XP_ORB.y + (icon / 4) * 16.0f) / ENT_TEX_H;
        const float u1 = u0 + 16.0f / ENT_TEX_W, v1 = v0 + 16.0f / ENT_TEX_H;
        // ExperienceOrbRenderer: the colour pulses between green and yellow
        const float t = (o.age * 20.0f) / 2.0f;
        const int r = (int)((std::sin(t) + 1.0f) * 0.5f * 255.0f);
        const int b = (int)((std::sin(t + 4.1887903f) + 1.0f) * 0.1f * 255.0f);
        const float s = 0.15f + icon * 0.012f;
        const CVector c = o.pos + CVector(0, 0, 0.1f);
        d3::Quad(c - R * s + U * s, c + R * s + U * s, c + R * s - U * s, c - R * s - U * s, u0, v0, u1, v1,
                 d3::Argb(r, 255, b, 255));
    }
    d3::Flush();
}

void XpClear() { gOrbs.clear(); }

} // namespace mc
