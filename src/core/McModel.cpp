#include <cstring>
#include "McModel.h"

#include "BlockMesh.h"
#include "GameState.h"
#include "Items.h"

namespace mc {

namespace {
struct NoSink : ModelSink {
    void Texture(int) override {}
    void Quad(const Vec3*, const float*, const float*, uint32_t) override {}
    bool Opaque(int, int, int) override { return false; }
} gNoSink;
ModelSink* gSink = &gNoSink;

ModelSink& Sink() { return *gSink; }

uint32_t Argb(int r, int g, int b, int a = 255) {
    return ((uint32_t)(a & 255) << 24) | ((uint32_t)(r & 255) << 16) | ((uint32_t)(g & 255) << 8) | (uint32_t)(b & 255);
}
uint32_t Gray(float v, int a = 255) {
    int c = (int)Clamp(v * 255.0f, 0.0f, 255.0f);
    return Argb(c, c, c, a);
}
// a quad whose corners take the texture's corners in order: (u0, v0), (u1, v0), (u1, v1), (u0, v1)
void Quad4(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float u0, float v0, float u1, float v1, uint32_t col) {
    const Vec3 p[4] = { p0, p1, p2, p3 };
    const float u[4] = { u0, u1, u1, u0 }, v[4] = { v0, v0, v1, v1 };
    Sink().Quad(p, u, v, col);
}
} // namespace

void SetModelSink(ModelSink* sink) { gSink = sink ? sink : &gNoSink; }

// ================================================================ cubes
void McCube(const Pose& pose, float x, float y, float z, float w, float h, float d, float U, float V,
            const ModelStyle& st, float inf, bool mirror) {
    float x0 = x - inf, y0 = y - inf, z0 = z - inf, x1 = x + w + inf, y1 = y + h + inf, z1 = z + d + inf;
    if (mirror)
        std::swap(x0, x1);
    const float k = 1.0f / 16.0f;
    const Vec3 v[8] = {
        pose.P(x0 * k, y0 * k, z0 * k), pose.P(x1 * k, y0 * k, z0 * k), pose.P(x1 * k, y1 * k, z0 * k), pose.P(x0 * k, y1 * k, z0 * k),
        pose.P(x0 * k, y0 * k, z1 * k), pose.P(x1 * k, y0 * k, z1 * k), pose.P(x1 * k, y1 * k, z1 * k), pose.P(x0 * k, y1 * k, z1 * k),
    };
    Vec3 centre(0, 0, 0);
    for (auto& p : v)
        centre += p;
    centre = centre * 0.125f;

    const float f4 = U, f5 = U + d, f6 = U + d + w, f7 = U + d + w + w, f8 = U + d + w + d, f9 = U + d + w + d + w;
    const float f10 = V, f11 = V + d, f12 = V + d + h;
    struct Poly { int a, b, c, e; float u0, v0, u1, v1; };
    const Poly polys[6] = {
        { 5, 4, 0, 1, f5, f10, f6, f11 }, // y0 (top of the part)
        { 2, 3, 7, 6, f6, f11, f7, f10 }, // y1
        { 0, 4, 7, 3, f4, f11, f5, f12 }, // x0
        { 1, 0, 3, 2, f5, f11, f6, f12 }, // z0 (front)
        { 5, 1, 2, 6, f6, f11, f8, f12 }, // x1
        { 4, 5, 6, 7, f8, f11, f9, f12 }, // z1 (back)
    };
    const float tw = (float)ENT_TEX_W, th = (float)ENT_TEX_H;
    for (const Poly& pl : polys) {
        Vec3 c[4] = { v[pl.a], v[pl.b], v[pl.c], v[pl.e] };
        Vec3 fc = (c[0] + c[1] + c[2] + c[3]) * 0.25f - centre;
        float m = fc.Length();
        float nz = m > 1e-6f ? fc.z / m : 0.0f;
        float nx = m > 1e-6f ? fc.x / m : 0.0f;
        float shade = st.light * Clamp(0.74f + 0.26f * nz + 0.06f * nx, 0.0f, 1.0f);
        float u0 = (st.tex.x + pl.u0) / tw, u1 = (st.tex.x + pl.u1) / tw;
        float v0 = (st.tex.y + pl.v0) / th, v1 = (st.tex.y + pl.v1) / th;
        if (st.untextured) {
            u0 = u1 = (ENT_WHITE.x + 4.0f) / tw;
            v0 = v1 = (ENT_WHITE.y + 4.0f) / th;
        }
        const float us[4] = { u1, u0, u0, u1 };
        const float vs[4] = { v0, v0, v1, v1 };
        uint32_t col = Argb((int)Clamp(shade * st.r * 255.0f, 0.0f, 255.0f), (int)Clamp(shade * st.g * 255.0f, 0.0f, 255.0f),
                                (int)Clamp(shade * st.b * 255.0f, 0.0f, 255.0f), 255);
        Sink().Quad(c, us, vs, col);
    }
}

// ================================================================ player (HumanoidModel / PlayerModel)
void ComputePlayerAnim(HumanoidAnim& a, const PlayerAnimInput& in) {
    a = HumanoidAnim();
    a.rArm.x = -5; a.rArm.y = 2;
    a.lArm.x = 5;  a.lArm.y = 2;
    a.rLeg.x = -1.9f; a.rLeg.y = 12;
    a.lLeg.x = 1.9f;  a.lLeg.y = 12;

    a.head.ry = in.headYaw;
    a.head.rx = in.gliding ? -kPi / 4.0f : in.headPitch;
    const float ls = in.limbSwing, la = in.limbAmount;
    a.rArm.rx = std::cos(ls * 0.6662f + kPi) * 2.0f * la * 0.5f;
    a.lArm.rx = std::cos(ls * 0.6662f) * 2.0f * la * 0.5f;
    a.rLeg.rx = std::cos(ls * 0.6662f) * 1.4f * la;
    a.lLeg.rx = std::cos(ls * 0.6662f + kPi) * 1.4f * la;
    a.rLeg.ry = 0.005f; a.lLeg.ry = -0.005f;
    a.rLeg.rz = 0.005f; a.lLeg.rz = -0.005f;
    if (in.riding) {
        a.rArm.rx += -kPi / 5.0f;
        a.lArm.rx += -kPi / 5.0f;
        a.rLeg.rx = -1.4137167f; a.rLeg.ry = kPi / 10.0f;  a.rLeg.rz = 0.07853982f;
        a.lLeg.rx = -1.4137167f; a.lLeg.ry = -kPi / 10.0f; a.lLeg.rz = -0.07853982f;
    }
    // the arm that uses the item: the right one, or the left one for the off hand (Minecraft mirrors the poses)
    const float side = in.useLeft ? -1.0f : 1.0f;
    PartState& mainArm = in.useLeft ? a.lArm : a.rArm;
    PartState& offArm = in.useLeft ? a.rArm : a.lArm;
    const bool twoHandedPose = in.bow || in.armPose == ARM_CROSSBOW_HOLD || in.armPose == ARM_CROSSBOW_CHARGE;
    if (in.bow) {
        mainArm.ry = -0.1f * side + a.head.ry;
        offArm.ry = 0.1f * side + a.head.ry + 0.4f * side;
        mainArm.rx = -kPi / 2.0f + a.head.rx;
        offArm.rx = -kPi / 2.0f + a.head.rx;
    } else if (in.armPose == ARM_CROSSBOW_HOLD) {
        mainArm.ry = -0.3f * side + a.head.ry;
        offArm.ry = 0.6f * side + a.head.ry;
        mainArm.rx = -kPi / 2.0f + a.head.rx + 0.1f;
        offArm.rx = -1.5f + a.head.rx;
    } else if (in.armPose == ARM_CROSSBOW_CHARGE) {
        float f2 = Clamp(in.useTicks / 25.0f, 0.0f, 1.0f);
        mainArm.ry = -0.8f * side;
        mainArm.rx = -0.97079635f;
        offArm.rx = -0.97079635f + (-kPi / 2.0f + 0.97079635f) * f2;
        offArm.ry = (0.4f + (0.85f - 0.4f) * f2) * side;
    } else if (in.armPose == ARM_SPYGLASS) {
        mainArm.rx = Clamp(a.head.rx - 1.9198622f - (in.crouch ? 0.2617994f : 0.0f), -2.4f, 3.3f);
        mainArm.ry = a.head.ry - 0.2617994f * side;
    } else if (in.armPose == ARM_THROW_SPEAR) {
        mainArm.rx = mainArm.rx * 0.5f - kPi;
        mainArm.ry = 0.0f;
    } else if (in.armPose == ARM_EAT) {
        // the hand goes to the mouth and bobs while chewing
        mainArm.rx = -1.35f + a.head.rx * 0.5f + std::fabs(std::cos(in.useTicks / 4.0f * kPi)) * 0.15f;
        mainArm.ry = -0.55f * side + a.head.ry;
    }
    // ITEM: an arm that just holds something
    const bool rightBusy = twoHandedPose || (in.armPose != ARM_DEFAULT && !in.useLeft);
    const bool leftBusy = twoHandedPose || (in.armPose != ARM_DEFAULT && in.useLeft);
    if (in.holding && !rightBusy)
        a.rArm.rx = a.rArm.rx * 0.5f - kPi / 10.0f;
    if (in.holdingLeft && !leftBusy)
        a.lArm.rx = a.lArm.rx * 0.5f - kPi / 10.0f;
    if (in.attack > 0.0f) {
        float f = in.attack;
        a.body.ry = std::sin(std::sqrt(f) * kPi * 2.0f) * 0.2f;
        if (in.attackLeft)
            a.body.ry *= -1.0f;
        a.rArm.z = std::sin(a.body.ry) * 5.0f;
        a.rArm.x = -std::cos(a.body.ry) * 5.0f;
        a.lArm.z = -std::sin(a.body.ry) * 5.0f;
        a.lArm.x = std::cos(a.body.ry) * 5.0f;
        a.rArm.ry += a.body.ry;
        a.lArm.ry += a.body.ry;
        a.lArm.rx += a.body.ry;
        f = 1.0f - in.attack;
        f *= f;
        f *= f;
        f = 1.0f - f;
        float f1 = std::sin(f * kPi);
        float f2 = std::sin(in.attack * kPi) * -(a.head.rx - 0.7f) * 0.75f;
        PartState& arm = in.attackLeft ? a.lArm : a.rArm;
        arm.rx -= f1 * 1.2f + f2;
        arm.ry += a.body.ry * 2.0f;
        arm.rz += std::sin(in.attack * kPi) * -0.4f;
    }
    if (in.crouch) {
        a.body.rx = 0.5f;
        a.rArm.rx += 0.4f;
        a.lArm.rx += 0.4f;
        a.rLeg.z = 4.0f; a.lLeg.z = 4.0f;
        a.rLeg.y = 12.2f; a.lLeg.y = 12.2f;
        a.head.y = 4.2f;
        a.body.y = 3.2f;
        a.lArm.y = 5.2f; a.rArm.y = 5.2f;
    }
    if (!twoHandedPose) {
        const bool spy = in.armPose == ARM_SPYGLASS;
        if (!(spy && !in.useLeft)) {
            a.rArm.rz += std::cos(in.age * 0.09f) * 0.05f + 0.05f;
            a.rArm.rx += std::sin(in.age * 0.067f) * 0.05f;
        }
        if (!(spy && in.useLeft)) {
            a.lArm.rz -= std::cos(in.age * 0.09f) * 0.05f + 0.05f;
            a.lArm.rx -= std::sin(in.age * 0.067f) * 0.05f;
        }
    }

    // swimming (HumanoidModel: swimAmount)
    if (in.swim > 0.0f) {
        const float s = in.swim;
        auto lerp = [](float t, float a0, float b0) { return a0 + (b0 - a0) * t; };
        auto quad = [](float f) { return -65.0f * f + f * f; };
        a.head.rx = lerp(s, a.head.rx, -kPi / 4.0f);
        float f5 = std::fmod(in.limbSwing, 26.0f);
        if (f5 < 0.0f)
            f5 += 26.0f;
        const float r1 = in.attack > 0.0f ? 0.0f : s;
        if (f5 < 14.0f) {
            a.lArm.rx = lerp(s, a.lArm.rx, 0.0f);
            a.rArm.rx = lerp(r1, a.rArm.rx, 0.0f);
            a.lArm.ry = lerp(s, a.lArm.ry, kPi);
            a.rArm.ry = lerp(r1, a.rArm.ry, kPi);
            a.lArm.rz = lerp(s, a.lArm.rz, kPi + 1.8707964f * quad(f5) / quad(14.0f));
            a.rArm.rz = lerp(r1, a.rArm.rz, kPi - 1.8707964f * quad(f5) / quad(14.0f));
        } else if (f5 < 22.0f) {
            float f6 = (f5 - 14.0f) / 8.0f;
            a.lArm.rx = lerp(s, a.lArm.rx, kPi / 2.0f * f6);
            a.rArm.rx = lerp(r1, a.rArm.rx, kPi / 2.0f * f6);
            a.lArm.ry = lerp(s, a.lArm.ry, kPi);
            a.rArm.ry = lerp(r1, a.rArm.ry, kPi);
            a.lArm.rz = lerp(s, a.lArm.rz, 5.012389f - 1.8707964f * f6);
            a.rArm.rz = lerp(r1, a.rArm.rz, 1.2707963f + 1.8707964f * f6);
        } else {
            float f3 = (f5 - 22.0f) / 4.0f;
            a.lArm.rx = lerp(s, a.lArm.rx, kPi / 2.0f - kPi / 2.0f * f3);
            a.rArm.rx = lerp(r1, a.rArm.rx, kPi / 2.0f - kPi / 2.0f * f3);
            a.lArm.ry = lerp(s, a.lArm.ry, kPi);
            a.rArm.ry = lerp(r1, a.rArm.ry, kPi);
            a.lArm.rz = lerp(s, a.lArm.rz, kPi);
            a.rArm.rz = lerp(r1, a.rArm.rz, kPi);
        }
        a.lLeg.rx = lerp(s, a.lLeg.rx, 0.3f * std::cos(in.limbSwing * 0.33333334f + kPi));
        a.rLeg.rx = lerp(s, a.rLeg.rx, 0.3f * std::cos(in.limbSwing * 0.33333334f));
    }
}

static Pose PartPose(const Pose& base, const PartState& s) { return Part(base, s.x, s.y, s.z, s.rx, s.ry, s.rz); }

Pose RightArmPose(const Pose& base, const HumanoidAnim& a) { return PartPose(base, a.rArm); }
Pose LeftArmPose(const Pose& base, const HumanoidAnim& a) { return PartPose(base, a.lArm); }

void DrawRightArm(const Pose& pose, const ModelStyle& st, bool sleeve) {
    McCube(pose, -3, -2, -2, 4, 12, 4, 40, 16, st);
    if (sleeve)
        McCube(pose, -3, -2, -2, 4, 12, 4, 40, 32, st, 0.25f);
}

void DrawPlayerModel(const Pose& base, const HumanoidAnim& a, const ModelStyle& st) {
    Sink().Texture(MT_ENTITY);
    Pose head = PartPose(base, a.head);
    McCube(head, -4, -8, -4, 8, 8, 8, 0, 0, st);
    McCube(head, -4, -8, -4, 8, 8, 8, 32, 0, st, 0.5f);
    Pose body = PartPose(base, a.body);
    McCube(body, -4, 0, -2, 8, 12, 4, 16, 16, st);
    McCube(body, -4, 0, -2, 8, 12, 4, 16, 32, st, 0.25f);
    DrawRightArm(PartPose(base, a.rArm), st, true);
    Pose la = PartPose(base, a.lArm);
    McCube(la, -1, -2, -2, 4, 12, 4, 32, 48, st);
    McCube(la, -1, -2, -2, 4, 12, 4, 48, 48, st, 0.25f);
    Pose rl = PartPose(base, a.rLeg);
    McCube(rl, -2, 0, -2, 4, 12, 4, 0, 16, st);
    McCube(rl, -2, 0, -2, 4, 12, 4, 0, 32, st, 0.25f);
    Pose ll = PartPose(base, a.lLeg);
    McCube(ll, -2, 0, -2, 4, 12, 4, 16, 48, st);
    McCube(ll, -2, 0, -2, 4, 12, 4, 0, 48, st, 0.25f);
}

void DrawElytra(const Pose& base, bool crouch, bool gliding, float dive, const ModelStyle& st) {
    Sink().Texture(MT_ENTITY);
    float x = 0.2617994f, z = -0.2617994f, y = 0.0f, yOff = 0.0f;
    if (gliding) {
        x = dive * 0.34906584f + (1.0f - dive) * x;
        z = dive * (-kPi / 2.0f) + (1.0f - dive) * z;
    } else if (crouch) {
        x = 0.6981317f;
        z = -kPi / 4.0f;
        yOff = 3.0f;
        y = 0.08726646f;
    }
    Pose b = base;
    b.Translate(0.0f, 0.0f, 0.125f);
    Pose lw = Part(b, 5.0f, yOff, 0.0f, x, y, z);
    McCube(lw, -10, 0, 0, 10, 20, 2, 22, 0, st, 1.0f);
    Pose rw = Part(b, -5.0f, yOff, 0.0f, x, -y, -z);
    McCube(rw, 0, 0, 0, 10, 20, 2, 22, 0, st, 1.0f, true);
}

namespace {
int ArmorMaterial(uint16_t id) {
    static int cache[ITEM_END] = {};
    static bool init = false;
    if (!init) {
        for (int i = 0; i < ITEM_END; ++i) {
            cache[i] = -1;
            if (!IsValidItem(i) || IsBlockItem(i) || !Item(i).armorSlot)
                continue;
            const char* key = Item(i).key;
            for (int m = 0; m < NUM_ARMOR_MATS; ++m)
                if (strncmp(key, ARMOR_MAT_KEYS[m], strlen(ARMOR_MAT_KEYS[m])) == 0)
                    cache[i] = m;
        }
        init = true;
    }
    return id < ITEM_END ? cache[id] : -1;
}
} // namespace

void DrawArmor(const Pose& base, const HumanoidAnim& a, const uint16_t armor[4], float light, float r, float g, float b) {
    Sink().Texture(MT_ENTITY);
    for (int slot = 0; slot < 4; ++slot) {
        const uint16_t id = armor[slot];
        const int mat = id ? ArmorMaterial(id) : -1;
        if (mat < 0 || Item(id).armorSlot != slot + 1)
            continue;
        ModelStyle st;
        st.light = light;
        st.r = r; st.g = g; st.b = b;
        const bool inner = slot == 2; // leggings use the inner layer
        st.tex = inner ? ENT_ARMOR_INNER[mat] : ENT_ARMOR_OUTER[mat];
        const float inf = inner ? 0.5f : 1.0f;
        switch (slot) {
        case 0: {
            Pose head = PartPose(base, a.head);
            McCube(head, -4, -8, -4, 8, 8, 8, 0, 0, st, inf);
            McCube(head, -4, -8, -4, 8, 8, 8, 32, 0, st, inf + 0.5f);
            break;
        }
        case 1:
            McCube(PartPose(base, a.body), -4, 0, -2, 8, 12, 4, 16, 16, st, inf);
            McCube(PartPose(base, a.rArm), -3, -2, -2, 4, 12, 4, 40, 16, st, inf);
            McCube(PartPose(base, a.lArm), -1, -2, -2, 4, 12, 4, 40, 16, st, inf, true);
            break;
        case 2:
            McCube(PartPose(base, a.body), -4, 0, -2, 8, 12, 4, 16, 16, st, inf);
            McCube(PartPose(base, a.rLeg), -2, 0, -2, 4, 12, 4, 0, 16, st, inf);
            McCube(PartPose(base, a.lLeg), -2, 0, -2, 4, 12, 4, 0, 16, st, inf, true);
            break;
        default:
            McCube(PartPose(base, a.rLeg), -2, 0, -2, 4, 12, 4, 0, 16, st, inf);
            McCube(PartPose(base, a.lLeg), -2, 0, -2, 4, 12, 4, 0, 16, st, inf, true);
            break;
        }
    }
}

// ================================================================ items
void ApplyDisplay(Pose& p, uint16_t id, DisplayCtx ctx) {
    if (!IsValidItem(id))
        return;
    const ItemDisplay& d = kDisplays[kItemDisplay[id]];
    const ItemTransform& t = ctx == CTX_GROUND ? d.ground : (ctx == CTX_FIRST_PERSON ? d.firstPerson : d.thirdPerson);
    p.Translate(t.trans[0] / 16.0f, t.trans[1] / 16.0f, t.trans[2] / 16.0f);
    p.RotXYZ(Rad(t.rot[0]), Rad(t.rot[1]), Rad(t.rot[2]));
    p.Scale(t.scale[0], t.scale[1], t.scale[2]);
}

namespace {
struct Edges {
    std::vector<uint8_t> e; // x, y, dir triples
};
std::unordered_map<int, Edges> gEdgeCache;

const Edges& EdgesFor(int tile) {
    auto it = gEdgeCache.find(tile);
    if (it != gEdgeCache.end())
        return it->second;
    Edges& ed = gEdgeCache[tile];
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            if (!Sink().Opaque(tile, x, y))
                continue;
            if (!Sink().Opaque(tile, x - 1, y)) { ed.e.push_back((uint8_t)x); ed.e.push_back((uint8_t)y); ed.e.push_back(0); }
            if (!Sink().Opaque(tile, x + 1, y)) { ed.e.push_back((uint8_t)x); ed.e.push_back((uint8_t)y); ed.e.push_back(1); }
            if (!Sink().Opaque(tile, x, y - 1)) { ed.e.push_back((uint8_t)x); ed.e.push_back((uint8_t)y); ed.e.push_back(2); }
            if (!Sink().Opaque(tile, x, y + 1)) { ed.e.push_back((uint8_t)x); ed.e.push_back((uint8_t)y); ed.e.push_back(3); }
        }
    return ed;
}

uint32_t ItemColor(float shade, bool glint) {
    float r = shade, g = shade, b = shade;
    if (glint) {
        float t = 0.5f + 0.5f * std::sin(gGame.age * 5.0f);
        r *= 0.80f + 0.20f * t;
        g *= 0.55f + 0.25f * t;
        b *= 1.0f;
    }
    return Argb((int)Clamp(r * 255.0f, 0.0f, 255.0f), (int)Clamp(g * 255.0f, 0.0f, 255.0f), (int)Clamp(b * 255.0f, 0.0f, 255.0f), 255);
}

void DrawBlockCube(const Pose& p, int block, float light) {
    struct F { int face; float c[4][3]; float shade; };
    // corners in texture order: top-left, top-right, bottom-right, bottom-left (Minecraft block model UVs)
    static const F faces[6] = {
        { FACE_TOP,    { { 0, 1, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 0, 1, 1 } }, 1.0f },
        { FACE_BOTTOM, { { 0, 0, 1 }, { 1, 0, 1 }, { 1, 0, 0 }, { 0, 0, 0 } }, 0.5f },
        { FACE_NORTH,  { { 1, 1, 0 }, { 0, 1, 0 }, { 0, 0, 0 }, { 1, 0, 0 } }, 0.8f },
        { FACE_SOUTH,  { { 0, 1, 1 }, { 1, 1, 1 }, { 1, 0, 1 }, { 0, 0, 1 } }, 0.8f },
        { FACE_WEST,   { { 0, 1, 0 }, { 0, 1, 1 }, { 0, 0, 1 }, { 0, 0, 0 } }, 0.6f },
        { FACE_EAST,   { { 1, 1, 1 }, { 1, 1, 0 }, { 1, 0, 0 }, { 1, 0, 1 } }, 0.6f },
    };
    const bool emissive = Block(block).emissive;
    for (const F& f : faces) {
        TileUV uv = AtlasTileUV(BlockFaceTile(block, f.face, 0));
        Vec3 c[4];
        for (int i = 0; i < 4; ++i)
            c[i] = p.P(f.c[i][0], f.c[i][1], f.c[i][2]);
        Quad4(c[0], c[1], c[2], c[3], uv.u0, uv.v0, uv.u1, uv.v1, Gray(emissive ? 1.0f : light * f.shade));
    }
}

void DrawSprite(const Pose& p, int tile, float light, bool glint) {
    TileUV uv = AtlasTileUV(tile);
    const float zf = 8.5f / 16.0f, zb = 7.5f / 16.0f;
    uint32_t col = ItemColor(light, glint);
    Quad4(p.P(0, 1, zf), p.P(1, 1, zf), p.P(1, 0, zf), p.P(0, 0, zf), uv.u0, uv.v0, uv.u1, uv.v1, col);
    Quad4(p.P(0, 1, zb), p.P(1, 1, zb), p.P(1, 0, zb), p.P(0, 0, zb), uv.u0, uv.v0, uv.u1, uv.v1, col);
    const Edges& ed = EdgesFor(tile);
    const float tu = (uv.u1 - uv.u0) / 16.0f, tv = (uv.v1 - uv.v0) / 16.0f;
    const float k = 1.0f / 16.0f;
    uint32_t side = ItemColor(light * 0.75f, glint), top = ItemColor(light * 0.95f, glint), bottom = ItemColor(light * 0.55f, glint);
    for (size_t i = 0; i + 2 < ed.e.size(); i += 3) {
        int px = ed.e[i], py = ed.e[i + 1], dir = ed.e[i + 2];
        float u = uv.u0 + (px + 0.5f) * tu, v = uv.v0 + (py + 0.5f) * tv;
        float xl = px * k, xr = (px + 1) * k, yt = 1.0f - py * k, yb = 1.0f - (py + 1) * k;
        switch (dir) {
        case 0: Quad4(p.P(xl, yt, zb), p.P(xl, yt, zf), p.P(xl, yb, zf), p.P(xl, yb, zb), u, v, u, v, side); break;
        case 1: Quad4(p.P(xr, yt, zf), p.P(xr, yt, zb), p.P(xr, yb, zb), p.P(xr, yb, zf), u, v, u, v, side); break;
        case 2: Quad4(p.P(xl, yt, zb), p.P(xr, yt, zb), p.P(xr, yt, zf), p.P(xl, yt, zf), u, v, u, v, top); break;
        default: Quad4(p.P(xl, yb, zf), p.P(xr, yb, zf), p.P(xr, yb, zb), p.P(xl, yb, zb), u, v, u, v, bottom); break;
        }
    }
}
} // namespace

