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
#include "Inventory.h"
#include "Items.h"
#include "McModel.h"
#include "Player3D.h"
#include "Render3D.h"
#include "Sound.h"
#include "Textures.h"

namespace mc {

namespace {
constexpr float kNpcScale = 0.9375f;

struct Look {
    int kind = NPC_VILLAGER;
    int variant = 0; // villager profession
    bool skip = false;   // was already invisible when we first saw it (script / cutscene)
    bool hidden = false; // we switched its GTA model off
    float limbSwing = 0.0f, limbAmount = 0.0f;
    float headYaw = 0.0f;
    float hurt = 0.0f;
    float lastHealth = -1.0f;
    float death = -1.0f;
    float say = 0.0f;
    int offer = 0;       // trade the villager shows next
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

SoundEvent SayOf(int kind) { return kind == NPC_VILLAGER ? SND_VILLAGER_SAY : SND_PILLAGER_SAY; }
SoundEvent HurtOf(int kind) { return kind == NPC_VILLAGER ? SND_VILLAGER_HURT : SND_PILLAGER_HURT; }
SoundEvent DeathOf(int kind) { return kind == NPC_VILLAGER ? SND_VILLAGER_DEATH : SND_PILLAGER_DEATH; }

RpAtomic* AtomicDrawnCB(RpAtomic* atomic, void* data) {
    if (data)
        RpAtomicSetFlags(atomic, RpAtomicGetFlags(atomic) | rpATOMICRENDER);
    else
        RpAtomicSetFlags(atomic, RpAtomicGetFlags(atomic) & ~rpATOMICRENDER);
    return atomic;
}

// ---------------------------------------------------------------- trading
struct Offer {
    uint16_t item;
    int count;
    int price; // emeralds
};
struct Want {
    uint16_t item;
    int count; // for one emerald
};
struct Profession {
    const char* name;
    Offer sells[5];
    int numSells;
    Want buys[4];
    int numBuys;
};
const Profession kProfessions[6] = {
    { "İşsiz Köylü", {}, 0, {}, 0 },
    { "Çiftçi",
      { { ID_BREAD, 6, 1 }, { ID_APPLE, 4, 1 }, { ID_PUMPKIN_PIE, 4, 1 }, { ID_GOLDEN_CARROT, 3, 3 } }, 4,
      { { ID_WHEAT, 20 }, { ID_CARROT, 22 }, { ID_POTATO, 26 }, { ID_BEETROOT, 15 } }, 4 },
    { "Kütüphaneci",
      { { ID_BOOK, 1, 1 }, { ID_GLASS, 4, 1 }, { ID_BOOKSHELF, 1, 9 }, { ID_EXPERIENCE_BOTTLE, 1, 3 } }, 4,
      { { ID_PAPER, 24 }, { ID_INK_SAC, 5 } }, 2 },
    { "Kasap",
      { { ID_COOKED_PORKCHOP, 5, 1 }, { ID_COOKED_CHICKEN, 8, 1 }, { ID_COOKED_BEEF, 5, 1 }, { ID_RABBIT_STEW, 1, 1 } }, 4,
      { { ID_CHICKEN, 14 }, { ID_PORKCHOP, 7 }, { ID_BEEF, 10 }, { ID_MUTTON, 7 } }, 4 },
    { "Rahip",
      { { ID_REDSTONE, 2, 1 }, { ID_LAPIS_LAZULI, 1, 1 }, { ID_GLOWSTONE, 1, 4 }, { ID_ENDER_PEARL, 1, 5 },
        { ID_EXPERIENCE_BOTTLE, 1, 3 } }, 5,
      { { ID_ROTTEN_FLESH, 32 }, { ID_GOLD_INGOT, 3 } }, 2 },
    { "Zırhçı",
      { { ID_IRON_HELMET, 1, 5 }, { ID_IRON_CHESTPLATE, 1, 9 }, { ID_IRON_LEGGINGS, 1, 7 }, { ID_IRON_BOOTS, 1, 4 },
        { ID_DIAMOND_CHESTPLATE, 1, 21 } }, 5,
      { { ID_COAL, 15 }, { ID_IRON_INGOT, 4 }, { ID_DIAMOND, 1 } }, 3 },
};

std::string OfferText(const Profession& pr, int offer) {
    std::string s = std::string(pr.name) + ": ";
    if (pr.numSells > 0) {
        const Offer& o = pr.sells[((offer % pr.numSells) + pr.numSells) % pr.numSells];
        s += std::to_string(o.price) + " Zümrüt -> " + std::to_string(o.count) + " " + ItemName(o.item);
    }
    if (pr.numBuys > 0) {
        const Want& w = pr.buys[((offer % pr.numBuys) + pr.numBuys) % pr.numBuys];
        s += "  |  " + std::to_string(w.count) + " " + ItemName(w.item) + " -> 1 Zümrüt";
    }
    return s;
}

void HappyParticles(const CVector& at) {
    for (int i = 0; i < 8; ++i) {
        Particle p;
        p.pos = at + CVector((Rand01() - 0.5f) * 0.8f, (Rand01() - 0.5f) * 0.8f, Rand01() * 0.6f);
        p.vel = CVector(0, 0, 0.6f);
        p.maxLife = p.life = 0.8f;
        p.tile = TILE_P_ENCHANTED_HIT;
        p.size = 0.09f;
        p.gravity = 0.0f;
        p.color = 0xFF40FF40;
        p.glow = true;
        SpawnParticle(p);
    }
}

void GiveOrDrop(uint16_t id, int count) {
    while (count > 0) {
        ItemStack s;
        s.id = id;
        s.count = (uint8_t)std::min(count, MaxStack(id));
        count -= s.count;
        int left = gInv.Add(s);
        if (left > 0) {
            s.count = (uint8_t)left;
            DropStackAtPlayer(s, false);
        }
    }
}
} // namespace

void SetPedDrawn(CPed* ped, bool drawn) {
    if (!ped)
        return;
    ped->bIsVisible = drawn;
    if (ped->m_pRwClump)
        RpClumpForAllAtomics(ped->m_pRwClump, AtomicDrawnCB, drawn ? (void*)1 : nullptr);
}

void UpdatePedBones(CPed* ped) {
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

void PedSkinsRestore() {
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
            l.kind = KindFor(p);
            l.variant = ProfessionFor(p, i);
            l.skip = !p->bIsVisible;
            l.lastHealth = p->m_fHealth;
            l.say = 4.0f + Rand01() * 40.0f;
            l.death = IsDead(p) ? 10.0f : -1.0f;
            l.offer = rand() % 5;
            it = gLooks.emplace(ref, l).first;
        }
        Look& l = it->second;
        l.seen = gFrame;
        if (l.skip)
            continue;
        SetPedDrawn(p, false);
        l.hidden = true;

        const CVector pos = p->GetPosition();
        const float dist = (pos - playerPos).Magnitude();
        CVector v = p->m_vecMoveSpeed * 50.0f;
        float speed = p->bInVehicle ? 0.0f : std::sqrt(v.x * v.x + v.y * v.y);
        float amount = Clamp(speed / 20.0f * 4.0f, 0.0f, 1.0f);
        l.limbAmount += (amount - l.limbAmount) * Clamp(dt * 8.0f, 0.0f, 1.0f);
        l.limbSwing += l.limbAmount * 20.0f * dt;
        l.hurt = std::max(0.0f, l.hurt - dt);

        const bool dead = IsDead(p);
        if (dead) {
            if (l.death < 0.0f) {
                l.death = 0.0f;
                if (dist < 40.0f)
                    PlaySfx(DeathOf(l.kind), &pos);
            }
            l.death += dt;
        } else {
            l.death = -1.0f;
            if (p->m_fHealth < l.lastHealth - 0.5f) {
                // (a burning ped loses health every frame: one grunt per flash is enough)
                if (l.hurt <= 0.0f && dist < 40.0f)
                    PlaySfx(HurtOf(l.kind), &pos);
                l.hurt = 0.45f;
            }
            l.say -= dt;
            if (l.say <= 0.0f) {
                l.say = 15.0f + Rand01() * 45.0f;
                if (dist < 18.0f && !p->bInVehicle)
                    PlaySfx(SayOf(l.kind), &pos, 0.8f, 0.9f + Rand01() * 0.2f);
            }
        }
        l.lastHealth = p->m_fHealth;

        // the head follows the player when he is close
        float wantYaw = 0.0f;
        if (!dead && !p->bInVehicle && dist < 6.0f && dist > 0.5f) {
            CVector fwd = Flat(p->GetForward());
            CVector right(fwd.y, -fwd.x, 0.0f);
            CVector dir = Flat(playerPos - pos);
            wantYaw = Clamp(std::atan2(dir.x * right.x + dir.y * right.y, dir.x * fwd.x + dir.y * fwd.y), -1.0f, 1.0f);
        }
        l.headYaw += (wantYaw - l.headYaw) * Clamp(dt * 5.0f, 0.0f, 1.0f);
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
    Look& l = it->second;
    const Profession& pr = kProfessions[((l.variant % 6) + 6) % 6];
    const CVector head = p->GetPosition() + CVector(0, 0, 0.7f);
    StartSwing();
    if (pr.numSells == 0 && pr.numBuys == 0) {
        PlaySfx(SND_VILLAGER_NO, &head);
        ShowMessage(std::string(pr.name) + ": bu köylünün mesleği yok");
        return true;
    }
    ItemStack& held = gInv.Held();
    // selling to the villager
    for (int i = 0; i < pr.numBuys; ++i) {
        const Want& w = pr.buys[i];
        if (held.Empty() || held.id != w.item)
            continue;
        if (gInv.CountOf(w.item) < w.count) {
            PlaySfx(SND_VILLAGER_NO, &head);
            ShowMessage(std::string(pr.name) + ": " + std::to_string(w.count) + " " + ItemName(w.item) + " gerekli");
            return true;
        }
        gInv.Remove(w.item, w.count);
        GiveOrDrop(ID_EMERALD, 1);
        PlaySfx(SND_VILLAGER_TRADE, &head);
        HappyParticles(head);
        ShowMessage(std::string("Sattın: ") + std::to_string(w.count) + " " + ItemName(w.item) + " -> 1 Zümrüt");
        gWorld.dirty = true;
        return true;
    }
    // buying with emeralds
    if (!held.Empty() && held.id == ID_EMERALD && pr.numSells > 0) {
        const Offer& o = pr.sells[((l.offer % pr.numSells) + pr.numSells) % pr.numSells];
        if (gGame.gameMode != MODE_CREATIVE && gInv.CountOf(ID_EMERALD) < o.price) {
            PlaySfx(SND_VILLAGER_NO, &head);
            ShowMessage(std::string(pr.name) + ": " + std::to_string(o.price) + " Zümrüt gerekli");
            return true;
        }
        if (gGame.gameMode != MODE_CREATIVE)
            gInv.Remove(ID_EMERALD, o.price);
        GiveOrDrop(o.item, o.count);
        PlaySfx(SND_VILLAGER_TRADE, &head);
        HappyParticles(head);
        ShowMessage(std::string("Aldın: ") + std::to_string(o.count) + " " + ItemName(o.item));
        gWorld.dirty = true;
        return true;
    }
    // anything else: show the next offer
    l.offer = (l.offer + 1) % 60;
    PlaySfx(SND_VILLAGER_YES, &head);
    ShowMessage(OfferText(pr, l.offer), 4.0f);
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
