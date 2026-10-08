#include "GtaHost.h"

#include "CColPoint.h"
#include "CEntity.h"
#include "CExplosion.h"
#include "CFireManager.h"
#include "CGame.h"
#include "CPlayerPed.h"
#include "CPools.h"
#include "CWaterLevel.h"
#include "CWeapon.h"
#include "CWeather.h"
#include "CWorld.h"
#include "common.h"

#include "Blocks.h"
#include "Carve.h"
#include "Collision.h"
#include "Entities.h"
#include "GameState.h"
#include "GtaCombat.h"
#include "GtaFishing.h"
#include "GtaMining.h"
#include "GtaMobs.h"
#include "Host.h"
#include "ModCommon.h"
#include "Movement.h"
#include "PedSkins.h"
#include "Terrain.h"

namespace mc {

namespace {
// explosions we made ourselves: GTA shows them, but their blocks are already broken
struct OwnBlast {
    CVector pos;
    float age;
};
std::vector<OwnBlast> gOwnBlasts;
uint8_t gExpCounter[16] = {};
CVector gExpPos[16];

struct GtaHost : Host {
    // ---- the GTA map
    bool SolidCell(const Int3& c) override { return GtaSolidCell(c); }
    bool WallBetween(const Int3& a, const Int3& b) override { return GtaWallBetween(a, b); }
    bool Supports(const Int3& c) override { return GtaSupports(c); }
    bool CapAt(const Int3& c, float* top) override { return TerrainCapAt(c.x, c.y, c.z, top); }
    bool GroundBelow(const Vec3& from, float maxDrop, float* z) override {
        bool found = false;
        float gz = CWorld::FindGroundZFor3DCoord(from.x, from.y, from.z, &found, nullptr);
        const int bx = FloorI(from.x), by = FloorI(from.y);
        if (found && TerrainIsOpened(bx, by) && std::fabs(gz - TerrainSurface(bx, by)) < 0.8f)
            found = false; // that ground has been dug away
        if (!found || from.z - gz > maxDrop)
            return false;
        *z = gz;
        return true;
    }
    bool LineBlocked(const Vec3& a, const Vec3& b) override {
        CColPoint cp;
        CEntity* e = nullptr;
        return CWorld::ProcessLineOfSight(a, b, cp, e, true, true, false, true, false, false, false, false) &&
               !IsCollisionObject(e);
    }
    bool WaterLevel(const Vec3& at, float* level) override {
        return CWaterLevel::GetWaterLevelNoWaves(at.x, at.y, at.z, level);
    }
    bool FlammableUnder(const Int3& c) override { return GtaFlammableUnder(c); }
    bool SoilBelow(const Vec3& from, bool sandToo, float* z) override {
        return GtaSoilBelow(from.x, from.y, from.z, sandToo, z);
    }
    bool CellBlocked(const Int3& c) override { return GtaCellBlocked(c); }
    bool Outdoors() override { return CGame::currArea == 0; }
    bool OwnGround(const Int3& c, float* depthShade) override {
        if (!TerrainOwnsCell(c.x, c.y, c.z))
            return false;
        *depthShade = TerrainDepthShade(c.x, c.y, c.z);
        return true;
    }
    bool Raining() override { return Outdoors() && CWeather::Rain > 0.2f; }
    bool SpawnGround(const Vec3& from, Vec3* ground) override {
        CVector out;
        if (!GtaAnimalSpawnGround(from, out))
            return false;
        *ground = out;
        return true;
    }

    // ---- what is under the crosshair, when it is GTA's (GtaMining.cpp)
    bool Pick(const PickRay& ray, const VoxelHit& blocks, Target& out) override { return GtaPick(ray, blocks, out); }
    void BreakTarget() override { GtaBreakTarget(); }

    // ---- the player is GTA's ped
    bool PlayerPos(Vec3* pos) override {
        CPlayerPed* ped = FindPlayerPed();
        if (!ped)
            return false;
        *pos = ped->GetPosition();
        return true;
    }