// TridentModel (entity/trident.png, 32x32): the pole with its three spikes; the spikes point to -y
void DrawTridentModel(const Pose& p, float light) {
    Sink().Texture(MT_ENTITY);
    ModelStyle st;
    st.tex = ENT_TRIDENT;
    st.light = light;
    McCube(p, -0.5f, 2.0f, -0.5f, 1, 25, 1, 0, 6, st);
    McCube(p, -1.5f, 0.0f, -0.5f, 3, 2, 1, 4, 0, st);
    McCube(p, -2.5f, -3.0f, -0.5f, 1, 4, 1, 4, 3, st);
    McCube(p, -0.5f, -4.0f, -0.5f, 1, 4, 1, 0, 0, st);
    McCube(p, 1.5f, -3.0f, -0.5f, 1, 4, 1, 4, 3, st, 0.0f, true);
    Sink().Texture(MT_ATLAS);
}

// trident_in_hand / trident_throwing: the trident's own hand transforms (not mirrored for the left hand)
void ApplyTridentDisplay(Pose& p, bool firstPerson, bool left, bool usingItem) {
    float t[3], r[3];
    if (firstPerson) {
        const float tr[3] = { left ? 13.0f : -3.0f, 17.0f, 1.0f }, rr[3] = { 0.0f, left ? 90.0f : -90.0f, left ? -25.0f : 25.0f };
        memcpy(t, tr, sizeof(t));
        memcpy(r, rr, sizeof(r));
    } else if (usingItem) {
        const float tr[3] = { 8.0f, -17.0f, left ? -7.0f : 9.0f }, rr[3] = { 0.0f, 90.0f, 180.0f };
        memcpy(t, tr, sizeof(t));
        memcpy(r, rr, sizeof(r));
    } else {
        const float tr[3] = { left ? 3.0f : 11.0f, 17.0f, left ? 12.0f : -2.0f }, rr[3] = { 0.0f, 60.0f, 0.0f };
        memcpy(t, tr, sizeof(t));
        memcpy(r, rr, sizeof(r));
    }
    if (left) {
        // ItemTransform.apply for the left hand
        t[0] = -t[0];
        r[1] = -r[1];
        r[2] = -r[2];
    }
    p.Translate(t[0] / 16.0f, t[1] / 16.0f, t[2] / 16.0f);
    p.RotXYZ(Rad(r[0]), Rad(r[1]), Rad(r[2]));
}

