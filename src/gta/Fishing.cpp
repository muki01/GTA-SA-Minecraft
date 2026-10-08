#include "Fishing.h"

#include "CCamera.h"
#include "CColPoint.h"
#include "CObject.h"
#include "CPed.h"
#include "CPlayerPed.h"
#include "CPools.h"
#include "CScene.h"
#include "CVehicle.h"
#include "CWaterLevel.h"
#include "CWorld.h"
#include "common.h"

#include "Collision.h"
#include "Draw3D.h"
#include "Game.h"
#include "Inventory.h"
#include "Items.h"
#include "Mobs.h"
#include "PedSkins.h"
#include "Pose.h"
#include "Render3D.h"
#include "Sound.h"
#include "Terrain.h"
#include "Textures.h"

namespace mc {

namespace {
enum BobState { BOB_FLYING, BOB_FLOATING, BOB_STUCK, BOB_HOOKED_MOB, BOB_HOOKED_PED, BOB_HOOKED_VEHICLE, BOB_HOOKED_OBJECT };

struct Bobber {
    bool active = false;
    int state = BOB_FLYING;
    CVector pos, vel;
    float waterZ = 0.0f;
    float wait = 0.0f;     // until a fish notices the bait
    float approach = 0.0f; // the fish is swimming towards the bobber
    float approachAngle = 0.0f;
    float nibble = 0.0f;   // time left to reel the fish in
    float dip = 0.0f;
    float age = 0.0f;
    uint32_t mobId = 0;
    int pedRef = -1;
    int vehRef = -1;
    int objRef = -1;
    CVector local; // hook point in the hooked vehicle's / object's own space
};
Bobber gBob;

CVector Norm(const CVector& v) {
    float m = v.Magnitude();
    return m > 1e-5f ? v * (1.0f / m) : CVector(0, 0, 1);
}

float Pitch() { return 0.4f / (Rand01() * 0.4f + 0.8f); }

void WaterParticle(const CVector& at, uint16_t tile, float up, float spread) {
    Particle p;
    p.pos = at + CVector((Rand01() - 0.5f) * spread, (Rand01() - 0.5f) * spread, 0.05f);
    p.vel = CVector((Rand01() - 0.5f) * 0.6f, (Rand01() - 0.5f) * 0.6f, up * (0.5f + Rand01()));
    p.maxLife = p.life = 0.35f + Rand01() * 0.35f;
    p.tile = tile;
    p.size = 0.07f;
    p.gravity = 6.0f;
    SpawnParticle(p);
}

ItemStack RollLoot() {
    ItemStack s;
    s.count = 1;
    float r = Rand01();
    if (r < 0.85f) {
        float f = Rand01();
        s.id = f < 0.6f ? ID_COD : (f < 0.85f ? ID_SALMON : (f < 0.87f ? ID_TROPICAL_FISH : ID_PUFFERFISH));
    } else if (r < 0.95f) {
        static const uint16_t junk[] = { ID_LEATHER, ID_BONE, ID_STRING, ID_BOWL, ID_STICK, ID_INK_SAC, ID_ROTTEN_FLESH,
                                         ID_FISHING_ROD };
        s.id = junk[rand() % 8];
        if (s.id == ID_FISHING_ROD)
            s.damage = (uint16_t)(Item(ID_FISHING_ROD).durability * (0.3f + Rand01() * 0.6f));
    } else {
        static const uint16_t treasure[] = { ID_NAME_TAG, ID_SADDLE, ID_BOW, ID_NAUTILUS_SHELL };
        s.id = treasure[rand() % 4];
    }
    return s;
}

void StartFloating(float waterZ) {
    gBob.state = BOB_FLOATING;
    gBob.waterZ = waterZ;
    gBob.pos.z = waterZ;
    gBob.vel = CVector(0, 0, 0);
    gBob.wait = 4.0f + Rand01() * 14.0f;
    gBob.dip = 0.15f;
    for (int i = 0; i < 6; ++i)
        WaterParticle(gBob.pos, TILE_P_SPLASH_0 + rand() % 4, 2.0f, 0.3f);
    PlaySfx(SND_BOBBER_SPLASH, &gBob.pos, 0.2f, 1.2f + Rand01() * 0.4f);
}

// hook point relative to an entity (survives the entity moving and turning)
CVector ToLocal(CEntity* e, const CVector& p) {
    const CMatrix& m = *e->m_matrix;
    CVector d = p - m.pos;
    return CVector(d.x * m.right.x + d.y * m.right.y + d.z * m.right.z, d.x * m.up.x + d.y * m.up.y + d.z * m.up.z,
                   d.x * m.at.x + d.y * m.at.y + d.z * m.at.z);
}

CVector FromLocal(CEntity* e, const CVector& l) {
    const CMatrix& m = *e->m_matrix;
    return m.pos + m.right * l.x + m.up * l.y + m.at * l.z;
}

CEntity* HookedEntity() {
    if (gBob.state == BOB_HOOKED_VEHICLE)
        return CPools::GetVehicle(gBob.vehRef);
    if (gBob.state == BOB_HOOKED_OBJECT)
        return CPools::GetObject(gBob.objRef);
    return nullptr;
}
} // namespace

bool FishingIsCast() { return gBob.active; }
void FishingClear() { gBob = Bobber(); }

bool FishingUse(CPlayerPed* ped) {
    if (!ped)
        return false;
    if (!gBob.active) {
        gBob = Bobber();
        gBob.active = true;
        const CVector look = gGame.lookDir;
        gBob.pos = gGame.eyePos + look * 0.7f - CVector(0, 0, 0.1f);
        gBob.vel = look * 21.0f + CVector(0, 0, 3.0f);
        PlaySfx(SND_BOBBER_THROW, nullptr, 0.5f, Pitch());
        StartSwing();
        return true;
    }
    // reel in: whatever hangs on the hook flies towards the player
    const CVector d = ped->GetPosition() - gBob.pos;
    const float dist = d.Magnitude();
    const CVector pull(d.x * 2.0f, d.y * 2.0f, d.z * 2.0f + std::sqrt(dist) * 1.6f);
    int wear = 0;
    if (gBob.state == BOB_FLOATING && gBob.nibble > 0.0f) {
        SpawnDrop(gBob.pos + CVector(0, 0, 0.3f), RollLoot(), pull, 0.3f);
        SpawnXp(ped->GetPosition() + CVector(0, 0, -0.5f), 1 + rand() % 6);
        for (int i = 0; i < 8; ++i)
            WaterParticle(gBob.pos, TILE_P_SPLASH_0 + rand() % 4, 3.0f, 0.4f);
        gSurvival.exhaustion += 0.05f;
        wear = 1;
    } else if (gBob.state == BOB_HOOKED_MOB) {
        int i = MobIndexById(gBob.mobId);
        if (i >= 0)
            MobPush(i, CVector(pull.x * 0.7f, pull.y * 0.7f, 5.0f + dist * 0.3f));
        wear = 3;
    } else if (gBob.state == BOB_HOOKED_PED) {
        CPed* p = CPools::ms_pPedPool ? CPools::ms_pPedPool->GetAtRef(gBob.pedRef) : nullptr;
        if (p)
            LaunchEntity(p, CVector(d.x * 1.5f, d.y * 1.5f, 6.0f + dist * 0.4f));
        wear = 3;
    } else if (CEntity* e = HookedEntity()) {
        const float k = gBob.state == BOB_HOOKED_VEHICLE ? 0.9f : 1.3f;
        LaunchEntity(e, CVector(d.x * k, d.y * k, 5.0f + dist * 0.3f));
        wear = 5;
    } else if (gBob.state == BOB_STUCK) {
        wear = 2;
    }
    // items lying around the hook come along
    for (auto& dr : gDrops)
        if ((dr.pos - gBob.pos).Magnitude() < 2.0f) {
            CVector to = ped->GetPosition() - dr.pos;
            dr.vel = CVector(to.x * 1.6f, to.y * 1.6f, 4.0f + to.Magnitude() * 0.3f);
            dr.pickupDelay = std::min(dr.pickupDelay, dr.age);
        }
    if (wear)
        DamageHeldItem(wear);
    PlaySfx(SND_BOBBER_RETRIEVE, nullptr, 1.0f, Pitch());
    gBob.active = false;
    StartSwing();
    return true;
}

void FishingUpdate(float dt, CPlayerPed* ped) {
    if (!gBob.active)
        return;
    const ItemStack& held = gInv.Held();
    const bool holding = !held.Empty() && Item(held.id).special == SP_FISHING_ROD;
    if (!ped || !gGta.enabled || ped->bInVehicle || ped->m_fHealth <= 0.0f || !holding ||
        (gBob.pos - ped->GetPosition()).Magnitude() > 40.0f) {
        gBob.active = false;
        return;
    }
    gBob.age += dt;

    switch (gBob.state) {
    case BOB_FLYING: {
        gBob.vel.z -= 12.0f * dt;
        gBob.vel = gBob.vel * std::pow(0.92f, dt * 20.0f);
        const CVector step = gBob.vel * dt;
        const float len = step.Magnitude();
        if (len < 1e-5f)
            break;
        const CVector dir = step * (1.0f / len);
        const CVector next = gBob.pos + step;

        float best = len;
        int what = 0; // 1 world, 2 ped, 3 mob, 4 water, 5 vehicle, 6 object
        CColPoint cp;
        CEntity* e = nullptr;
        CPed* hitPed = nullptr;
        if (CWorld::ProcessLineOfSight(gBob.pos, next, cp, e, true, true, true, true, false, false, false, false) &&
            e != ped && !IsCollisionObject(e) && !TerrainIgnoreHit(cp.m_vecPoint, e)) {
            float d = (cp.m_vecPoint - gBob.pos).Magnitude();
            if (d < best) {
                best = d;
                what = 1;
                if (e && e->m_nType == ENTITY_TYPE_PED) {
                    what = 2;
                    hitPed = static_cast<CPed*>(e);
                } else if (e && e->m_nType == ENTITY_TYPE_VEHICLE) {
                    what = 5;
                } else if (e && e->m_nType == ENTITY_TYPE_OBJECT) {
                    what = 6;
                }
            }
        }
        VoxelHit vh = RaycastVoxels(gBob.pos, dir, len);
        if (vh.hit && vh.dist < best) {
            best = vh.dist;
            what = 1;
        }
        PedHit ph = RaycastPeds(gBob.pos, dir, len, ped, false);
        if (ph.ped && ph.dist < best) {
            best = ph.dist;
            what = 2;
            hitPed = ph.ped;
        }
        MobHit mh = MobsRaycast(gBob.pos, dir, len);
        if (mh.index >= 0 && mh.dist < best) {
            best = mh.dist;
            what = 3;
        }
        float wl = 0.0f;
        if (CWaterLevel::GetWaterLevelNoWaves(next.x, next.y, next.z, &wl) && next.z <= wl && gBob.pos.z >= wl - 0.3f &&
            step.z < 0.0f) {
            float d = Clamp((gBob.pos.z - wl) / -step.z, 0.0f, 1.0f) * len;
            if (d <= best) {
                best = d;
                what = 4;
            }
        }
        gBob.pos = gBob.pos + dir * std::max(0.0f, best - (what == 1 ? 0.05f : 0.0f));
        switch (what) {
        case 1:
            gBob.state = BOB_STUCK;
            gBob.vel = CVector(0, 0, 0);
            break;
        case 2:
            gBob.state = BOB_HOOKED_PED;
            gBob.pedRef = CPools::ms_pPedPool->GetRef(hitPed);
            break;
        case 3:
            gBob.state = BOB_HOOKED_MOB;
            gBob.mobId = MobIdAt(mh.index);
            break;
        case 4:
            StartFloating(wl);
            break;
        case 5:
            gBob.state = BOB_HOOKED_VEHICLE;
            gBob.vehRef = CPools::GetVehicleRef(static_cast<CVehicle*>(e));
            gBob.local = ToLocal(e, gBob.pos);
            break;
        case 6:
            gBob.state = BOB_HOOKED_OBJECT;
            gBob.objRef = CPools::GetObjectRef(static_cast<CObject*>(e));
            gBob.local = ToLocal(e, gBob.pos);
            break;
        default:
            break;
        }
        break;
    }
    case BOB_FLOATING: {
        float wl;
        if (CWaterLevel::GetWaterLevelNoWaves(gBob.pos.x, gBob.pos.y, gBob.pos.z, &wl))
            gBob.waterZ = wl;
        gBob.dip *= std::pow(0.05f, dt);
        gBob.pos.z = gBob.waterZ + std::sin(gBob.age * 3.0f) * 0.025f - gBob.dip;
        const CVector surface(gBob.pos.x, gBob.pos.y, gBob.waterZ);
        if (gBob.nibble > 0.0f) {
            gBob.nibble -= dt;
            if (rand() % 3 == 0)
                WaterParticle(surface, TILE_P_BUBBLE, 1.0f, 0.3f);
            if (gBob.nibble <= 0.0f)
                gBob.wait = 4.0f + Rand01() * 10.0f; // it got away
        } else if (gBob.approach > 0.0f) {
            gBob.approach -= dt;
            // the wake of the fish coming closer
            float r = gBob.approach * 1.6f;
            CVector at = surface + CVector(std::cos(gBob.approachAngle) * r, std::sin(gBob.approachAngle) * r, 0.0f);
            if (rand() % 2 == 0)
                WaterParticle(at, TILE_P_SPLASH_0 + rand() % 4, 0.8f, 0.12f);
            if (gBob.approach <= 0.0f) {
                gBob.nibble = 1.0f + Rand01() * 1.0f;
                gBob.dip = 0.3f;
                PlaySfx(SND_BOBBER_SPLASH, &gBob.pos, 0.4f, 1.0f + (Rand01() - Rand01()) * 0.4f);
                for (int i = 0; i < 10; ++i)
                    WaterParticle(surface, i % 2 ? TILE_P_BUBBLE : TILE_P_SPLASH_0 + rand() % 4, 2.5f, 0.35f);
            }
        } else {
            gBob.wait -= dt;
            if (gBob.wait <= 0.0f) {
                gBob.approach = 1.0f + Rand01() * 2.0f;
                gBob.approachAngle = Rand01() * 6.2831853f;
            }
        }
        break;
    }
    case BOB_HOOKED_MOB: {
        int i = MobIndexById(gBob.mobId);
        if (i < 0) {
            gBob.state = BOB_STUCK;
            break;
        }
        gBob.pos = MobCentre(i) + CVector(0, 0, 0.2f);
        break;
    }
    case BOB_HOOKED_PED: {
        CPed* p = CPools::ms_pPedPool ? CPools::ms_pPedPool->GetAtRef(gBob.pedRef) : nullptr;
        if (!p) {
            gBob.state = BOB_STUCK;
            break;
        }
        gBob.pos = p->GetPosition() + CVector(0, 0, 0.3f);
        break;
    }
    case BOB_HOOKED_VEHICLE:
    case BOB_HOOKED_OBJECT: {
        CEntity* e = HookedEntity();
        if (!e) {
            gBob.state = BOB_STUCK;
            break;
        }
        gBob.pos = FromLocal(e, gBob.local);
        break;
    }
    default:
        break;
    }
}

void FishingRender(float light) {
    CPlayerPed* ped = FindPlayerPed();
    if (!gBob.active || !ped || !gEntityTex.tex)
        return;
    const CMatrix& cm = TheCamera.m_mCameraMatrix;
    const CVector R = cm.right * -1.0f, U = cm.at, F = cm.up, cam = cm.pos;
    d3::SetRaster(gEntityTex.Raster());

    // the bobber always faces the camera
    {
        const float u0 = (float)ENT_HOOK.x / ENT_TEX_W, v0 = (float)ENT_HOOK.y / ENT_TEX_H;
        const float u1 = (float)(ENT_HOOK.x + ENT_HOOK.w) / ENT_TEX_W, v1 = (float)(ENT_HOOK.y + ENT_HOOK.h) / ENT_TEX_H;
        const CVector b = gBob.pos - U * 0.1f;
        d3::Quad(b - R * 0.25f + U * 0.5f, b + R * 0.25f + U * 0.5f, b + R * 0.25f, b - R * 0.25f, u0, v0, u1, v1, d3::Gray(light));
    }

    // where the line leaves the rod
    CVector tip;
    if (gGame.cameraMode == CAM_FIRST) {
        // the tip of the rod as the hand renderer draws it (view space of a 70 degree camera)
        float f = 1.0f;
        if (Scene.m_pCamera) {
            float vy = Scene.m_pCamera->viewWindow.y;
            if (vy > 0.05f && vy < 5.0f)
                f = vy / std::tan(Rad(35.0f));
        }
        float sw = gGame.swing >= 0.0f ? std::sin(std::sqrt(gGame.swing) * kPi) : 0.0f;
        float lower = 1.0f - Clamp(gGame.handHeight, 0.0f, 1.0f);
        tip = cam + F * 0.79f + R * (f * (0.56f - sw * 0.25f)) + U * (f * (-0.07f - sw * 0.3f - lower * 0.6f));
    } else {
        CVector fwd = ped->GetForward();
        fwd.z = 0.0f;
        fwd = Norm(fwd);
        CVector right(fwd.y, -fwd.x, 0.0f);
        tip = ped->GetPosition() + CVector(0, 0, (ped->bIsDucking ? 0.0f : 0.17f)) + right * 0.35f + fwd * 0.8f;
    }

    // the line sags like Minecraft's: z follows (t^2 + t) / 2
    const CVector start = gBob.pos + CVector(0, 0, 0.25f);
    const CVector d = tip - start;
    const float u = (ENT_WHITE.x + 4.0f) / ENT_TEX_W, v = (ENT_WHITE.y + 4.0f) / ENT_TEX_H;
    const RwUInt32 black = d3::Argb(0, 0, 0, 255);
    CVector prev = start;
    const int N = 16;
    for (int k = 1; k <= N; ++k) {
        float t = (float)k / N;
        CVector p(start.x + d.x * t, start.y + d.y * t, start.z + d.z * (t * t + t) * 0.5f);
        CVector mid = (p + prev) * 0.5f;
        CVector toCam = cam - mid;
        float camDist = toCam.Magnitude();
        CVector side = CVector::Cross(p - prev, toCam);
        float sm = side.Magnitude();
        if (sm > 1e-6f) {
            side = side * (std::max(0.004f, camDist * 0.0012f) / sm);
            d3::Quad(prev - side, prev + side, p + side, p - side, u, v, u, v, black);
        }
        prev = p;
    }
    d3::Flush();
}

} // namespace mc
