#include "Blocks.h"

#include <unordered_set>

#include "CColPoint.h"
#include "CEntity.h"
#include "CFireManager.h"
#include "CGame.h"
#include "CPlayerPed.h"
#include "CPointLights.h"
#include "CPools.h"
#include "CVehicle.h"
#include "CWeapon.h"
#include "CWeather.h"
#include "CWorld.h"

#include "Collision.h"
#include "Combat.h"
#include "Draw3D.h"
#include "Game.h"
#include "GtaWorld.h"
#include "Items.h"
#include "Movement.h"
#include "Render3D.h"
#include "Sound.h"
#include "Terrain.h"
#include "Textures.h"
#include "World.h"

namespace mc {

namespace {
struct FallingBlock {
    CVector pos; // bottom centre
    CVector vel;
    int block;
    float age = 0.0f;
};
std::vector<FallingBlock> gFalling;

struct Scheduled {
    Int3 p;
    float due;
};
std::vector<Scheduled> gFluidQueue;
std::unordered_set<Int3, Int3Hash> gFluidScheduled;
std::unordered_map<Int3, uint8_t, Int3Hash> gGtaSolidCache; // 1 free, 2 solid
float gTickTimer = 0.0f;

constexpr float kWaterDelay = 0.25f, kLavaDelay = 1.5f;

Int3 Up(const Int3& p) { return { p.x, p.y, p.z + 1 }; }
Int3 Down(const Int3& p) { return { p.x, p.y, p.z - 1 }; }
CVector Centre(const Int3& p) { return CVector(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f); }

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

// a GTA wall between two neighbouring cells
bool FlowBlocked(const Int3& a, const Int3& b) {
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

void Fizz(const Int3& p) {
    CVector c = Centre(p);
    PlaySfx(SND_LAVA_EXTINGUISH, &c, 0.6f, 2.6f + (Rand01() - Rand01()) * 0.8f);
    for (int i = 0; i < 8; ++i) {
        Particle s;
        s.pos = c + CVector(Rand01() - 0.5f, Rand01() - 0.5f, 0.4f);
        s.vel = CVector(0, 0, 1.0f + Rand01());
        s.maxLife = s.life = 0.8f;
        s.tile = TILE_P_GENERIC_0;
        s.anim = 2;
        s.size = 0.15f;
        s.gravity = -0.5f;
        s.color = 0xFF404040;
        SpawnParticle(s);
    }
}

void Happy(const CVector& at, int n) {
    for (int i = 0; i < n; ++i) {
        Particle p;
        p.pos = at + CVector((Rand01() - 0.5f) * 1.2f, (Rand01() - 0.5f) * 1.2f, Rand01() * 0.8f);
        p.vel = CVector(0, 0, 0.3f);
        p.maxLife = p.life = 0.9f;
        p.tile = TILE_P_GLINT;
        p.size = 0.08f;
        p.gravity = 0.0f;
        p.color = 0xFF50E050;
        p.glow = true;
        SpawnParticle(p);
    }
}

void ScheduleFluid(const Int3& p, float delay) {
    if (gFluidQueue.size() > 20000)
        return;
    if (gFluidScheduled.insert(p).second)
        gFluidQueue.push_back({ p, gGame.age + delay });
}

int FluidHeightLevel(Voxel v) { return (VoxMeta(v) & META_FLUID_FALLING) ? 0 : (VoxMeta(v) & META_FLUID_LEVEL); }
bool IsSource(Voxel v) { return IsFluidBlock(VoxBlock(v)) && (VoxMeta(v) & 0xF) == 0; }

bool CanFlowInto(int fluid, const Int3& from, const Int3& to) {
    const int b = gWorld.GetBlock(to.x, to.y, to.z);
    const int other = fluid == ID_WATER ? ID_LAVA : ID_WATER;
    if (b != ID_AIR && b != fluid && b != other && !IsPlantBlock(b) && !IsFireBlock(b))
        return false;
    if (to.z < -200)
        return false;
    if (b == ID_AIR || IsPlantBlock(b) || IsFireBlock(b)) {
        if (GtaSolidCell(to))
            return false;
        if (to.z == from.z && FlowBlocked(from, to))
            return false;
    }
    return true;
}

void FlowInto(int fluid, const Int3& from, const Int3& to, int meta) {
    const Voxel tv = gWorld.Get(to.x, to.y, to.z);
    const int b = VoxBlock(tv);
    if (b == ID_LAVA && fluid == ID_WATER) {
        gWorld.Set(to.x, to.y, to.z, MakeVox(IsSource(tv) ? ID_OBSIDIAN : ID_COBBLESTONE));
        Fizz(to);
        return;
    }
    if (b == ID_WATER && fluid == ID_LAVA) {
        gWorld.Set(to.x, to.y, to.z, MakeVox(to.z < from.z ? ID_STONE : ID_COBBLESTONE));
        Fizz(to);
        return;
    }
    if (b == fluid) {
        if (IsSource(tv))
            return;
        const int cur = FluidHeightLevel(tv), want = (meta & META_FLUID_FALLING) ? 0 : (meta & META_FLUID_LEVEL);
        if (cur <= want && !((meta & META_FLUID_FALLING) && !(VoxMeta(tv) & META_FLUID_FALLING)))
            return;
    }
    if (IsPlantBlock(b))
        SpawnBlockDrops(b, Centre(to));
    if (IsFireBlock(b)) {
        const CVector c = Centre(to);
        PlaySfx(SND_FIRE_EXTINGUISH, &c, 0.5f);
    }
    gWorld.Set(to.x, to.y, to.z, MakeVox(fluid, meta));
}

void FluidTick(const Int3& p) {
    const Voxel v = gWorld.Get(p.x, p.y, p.z);
    const int F = VoxBlock(v);
    if (!IsFluidBlock(F))
        return;
    int meta = VoxMeta(v);
    const int drop = F == ID_WATER ? 1 : 2;

    // lava that touches water hardens
    if (F == ID_LAVA) {
        const Int3 around[5] = { Up(p), { p.x + 1, p.y, p.z }, { p.x - 1, p.y, p.z }, { p.x, p.y + 1, p.z }, { p.x, p.y - 1, p.z } };
        for (const Int3& n : around)
            if (gWorld.GetBlock(n.x, n.y, n.z) == ID_WATER) {
                gWorld.Set(p.x, p.y, p.z, MakeVox(IsSource(v) ? ID_OBSIDIAN : ID_COBBLESTONE));
                Fizz(p);
                return;
            }
    }

    const Int3 below = Down(p);
    if (!IsSource(v)) {
        // a flowing cell lives from its neighbours
        const bool fromAbove = gWorld.GetBlock(p.x, p.y, p.z + 1) == F;
        int best = 99, sources = 0;
        for (int f = 0; f < 4; ++f) {
            const Int3& d = FACE_DIR[f];
            Voxel nv = gWorld.Get(p.x + d.x, p.y + d.y, p.z);
            if (VoxBlock(nv) != F)
                continue;
            best = std::min(best, FluidHeightLevel(nv));
            if (IsSource(nv))
                sources++;
        }
        const Voxel bv = gWorld.Get(below.x, below.y, below.z);
        int newMeta;
        if (F == ID_WATER && sources >= 2 && (IsSolidBlock(VoxBlock(bv)) || IsSource(bv) || GtaSolidCell(below)))
            newMeta = 0; // infinite water
        else if (fromAbove)
            newMeta = META_FLUID_FALLING;
        else if (best + drop <= 7)
            newMeta = best + drop;
        else {
            gWorld.Set(p.x, p.y, p.z, MakeVox(ID_AIR));
            return;
        }
        if (newMeta != meta) {
            gWorld.Set(p.x, p.y, p.z, MakeVox(F, newMeta));
            meta = newMeta;
        }
    }

    // spread: down first, sideways only when it cannot fall
    if (CanFlowInto(F, p, below)) {
        FlowInto(F, p, below, META_FLUID_FALLING);
        return;
    }
    const int spread = ((meta & META_FLUID_FALLING) ? 0 : (meta & META_FLUID_LEVEL)) + drop;
    if (spread > 7)
        return;
    for (int f = 0; f < 4; ++f) {
        const Int3& d = FACE_DIR[f];
        Int3 n{ p.x + d.x, p.y + d.y, p.z };
        if (CanFlowInto(F, p, n))
            FlowInto(F, p, n, spread);
    }
}

// ---------------------------------------------------------------- fire (FireBlock)
bool FlammableAround(const Int3& p) {
    for (const Int3& d : FACE_DIR)
        if (FireIgnite(gWorld.GetBlock(p.x + d.x, p.y + d.y, p.z + d.z)) > 0)
            return true;
    return false;
}

int IgniteOddsAt(const Int3& p) {
    int best = 0;
    for (const Int3& d : FACE_DIR)
        best = std::max(best, FireIgnite(gWorld.GetBlock(p.x + d.x, p.y + d.y, p.z + d.z)));
    return best;
}

bool SturdyBelow(const Int3& p) {
    const int below = gWorld.GetBlock(p.x, p.y, p.z - 1);
    return IsSolidBlock(below) || (below == ID_AIR && GtaSupports(p));
}

// FireBlock.canSurvive
bool FireSurvives(const Int3& p) { return SturdyBelow(p) || FlammableAround(p); }

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

void SetFire(const Int3& p, int age) {
    gWorld.Set(p.x, p.y, p.z, MakeVox(ID_FIRE, std::clamp(age, 0, 15)));
}

// FireBlock.checkBurnOut
void BurnOut(const Int3& n, int chance, int age) {
    const int b = gWorld.GetBlock(n.x, n.y, n.z);
    const int odds = FireBurn(b);
    if (odds <= 0 || rand() % chance >= odds)
        return;
    if (b == ID_TNT) {
        gWorld.Set(n.x, n.y, n.z, MakeVox(ID_AIR));
        IgniteTnt(n, 4.0f);
        return;
    }
    if (rand() % (age + 10) < 5)
        SetFire(n, age + (rand() % 5) / 4);
    else
        gWorld.Set(n.x, n.y, n.z, MakeVox(ID_AIR));
}

void FireTick(const Int3& p) {
    const Voxel v = gWorld.Get(p.x, p.y, p.z);
    if (VoxBlock(v) != ID_FIRE)
        return;
    if (!FireSurvives(p)) {
        gWorld.Set(p.x, p.y, p.z, MakeVox(ID_AIR));
        return;
    }
    const int age = VoxMeta(v);
    // rain puts fires out (outside)
    if (CGame::currArea == 0 && CWeather::Rain > 0.2f && Rand01() < 0.2f + age * 0.03f) {
        gWorld.Set(p.x, p.y, p.z, MakeVox(ID_AIR));
        return;
    }
    const int newAge = std::min(15, age + (rand() % 3) / 2);
    if (newAge != age)
        gWorld.SetRaw(p.x, p.y, p.z, MakeVox(ID_FIRE, newAge));
    const int below = gWorld.GetBlock(p.x, p.y, p.z - 1);
    if (!FlammableAround(p)) {
        if (!SturdyBelow(p) || age > 3)
            gWorld.Set(p.x, p.y, p.z, MakeVox(ID_AIR));
        return;
    }
    if (age == 15 && rand() % 4 == 0 && FireBurn(below) == 0) {
        gWorld.Set(p.x, p.y, p.z, MakeVox(ID_AIR));
        return;
    }
    for (int f = 0; f < 6; ++f) {
        const Int3& d = FACE_DIR[f];
        BurnOut({ p.x + d.x, p.y + d.y, p.z + d.z }, f >= 4 ? 250 : 300, age);
    }
    // spread into the air around (more likely upwards)
    for (int dz = -1; dz <= 4; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0 && dz == 0)
                    continue;
                const Int3 q{ p.x + dx, p.y + dy, p.z + dz };
                if (gWorld.GetBlock(q.x, q.y, q.z) != ID_AIR)
                    continue;
                const int odds = IgniteOddsAt(q);
                if (odds <= 0)
                    continue;
                const int k = 100 + (dz > 1 ? (dz - 1) * 100 : 0);
                const int l = (odds + 40 + 14) / (age + 30);
                if (l > 0 && rand() % k <= l)
                    SetFire(q, age + (rand() % 5) / 4);
            }
}

// LavaFluid.randomTick: lava sets things around it on fire
void LavaTick(const Int3& p) {
    const int i = rand() % 3;
    if (i > 0) {
        Int3 q = p;
        for (int j = 0; j < i; ++j) {
            q = { q.x + rand() % 3 - 1, q.y + rand() % 3 - 1, q.z + 1 };
            const int b = gWorld.GetBlock(q.x, q.y, q.z);
            if (b == ID_AIR) {
                if (IgniteOddsAt(q) > 0 || GtaFlammableUnder(q)) {
                    SetFire(q, 0);
                    return;
                }
            } else if (IsSolidBlock(b)) {
                return;
            }
        }
    } else {
        for (int k = 0; k < 3; ++k) {
            const Int3 q{ p.x + rand() % 3 - 1, p.y + rand() % 3 - 1, p.z };
            const Int3 up{ q.x, q.y, q.z + 1 };
            if (gWorld.GetBlock(up.x, up.y, up.z) == ID_AIR &&
                (FireIgnite(gWorld.GetBlock(q.x, q.y, q.z)) > 0 || GtaFlammableUnder(up)))
                SetFire(up, 0);
        }
    }
}

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

void BlockUpdate(const Int3& p) {
    const Voxel v = gWorld.Get(p.x, p.y, p.z);
    const int b = VoxBlock(v);
    if (b == ID_AIR)
        return;
    if (IsFluidBlock(b)) {
        ScheduleFluid(p, b == ID_WATER ? kWaterDelay : kLavaDelay);
        return;
    }
    if (IsGravityBlock(b)) {
        const int below = gWorld.GetBlock(p.x, p.y, p.z - 1);
        if (!IsSolidBlock(below) && !GtaSupports(p)) {
            FallingBlock f;
            f.pos = CVector(p.x + 0.5f, p.y + 0.5f, (float)p.z);
            f.vel = CVector(0, 0, 0);
            f.block = b;
            gWorld.Set(p.x, p.y, p.z, MakeVox(ID_AIR));
            if (gFalling.size() < 400)
                gFalling.push_back(f);
        }
        return;
    }
    if (IsPlantBlock(b)) {
        const int below = gWorld.GetBlock(p.x, p.y, p.z - 1);
        if (!IsSolidBlock(below) && !GtaSupports(p)) {
            SpawnBlockDrops(b, Centre(p));
            gWorld.Set(p.x, p.y, p.z, MakeVox(ID_AIR));
        }
        return;
    }
    if (IsFireBlock(b) && !FireSurvives(p))
        gWorld.Set(p.x, p.y, p.z, MakeVox(ID_AIR));
}

void UpdateFalling(float dt) {
    for (size_t i = 0; i < gFalling.size();) {
        FallingBlock& f = gFalling[i];
        f.age += dt;
        f.vel.z = std::max(-60.0f, f.vel.z - 32.0f * dt);
        CVector np = f.pos + f.vel * dt;
        bool landed = false;
        const Int3 cell{ FloorI(np.x), FloorI(np.y), FloorI(np.z) };
        if (gWorld.IsSolid(cell.x, cell.y, cell.z) || TerrainCapAt(cell.x, cell.y, cell.z, nullptr)) {
            np.z = (float)cell.z + 1.0f;
            landed = true;
        } else {
            float gz;
            if (GroundBelow(CVector(np.x, np.y, f.pos.z + 0.3f), 1.0f, &gz) && np.z <= gz) {
                np.z = gz;
                landed = true;
            }
        }
        f.pos = np;
        if (landed || f.age > 20.0f || f.pos.z < -200.0f) {
            const Int3 at{ FloorI(f.pos.x), FloorI(f.pos.y), FloorI(f.pos.z + 0.25f) };
            const int there = gWorld.GetBlock(at.x, at.y, at.z);
            if (landed && (there == ID_AIR || IsFluidBlock(there) || IsPlantBlock(there))) {
                gWorld.Set(at.x, at.y, at.z, MakeVox(f.block));
                CVector c = Centre(at);
                PlaySfx((SoundEvent)(SND_PLACE_STONE + Block(f.block).sound), &c, 0.6f);
            } else {
                SpawnDropItem(f.pos + CVector(0, 0, 0.5f), (uint16_t)f.block, 1);
            }
            gFalling[i] = gFalling.back();
            gFalling.pop_back();
        } else {
            ++i;
        }
    }
}

// ---------------------------------------------------------------- trees
bool Free(int x, int y, int z) {
    int b = gWorld.GetBlock(x, y, z);
    return b == ID_AIR || IsPlantBlock(b) || (Block(b).sound == SG_GRASS && Block(b).render == RENDER_CUTOUT);
}

void Leaf(int x, int y, int z, int leaves) {
    int b = gWorld.GetBlock(x, y, z);
    if (b == ID_AIR || IsPlantBlock(b))
        gWorld.SetRaw(x, y, z, MakeVox(leaves));
}

void PutLog(int x, int y, int z, int log) { gWorld.SetRaw(x, y, z, MakeVox(log)); }

struct Species {
    int sapling, log, leaves;
};

void Blob(int x, int y, int top, int leaves, int lower, int upper) {
    // oak style crown: two wide layers, two narrow ones
    for (int dz = -3; dz <= 0; ++dz) {
        const int r = dz >= -1 ? upper : lower;
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                const bool corner = std::abs(dx) == r && std::abs(dy) == r;
                if (corner && (dz == 0 || Rand01() < 0.5f))
                    continue;
                Leaf(x + dx, y + dy, top + dz, leaves);
            }
    }
    Leaf(x, y, top + 1, leaves);
    Leaf(x + 1, y, top + 1, leaves);
    Leaf(x - 1, y, top + 1, leaves);
    Leaf(x, y + 1, top + 1, leaves);
    Leaf(x, y - 1, top + 1, leaves);
}

bool GrowAt(const Int3& p, const Species& s) {
    const int x = p.x, y = p.y, z = p.z;
    int height = 4 + rand() % 3;
    if (s.log == ID_BIRCH_LOG)
        height = 5 + rand() % 3;
    else if (s.log == ID_SPRUCE_LOG)
        height = 6 + rand() % 4;
    else if (s.log == ID_JUNGLE_LOG)
        height = 6 + rand() % 5;
    for (int h = 1; h <= height + 1; ++h)
        if (!Free(x, y, z + h))
            return false;
    gWorld.SetRaw(x, y, z, MakeVox(ID_AIR));
    gWorld.ticking.erase(p);
    if (gWorld.GetBlock(x, y, z - 1) == ID_GRASS_BLOCK)
        gWorld.SetRaw(x, y, z - 1, MakeVox(ID_DIRT));

    if (s.log == ID_SPRUCE_LOG) {
        for (int h = 0; h < height; ++h)
            PutLog(x, y, z + h, s.log);
        // cone: radius 0..3 growing downwards, every other layer narrower
        int r = 0;
        for (int zz = z + height; zz >= z + 2; --zz) {
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::abs(dx) == r && std::abs(dy) == r && r > 0)
                        continue;
                    if (dx == 0 && dy == 0 && zz < z + height)
                        continue;
                    Leaf(x + dx, y + dy, zz, s.leaves);
                }
            r = r >= 2 + (height > 7) ? 1 : r + 1;
        }
        Leaf(x, y, z + height, s.leaves);
    } else if (s.log == ID_ACACIA_LOG || s.log == ID_CHERRY_LOG) {
        // the trunk bends to one side, a flat crown on top
        const int dir = rand() % 4;
        const Int3& d = FACE_DIR[dir];
        const int straight = 2 + rand() % 2;
        int cx = x, cy = y, cz = z;
        for (int h = 0; h < height; ++h) {
            if (h >= straight) {
                cx += d.x;
                cy += d.y;
            }
            PutLog(cx, cy, cz, s.log);
            ++cz;
        }
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -3; dx <= 3; ++dx) {
                if (std::abs(dx) + std::abs(dy) > 4)
                    continue;
                Leaf(cx + dx, cy + dy, cz - 1, s.leaves);
                if (std::abs(dx) + std::abs(dy) <= 2)
                    Leaf(cx + dx, cy + dy, cz, s.leaves);
            }
    } else {
        for (int h = 0; h < height; ++h)
            PutLog(x, y, z + h, s.log);
        const bool big = s.log == ID_DARK_OAK_LOG || s.log == ID_JUNGLE_LOG;
        Blob(x, y, z + height - 1, s.leaves, big ? 3 : 2, big ? 2 : 1);
    }
    CVector c = Centre(p);
    Happy(c + CVector(0, 0, 1.0f), 15);
    PlaySfx(SND_PLACE_GRASS, &c);
    return true;
}