void DrawItemModel(Pose p, uint16_t id, float light, int tileOverride, bool inHand) {
    if (!IsValidItem(id))
        return;
    Sink().Texture(MT_ATLAS);
    p.Translate(-0.5f, -0.5f, -0.5f);
    if (inHand && id == ID_TRIDENT) {
        p.Scale(1.0f, -1.0f, -1.0f); // the "special" model's transformation
        DrawTridentModel(p, light);
        return;
    }
    if (IsBlockItem(id) && Block(id).shape == SHAPE_CROSS)
        DrawSprite(p, Block(id).tex[0], light, false);
    else if (IsBlockItem(id))
        DrawBlockCube(p, id, light);
    else
        DrawSprite(p, tileOverride >= 0 ? tileOverride : Item(id).tile, light, Item(id).glint != 0);
}

void DrawHeldItem(const Pose& armPose, uint16_t id, float light, int tileOverride, bool left, bool usingItem) {
    if (!IsValidItem(id))
        return;
    Pose p = armPose;
    p.RotX(Rad(-90.0f));
    p.RotY(Rad(180.0f));
    p.Translate((left ? -1.0f : 1.0f) / 16.0f, 0.125f, -0.625f);
    if (id == ID_TRIDENT) {
        ApplyTridentDisplay(p, false, left, usingItem);
        DrawItemModel(p, id, light, -1, true);
        return;
    }
    if (left)
        p.X = p.X * -1.0f; // the left-hand display transforms are the right-hand ones mirrored
    ApplyDisplay(p, id, CTX_THIRD_PERSON);
    DrawItemModel(p, id, light, tileOverride);
}