    // ---- what only GTA can do
    void FluidPlaced(const Int3& c) override { GtaSolidCellForget(c); }
    Vec3 PlayerVelocity() override { return mc::PlayerVelocity(FindPlayerPed()); }
    bool PlayerOnGround() override {
        CPlayerPed* ped = FindPlayerPed();
        return ped && mc::PlayerOnGround(ped);
    }
    bool PlayerFalling() override {
        CPlayerPed* ped = FindPlayerPed();
        return ped && (gGame.jumping ? gGame.flyVel.z < 0.0f : (!ped->bIsStanding && ped->m_vecMoveSpeed.z < 0.0f));
    }
    int PlayerVehicle() override {
        CPlayerPed* ped = FindPlayerPed();
        return ped && ped->bInVehicle && ped->m_pVehicle ? CPools::GetVehicleRef(ped->m_pVehicle) : -1;
    }
    void MovePlayer(const Vec3& to) override {
        if (CPlayerPed* ped = FindPlayerPed()) {
            StopFlying(ped);
            ped->Teleport(to, false);
        }
    }
    void HurtPlayer(float halfHearts) override {
        if (CPlayerPed* ped = FindPlayerPed()) {
            const float maxH = ped->m_fMaxHealth > 1.0f ? ped->m_fMaxHealth : 100.0f;
            CWeapon::GenerateDamageEvent(ped, nullptr, WEAPONTYPE_FALL, (int)(maxH * halfHearts / 20.0f), (ePedPieceTypes)3, 0);
        }
    }
    float AimDistance(const Vec3& origin, const Vec3& dir, float reach, float nothing) override {
        return GtaAimDistance(origin, dir, reach, nothing);
    }
    HostHit BlowTrace(const Vec3& origin, const Vec3& dir, float reach) override { return GtaBlowTrace(origin, dir, reach); }
    HostHit ShotTrace(const Vec3& from, const Vec3& dir, float len, const ShotOwner& by) override {
        return GtaShotTrace(from, dir, len, by);
    }
    void HurtBeing(int being, float halfHearts, int how, const ShotOwner* by) override { GtaHurtBeing(being, halfHearts, how, by); }
    void PushBeing(int being, const Vec3& velocity) override { GtaPushBeing(being, velocity); }
    void HurtVehicle(int vehicle, float halfHearts) override { GtaHurtVehicle(vehicle, halfHearts); }
    HostHit BeingTrace(const Vec3& origin, const Vec3& dir, float reach) override {
        HostHit h;
        const PedHit ph = RaycastPeds(origin, dir, reach, FindPlayerPed(), false);
        if (ph.ped) {
            h.being = CPools::GetPedRef(ph.ped);
            h.beingDist = ph.dist;
            h.beingPoint = ph.point;
        }
        return h;
    }
    bool UseOnBeing(int being) override {
        return VillagerInteract(CPools::ms_pPedPool ? CPools::ms_pPedPool->GetAtRef(being) : nullptr);
    }
    HostHit HookTrace(const Vec3& from, const Vec3& dir, float len) override { return GtaHookTrace(from, dir, len); }
    bool HookPoint(const HostHit& hooked, Vec3* at) override {
        CVector p;
        if (!GtaHookPoint(hooked, p))
            return false;
        *at = p;
        return true;
    }
    bool Fling(const HostHit& what, const Vec3& velocity) override { return GtaFling(what, velocity); }
    void Gust(const Vec3& centre, float radius, float side, float up, bool playerToo) override {
        GtaGust(centre, radius, side, up, playerToo);
    }
    void Douse(const Vec3& at, float radius) override { gFireManager.ExtinguishPoint(at, radius); }
    void Ignite(const Vec3& at, float seconds, int spread) override { GtaIgnite(at, seconds, spread); }
    bool Thunderstorm() override { return CWeather::Rain > 0.1f; }
    bool PlaceVehicle(int kind, const Vec3& at, float headingDeg) override { return GtaPlaceVehicle(kind, at, headingDeg); }
    void BoostVehicle(float seconds) override { GtaBoostVehicle(seconds); }
    bool ItemAttack(int special) override { return GtaItemAttack(special); }
    bool ItemUse(int special) override { return GtaItemUse(special); }
    void Explosion(const Vec3& at, int kind) override {
        static const eExplosionType kTypes[] = { EXPLOSION_GRENADE, EXPLOSION_SMALL, EXPLOSION_MOLOTOV }; // BlastKind
        gOwnBlasts.push_back({ at, 0.0f });
        CExplosion::AddExplosion(nullptr, FindPlayerPed(), kTypes[kind], at, 0, true, -1.0f, false);
    }
    void BlastMap(const Vec3& at, float radius, std::vector<BlastedCell>& opened) override {
        TerrainExplode(at, radius);
        // GTA buildings next to the blast: a crater in them too
        for (const CarveOpened& o : CarveExplode(at, radius))
            opened.push_back({ o.cell, o.block, o.surface });
    }
};
GtaHost gGtaHost;
const bool gGtaHostSet = (SetHost(&gGtaHost), true);
} // namespace

void PollExplosions(float dt) {
    for (size_t i = 0; i < gOwnBlasts.size();) {
        gOwnBlasts[i].age += dt;
        if (gOwnBlasts[i].age > 2.0f) {
            gOwnBlasts[i] = gOwnBlasts.back();
            gOwnBlasts.pop_back();
        } else {
            ++i;
        }
    }
    for (int i = 0; i < 16; ++i) {
        CExplosion& e = aExplosions[i];
        // the "active counter" byte lives at offset 0x28 (starts at 1, counts frames, 0 = free slot)
        const uint8_t counter = *(reinterpret_cast<const uint8_t*>(&e) + 0x28);
        const bool fresh = counter != 0 && (gExpCounter[i] == 0 || counter < gExpCounter[i] ||
                                            (e.m_vecPosition - gExpPos[i]).Magnitude() > 0.01f);
        gExpCounter[i] = counter;
        gExpPos[i] = e.m_vecPosition;
        if (!fresh)
            continue;
        bool own = false;
        for (auto& o : gOwnBlasts)
            if ((o.pos - e.m_vecPosition).Magnitude() < 0.05f)
                own = true;
        if (own)
            continue;
        float radius = 0.0f;
        switch (e.m_nType) {
        case EXPLOSION_GRENADE: radius = 3.2f; break;
        case EXPLOSION_ROCKET: case EXPLOSION_TANK_FIRE: radius = 3.5f; break;
        case EXPLOSION_WEAK_ROCKET: case EXPLOSION_MINE: radius = 2.5f; break;
        case EXPLOSION_CAR: case EXPLOSION_QUICK_CAR: case EXPLOSION_BOAT: radius = 3.0f; break;
        case EXPLOSION_AIRCRAFT: radius = 4.5f; break;
        case EXPLOSION_SMALL: case EXPLOSION_RC_VEHICLE: case EXPLOSION_OBJECT: radius = 1.5f; break;
        default: radius = 0.0f; break;
        }
        if (radius > 0)
            ExplodeAt(e.m_vecPosition, radius, false);
    }
}

} // namespace mc
