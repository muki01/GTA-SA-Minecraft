#include "Xp.h"

#include "CCamera.h"

#include "Draw3D.h"
#include "Entities.h"
#include "ModCommon.h"
#include "Textures.h"

namespace mc {

namespace {
int OrbIcon(int value) {
    static const int kSteps[] = { 2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3 };
    for (int i = 0; i < 10; ++i)
        if (value >= kSteps[i])
            return 10 - i;
    return 0;
}
} // namespace

void XpRender() {
    if (gXpOrbs.empty() || !gEntityTex.tex)
        return;
    const CMatrix& cm = TheCamera.m_mCameraMatrix;
    const CVector R = cm.right * -1.0f, U = cm.at;
    d3::SetRaster(gEntityTex.Raster());
    for (auto& o : gXpOrbs) {
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

} // namespace mc