void DrawPlayerFull(const Pose& base, const PlayerDrawInput& in) {
    HumanoidAnim a;
    ComputePlayerAnim(a, in.anim);
    ModelStyle st;
    st.tex = ENT_STEVE;
    st.light = in.light;
    st.r = in.r; st.g = in.g; st.b = in.b;
    DrawPlayerModel(base, a, st);
    DrawArmor(base, a, in.armor, in.light, in.r, in.g, in.b);
    if (in.elytra) {
        ModelStyle es = st;
        es.tex = ENT_ELYTRA;
        Pose b = Part(base, a.body.x, a.body.y, a.body.z, a.body.rx, a.body.ry, a.body.rz);
        DrawElytra(b, in.anim.crouch, in.anim.gliding, in.dive, es);
    }
    if (in.held)
        DrawHeldItem(RightArmPose(base, a), in.held, in.light, in.heldTile, false, in.heldUsing);
    if (in.offHeld)
        DrawHeldItem(LeftArmPose(base, a), in.offHeld, in.light, in.offTile, true, in.offUsing);
}

Pose ArmPoseFromBones(const Vec3& elbow, const Vec3& hand, const Vec3& bodyRight, float scale, bool left) {
    Vec3 d = hand - elbow;
    float m = d.Length();
    d = m > 1e-4f ? d * (1.0f / m) : Vec3(0, 0, -1);
    Vec3 x0 = bodyRight * -1.0f;
    Vec3 x = x0 - d * (x0.x * d.x + x0.y * d.y + x0.z * d.z);
    float xm = x.Length();
    x = xm > 1e-4f ? x * (1.0f / xm) : Vec3(1, 0, 0);
    Vec3 z = Cross(x, d);
    Pose p;
    p.X = x * scale;
    p.Y = d * scale;
    p.Z = z * scale;
    // the hand end of the arm cube (x = -1 or 1, y = 10 px) sits on the hand bone
    p.o = hand - p.D((left ? 1.0f : -1.0f) / 16.0f, 10.0f / 16.0f, 0.0f);
    return p;
}

