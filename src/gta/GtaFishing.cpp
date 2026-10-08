#include "GtaFishing.h"

#include "CCamera.h"
#include "CColPoint.h"
#include "CObject.h"
#include "CPed.h"
#include "CPlayerPed.h"
#include "CPools.h"
#include "CScene.h"
#include "CVehicle.h"
#include "CWorld.h"
#include "common.h"

#include "Collision.h"
#include "Draw3D.h"
#include "Fishing.h"
#include "Game.h"
#include "PedSkins.h"
#include "Pose.h"
#include "Renderers.h"
#include "Terrain.h"
#include "Textures.h"

// The GTA side of the fishing rod: what of the GTA world the hook meets and holds on to, and drawing the bobber and
// the line. The rod itself is the core's business (src/core/Fishing.h).

namespace mc {

namespace {
CVector Norm(const CVector& v) {
    float m = v.Magnitude();
    return m > 1e-5f ? v * (1.0f / m) : CVector(0, 0, 1);
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

CPed* PedAt(int ref) { return CPools::ms_pPedPool ? CPools::ms_pPedPool->GetAtRef(ref) : nullptr; }

// the vehicle or loose object on the hook
CEntity* ThingOf(const HostHit& h) {
    if (h.vehicle >= 0)
        return CPools::GetVehicle(h.vehicle);
    if (h.object >= 0)
        return CPools::GetObject(h.object);
    return nullptr;
}
} // namespace

HostHit GtaHookTrace(const CVector& from, const CVector& dir, float len) {
    HostHit out;
    CPlayerPed* ped = FindPlayerPed();
    const CVector next = from + dir * len;
    float losDist = len;
    CColPoint cp;
    CEntity* e = nullptr;
    if (CWorld::ProcessLineOfSight(from, next, cp, e, true, true, true, true, false, false, false, false) &&
        e != ped && !IsCollisionObject(e) && !TerrainIgnoreHit(cp.m_vecPoint, e)) {
        const float d = (cp.m_vecPoint - from).Magnitude();
        if (d < len) {
            losDist = d;
            if (e && e->m_nType == ENTITY_TYPE_PED) {
                out.being = CPools::GetPedRef(static_cast<CPed*>(e));
                out.beingDist = d;
                out.beingPoint = cp.m_vecPoint;
            } else {
                out.hit = true;
                out.dist = d;
                out.point = cp.m_vecPoint;
                if (e && e->m_nType == ENTITY_TYPE_VEHICLE) {
                    out.vehicle = CPools::GetVehicleRef(static_cast<CVehicle*>(e));
                    out.local = ToLocal(e, cp.m_vecPoint);
                } else if (e && e->m_nType == ENTITY_TYPE_OBJECT) {
                    out.object = CPools::GetObjectRef(static_cast<CObject*>(e));
                    out.local = ToLocal(e, cp.m_vecPoint);
                }
            }
        }
    }
    const PedHit ph = RaycastPeds(from, dir, len, ped, false);
    if (ph.ped && ph.dist < losDist) {
        out.being = CPools::GetPedRef(ph.ped);
        out.beingDist = ph.dist;
        out.beingPoint = ph.point;
    }
    return out;
}

bool GtaHookPoint(const HostHit& hooked, CVector& at) {
    if (hooked.being >= 0) {
        CPed* p = PedAt(hooked.being);
        if (!p)
            return false;
        at = p->GetPosition() + CVector(0, 0, 0.3f);
        return true;
    }
    CEntity* e = ThingOf(hooked);
    if (!e)
        return false;
    at = FromLocal(e, hooked.local);
    return true;
}

bool GtaFling(const HostHit& what, const CVector& velocity) {
    CEntity* e = what.being >= 0 ? PedAt(what.being) : ThingOf(what);
    if (!e)
        return false;
    LaunchEntity(e, velocity);
    return true;
}

void FishingRender(float light) {
    CPlayerPed* ped = FindPlayerPed();
    if (!gBobber.active || !ped || !gEntityTex.tex)
        return;
    const CMatrix& cm = TheCamera.m_mCameraMatrix;
    const CVector R = cm.right * -1.0f, U = cm.at, F = cm.up, cam = cm.pos;
    d3::SetRaster(gEntityTex.Raster());

    DrawFishingHook(R, U, light);

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

    DrawFishingLine(tip, cam);
    d3::Flush();
}

} // namespace mc
