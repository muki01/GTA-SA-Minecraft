#include "PedSkins.h"

#include "CCamera.h"
#include "CCarCtrl.h"
#include "CObject.h"
#include "CPed.h"
#include "CPlayerPed.h"
#include "CPools.h"
#include "CVehicle.h"
#include "RenderWare.h"
#include "common.h"
#include "ePedBones.h"
#include "ePedType.h"

#include "Collision.h"
#include "Config.h"
#include "Draw3D.h"
#include "Game.h"
#include "McModel.h"
#include "Player3D.h"
#include "Render3D.h"
#include "Sound.h"
#include "Textures.h"
#include "Villagers.h"

namespace mc {

namespace {
constexpr float kNpcScale = 0.9375f;

// a GTA ped as a villager: the core's NpcLook, and what GTA needs on top
struct Look : NpcLook {
    bool skip = false;   // was already invisible when we first saw it (script / cutscene)
    bool hidden = false; // we switched its GTA model off
    unsigned seen = 0;
};

std::unordered_map<int, Look> gLooks; // by ped pool reference
bool gActive = false;
unsigned gFrame = 0;

struct Launched {
    int ref;
    float time;
    CVector vel;
};
std::vector<Launched> gLaunched;

CVector Flat(CVector v) {
    v.z = 0.0f;
    float m = v.Magnitude();
    return m > 1e-4f ? v * (1.0f / m) : CVector(0, 1, 0);
}

float Dot(const CVector& a, const CVector& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

int KindFor(CPed* p) {
    int t = p->m_nPedType;
    if (t == PED_TYPE_COP)
        return NPC_PILLAGER;
    if ((t >= PED_TYPE_GANG1 && t <= PED_TYPE_GANG10) || t == PED_TYPE_DEALER || t == PED_TYPE_CRIMINAL)
        return NPC_VINDICATOR;
    return NPC_VILLAGER;
}

int ProfessionFor(CPed* p, int slot) {
    if (p->m_nPedType == PED_TYPE_MEDIC)
        return 4; // cleric
    if (p->m_nPedType == PED_TYPE_FIREMAN)
        return 5; // armorer
    return (int)((p->m_nModelIndex * 7u + (unsigned)slot) % 6u);
}

bool IsDead(CPed* p) {
    return p->m_fHealth <= 0.0f || p->m_ePedState == PEDSTATE_DIE || p->m_ePedState == PEDSTATE_DEAD;
}

bool HasGun(CPed* p) {
    const int w = p->m_aWeapons[p->m_nSelectedWepSlot].m_eWeaponType;
    return (w >= WEAPONTYPE_PISTOL && w <= WEAPONTYPE_MINIGUN);
}

bool HasMelee(CPed* p) {
    const int w = p->m_aWeapons[p->m_nSelectedWepSlot].m_eWeaponType;
    return w > WEAPONTYPE_UNARMED && w < WEAPONTYPE_PISTOL;
}

CVector Bone(CPed* p, unsigned int id) {
    RwV3d v;
    p->GetBonePosition(v, id, false);
    return CVector(v.x, v.y, v.z);
}

RpAtomic* AtomicDrawnCB(RpAtomic* atomic, void* data) {
    if (data)
        RpAtomicSetFlags(atomic, RpAtomicGetFlags(atomic) | rpATOMICRENDER);
    else
        RpAtomicSetFlags(atomic, RpAtomicGetFlags(atomic) & ~rpATOMICRENDER);
    return atomic;
}

} // namespace

void SetPedDrawn(CPed* ped, bool drawn) {
    if (!ped)
        return;
    ped->bIsVisible = drawn;
    if (ped->m_pRwClump)
        RpClumpForAllAtomics(ped->m_pRwClump, AtomicDrawnCB, drawn ? (void*)1 : nullptr);
}

// A hidden ped is not pre-rendered, so GTA stops updating its skeleton (and the hit spheres that
// bullets, arrows and fists test against). This brings it up to date.
static void UpdatePedBones(CPed* ped) {
    if (ped && ped->m_pRwObject)
        ped->UpdateRpHAnim();
}

float SeatedFit(CPed* p, const CVector& up, float headAboveHipPx, CVector* hip) {
    const CVector pos = p->GetPosition();
    *hip = pos;
    if (!p->m_pRwObject)
        return 0.6f;
    CVector pelvis = Bone(p, BONE_PELVIS), head = Bone(p, BONE_HEAD);
    if ((pelvis - pos).Magnitude() > 1.5f || (head - pos).Magnitude() > 2.0f)
        return 0.6f; // skeleton not up to date
    float avail = Dot(head - pelvis, up) + 0.30f; // the head may poke a little above GTA's
    if (!(avail > 0.3f && avail < 2.0f))
        return 0.6f;
    *hip = pelvis;
    // bikes, quads and bicycles have no roof: full size, like on foot
    if (CVehicle* v = p->m_pVehicle) {
        const int sub = v->m_nVehicleSubClass;
        if (sub == VEHICLE_BIKE || sub == VEHICLE_BMX || sub == VEHICLE_QUAD)
            return kNpcScale;
    }
    return Clamp(avail / (headAboveHipPx / 16.0f), 0.5f, kNpcScale);
}

void PedSkinsForget() {
    gLooks.clear();
    gLaunched.clear();
}

// shows the GTA models again
static void PedSkinsRestore() {
    auto* pool = CPools::ms_pPedPool;
    if (pool) {
        for (auto& kv : gLooks) {
            if (!kv.second.hidden)
                continue;
            if (CPed* p = pool->GetAtRef(kv.first))
                SetPedDrawn(p, true);
        }
    }
    gLooks.clear();
    gActive = false;
}

void PedSkinsUpdate(float dt, CPlayerPed* player) {
    const bool want = gGta.enabled && gConfig.pedSkins && gEntityTex.tex;
    if (!want) {
        if (gActive)
            PedSkinsRestore();
        return;
    }
    auto* pool = CPools::ms_pPedPool;
    if (!pool)
        return;
    gActive = true;
    ++gFrame;
    const CVector playerPos = player ? player->GetPosition() : CVector(0, 0, -10000.0f);

    for (int i = 0; i < pool->m_nSize; ++i) {
        CPed* p = pool->GetAt(i);
        if (!p || p == player || p->IsPlayer())
            continue;
        const int ref = pool->GetRef(p);
        auto it = gLooks.find(ref);
        if (it == gLooks.end()) {
            Look l;
            static_cast<NpcLook&>(l) = NpcStart(KindFor(p), ProfessionFor(p, i), p->m_fHealth, IsDead(p));
            l.skip = !p->bIsVisible;
            it = gLooks.emplace(ref, l).first;
        }
        Look& l = it->second;
        l.seen = gFrame;
        if (l.skip)
            continue;
        SetPedDrawn(p, false);
        l.hidden = true;

        NpcFacts f;
        f.pos = p->GetPosition();
        f.forward = p->GetForward();
        const CVector v = p->m_vecMoveSpeed * 50.0f;
        f.speed = p->bInVehicle ? 0.0f : std::sqrt(v.x * v.x + v.y * v.y);
        f.health = p->m_fHealth;
        f.dead = IsDead(p);
        f.seated = p->bInVehicle;
        NpcTick(l, dt, f, playerPos);
    }

    // forget peds that no longer exist
    if ((gFrame & 63) == 0) {
        for (auto it = gLooks.begin(); it != gLooks.end();) {
            if (it->second.seen != gFrame)
                it = gLooks.erase(it);
            else
                ++it;
        }
    }
}

void PedSkinsAfterProcess(CPlayerPed* player) {
    if (gActive) {
        if (auto* pool = CPools::ms_pPedPool)
            for (auto& kv : gLooks)
                if (kv.second.hidden)
                    if (CPed* p = pool->GetAtRef(kv.first))
                        UpdatePedBones(p);
    }
    if (player && gGta.hidPlayer)
        UpdatePedBones(player);
}

namespace {
void DrawPedNpc(CPed* p, const Look& l, float light) {
    const CVector pos = p->GetPosition();
    CVector fwd, up, right, feet;
    const bool riding = p->bInVehicle && p->m_pVehicle;
    const bool gun = HasGun(p);
    const int kind = l.kind == NPC_VILLAGER && gun ? NPC_VINDICATOR : l.kind;
    float scale = kNpcScale;
    if (riding) {
        const CMatrix& vm = *p->m_pVehicle->m_matrix;
        fwd = vm.up;
        up = vm.at;
        right = CVector::Cross(fwd, up);
        CVector hip;
        scale = SeatedFit(p, up, 22.0f, &hip);
        feet = hip - up * (0.75f * scale);
    } else {
        fwd = Flat(p->GetForward());
        up = CVector(0, 0, 1);
        right = CVector(fwd.y, -fwd.x, 0.0f);
        feet = pos - CVector(0, 0, 1.0f);
    }
    float r = 1.0f, g = 1.0f, b = 1.0f;
    if (l.death >= 0.0f && !riding) {
        float f = std::min(1.0f, std::sqrt(l.death * 1.6f));
        float a = f * (kPi / 2.0f);
        CVector r2 = right * std::cos(a) + up * std::sin(a);
        CVector u2 = up * std::cos(a) - right * std::sin(a);
        right = r2;
        up = u2;
    }
    if (l.hurt > 0.0f || (l.death >= 0.0f && l.death < 1.0f)) {
        g = 0.45f;
        b = 0.45f;
    }
    NpcAnim a;
    a.limbSwing = l.limbSwing;
    a.limbAmount = l.death >= 0.0f ? 0.0f : l.limbAmount;
    a.headYaw = l.death >= 0.0f ? 0.0f : l.headYaw;
    a.riding = riding;
    a.crossbow = l.death < 0.0f && gun;
    a.armed = l.death < 0.0f && HasMelee(p);
    a.variant = l.variant;
    DrawNpc(kind, EntityPose(feet, right, up, fwd, scale), a, light, r, g, b);
    if (!riding)
        AddShadow(CVector(pos.x, pos.y, pos.z - 1.0f), 0.5f, 0.8f);
}
} // namespace

void PedSkinsRender(float light) {
    if (!gActive || !gEntityTex.tex)
        return;
    auto* pool = CPools::ms_pPedPool;
    if (!pool)
        return;
    const CVector cam = TheCamera.GetPosition();
    for (auto& kv : gLooks) {
        const Look& l = kv.second;
        if (!l.hidden)
            continue;
        CPed* p = pool->GetAtRef(kv.first);
        if (!p || (p->bInVehicle && p->m_pVehicle))
            continue; // drivers and passengers are drawn together with their vehicle
        const CVector pos = p->GetPosition();
        CVector d = pos - cam;
        if (d.x * d.x + d.y * d.y + d.z * d.z > 140.0f * 140.0f)
            continue;
        if (!TheCamera.IsSphereVisible(pos, 2.2f))
            continue;
        DrawPedNpc(p, l, light);
    }
    d3::Flush();
}

// Called right before GTA draws a vehicle: its occupants have to be drawn first so that the
// windows end up in front of them.
void RenderVehicleOccupants(CVehicle* veh) {
    if (!veh || !gEntityTex.tex)
        return;
    CPlayerPed* player = FindPlayerPed();
    auto* pool = CPools::ms_pPedPool;
    CPed* seats[9];
    seats[0] = veh->m_pDriver;
    for (int i = 0; i < 8; ++i)
        seats[i + 1] = veh->m_apPassengers[i];
    bool steve = false;
    const Look* looks[9] = {};
    bool any = false;
    for (int i = 0; i < 9; ++i) {
        CPed* p = seats[i];
        if (!p || !p->bInVehicle)
            continue;
        if (p == player) {
            steve = gGta.enabled && gGta.inWorld && gGta.steve && gGame.cameraMode != CAM_FIRST;
            any = any || steve;
        } else if (gActive && pool) {
            auto it = gLooks.find(pool->GetRef(p));
            if (it != gLooks.end() && it->second.hidden) {
                looks[i] = &it->second;
                any = true;
            }
        }
    }
    if (!any)
        return;
    d3::StateGuard guard;
    d3::StateOpaque();
    const float light = DaylightFactor();
    for (int i = 0; i < 9; ++i)
        if (looks[i])
            DrawPedNpc(seats[i], *looks[i], light);
    if (steve)
        RenderPlayerInVehicle(light);
    d3::Flush();
}

// ---------------------------------------------------------------- ray tests
PedHit RaycastPeds(const CVector& o, const CVector& dir, float maxDist, const CEntity* ignore, bool includePlayer, bool seated) {
    PedHit best;
    best.dist = maxDist;
    auto* pool = CPools::ms_pPedPool;
    if (!pool)
        return best;
    for (int i = 0; i < pool->m_nSize; ++i) {
        CPed* p = pool->GetAt(i);
        const bool inVehicle = p && p->bInVehicle && p->m_pVehicle;
        if (!p || p == ignore || (inVehicle && !seated))
            continue;
        if (p->IsPlayer() && !includePlayer)
            continue;
        const CVector c = p->GetPosition();
        float r = 0.36f, z0 = c.z - 1.0f, z1 = c.z + 0.9f;
        if (inVehicle) {
            // sitting: GTA keeps the ped at its seat, the body from the hips up
            r = 0.33f;
            z0 = c.z - 0.45f;
            z1 = c.z + 0.8f;
        } else if (IsDead(p)) {
            r = 0.9f;
            z1 = c.z - 0.45f;
        } else if (p->bIsDucking) {
            z1 = c.z + 0.3f;
        }
        const float lo[3] = { c.x - r, c.y - r, z0 }, hi[3] = { c.x + r, c.y + r, z1 };
        const float ro[3] = { o.x, o.y, o.z }, rd[3] = { dir.x, dir.y, dir.z };
        float t0 = 0.0f, t1 = best.dist;
        bool miss = false;
        for (int a = 0; a < 3 && !miss; ++a) {
            if (std::fabs(rd[a]) < 1e-6f) {
                miss = ro[a] < lo[a] || ro[a] > hi[a];
                continue;
            }
            float ta = (lo[a] - ro[a]) / rd[a], tb = (hi[a] - ro[a]) / rd[a];
            if (ta > tb)
                std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            miss = t0 > t1;
        }
        if (miss)
            continue;
        best.ped = p;
        best.dist = t0;
        best.point = o + dir * t0;
    }
    return best;
}

// ---------------------------------------------------------------- trading
bool VillagerInteract(CPed* p) {
    if (!gActive || !p || IsDead(p) || p->bInVehicle)
        return false;
    auto* pool = CPools::ms_pPedPool;
    auto it = gLooks.find(pool->GetRef(p));
    if (it == gLooks.end() || it->second.kind != NPC_VILLAGER || HasGun(p))
        return false;
    VillagerTrade(it->second.variant, it->second.offer, p->GetPosition() + CVector(0, 0, 0.7f));
    return true;
}

// ---------------------------------------------------------------- flying things
void LaunchPed(CPed* p, const CVector& velocity) {
    if (!p || p->bInVehicle)
        return;
    auto* pool = CPools::ms_pPedPool;
    if (!pool)
        return;
    const int ref = pool->GetRef(p);
    for (auto& l : gLaunched)
        if (l.ref == ref) {
            l.time = 0.25f;
            l.vel = velocity;
            return;
        }
    gLaunched.push_back({ ref, 0.25f, velocity });
    CVector pos = p->GetPosition();
    pos.z += 0.3f;
    p->SetPosn(pos);
    p->bIsStanding = false;
    p->bWasStanding = false;
    p->m_vecMoveSpeed = velocity * (1.0f / 50.0f);
}

void LaunchEntity(CEntity* e, const CVector& vel) {
    if (!e || IsCollisionObject(e))
        return;
    CPlayerPed* self = FindPlayerPed();
    if (e == self)
        return;
    const CVector speed = vel * (1.0f / 50.0f);
    const CVector spin((Rand01() - 0.5f) * 0.14f, (Rand01() - 0.5f) * 0.14f, (Rand01() - 0.5f) * 0.14f);
    switch (e->m_nType) {
    case ENTITY_TYPE_PED: {
        CPed* p = static_cast<CPed*>(e);
        if (p->bInVehicle && p->m_pVehicle)
            LaunchEntity(p->m_pVehicle, vel);
        else
            LaunchPed(p, vel);
        break;
    }
    case ENTITY_TYPE_VEHICLE: {
        CVehicle* v = static_cast<CVehicle*>(e);
        if (self && self->bInVehicle && self->m_pVehicle == v)
            return;
        if (v->m_nStatus == STATUS_SIMPLE)
            CCarCtrl::SwitchVehicleToRealPhysics(v);
        if (v->bIsStatic) {
            v->SetIsStatic(false);
            v->AddToMovingList();
        }
        v->m_vecMoveSpeed = speed;
        v->m_vecTurnSpeed = spin;
        break;
    }
    case ENTITY_TYPE_OBJECT: {
        CObject* o = static_cast<CObject*>(e);
        if (o->bDisableTurnForce || o->bInfiniteMass)
            return; // bolted to the map for good
        if (o->bIsStatic) {
            o->SetIsStatic(false);
            o->AddToMovingList();
        }
        o->m_vecMoveSpeed = speed;
        o->m_vecTurnSpeed = spin;
        break;
    }
    default:
        break;
    }
}

void UpdateLaunchedPeds(float dt) {
    auto* pool = CPools::ms_pPedPool;
    for (size_t i = 0; i < gLaunched.size();) {
        Launched& l = gLaunched[i];
        l.time -= dt;
        CPed* p = pool ? pool->GetAtRef(l.ref) : nullptr;
        // the first frames GTA may still glue the ped to the ground: keep pushing
        if (p && !p->bInVehicle && l.time > 0.0f) {
            p->bIsStanding = false;
            p->m_vecMoveSpeed = l.vel * (1.0f / 50.0f);
            l.vel.z -= 20.0f * dt;
            ++i;
        } else {
            gLaunched[i] = gLaunched.back();
            gLaunched.pop_back();
        }
    }
}

} // namespace mc