void DrawFirstPerson(const Pose& view, const FirstPersonInput& in) {
    Pose p = view;
    // view bobbing, hand only
    if (in.bob > 0.001f) {
        float f1 = -in.walkDist, f2 = in.bob;
        p.Translate(std::sin(f1 * kPi) * f2 * 0.5f, -std::fabs(std::cos(f1 * kPi) * f2), 0.0f);
        p.RotZ(Rad(std::sin(f1 * kPi) * f2 * 3.0f));
        p.RotX(Rad(std::fabs(std::cos(f1 * kPi - 0.2f) * f2) * 5.0f));
    }
    p.RotX(Rad(in.swayPitch));
    p.RotY(Rad(in.swayYaw));
    if (in.leftHand) {
        // ItemInHandRenderer multiplies every x offset and y/z rotation by -1 for the left arm: a mirror
        if (!IsValidItem(in.item))
            return; // an empty off hand is not drawn
        p.X = p.X * -1.0f;
    }
    const float swing = Clamp(in.swing, 0.0f, 1.0f);
    const float sq = std::sqrt(swing);

    if (!IsValidItem(in.item)) {
        // renderPlayerArm (right hand)
        float f2 = -0.3f * std::sin(sq * kPi);
        float f3 = 0.4f * std::sin(sq * kPi * 2.0f);
        float f4 = -0.4f * std::sin(swing * kPi);
        p.Translate(f2 + 0.64000005f, f3 - 0.6f + in.lowered * -0.6f, f4 - 0.71999997f);
        p.RotY(Rad(45.0f));
        float f5 = std::sin(swing * swing * kPi);
        float f6 = std::sin(sq * kPi);
        p.RotY(Rad(f6 * 70.0f));
        p.RotZ(Rad(f5 * -20.0f));
        p.Translate(-1.0f, 3.6f, 3.5f);
        p.RotZ(Rad(120.0f));
        p.RotX(Rad(200.0f));
        p.RotY(Rad(-135.0f));
        p.Translate(5.6f, 0.0f, 0.0f);
        Sink().Texture(MT_ENTITY);
        ModelStyle st;
        st.tex = ENT_STEVE;
        st.light = in.light;
        if (!in.steveArm) {
            st.untextured = true;
            st.r = 0.44f; st.g = 0.29f; st.b = 0.19f;
        }
        DrawRightArm(Part(p, -5.0f, 2.0f, 0.0f, 0.0f, 0.0f, std::cos(in.age * 0.09f) * 0.05f + 0.05f), st, in.steveArm);
        return;
    }

    if (in.eat >= 0.0f) {
        float f = (1.0f - Clamp(in.eat, 0.0f, 1.0f)) * 32.0f; // remaining use ticks
        float f1 = f / 32.0f;
        if (f1 < 0.8f)
            p.Translate(0.0f, std::fabs(std::cos(f / 4.0f * kPi) * 0.1f), 0.0f);
        float f3 = 1.0f - std::pow(f1, 27.0f);
        p.Translate(f3 * 0.6f, f3 * -0.5f, 0.0f);
        p.RotY(Rad(f3 * 90.0f));
        p.RotX(Rad(f3 * 10.0f));
        p.RotZ(Rad(f3 * 30.0f));
        p.Translate(0.56f, -0.52f + in.lowered * -0.6f, -0.72f);
    } else if (in.crossbowTicks >= 0.0f) {
        // ItemInHandRenderer: charging crossbow
        p.Translate(0.56f, -0.52f + in.lowered * -0.6f, -0.72f);
        p.Translate(-0.4785682f, -0.094387f, 0.05731531f);
        p.RotX(Rad(-11.935f));
        p.RotY(Rad(65.3f));
        p.RotZ(Rad(-9.785f));
        float f9 = in.crossbowTicks;
        float f13 = std::min(1.0f, f9 / 25.0f);
        if (f13 > 0.1f) {
            float f16 = std::sin((f9 - 0.1f) * 1.3f);
            p.Translate(0.0f, f16 * (f13 - 0.1f) * 0.004f, 0.0f);
        }
        p.Translate(0.0f, 0.0f, f13 * 0.04f);
        p.Scale(1.0f, 1.0f, 1.0f + f13 * 0.2f);
        p.RotY(Rad(-45.0f));
    } else if (in.tridentTicks >= 0.0f) {
        // ItemInHandRenderer: raised trident
        p.Translate(0.56f, -0.52f + in.lowered * -0.6f, -0.72f);
        p.Translate(-0.5f, 0.7f, 0.1f);
        p.RotX(Rad(-55.0f));
        p.RotY(Rad(35.3f));
        p.RotZ(Rad(-9.785f));
        float f8 = in.tridentTicks;
        float f12 = std::min(1.0f, f8 / 10.0f);
        if (f12 > 0.1f) {
            float f15 = std::sin((f8 - 0.1f) * 1.3f);
            p.Translate(0.0f, f15 * (f12 - 0.1f) * 0.004f, 0.0f);
        }
        p.Translate(0.0f, 0.0f, f12 * 0.2f);
        p.Scale(1.0f, 1.0f, 1.0f + f12 * 0.2f);
        p.RotY(Rad(-45.0f));
    } else if (in.bowTicks >= 0.0f) {
        p.Translate(0.56f, -0.52f + in.lowered * -0.6f, -0.72f);
        p.Translate(-0.2785682f, 0.18344387f, 0.15731531f);
        p.RotX(Rad(-13.935f));
        p.RotY(Rad(35.3f));
        p.RotZ(Rad(-9.785f));
        float f8 = in.bowTicks;
        float f12 = f8 / 20.0f;
        f12 = (f12 * f12 + f12 * 2.0f) / 3.0f;
        if (f12 > 1.0f)
            f12 = 1.0f;
        if (f12 > 0.1f) {
            float f15 = std::sin((f8 - 0.1f) * 1.3f);
            float f18 = f12 - 0.1f;
            p.Translate(0.0f, f15 * f18 * 0.004f, 0.0f);
        }
        p.Translate(0.0f, 0.0f, f12 * 0.04f);
        p.Scale(1.0f, 1.0f, 1.0f + f12 * 0.2f);
        p.RotY(Rad(-45.0f));
    } else {
        float f5 = -0.4f * std::sin(sq * kPi);
        float f6 = 0.2f * std::sin(sq * kPi * 2.0f);
        float f10 = -0.2f * std::sin(swing * kPi);
        p.Translate(f5, f6, f10);
        p.Translate(0.56f, -0.52f + in.lowered * -0.6f, -0.72f);
        float f = std::sin(swing * swing * kPi);
        p.RotY(Rad(45.0f + f * -20.0f));
        float f1 = std::sin(sq * kPi);
        p.RotZ(Rad(f1 * -20.0f));
        p.RotX(Rad(f1 * -80.0f));
        p.RotY(Rad(-45.0f));
        if (in.crossbowLoaded && swing < 0.001f) {
            // a loaded crossbow is held further to the middle
            p.Translate(-0.641864f, 0.0f, 0.0f);
            p.RotY(Rad(10.0f));
        }
    }
    if (in.item == ID_TRIDENT) {
        if (in.leftHand)
            p.X = p.X * -1.0f; // the mirrored arm movement stays, the trident's own left-hand transform takes over
        ApplyTridentDisplay(p, true, in.leftHand, false);
        DrawItemModel(p, in.item, in.light, -1, true);
        return;
    }
    ApplyDisplay(p, in.item, CTX_FIRST_PERSON);
    DrawItemModel(p, in.item, in.light, in.tileOverride);
}

