#include "GtaMobs.h"

#include "CCamera.h"
#include "CColPoint.h"
#include "CPlayerPed.h"
#include "CPools.h"
#include "CVehicle.h"
#include "CWaterLevel.h"
#include "CWorld.h"

#include "Draw3D.h"
#include "Game.h"
#include "GtaWorld.h"
#include "Host.h"
#include "Inventory.h"
#include "McModel.h"
#include "Mobs.h"
#include "PlayerAnim.h"
#include "Render3D.h"
#include "Textures.h"

// The GTA side of the animals: drawing them, GTA's vehicles running into them, and where on the GTA map a herd may
// appear. How they live is the core's business (src/core/Mobs.h).

namespace mc {

namespace {
// vehicles hurt and throw the animals they run into
void CarsHitMobs() {
    auto* pool = CPools::ms_pVehiclePool;
    if (!pool || gMobs.empty())
        return;
    for (int i = 0; i < pool->m_nSize; ++i) {
        CVehicle* v = pool->GetAt(i);
        if (!v)
            continue;
        CVector vel = v->m_vecMoveSpeed * 50.0f;
        float speed = vel.Magnitude();
        if (speed < 4.0f)
            continue;
        CVector vp = v->GetPosition();
        for (size_t j = 0; j < gMobs.size(); ++j) {
            Mob& m = gMobs[j];
            if (m.death >= 0.0f || m.hitByCar > 0.0f)
                continue;
            CVector d = (m.pos + CVector(0, 0, MobHeight(m) * 0.5f)) - vp;
            if (std::fabs(d.z) > 1.8f || d.x * d.x + d.y * d.y > 2.3f * 2.3f)
                continue;
            m.hitByCar = 0.6f;
            MobHurt((int)j, speed * 0.45f, vp, 0.0f);
            m.vel = vel * 1.1f + CVector(0, 0, 3.0f + speed * 0.25f);
            m.onGround = false;
        }
    }
}
} // namespace

// Host::SpawnGround: grass, earth or sand of the GTA map (rarely anything else), not too steep, not under water
bool GtaAnimalSpawnGround(const CVector& from, CVector& out) {
    CColPoint cp;
    CEntity* e = nullptr;
    if (!CWorld::ProcessVerticalLine(from, from.z - 70.0f, cp, e, true, false, false, false, false, false, nullptr))
        return false;
    int block = VirtualBlockFor(cp, e);
    bool natural = block == ID_GRASS_BLOCK || block == ID_DIRT || block == ID_SAND;
    if (!natural && Rand01() > 0.12f)
        return false;
    if (cp.m_vecNormal.z < 0.75f)
        return false; // too steep
    float wl;
    if (CWaterLevel::GetWaterLevelNoWaves(cp.m_vecPoint.x, cp.m_vecPoint.y, cp.m_vecPoint.z, &wl) && wl > cp.m_vecPoint.z + 0.2f)
        return false;
    out = cp.m_vecPoint;
    float gz;
    if (GroundBelow(CVector(out.x, out.y, out.z + 3.0f), 6.0f, &gz))
        out.z = gz; // blocks placed on top of the ground
    return true;
}

void MobsUpdate(float dt, CPlayerPed* player) {
    if (dt <= 0.0f || !player)
        return; // (no player for a moment must not make every animal "too far away")
    const CVector playerPos = player->GetPosition();
    if (gGta.enabled)
        MobsSpawnTick(dt, playerPos);
    CarsHitMobs();
    MobsTick(dt, playerPos, gGta.enabled ? gInv.Held().id : 0, !player->bInVehicle);
}

void MobsRender(float light) {
    if (gMobs.empty() || !gEntityTex.tex)
        return;
    const CVector cam = TheCamera.GetPosition();
    for (auto& m : gMobs) {
        CVector centre = m.pos + CVector(0, 0, MobHeight(m) * 0.5f);
        CVector d = centre - cam;
        if (d.x * d.x + d.y * d.y + d.z * d.z > 120.0f * 120.0f)
            continue;
        if (!TheCamera.IsSphereVisible(centre, 1.6f))
            continue;
        CVector fwd(-std::sin(m.yaw), std::cos(m.yaw), 0.0f);
        CVector up(0, 0, 1);
        CVector right(fwd.y, -fwd.x, 0.0f);
        float r = 1.0f, g = 1.0f, b = 1.0f;
        if (m.death >= 0.0f) {
            Vec3 r2 = right, u2 = up;
            DeathTilt(m.death, r2, u2); // the dead animal tips over onto its side
            right = ToGta(r2);
            up = ToGta(u2);
        }
        if (m.hurt > 0.0f || m.death >= 0.0f) {
            g = 0.45f;
            b = 0.45f;
        }
        Pose base = EntityPose(m.pos, right, up, fwd, MobScale(m));
        MobAnim a;
        a.limbSwing = m.limbSwing;
        a.limbAmount = m.limbAmount;
        a.headYaw = m.headYaw;
        a.headPitch = m.headPitch;
        a.wingFlap = (std::sin(m.flap) + 1.0f) * m.flapSpeed;
        a.sheared = m.sheared;
        a.saddled = m.saddled;
        DrawMob(m.kind, base, a, light, r, g, b);
        if (m.groundZ > -900.0f)
            AddShadow(CVector(m.pos.x, m.pos.y, m.groundZ), MobWidth(m) * 0.75f,
                      Clamp(1.0f - (m.pos.z - m.groundZ) / 6.0f, 0.0f, 1.0f));
    }
    d3::Flush();
}

} // namespace mc