Species SpeciesOf(int sapling) {
    switch (sapling) {
    case ID_SPRUCE_SAPLING: return { sapling, ID_SPRUCE_LOG, ID_SPRUCE_LEAVES };
    case ID_BIRCH_SAPLING: return { sapling, ID_BIRCH_LOG, ID_BIRCH_LEAVES };
    case ID_JUNGLE_SAPLING: return { sapling, ID_JUNGLE_LOG, ID_JUNGLE_LEAVES };
    case ID_ACACIA_SAPLING: return { sapling, ID_ACACIA_LOG, ID_ACACIA_LEAVES };
    case ID_DARK_OAK_SAPLING: return { sapling, ID_DARK_OAK_LOG, ID_DARK_OAK_LEAVES };
    case ID_CHERRY_SAPLING: return { sapling, ID_CHERRY_LOG, ID_CHERRY_LEAVES };
    default: return { sapling, ID_OAK_LOG, ID_OAK_LEAVES };
    }
}

uint16_t RandomFlower() {
    static const uint16_t kFlowers[] = { ID_DANDELION, ID_POPPY, ID_BLUE_ORCHID, ID_ALLIUM, ID_AZURE_BLUET, ID_RED_TULIP,
                                         ID_ORANGE_TULIP, ID_WHITE_TULIP, ID_PINK_TULIP, ID_OXEYE_DAISY, ID_CORNFLOWER,
                                         ID_LILY_OF_THE_VALLEY };
    return kFlowers[rand() % (sizeof(kFlowers) / sizeof(kFlowers[0]))];
}