// ================================================================ mobs
void DrawMob(int kind, const Pose& base, const MobAnim& a, float light, float r, float g, float b) {
    Sink().Texture(MT_ENTITY);
    ModelStyle st;
    st.light = light;
    st.r = r; st.g = g; st.b = b;
    const float sw = a.limbSwing, am = a.limbAmount;
    const float legA = std::cos(sw * 0.6662f) * 1.4f * am, legB = std::cos(sw * 0.6662f + kPi) * 1.4f * am;
    switch (kind) {
    case MOB_COW: {
        st.tex = ENT_COW;
        Pose head = Part(base, 0, 4, -8, a.headPitch, a.headYaw, 0);
        McCube(head, -4, -4, -6, 8, 8, 6, 0, 0, st);
        McCube(head, -3, 1, -7, 6, 3, 1, 1, 33, st);
        McCube(head, -5, -5, -5, 1, 3, 1, 22, 0, st);
        McCube(head, 4, -5, -5, 1, 3, 1, 22, 0, st);
        Pose body = Part(base, 0, 5, 2, kPi / 2.0f, 0, 0);
        McCube(body, -6, -10, -7, 12, 18, 10, 18, 4, st);
        McCube(body, -2, 2, -8, 4, 6, 1, 52, 0, st);
        McCube(Part(base, -4, 12, 7, legA, 0, 0), -2, 0, -2, 4, 12, 4, 0, 16, st);
        McCube(Part(base, 4, 12, 7, legB, 0, 0), -2, 0, -2, 4, 12, 4, 0, 16, st, 0, true);
        McCube(Part(base, -4, 12, -5, legB, 0, 0), -2, 0, -2, 4, 12, 4, 0, 16, st);
        McCube(Part(base, 4, 12, -5, legA, 0, 0), -2, 0, -2, 4, 12, 4, 0, 16, st, 0, true);
        break;
    }
    case MOB_PIG: {
        st.tex = ENT_PIG;
        Pose head = Part(base, 0, 12, -6, a.headPitch, a.headYaw, 0);
        McCube(head, -4, -4, -8, 8, 8, 8, 0, 0, st);
        McCube(head, -2, 0, -9, 4, 3, 1, 16, 16, st);
        Pose body = Part(base, 0, 11, 2, kPi / 2.0f, 0, 0);
        McCube(body, -5, -10, -7, 10, 16, 8, 28, 8, st);
        McCube(Part(base, -3, 18, 7, legA, 0, 0), -2, 0, -2, 4, 6, 4, 0, 16, st);
        McCube(Part(base, 3, 18, 7, legB, 0, 0), -2, 0, -2, 4, 6, 4, 0, 16, st);
        McCube(Part(base, -3, 18, -5, legB, 0, 0), -2, 0, -2, 4, 6, 4, 0, 16, st);
        McCube(Part(base, 3, 18, -5, legA, 0, 0), -2, 0, -2, 4, 6, 4, 0, 16, st);
        if (a.saddled) {
            ModelStyle sd = st;
            sd.tex = ENT_PIG_SADDLE;
            McCube(head, -4, -4, -8, 8, 8, 8, 0, 0, sd, 0.5f);
            McCube(body, -5, -10, -7, 10, 16, 8, 28, 8, sd, 0.5f);
        }
        break;
    }
    case MOB_SHEEP: {
        st.tex = ENT_SHEEP;
        Pose head = Part(base, 0, 6, -8, a.headPitch, a.headYaw, 0);
        McCube(head, -3, -4, -6, 6, 6, 8, 0, 0, st);
        Pose body = Part(base, 0, 5, 2, kPi / 2.0f, 0, 0);
        McCube(body, -4, -10, -7, 8, 16, 6, 28, 8, st);
        const float lx[4] = { -3, 3, -3, 3 }, lz[4] = { 7, 7, -5, -5 }, lr[4] = { legA, legB, legB, legA };
        for (int i = 0; i < 4; ++i)
            McCube(Part(base, lx[i], 12, lz[i], lr[i], 0, 0), -2, 0, -2, 4, 12, 4, 0, 16, st);
        if (!a.sheared) {
            ModelStyle w = st;
            w.tex = ENT_SHEEP_WOOL;
            McCube(head, -3, -4, -4, 6, 6, 6, 0, 0, w, 0.6f);
            McCube(body, -4, -10, -7, 8, 16, 6, 28, 8, w, 1.75f);
            for (int i = 0; i < 4; ++i)
                McCube(Part(base, lx[i], 12, lz[i], lr[i], 0, 0), -2, 0, -2, 4, 6, 4, 0, 16, w, 0.5f);
        }
        break;
    }
    case MOB_CHICKEN: {
        st.tex = ENT_CHICKEN;
        Pose head = Part(base, 0, 15, -4, a.headPitch, a.headYaw, 0);
        McCube(head, -2, -6, -2, 4, 6, 3, 0, 0, st);
        McCube(head, -2, -4, -4, 4, 2, 2, 14, 0, st);
        McCube(head, -1, -2, -3, 2, 2, 2, 14, 4, st);
        Pose body = Part(base, 0, 16, 0, kPi / 2.0f, 0, 0);
        McCube(body, -3, -4, -3, 6, 8, 6, 0, 9, st);
        McCube(Part(base, -2, 19, 1, legA, 0, 0), -1, 0, -3, 3, 5, 3, 26, 0, st);
        McCube(Part(base, 1, 19, 1, legB, 0, 0), -1, 0, -3, 3, 5, 3, 26, 0, st);
        McCube(Part(base, -4, 13, 0, 0, 0, a.wingFlap), 0, 0, -3, 1, 4, 6, 24, 13, st);
        McCube(Part(base, 4, 13, 0, 0, 0, -a.wingFlap), -1, 0, -3, 1, 4, 6, 24, 13, st);
        break;
    }
    default:
        break;
    }
}

