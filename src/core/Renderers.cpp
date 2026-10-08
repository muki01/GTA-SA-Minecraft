#include "Renderers.h"

#include <algorithm>
#include <cmath>

#include "BlockMesh.h"
#include "BlockRules.h"
#include "Fishing.h"
#include "GameState.h"
#include "Items.h"
#include "McModel.h"
#include "Pose.h"

namespace mc {

namespace {
Vec3 Norm(const Vec3& v) {
    float m = v.Length();
    return m > 1e-5f ? v * (1.0f / m) : Vec3(0, 0, 1);
}

Vec3 Normalized(const Vec3& v) {
    float m = v.Length();
    return m > 1e-5f ? v * (1.0f / m) : Vec3(0, 1, 0);
}

int OrbIcon(int value) {
    static const int kSteps[] = { 2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3 };
    for (int i = 0; i < 10; ++i)
        if (value >= kSteps[i])
            return 10 - i;
    return 0;
}
} // namespace

void DrawCube(const Vec3& c, float half, const Vec3& r, const Vec3& u, const Vec3& f, int block, float light) {
    TheModelSink().Texture(MT_ATLAS);
    for (int face = 0; face < 6; ++face) {
        TileUV uv = AtlasTileUV(BlockFaceTile(block, face, Block(block).shape == SHAPE_FACING ? 2 : 0));
        float us[4] = { uv.u0, uv.u1, uv.u1, uv.u0 };
        float vs[4] = { uv.v1, uv.v1, uv.v0, uv.v0 };
        float s = Block(block).emissive ? 1.0f : light * kFaceShade[face];
        Vec3 p[4];
        for (int k = 0; k < 4; ++k) {
            const int* cc = kFaceCorners[face][k];
            p[k] = c + r * ((cc[0] * 2 - 1) * half) + f * ((cc[1] * 2 - 1) * half) + u * ((cc[2] * 2 - 1) * half);
        }
        TheModelSink().Quad(p, us, vs, ModelGray(s));
    }
}

void DrawSprite3D(const Vec3& c, const Vec3& au, const Vec3& av, float half, uint16_t tile, float light) {
    TheModelSink().Texture(MT_ATLAS);
    TileUV uv = AtlasTileUV(tile);
    // texture v runs downwards, av points up
    ModelQuad(c - au * half + av * half, c + au * half + av * half, c + au * half - av * half, c - au * half - av * half,
             uv.u0, uv.v0, uv.u1, uv.v1, ModelGray(light));
}

// ItemEntityRenderer: an item lying on the ground bobs and spins; a stack shows a few copies
bool DrawDrop(const DropEntity& d, float light) {
    // ItemEntityRenderer: bob, spin, then the model's "ground" transform
    const uint16_t id = d.stack.id;
    if (!IsValidItem(id))
        return false;
    const float groundScale = kDisplays[kItemDisplay[id]].ground.scale[1];
    Pose p = WorldPose(d.pos);
    p.Translate(0.0f, std::sin(d.age * 2.0f + d.spin) * 0.1f + 0.1f + 0.25f * groundScale, 0.0f);
    p.RotY(d.age + d.spin);
    int copies = d.stack.count > 48 ? 5 : d.stack.count > 32 ? 4 : d.stack.count > 16 ? 3 : d.stack.count > 1 ? 2 : 1;
    const bool flat = !IsBlockItem(id);
    if (flat)
        p.Translate(0.0f, 0.0f, -0.09375f * groundScale * (copies - 1) * 0.5f);
    static const float kJitter[5][3] = { { 0, 0, 0 }, { 0.11f, 0.07f, -0.09f }, { -0.1f, 0.12f, 0.08f },
                                         { 0.06f, -0.04f, 0.13f }, { -0.12f, 0.03f, -0.11f } };
    for (int i = 0; i < copies; ++i) {
        Pose q = p;
        if (flat)
            q.Translate(0.0f, 0.0f, 0.09375f * groundScale * i);
        else
            q.Translate(kJitter[i][0], kJitter[i][1], kJitter[i][2]);
        ApplyDisplay(q, id, CTX_GROUND);
        DrawItemModel(q, id, light);
    }
    return true;
}

void DrawPrimedTnt(const PrimedTnt& t, float light) {
    // TntRenderer: swells just before the blast and blinks white
    float half = 0.49f;
    if (t.fuse < 0.5f) {
        float g = Clamp(1.0f - t.fuse / 0.5f, 0.0f, 1.0f);
        g *= g;
        half *= 1.0f + g * g * 0.3f;
    }
    const Vec3 c = t.pos + Vec3(0, 0, 0.5f);
    DrawCube(c, half, Vec3(1, 0, 0), Vec3(0, 0, 1), Vec3(0, 1, 0), ID_TNT, light);
    if (((int)(t.fuse * 20.0f) / 5) % 2 == 0) {
        TheModelSink().Texture(MT_ENTITY);
        const float h = half + 0.004f;
        const float u = (ENT_WHITE.x + 4.0f) / ENT_TEX_W, v = (ENT_WHITE.y + 4.0f) / ENT_TEX_H;
        const uint32_t white = ModelColor(255, 255, 255, 150);
        for (int face = 0; face < 6; ++face) {
            Vec3 q[4];
            for (int k = 0; k < 4; ++k) {
                const int* cc = kFaceCorners[face][k];
                q[k] = c + Vec3((cc[0] * 2 - 1) * h, (cc[1] * 2 - 1) * h, (cc[2] * 2 - 1) * h);
            }
            ModelQuad(q[0], q[1], q[2], q[3], u, v, u, v, white);
        }
        TheModelSink().Texture(MT_ATLAS);
    }
}

// ExperienceOrbRenderer: bigger orbs for more experience, the colour pulses between green and yellow
void DrawXpOrb(const XpOrb& o, const Vec3& R, const Vec3& U) {
    TheModelSink().Texture(MT_ENTITY);
    const int icon = OrbIcon(o.value);
    const float u0 = (ENT_XP_ORB.x + (icon % 4) * 16.0f) / ENT_TEX_W, v0 = (ENT_XP_ORB.y + (icon / 4) * 16.0f) / ENT_TEX_H;
    const float u1 = u0 + 16.0f / ENT_TEX_W, v1 = v0 + 16.0f / ENT_TEX_H;
    // ExperienceOrbRenderer: the colour pulses between green and yellow
    const float t = (o.age * 20.0f) / 2.0f;
    const int r = (int)((std::sin(t) + 1.0f) * 0.5f * 255.0f);
    const int b = (int)((std::sin(t + 4.1887903f) + 1.0f) * 0.1f * 255.0f);
    const float s = 0.15f + icon * 0.012f;
    const Vec3 c = o.pos + Vec3(0, 0, 0.1f);
    ModelQuad(c - R * s + U * s, c + R * s + U * s, c + R * s - U * s, c - R * s - U * s, u0, v0, u1, v1,
             ModelColor(r, 255, b, 255));
}

// a particle facing the camera; sparks and smoke run through their frames as they age
void DrawParticle(const Particle& p, const Vec3& camRight, const Vec3& camUp, float light) {
    TheModelSink().Texture(MT_ATLAS);
    static const uint16_t kSpark[8] = { TILE_P_SPARK_0, TILE_P_SPARK_1, TILE_P_SPARK_2, TILE_P_SPARK_3,
                                        TILE_P_SPARK_4, TILE_P_SPARK_5, TILE_P_SPARK_6, TILE_P_SPARK_7 };
    static const uint16_t kSmoke[8] = { TILE_P_GENERIC_0, TILE_P_GENERIC_1, TILE_P_GENERIC_2, TILE_P_GENERIC_3,
                                        TILE_P_GENERIC_4, TILE_P_GENERIC_5, TILE_P_GENERIC_6, TILE_P_GENERIC_7 };
    uint16_t tile = p.tile;
    if (p.anim) {
        int frame = std::clamp((int)((1.0f - p.life / p.maxLife) * 8.0f), 0, 7);
        tile = p.anim == 1 ? kSpark[7 - frame] : kSmoke[7 - frame];
    }
    TileUV uv = AtlasTileUV(tile);
    float tu = uv.u1 - uv.u0, tv = uv.v1 - uv.v0;
    float u0 = uv.u0 + tu * p.u, v0 = uv.v0 + tv * p.v;
    float u1 = u0 + tu * p.sub, v1 = v0 + tv * p.sub;
    Vec3 r = camRight * p.size, up = camUp * p.size;
    uint32_t col = p.color;
    if (!p.glow) {
        int a = (col >> 24) & 255, rr = (int)(((col >> 16) & 255) * light), gg = (int)(((col >> 8) & 255) * light),
            bb = (int)((col & 255) * light);
        col = ModelColor(rr, gg, bb, a);
    }
    ModelQuad(p.pos - r + up, p.pos + r + up, p.pos + r - up, p.pos - r - up, u0, v0, u1, v1, col);
}

void DrawFallingBlocks(float light) {
    for (auto& f : FallingBlocks())
        DrawCube(f.pos + Vec3(0, 0, 0.5f), 0.5f, Vec3(1, 0, 0), Vec3(0, 0, 1), Vec3(0, 1, 0), f.block, light);
}

// arrows (two crossed strips), tridents (their model), the rest as their item sprite
void DrawProjectile(const Projectile& pr, const Vec3& camR, const Vec3& camU, float light) {
    if (pr.type == PJ_ARROW) {
        TheModelSink().Texture(MT_ENTITY);
        Vec3 dir = Norm(pr.vel);
        Vec3 side = Norm(Vec3(-dir.y, dir.x, 0.0f));
        if (std::fabs(dir.z) > 0.99f)
            side = Vec3(1, 0, 0);
        Vec3 up = Norm(Cross(side, dir));
        // ArrowRenderer: two crossed 16x5 strips, the feathers at u = 0 and the tip at u = 16
        Vec3 tail = pr.pos - dir * 0.675f, head = pr.pos + dir * 0.225f;
        const float w = 0.14f;
        const float u0 = (float)ENT_ARROW.x / ENT_TEX_W, u1 = (ENT_ARROW.x + 16.0f) / ENT_TEX_W;
        const float v0 = (float)ENT_ARROW.y / ENT_TEX_H, v1 = (ENT_ARROW.y + 5.0f) / ENT_TEX_H;
        uint32_t c = ModelGray(light);
        ModelQuad(tail + side * w, head + side * w, head - side * w, tail - side * w, u0, v0, u1, v1, c);
        ModelQuad(tail + up * w, head + up * w, head - up * w, tail - up * w, u0, v0, u1, v1, c);
    } else if (pr.type == PJ_TRIDENT) {
        // the item sprite with its diagonal along the flight direction (handle behind)
        Vec3 dir = pr.returning ? Norm(pr.vel * -1.0f) : Norm(pr.vel);
        if (pr.stuck && !pr.returning)
            dir = Norm(pr.vel * -10.0f);
        Vec3 side = Norm(Cross(dir, Vec3(0, 0, 1)));
        if (side.Length() < 0.5f)
            side = Vec3(1, 0, 0);
        // ThrownTridentRenderer: the 3D trident, spikes first
        Pose p;
        p.Y = dir * -1.0f;
        p.X = side;
        p.Z = Cross(p.X, p.Y);
        p.o = pr.pos;
        DrawTridentModel(p, light);
    } else {
        TheModelSink().Texture(MT_ATLAS);
        uint16_t id = pr.type == PJ_SNOWBALL  ? ID_SNOWBALL
                    : pr.type == PJ_EGG       ? ID_EGG
                    : pr.type == PJ_PEARL     ? ID_ENDER_PEARL
                    : pr.type == PJ_FIREBALL  ? ID_FIRE_CHARGE
                    : pr.type == PJ_WIND      ? ID_WIND_CHARGE
                    : pr.type == PJ_XPBOTTLE  ? ID_EXPERIENCE_BOTTLE
                                              : ID_FIREWORK_ROCKET;
        float size = pr.type == PJ_FIREBALL ? 0.4f : 0.15f;
        DrawSprite3D(pr.pos, camR, camU, size, Item(id).tile,
                       pr.type == PJ_FIREWORK || pr.type == PJ_FIREBALL || pr.type == PJ_ROCKET ? 1.0f : light);
    }
}

// LightningBoltRenderer: a jagged column that flickers (the host blends it additively)
void DrawBolt(const Bolt& b, const Vec3& cam) {
    TheModelSink().Texture(MT_ENTITY);
    const float u = (ENT_WHITE.x + 4.0f) / ENT_TEX_W, v = (ENT_WHITE.y + 4.0f) / ENT_TEX_H;
    const Vec3 at = b.at;
    // LightningBoltRenderer: a jagged column that flickers
    if (((int)(b.age * 30.0f)) % 3 == 2)
        return; // (it flickers)
    uint32_t seed = b.seed;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return ((seed >> 8) & 0xFFFF) / 65535.0f - 0.5f;
    };
    int alpha = (int)(Clamp(1.0f - b.age / 0.5f, 0.0f, 1.0f) * 200.0f);
    for (int branch = 0; branch < 3; ++branch) {
        Vec3 p = at + Vec3(0, 0, branch == 0 ? 0.0f : 20.0f + branch * 15.0f);
        Vec3 top = at + Vec3(rnd() * 20.0f, rnd() * 20.0f, 110.0f);
        const int segs = branch == 0 ? 16 : 6;
        Vec3 prev = branch == 0 ? at : p + Vec3(rnd() * 6.0f, rnd() * 6.0f, 0.0f);
        for (int k = 1; k <= segs; ++k) {
            float t = (float)k / segs;
            Vec3 next = (branch == 0 ? at : prev) * (1.0f - t) + top * t;
            if (branch == 0)
                next = at * (1.0f - t) + top * t;
            next += Vec3(rnd() * 3.0f, rnd() * 3.0f, 0.0f);
            Vec3 mid = (next + prev) * 0.5f;
            Vec3 side = Cross(next - prev, cam - mid);
            float sm = side.Length();
            if (sm > 1e-4f) {
                side = side * ((branch == 0 ? 0.35f : 0.18f) / sm);
                ModelQuad(prev - side, prev + side, next + side, next - side, u, v, u, v, ModelColor(200, 210, 255, alpha));
                ModelQuad(prev - side * 2.5f, prev + side * 2.5f, next + side * 2.5f, next - side * 2.5f, u, v, u, v,
                         ModelColor(120, 140, 255, alpha / 3));
            }
            prev = next;
        }
    }
}

