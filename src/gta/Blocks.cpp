#include "Blocks.h"

#include "CColPoint.h"
#include "CEntity.h"
#include "CFireManager.h"
#include "CPlayerPed.h"
#include "CPointLights.h"
#include "CPools.h"
#include "CVehicle.h"
#include "CWeapon.h"
#include "CWorld.h"

#include "BlockRules.h"
#include "Collision.h"
#include "Draw3D.h"
#include "Game.h"
#include "GtaWorld.h"
#include "Movement.h"
#include "Render3D.h"
#include "Sound.h"
#include "Terrain.h"
#include "Textures.h"
#include "World.h"

// The GTA side of the block logic: what the GTA map means for the blocks (ground that fills a cell, walls that
// stop water, bushes that burn), and what lava and fire do to people and cars. How fluids flow, fire spreads,
// sand falls and trees grow is the core's business (src/core/BlockRules.h).

namespace mc {

namespace {
std::unordered_map<Int3, uint8_t, Int3Hash> gGtaSolidCache; // 1 free, 2 solid

CVector Centre(const Int3& p) { return CVector(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f); }

} // namespace

// ================================================================ the GTA map as the blocks see it (for GtaHost.cpp)
// a cell that the GTA ground fills (more than half of it)
bool GtaSolidCell(const Int3& c) {
    if (TerrainIsOpened(c.x, c.y))
        return false;
    auto it = gGtaSolidCache.find(c);
    if (it != gGtaSolidCache.end())
        return it->second == 2;
    CColPoint cp;
    CEntity* e = nullptr;
    bool solid = CWorld::ProcessVerticalLine(CVector(c.x + 0.5f, c.y + 0.5f, c.z + 1.5f), (float)c.z, cp, e, true, false,
                                             false, false, false, false, nullptr) &&
                 !TerrainIgnoreHit(cp.m_vecPoint, e) && cp.m_vecPoint.z > c.z + 0.5f;
    if (gGtaSolidCache.size() > 50000)
        gGtaSolidCache.clear();
    gGtaSolidCache[c] = solid ? 2 : 1;
    return solid;
}

void GtaSolidCellForget(const Int3& c) { gGtaSolidCache.erase(c); }

// a GTA wall between two neighbouring cells
bool GtaWallBetween(const Int3& a, const Int3& b) {
    CColPoint cp;
    CEntity* e = nullptr;
    CVector pa = Centre(a), pb = Centre(b);
    pa.z -= 0.3f;
    pb.z -= 0.3f;
    return CWorld::ProcessLineOfSight(pa, pb, cp, e, true, false, false, true, false, false, false, false) &&
           !IsCollisionObject(e) && !TerrainIgnoreHit(cp.m_vecPoint, e);
}

// the GTA map (or a car / prop) carries the block in this cell
bool GtaSupports(const Int3& c) {
    if (GtaSolidCell(c))
        return true;
    CColPoint cp;
    CEntity* e = nullptr;
    return CWorld::ProcessVerticalLine(CVector(c.x + 0.5f, c.y + 0.5f, c.z + 0.6f), c.z - 0.3f, cp, e, true, true, false,
                                       true, false, false, nullptr) &&
           !IsCollisionObject(e) && !TerrainIgnoreHit(cp.m_vecPoint, e);
}

// GTA bushes and dry grass under this cell (they catch fire from lava)
bool GtaFlammableUnder(const Int3& c) {
    CColPoint cp;
    CEntity* e = nullptr;
    if (!CWorld::ProcessVerticalLine(CVector(c.x + 0.5f, c.y + 0.5f, c.z + 0.9f), c.z - 0.6f, cp, e, true, false, false, true,
                                     false, false, nullptr) ||
        TerrainIgnoreHit(cp.m_vecPoint, e) || IsCollisionObject(e))
        return false;
    const GtaMaterial m = MaterialFor(cp, e);
    return m.kind == GM_LEAVES || m.kind == GM_GRASS || m.kind == GM_CROP;
}

// soil of the GTA map (grass, dirt; sand too for dead bushes) under a point
bool GtaSoilBelow(float x, float y, float fromZ, bool sandToo, float* gz) {
    CColPoint cp;
    CEntity* e = nullptr;
    if (!CWorld::ProcessVerticalLine(CVector(x, y, fromZ), fromZ - 5.0f, cp, e, true, false, false, false, false, false, nullptr))
        return false;
    if (TerrainIgnoreHit(cp.m_vecPoint, e))
        return false;
    const GtaMaterial m = MaterialFor(cp, e);
    const bool ok = m.kind == GM_GRASS || m.block == ID_DIRT || m.block == ID_MUD ||
                    (sandToo && (m.block == ID_SAND || m.block == ID_RED_SAND || m.block == ID_TERRACOTTA));
    if (ok && gz)
        *gz = cp.m_vecPoint.z;
    return ok;
}

namespace {
// ---------------------------------------------------------------- heat: what lava and fire do to the things around them
struct Hot {
    Int3 p;
    bool lava;
    float d2;
};
std::vector<Hot> gHot;   // lava surfaces and fires near the player
float gHotTimer = 0.0f;
float gBurnTimer = 0.0f;

int HotBlockAt(const CVector& p) {
    const int b = gWorld.GetBlock(FloorI(p.x), FloorI(p.y), FloorI(p.z));
    if (b == ID_FIRE)
        return ID_FIRE;
    return FluidAt(p) == ID_LAVA ? ID_LAVA : ID_AIR;
}

void CollectHot(const CVector& at) {
    gHot.clear();
    for (const Int3& p : gWorld.ticking) {
        const float dx = p.x + 0.5f - at.x, dy = p.y + 0.5f - at.y, dz = p.z + 0.5f - at.z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 48.0f * 48.0f)
            continue;
        const int b = gWorld.GetBlock(p.x, p.y, p.z);
        if (b == ID_FIRE)
            gHot.push_back({ p, false, d2 });
        else if (b == ID_LAVA && gWorld.GetBlock(p.x, p.y, p.z + 1) != ID_LAVA)
            gHot.push_back({ p, true, d2 });
    }
    std::sort(gHot.begin(), gHot.end(), [](const Hot& a, const Hot& b) { return a.d2 < b.d2; });
    if (gHot.size() > 400)
        gHot.resize(400);
}

// people and cars that step into lava or fire burn
void BurnEntities() {
    CPlayerPed* player = FindPlayerPed();
    if (!player)
        return;
    const CVector pp = player->GetPosition();
    if (auto* pool = CPools::ms_pPedPool)
        for (int i = 0; i < pool->m_nSize; ++i) {
            CPed* ped = pool->GetAt(i);
            if (!ped || ped->bInVehicle || ped->m_fHealth <= 0.0f || (ped->GetPosition() - pp).Magnitude() > 60.0f)
                continue;
            const CVector pos = ped->GetPosition();
            int hot = HotBlockAt(pos + CVector(0, 0, -0.9f));
            if (hot == ID_AIR)
                hot = HotBlockAt(pos);
            const bool isPlayer = ped == player;
            if (isPlayer) {
                // our water puts the player out
                if (ped->m_pFire && FluidAt(pos + CVector(0, 0, -0.5f)) == ID_WATER)
                    gFireManager.ExtinguishPoint(pos, 3.0f);
                if (hot == ID_AIR || gGame.gameMode == MODE_CREATIVE)
                    continue;
                if (hot == ID_LAVA && MovementControllerActive())
                    continue; // the movement code burns the player in lava itself
            } else if (hot == ID_AIR) {
                continue;
            }
            if (ped->bFireProof)
                continue; // fire resistance
            if (isPlayer)
                NoteDamage(hot == ID_LAVA ? STR_DEATH_LAVA : STR_DEATH_FIRE);
            if (!ped->m_pFire)
                gFireManager.StartFire(ped, nullptr, 0.8f, 1, 7000, 1);
            CWeapon::GenerateDamageEvent(ped, nullptr, WEAPONTYPE_FTHROWER, hot == ID_LAVA ? 10 : 3, (ePedPieceTypes)3, 0);
        }
    if (auto* pool = CPools::ms_pVehiclePool)
        for (int i = 0; i < pool->m_nSize; ++i) {
            CVehicle* v = pool->GetAt(i);
            if (!v || v->m_fHealth <= 0.0f || (v->GetPosition() - pp).Magnitude() > 60.0f)
                continue;
            const CVector pos = v->GetPosition();
            const int hot = HotBlockAt(pos + CVector(0, 0, -0.4f));
            if (hot == ID_LAVA)
                v->m_fHealth -= 100.0f; // catches fire at 250, then blows up
            else if (hot == ID_FIRE)
                v->m_fHealth -= 10.0f;
        }
}

// popping lava, smoking fire, their sounds and their light
void HeatEffects(float dt) {
    int lights = 0;
    for (const Hot& h : gHot) {
        const CVector c(h.p.x + 0.5f, h.p.y + 0.5f, h.p.z + 0.5f);
        if (lights < 5 && h.d2 < 40.0f * 40.0f) {
            ++lights;
            if (h.lava)
                CPointLights::AddLight(0, c + CVector(0, 0, 0.7f), CVector(0, 0, 0), 9.0f, 1.0f, 0.42f, 0.08f, 0, false, nullptr);
            else
                CPointLights::AddLight(0, c + CVector(0, 0, 0.5f), CVector(0, 0, 0), 7.0f, 1.0f, 0.55f, 0.18f, 0, false, nullptr);
        }
        if (h.d2 > 32.0f * 32.0f)
            continue;
        if (h.lava) {
            if (Rand01() < 0.12f * dt) {
                // LiquidBlock.animateTick: a glowing drop pops out of the surface
                const float top = (float)h.p.z + FluidOwnHeight(gWorld.Get(h.p.x, h.p.y, h.p.z));
                Particle s;
                s.pos = CVector(h.p.x + Rand01(), h.p.y + Rand01(), top);
                s.vel = CVector((Rand01() - 0.5f) * 2.0f, (Rand01() - 0.5f) * 2.0f, 3.0f + Rand01() * 3.0f);
                s.maxLife = s.life = 1.0f + Rand01() * 1.5f;
                s.tile = TILE_P_LAVA;
                s.size = 0.06f + Rand01() * 0.05f;
                s.gravity = 14.0f;
                s.glow = true;
                SpawnParticle(s);
                if (Rand01() < 0.6f)
                    PlaySfx(SND_LAVA_POP, &s.pos, 0.2f + Rand01() * 0.2f, 0.9f + Rand01() * 0.15f);
            }
            if (Rand01() < 0.02f * dt)
                PlaySfx(SND_LAVA_AMBIENT, &c, 0.2f + Rand01() * 0.2f, 0.9f + Rand01() * 0.15f);
        } else {
            if (Rand01() < 2.5f * dt) {
                Particle s;
                s.pos = CVector(h.p.x + Rand01(), h.p.y + Rand01(), h.p.z + 0.5f + Rand01() * 0.6f);
                s.vel = CVector(0, 0, 0.8f + Rand01());
                s.maxLife = s.life = 1.2f + Rand01();
                s.tile = TILE_P_GENERIC_0;
                s.anim = 2;
                s.size = 0.18f + Rand01() * 0.1f;
                s.gravity = -0.3f;
                s.color = 0xFF303030;
                SpawnParticle(s);
            }
            if (Rand01() < 0.5f * dt)
                PlaySfx(SND_FIRE_AMBIENT, &c, 0.5f + Rand01() * 0.5f, 0.3f + Rand01() * 0.7f);
        }
    }
}
} // namespace

// ================================================================ public
void BlocksClear() {
    gHot.clear();
    BlockRulesClear();
    gGtaSolidCache.clear();
}

void BlocksUpdate(float dt) {
    // the blocks' own life: updates, fluids, falling blocks, random ticks
    BlocksTick(dt, gGame.age);
    // heat
    if (CPlayerPed* player = FindPlayerPed()) {
        gHotTimer -= dt;
        if (gHotTimer <= 0.0f) {
            gHotTimer = 0.5f;
            CollectHot(player->GetPosition());
        }
        gBurnTimer -= dt;
        if (gBurnTimer <= 0.0f) {
            gBurnTimer = 0.25f;
            BurnEntities();
        }
        HeatEffects(dt);
    }
}

void RenderFallingBlocks(float light) {
    const std::vector<FallingBlock>& falling = FallingBlocks();
    if (falling.empty())
        return;
    d3::SetRaster(gAtlasTex.Raster());
    for (auto& f : falling)
        EmitItemCube(ToGta(f.pos) + CVector(0, 0, 0.5f), 0.5f, CVector(1, 0, 0), CVector(0, 0, 1), CVector(0, 1, 0), f.block, light);
    d3::Flush();
}

} // namespace mc