void DrawNpc(int kind, const Pose& base, const NpcAnim& a, float light, float r, float g, float b) {
    Sink().Texture(MT_ENTITY);
    ModelStyle st;
    st.light = light;
    st.r = r; st.g = g; st.b = b;
    const float sw = a.limbSwing, am = a.limbAmount;
    float rLeg = std::cos(sw * 0.6662f) * 1.4f * am * 0.5f, lLeg = std::cos(sw * 0.6662f + kPi) * 1.4f * am * 0.5f;
    float legY = 0.0f;
    if (a.riding) {
        rLeg = lLeg = -1.4137167f;
        legY = kPi / 10.0f;
    }
    Pose head = Part(base, 0, 0, 0, a.headPitch, a.headYaw, 0);
    if (kind == NPC_VILLAGER) {
        static const GuiRect* skins[6] = { &ENT_VILLAGER_0, &ENT_VILLAGER_1, &ENT_VILLAGER_2, &ENT_VILLAGER_3, &ENT_VILLAGER_4, &ENT_VILLAGER_5 };
        st.tex = *skins[((a.variant % 6) + 6) % 6];
        McCube(head, -4, -10, -4, 8, 10, 8, 0, 0, st);
        McCube(head, -4, -10, -4, 8, 10, 8, 32, 0, st, 0.51f);
        Pose rim = head;
        rim.RotX(-kPi / 2.0f);
        McCube(rim, -8, -8, -6, 16, 16, 1, 30, 47, st);
        McCube(Part(head, 0, -2, 0), -1, -1, -6, 2, 4, 2, 24, 0, st);
        McCube(base, -4, 0, -3, 8, 12, 6, 16, 20, st);
        McCube(base, -4, 0, -3, 8, 20, 6, 0, 38, st, 0.5f);
        Pose arms = Part(base, 0, 3, -1, -0.75f, 0, 0);
        McCube(arms, -8, -2, -2, 4, 8, 4, 44, 22, st);
        McCube(arms, 4, -2, -2, 4, 8, 4, 44, 22, st, 0, true);
        McCube(arms, -4, 2, -2, 8, 4, 4, 40, 38, st);
    } else {
        const bool pillager = kind == NPC_PILLAGER;
        st.tex = pillager ? ENT_PILLAGER : ENT_VINDICATOR;
        McCube(head, -4, -10, -4, 8, 10, 8, 0, 0, st);
        McCube(Part(head, 0, -2, 0), -1, -1, -6, 2, 4, 2, 24, 0, st);
        McCube(base, -4, 0, -3, 8, 12, 6, 16, 20, st);
        McCube(base, -4, 0, -3, 8, 20, 6, 0, 38, st, 0.5f);
        if (!pillager && !a.armed && !a.crossbow) {
            // calm vindicator: arms crossed like a villager
            Pose arms = Part(base, 0, 3, -1, -0.75f, 0, 0);
            McCube(arms, -8, -2, -2, 4, 8, 4, 44, 22, st);
            McCube(arms, 4, -2, -2, 4, 8, 4, 44, 22, st, 0, true);
            McCube(arms, -4, 2, -2, 8, 4, 4, 40, 38, st);
        } else {
            float rax = std::cos(sw * 0.6662f + kPi) * 2.0f * am * 0.5f, lax = std::cos(sw * 0.6662f) * 2.0f * am * 0.5f;
            float ray = 0.0f, lay = 0.0f;
            if (a.riding) {
                rax -= kPi / 5.0f;
                lax -= kPi / 5.0f;
            } else if (a.crossbow) { // aiming the crossbow
                ray = -0.3f + a.headYaw;
                lay = 0.6f + a.headYaw;
                rax = -kPi / 2.0f + a.headPitch + 0.1f;
                lax = -1.5f + a.headPitch;
            } else if (!pillager && a.armed) { // angry vindicator: axe raised
                rax = -1.8849558f;
            }
            Pose ra = Part(base, -5, 2, 0, rax, ray, 0);
            McCube(ra, -3, -2, -2, 4, 12, 4, 40, 46, st);
            McCube(Part(base, 5, 2, 0, lax, lay, 0), -1, -2, -2, 4, 12, 4, 40, 46, st, 0, true);
            if (!a.riding && (a.crossbow || a.armed)) {
                DrawHeldItem(ra, a.crossbow ? ID_CROSSBOW : ID_IRON_AXE, light, a.crossbow ? TILE_CROSSBOW_ARROW : -1);
                Sink().Texture(MT_ENTITY);
            }
        }
    }
    McCube(Part(base, -2, 12, 0, rLeg, legY, 0), -2, 0, -2, 4, 12, 4, 0, 22, st);
    McCube(Part(base, 2, 12, 0, lLeg, -legY, 0), -2, 0, -2, 4, 12, 4, 0, 22, st, 0, true);
}

} // namespace mc