// FishingHookRenderer: the bobber faces the camera
void DrawFishingHook(const Vec3& R, const Vec3& U, float light) {
    TheModelSink().Texture(MT_ENTITY);
    const float u0 = (float)ENT_HOOK.x / ENT_TEX_W, v0 = (float)ENT_HOOK.y / ENT_TEX_H;
    const float u1 = (float)(ENT_HOOK.x + ENT_HOOK.w) / ENT_TEX_W, v1 = (float)(ENT_HOOK.y + ENT_HOOK.h) / ENT_TEX_H;
    const Vec3 b = gBobber.pos - U * 0.1f;
    ModelQuad(b - R * 0.25f + U * 0.5f, b + R * 0.25f + U * 0.5f, b + R * 0.25f, b - R * 0.25f, u0, v0, u1, v1, ModelGray(light));
}

// the line from the rod's tip to the bobber
void DrawFishingLine(const Vec3& tip, const Vec3& cam) {
    TheModelSink().Texture(MT_ENTITY);
    // the line sags like Minecraft's: z follows (t^2 + t) / 2
    const Vec3 start = gBobber.pos + Vec3(0, 0, 0.25f);
    const Vec3 d = tip - start;
    const float u = (ENT_WHITE.x + 4.0f) / ENT_TEX_W, v = (ENT_WHITE.y + 4.0f) / ENT_TEX_H;
    const uint32_t black = ModelColor(0, 0, 0, 255);
    Vec3 prev = start;
    const int N = 16;
    for (int k = 1; k <= N; ++k) {
        float t = (float)k / N;
        Vec3 p(start.x + d.x * t, start.y + d.y * t, start.z + d.z * (t * t + t) * 0.5f);
        Vec3 mid = (p + prev) * 0.5f;
        Vec3 toCam = cam - mid;
        float camDist = toCam.Length();
        Vec3 side = Cross(p - prev, toCam);
        float sm = side.Length();
        if (sm > 1e-6f) {
            side = side * (std::max(0.004f, camDist * 0.0012f) / sm);
            ModelQuad(prev - side, prev + side, p + side, p - side, u, v, u, v, black);
        }
        prev = p;
    }
}

int CrackTile(float progress) { return TILE_DESTROY_0 + (int)Clamp(progress * 10.0f, 0.0f, 9.0f); }

// GameRenderer.bobView
void BobView(const Vec3& front, Vec3& pos, Vec3& look) {
    Vec3 right = Cross(front, Vec3(0, 0, 1));
    float rm = right.Length();
    if (rm <= 1e-3f)
        return;
    right = right * (1.0f / rm);
    Vec3 up = Cross(right, front);
    float f1 = -gGame.walkDist * kPi, f2 = gGame.bob;
    pos = pos + up * std::fabs(std::cos(f1) * f2) - right * (std::sin(f1) * f2 * 0.5f);
    look = Normalized(front - up * std::tan(Rad(std::fabs(std::cos(f1 - 0.2f) * f2) * 5.0f)));
}

float FovWanted(float base) { return base * gGame.fovMod * (gGame.spyglass ? 0.1f : 1.0f); }

} // namespace mc