bool IsSoil(int b) {
    return b == ID_GRASS_BLOCK || b == ID_DIRT || b == ID_PODZOL || b == ID_COARSE_DIRT || b == ID_MYCELIUM ||
           b == ID_MOSS_BLOCK || b == ID_MUD;
}

bool GtaGroundIs(float x, float y, float fromZ, bool sandToo, float* gz) {
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
} // namespace

// ================================================================ public
void BlocksClear() {
    gHot.clear();
    gFalling.clear();
    gFluidQueue.clear();
    gFluidScheduled.clear();
    gGtaSolidCache.clear();
}

void BlocksUpdate(float dt) {
    // neighbour updates queued by World::Set
    if (!gWorld.updates.empty()) {
        std::vector<Int3> batch;
        const size_t n = std::min<size_t>(gWorld.updates.size(), 600);
        batch.assign(gWorld.updates.begin(), gWorld.updates.begin() + n);
        gWorld.updates.erase(gWorld.updates.begin(), gWorld.updates.begin() + n);
        std::unordered_set<Int3, Int3Hash> seen;
        for (const Int3& p : batch)
            if (seen.insert(p).second)
                BlockUpdate(p);
    }
    // fluids
    if (!gFluidQueue.empty()) {
        std::vector<Int3> due;
        for (size_t i = 0; i < gFluidQueue.size();) {
            if (gFluidQueue[i].due <= gGame.age && due.size() < 300) {
                due.push_back(gFluidQueue[i].p);
                gFluidScheduled.erase(gFluidQueue[i].p);
                gFluidQueue[i] = gFluidQueue.back();
                gFluidQueue.pop_back();
            } else {
                ++i;
            }
        }
        for (const Int3& p : due)
            FluidTick(p);
    }
    UpdateFalling(dt);
    // random ticks: saplings grow, lava sets things on fire, fire spreads and dies down
    gTickTimer += dt;
    if (gTickTimer >= 1.0f) {
        gTickTimer = 0.0f;
        std::vector<Int3> cells(gWorld.ticking.begin(), gWorld.ticking.end());
        for (const Int3& p : cells) {
            const int b = gWorld.GetBlock(p.x, p.y, p.z);
            if (IsSaplingBlock(b)) {
                if (Rand01() < 1.0f / 50.0f)
                    GrowSapling(p);
            } else if (b == ID_LAVA) {
                if (Rand01() < 0.35f)
                    LavaTick(p);
            } else if (b == ID_FIRE) {
                if (Rand01() < 0.6f)
                    FireTick(p);
            } else {
                gWorld.ticking.erase(p);
            }
        }
    }
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

bool PlaceFluid(int block, const Int3& c) {
    const int b = gWorld.GetBlock(c.x, c.y, c.z);
    if (b != ID_AIR && !IsPlantBlock(b) && !IsFluidBlock(b) && !IsFireBlock(b))
        return false;
    if (IsPlantBlock(b))
        SpawnBlockDrops(b, Centre(c));
    gWorld.Set(c.x, c.y, c.z, MakeVox(block, 0));
    gGtaSolidCache.erase(c);
    return true;
}

bool TakeFluid(const Int3& c, int* block) {
    const Voxel v = gWorld.Get(c.x, c.y, c.z);
    if (!IsSource(v))
        return false;
    *block = VoxBlock(v);
    gWorld.Set(c.x, c.y, c.z, MakeVox(ID_AIR));
    return true;
}

int FluidAt(const CVector& p) {
    const int x = FloorI(p.x), y = FloorI(p.y), z = FloorI(p.z);
    const Voxel v = gWorld.Get(x, y, z);
    const int b = VoxBlock(v);
    if (!IsFluidBlock(b))
        return ID_AIR;
    float h = 1.0f;
    if (!(VoxMeta(v) & META_FLUID_FALLING) && gWorld.GetBlock(x, y, z + 1) != b)
        h = (8.0f - (VoxMeta(v) & META_FLUID_LEVEL)) / 9.0f;
    return p.z - z <= h ? b : ID_AIR;
}

float FluidDepth(const CVector& feet, float height, int* block) {
    int found = ID_AIR, inside = 0;
    for (int i = 0; i < 4; ++i) {
        int b = FluidAt(feet + CVector(0, 0, 0.05f + (height - 0.1f) * i / 3.0f));
        if (b != ID_AIR) {
            found = b;
            ++inside;
        }
    }
    if (block)
        *block = found;
    return inside / 4.0f;
}

int PlantCellOnGround(float groundZ) { return FloorI(groundZ + 0.25f); }

CVector FluidFlowAt(const Int3& c) {
    const Voxel v = gWorld.Get(c.x, c.y, c.z);
    const int b = VoxBlock(v);
    if (!IsFluidBlock(b))
        return CVector(0, 0, 0);
    return FluidFlowT(b, v, [&](int dx, int dy, int dz) { return gWorld.Get(c.x + dx, c.y + dy, c.z + dz); });
}

bool PlaceFire(const Int3& c) {
    const int b = gWorld.GetBlock(c.x, c.y, c.z);
    if (b != ID_AIR && !IsPlantBlock(b))
        return false;
    if (!FireSurvives(c))
        return false;
    SetFire(c, 0);
    return true;
}

bool FireAt(const CVector& p) { return gWorld.GetBlock(FloorI(p.x), FloorI(p.y), FloorI(p.z)) == ID_FIRE; }

bool CanPlantAt(int plant, const Int3& c, bool creative) {
    const int here = gWorld.GetBlock(c.x, c.y, c.z);
    if (here != ID_AIR)
        return false;
    const int below = gWorld.GetBlock(c.x, c.y, c.z - 1);
    const bool sandy = plant == ID_DEAD_BUSH;
    const bool mushroom = plant == ID_BROWN_MUSHROOM || plant == ID_RED_MUSHROOM;
    if (below != ID_AIR)
        return IsSolidBlock(below) &&
               (creative || mushroom || IsSoil(below) ||
                (sandy && (below == ID_SAND || below == ID_RED_SAND || below == ID_TERRACOTTA)));
    // on the GTA map
    if (!GtaSupports(c))
        return false;
    return creative || mushroom || GtaGroundIs(c.x + 0.5f, c.y + 0.5f, c.z + 1.2f, sandy, nullptr);
}

bool GrowSapling(const Int3& p) {
    const int b = gWorld.GetBlock(p.x, p.y, p.z);
    if (!IsSaplingBlock(b)) {
        gWorld.ticking.erase(p);
        return false;
    }
    return GrowAt(p, SpeciesOf(b));
}

bool ApplyBoneMeal(bool voxel, const Int3& cell, const CVector& point, const CVector& normal, bool gtaGrass) {
    auto spread = [&](float cx, float cy, float cz) {
        int placed = 0;
        for (int i = 0; i < 24 && placed < 10; ++i) {
            const int x = FloorI(cx) + rand() % 7 - 3, y = FloorI(cy) + rand() % 7 - 3;
            const uint16_t plant = Rand01() < 0.7f ? ID_SHORT_GRASS : (Rand01() < 0.85f ? RandomFlower() : ID_FERN);
            // a grass block nearby?
            bool done = false;
            for (int z = FloorI(cz) + 2; z >= FloorI(cz) - 2 && !done; --z)
                if (gWorld.GetBlock(x, y, z) == ID_GRASS_BLOCK && gWorld.GetBlock(x, y, z + 1) == ID_AIR) {
                    gWorld.Set(x, y, z + 1, MakeVox(plant));
                    done = true;
                }
            float gz;
            if (!done && GtaGroundIs(x + 0.5f, y + 0.5f, cz + 2.0f, false, &gz)) {
                const int z = PlantCellOnGround(gz);
                if (gWorld.GetBlock(x, y, z) == ID_AIR) {
                    gWorld.Set(x, y, z, MakeVox(plant));
                    done = true;
                }
            }
            if (done) {
                ++placed;
                Happy(CVector(x + 0.5f, y + 0.5f, cz + 0.5f), 2);
            }
        }
        return placed > 0;
    };
    if (voxel) {
        const int b = gWorld.GetBlock(cell.x, cell.y, cell.z);
        if (IsSaplingBlock(b)) {
            Happy(Centre(cell), 10);
            if (Rand01() < 0.45f)
                GrowSapling(cell);
            return true;
        }
        if (b == ID_GRASS_BLOCK && normal.z > 0.5f) {
            spread(cell.x + 0.5f, cell.y + 0.5f, cell.z + 1.0f);
            Happy(Centre(cell) + CVector(0, 0, 0.6f), 8);
            return true;
        }
        return false;
    }
    if (gtaGrass) {
        spread(point.x, point.y, point.z);
        Happy(point, 8);
        return true;
    }
    return false;
}

void RenderFallingBlocks(float light) {
    if (gFalling.empty())
        return;
    d3::SetRaster(gAtlasTex.Raster());
    for (auto& f : gFalling)
        EmitItemCube(f.pos + CVector(0, 0, 0.5f), 0.5f, CVector(1, 0, 0), CVector(0, 0, 1), CVector(0, 1, 0), f.block, light);
    d3::Flush();
}

} // namespace mc
