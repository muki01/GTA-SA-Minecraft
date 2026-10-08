#include "Gui.h"

#include <string>

#include "CCamera.h"

#include "CPad.h"
#include "CPlayerPed.h"
#include "CSprite2d.h"
#include "RenderWare.h"
#include "common.h"

#include "Config.h"
#include "Game.h"
#include "Gui2D.h"
#include "Hud.h"
#include "Items.h"
#include "Screens.h"
#include "Textures.h"

namespace mc {

// ================================================================ 2D batching
namespace {
constexpr int kMaxQuads = 2048;
RwIm2DVertex gV[kMaxQuads * 4];
RwImVertexIndex gIdx[kMaxQuads * 6];
int gNumV = 0;
RwRaster* gRaster = nullptr;
bool gIdxInit = false;

inline RwUInt32 Argb(int r, int g, int b, int a = 255) {
    return ((RwUInt32)a << 24) | ((RwUInt32)r << 16) | ((RwUInt32)g << 8) | (RwUInt32)b;
}

void Flush() {
    if (!gNumV)
        return;
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, gRaster);
    RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, gV, gNumV, gIdx, gNumV / 4 * 6);
    gNumV = 0;
}

void UseRaster(RwRaster* r) {
    if (r != gRaster) {
        Flush();
        gRaster = r;
    }
}

inline void Vtx(RwIm2DVertex& v, float x, float y, float u, float vv, RwUInt32 col) {
    v.x = x - 0.5f;
    v.y = y - 0.5f;
    v.z = CSprite2d::NearScreenZ;
    v.rhw = CSprite2d::RecipNearClip;
    v.emissiveColor = col;
    v.u = u;
    v.v = vv;
}

void Quad4(const float* xs, const float* ys, const float* us, const float* vs, RwUInt32 col) {
    if (gNumV + 4 > kMaxQuads * 4)
        Flush();
    for (int i = 0; i < 4; ++i)
        Vtx(gV[gNumV + i], xs[i], ys[i], us[i], vs[i], col);
    gNumV += 4;
}

void Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, RwUInt32 col) {
    const float xs[4] = { x0, x1, x1, x0 };
    const float ys[4] = { y0, y0, y1, y1 };
    const float us[4] = { u0, u1, u1, u0 };
    const float vs[4] = { v0, v0, v1, v1 };
    Quad4(xs, ys, us, vs, col);
}

void Sprite(const Tex& t, int sx, int sy, int sw, int sh, float x, float y, float s, RwUInt32 col = 0xFFFFFFFF) {
    UseRaster(t.Raster());
    float u0 = (float)sx / t.w, v0 = (float)sy / t.h, u1 = (float)(sx + sw) / t.w, v1 = (float)(sy + sh) / t.h;
    Quad(x, y, x + sw * s, y + sh * s, u0, v0, u1, v1, col);
}
void GuiSprite(int sx, int sy, int sw, int sh, float x, float y, float s, RwUInt32 col = 0xFFFFFFFF) {
    Sprite(gGuiTex, sx, sy, sw, sh, x, y, s, col);
}
void GuiSprite(const GuiRect& r, float x, float y, float s, RwUInt32 col = 0xFFFFFFFF) {
    GuiSprite(r.x, r.y, r.w, r.h, x, y, s, col);
}

void FillRect(float x0, float y0, float x1, float y1, RwUInt32 col) {
    UseRaster(gGuiTex.Raster());
    float u = (GUI_WHITE.x + 2.0f) / GUI_TEX_W, v = (GUI_WHITE.y + 2.0f) / GUI_TEX_H;
    Quad(x0, y0, x1, y1, u, v, u, v, col);
}

// ---------------------------------------------------------------- text (Minecraft bitmap font)
uint32_t NextCodepoint(const char*& p) {
    unsigned char c = (unsigned char)*p++;
    if (c < 0x80)
        return c;
    int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    uint32_t cp = c & (0x3F >> extra);
    for (int i = 0; i < extra && *p; ++i)
        cp = (cp << 6) | ((unsigned char)*p++ & 0x3F);
    return cp;
}

float TextWidth(const char* text, float s) {
    float w = 0;
    for (const char* p = text; *p;) {
        int slot = FontSlotForCodepoint(NextCodepoint(p));
        w += (FONT_WIDTHS[slot] + 1) * s;
    }
    return w > 0 ? w - s : 0;
}

void TextRaw(const char* text, float x, float y, float s, RwUInt32 col) {
    UseRaster(gFontTex.Raster());
    const float cw = 16.0f / (FONT_COLS * 16), ch = 16.0f / (FONT_ROWS * 16);
    for (const char* p = text; *p;) {
        int slot = FontSlotForCodepoint(NextCodepoint(p));
        if (slot != ' ') {
            float u0 = (slot % FONT_COLS) * cw, v0 = (slot / FONT_COLS) * ch;
            Quad(x, y - 4 * s, x + 16 * s, y + 12 * s, u0, v0, u0 + cw, v0 + ch, col);
        }
        x += (FONT_WIDTHS[slot] + 1) * s;
    }
}

void Text(const char* text, float x, float y, float s, RwUInt32 col = 0xFFFFFFFF, bool shadow = true) {
    if (shadow) {
        int a = (col >> 24) & 0xFF, r = (col >> 16) & 0xFF, g = (col >> 8) & 0xFF, b = col & 0xFF;
        TextRaw(text, x + s, y + s, s, Argb(r / 4, g / 4, b / 4, a));
    }
    TextRaw(text, x, y, s, col);
}

// ---------------------------------------------------------------- items
void AtlasQuad4(int tile, const float* xs, const float* ys, RwUInt32 col) {
    UseRaster(gAtlasTex.Raster());
    TileUV uv = AtlasTileUV(tile);
    const float us[4] = { uv.u0, uv.u1, uv.u1, uv.u0 };
    const float vs[4] = { uv.v0, uv.v0, uv.v1, uv.v1 };
    Quad4(xs, ys, us, vs, col);
}

void DrawItemIcon(uint16_t id, float x, float y, float s) {
    if (!IsValidItem(id))
        return;
    if (IsBlockItem(id) && Block(id).shape == SHAPE_CROSS) {
        float xs[4] = { x, x + 16 * s, x + 16 * s, x };
        float ys[4] = { y, y, y + 16 * s, y + 16 * s };
        AtlasQuad4(Block(id).tex[0], xs, ys, 0xFFFFFFFF);
    } else if (IsBlockItem(id)) {
        int meta = Block(id).shape == SHAPE_FACING ? 2 : 0;
        auto P = [&](float px) { return px * s; };
        {
            float xs[4] = { x + P(0.93f), x + P(8.0f), x + P(15.07f), x + P(8.0f) };
            float ys[4] = { y + P(3.69f), y + P(0.15f), y + P(3.69f), y + P(7.22f) };
            AtlasQuad4(BlockFaceTile(id, FACE_TOP, meta), xs, ys, 0xFFFFFFFF);
        }
        {
            float xs[4] = { x + P(0.93f), x + P(8.0f), x + P(8.0f), x + P(0.93f) };
            float ys[4] = { y + P(3.69f), y + P(7.22f), y + P(15.88f), y + P(12.35f) };
            AtlasQuad4(BlockFaceTile(id, FACE_SOUTH, meta), xs, ys, Argb(204, 204, 204));
        }
        {
            float xs[4] = { x + P(8.0f), x + P(15.07f), x + P(15.07f), x + P(8.0f) };
            float ys[4] = { y + P(7.22f), y + P(3.69f), y + P(12.35f), y + P(15.88f) };
            AtlasQuad4(BlockFaceTile(id, FACE_EAST, meta), xs, ys, Argb(153, 153, 153));
        }
    } else {
        float xs[4] = { x, x + 16 * s, x + 16 * s, x };
        float ys[4] = { y, y, y + 16 * s, y + 16 * s };
        AtlasQuad4(Item(id).tile, xs, ys, 0xFFFFFFFF);
    }
}

void DrawStack(const ItemStack& st, float x, float y, float s) {
    if (st.Empty())
        return;
    DrawItemIcon(st.id, x, y, s);
    const ItemDef& d = Item(st.id);
    if (d.durability && st.damage > 0) {
        float frac = Clamp(1.0f - (float)st.damage / d.durability, 0.0f, 1.0f);
        int w = (int)std::round(13.0f * frac);
        FillRect(x + 2 * s, y + 13 * s, x + 15 * s, y + 15 * s, Argb(0, 0, 0));
        // Minecraft: hue from red (0) to green (1/3)
        float h = frac / 3.0f * 6.0f;
        int r = (int)(255 * Clamp(2.0f - h, 0.0f, 1.0f)), g = (int)(255 * Clamp(h, 0.0f, 1.0f));
        FillRect(x + 2 * s, y + 13 * s, x + (2 + w) * s, y + 14 * s, Argb(r, g, 0));
    }
    if (st.count > 1) {
        char buf[8];
        sprintf(buf, "%d", st.count);
        float tw = TextWidth(buf, s);
        Text(buf, x + 17 * s - tw, y + 9 * s, s);
    }
}

void Tooltip(const char* text, float mx, float my, float s) {
    float w = TextWidth(text, s);
    float x = mx + 12 * s, y = my - 12 * s;
    float h = 8 * s;
    int W = RsGlobal.maximumWidth;
    if (x + w + 4 * s > W)
        x = mx - 16 * s - w;
    RwUInt32 bg = Argb(16, 0, 16, 240);
    FillRect(x - 3 * s, y - 4 * s, x + w + 3 * s, y + h + 4 * s, bg);
    FillRect(x - 4 * s, y - 3 * s, x + w + 4 * s, y + h + 3 * s, bg);
    RwUInt32 b1 = Argb(80, 0, 255, 80), b2 = Argb(40, 0, 127, 80);
    FillRect(x - 3 * s, y - 3 * s, x + w + 3 * s, y - 2 * s, b1);
    FillRect(x - 3 * s, y + h + 2 * s, x + w + 3 * s, y + h + 3 * s, b2);
    FillRect(x - 3 * s, y - 2 * s, x - 2 * s, y + h + 2 * s, b1);
    FillRect(x + w + 2 * s, y - 2 * s, x + w + 3 * s, y + h + 2 * s, b1);
    Text(text, x, y, s);
}

// 2D front view of Steve (inventory preview)
void DrawSteve2D(float cx, float top, float s) {
    const Tex& t = gEntityTex;
    const float k = 2.0f * s; // screen pixels per skin pixel
    float x0 = cx - 4 * k;
    struct P { int sx, sy, ox, oy, w, h; float x, y; };
    const P parts[] = {
        { 8, 8, 40, 8, 8, 8, x0, top },                    // head + hat
        { 20, 20, 20, 36, 8, 12, x0, top + 8 * k },        // body + jacket
        { 44, 20, 44, 36, 4, 12, x0 - 4 * k, top + 8 * k },  // right arm (viewer's left)
        { 36, 52, 52, 52, 4, 12, x0 + 8 * k, top + 8 * k },  // left arm
        { 4, 20, 4, 36, 4, 12, x0, top + 20 * k },         // right leg
        { 20, 52, 4, 52, 4, 12, x0 + 4 * k, top + 20 * k },  // left leg
    };
    for (auto& p : parts) {
        Sprite(t, p.sx, p.sy, p.w, p.h, p.x, p.y, k);
        Sprite(t, p.ox, p.oy, p.w, p.h, p.x, p.y, k);
    }
}

// ---------------------------------------------------------------- render state
struct Saved2D {
    void *zt, *zw, *va, *src, *dst, *cull, *fog, *flt, *ras, *atf, *atr;
    void Save() {
        RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &zt);
        RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &zw);
        RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &va);
        RwRenderStateGet(rwRENDERSTATESRCBLEND, &src);
        RwRenderStateGet(rwRENDERSTATEDESTBLEND, &dst);
        RwRenderStateGet(rwRENDERSTATECULLMODE, &cull);
        RwRenderStateGet(rwRENDERSTATEFOGENABLE, &fog);
        RwRenderStateGet(rwRENDERSTATETEXTUREFILTER, &flt);
        RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &ras);
        RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &atf);
        RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, &atr);
    }
    void Restore() {
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, zt);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, zw);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, va);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, src);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, dst);
        RwRenderStateSet(rwRENDERSTATECULLMODE, cull);
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, fog);
        RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, flt);
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, ras);
        RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, atf);
        RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, atr);
    }
};

void Begin2D() {
    if (!gIdxInit) {
        for (int q = 0; q < kMaxQuads; ++q) {
            gIdx[q * 6 + 0] = (RwImVertexIndex)(q * 4 + 0);
            gIdx[q * 6 + 1] = (RwImVertexIndex)(q * 4 + 1);
            gIdx[q * 6 + 2] = (RwImVertexIndex)(q * 4 + 2);
            gIdx[q * 6 + 3] = (RwImVertexIndex)(q * 4 + 0);
            gIdx[q * 6 + 4] = (RwImVertexIndex)(q * 4 + 2);
            gIdx[q * 6 + 5] = (RwImVertexIndex)(q * 4 + 3);
        }
        gIdxInit = true;
    }
    gNumV = 0;
    gRaster = nullptr;
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
    RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONGREATER);
    RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)0);
}

// ================================================================ screens & slots
float gWinX = 0, gWinY = 0; // where the open screen's window sits, screen pixels

// BuildSlots, and the window in the middle of the screen
void PlaceWindow() {
    BuildSlots();
    const int s = GuiScale();
    gWinX = (float)((RsGlobal.maximumWidth - gWinW * s) / 2);
    gWinY = (float)((RsGlobal.maximumHeight - gWinH * s) / 2);
}

float MouseGx() { return (gGame.cursorX - gWinX) / GuiScale(); }
float MouseGy() { return (gGame.cursorY - gWinY) / GuiScale(); }

} // namespace

// ================================================================ public
int GuiScale() {
    if (gConfig.guiScale > 0)
        return gConfig.guiScale;
    int W = RsGlobal.maximumWidth, H = RsGlobal.maximumHeight;
    int s = 1;
    while (s < 6 && W / (s + 1) >= 320 && H / (s + 1) >= 240)
        s++;
    return s;
}

void GuiProcessInput() {
    if (gGame.screen == SCREEN_NONE)
        return;
    PlaceWindow();
    bool lClick = CPad::NewMouseControllerState.lmb && !CPad::OldMouseControllerState.lmb;
    bool rClick = CPad::NewMouseControllerState.rmb && !CPad::OldMouseControllerState.rmb;
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (CPad::NewMouseControllerState.wheelUp)
        ScreenScroll(-1);
    if (CPad::NewMouseControllerState.wheelDown)
        ScreenScroll(1);
    ScreenClick(MouseGx(), MouseGy(), lClick, rClick, shift);
}

// ================================================================ shared 2D helpers (Gui2D.h)
namespace ui {
namespace {
Saved2D gUiSaved;
}
void Begin() {
    gUiSaved.Save();
    Begin2D();
}
void End() {
    Flush();
    gUiSaved.Restore();
}
void Linear(bool on) {
    Flush();
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)(on ? rwFILTERLINEAR : rwFILTERNEAREST));
}
RwUInt32 Color(int r, int g, int b, int a) { return Argb(r, g, b, a); }
void Rect(float x0, float y0, float x1, float y1, RwUInt32 col) { FillRect(x0, y0, x1, y1, col); }
void Image(const Tex& t, const GuiRect& src, float x0, float y0, float x1, float y1, RwUInt32 col) {
    if (!t.tex || t.w <= 0 || t.h <= 0)
        return;
    UseRaster(t.Raster());
    Quad(x0, y0, x1, y1, (float)src.x / t.w, (float)src.y / t.h, (float)(src.x + src.w) / t.w, (float)(src.y + src.h) / t.h, col);
}
float TextW(const char* text, float scale) { return TextWidth(text, scale); }
void Label(const char* text, float x, float y, float scale, RwUInt32 col, bool shadow) { Text(text, x, y, scale, col, shadow); }
void LabelCentered(const char* text, float cx, float y, float scale, RwUInt32 col, bool shadow) {
    Text(text, cx - TextWidth(text, scale) * 0.5f, y, scale, col, shadow);
}
void Cursor(float x, float y, float scale) { GuiSprite(GUI_CURSOR, x, y, scale); }
} // namespace ui

// one piece of the core's HUD (BuildHud) on the screen
static void Anchor(const HudPiece& p, int s, float& x, float& y) {
    const int W = RsGlobal.maximumWidth, H = RsGlobal.maximumHeight;
    switch (p.anchor) {
    case AT_TOP: x = W * 0.5f + p.x * s; y = p.y * s; break;
    case AT_TOP_RIGHT: x = (float)W + p.x * s; y = p.y * s; break;
    case AT_CENTRE: // (whole screen pixels, as Minecraft centres the crosshair)
        x = (float)((W + (int)(p.x * 2.0f) * s) / 2);
        y = (float)((H + (int)(p.y * 2.0f) * s) / 2);
        break;
    case AT_QUARTER: x = W * 0.5f + p.x * s; y = H * 0.25f + p.y * s; break;
    case AT_BOTTOM: x = (float)(W / 2) + p.x * s; y = (float)H + p.y * s; break;
    case AT_WINDOW: x = gWinX + p.x * s; y = gWinY + p.y * s; break;
    case AT_CURSOR: x = gGame.cursorX + p.x * s; y = gGame.cursorY + p.y * s; break;
    default: x = p.x * s; y = p.y * s; break;
    }
}

static void Paint(const HudPiece& p, int s) {
    const float W = (float)RsGlobal.maximumWidth, H = (float)RsGlobal.maximumHeight;
    float x, y;
    Anchor(p, s, x, y);
    switch (p.kind) {
    case HP_SPRITE:
        if (p.texture == HT_MENU) {
            if (gMenuTex.tex)
                Sprite(gMenuTex, p.src.x, p.src.y, p.src.w, p.src.h, x, y, (float)s);
            else
                FillRect(x, y, x + p.src.w * s, y + p.src.h * s, Argb(0, 0, 0, 160));
        } else if (p.invert) {
            Flush();
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDINVDESTCOLOR);
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCCOLOR);
            GuiSprite(p.src, x, y, (float)s, p.color);
            Flush();
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
        } else {
            GuiSprite(p.src, x, y, (float)s, p.color);
        }
        break;
    case HP_STACK: DrawStack(*p.stack, x, y, (float)s); break;
    case HP_ICON: DrawItemIcon(p.item, x, y, (float)s); break;
    case HP_TEXT: {
        const float sc = p.scale * s;
        const float tw1 = TextWidth(p.text.c_str(), sc);
        const float tw = tw1 + (p.text2.empty() ? 0.0f : TextWidth(p.text2.c_str(), sc));
        const float tx = p.align == 1 ? x - tw * 0.5f : p.align == 2 ? x - tw : x;
        if (p.outline) {
            Text(p.text.c_str(), tx + s, y, sc, 0xFF000000, false);
            Text(p.text.c_str(), tx - s, y, sc, 0xFF000000, false);
            Text(p.text.c_str(), tx, y + s, sc, 0xFF000000, false);
            Text(p.text.c_str(), tx, y - s, sc, 0xFF000000, false);
        }
        Text(p.text.c_str(), tx, y, sc, p.color, p.shadow);
        if (!p.text2.empty())
            Text(p.text2.c_str(), tx + tw1 + s, y, sc, p.color2, p.shadow);
        break;
    }
    case HP_FILL:
        if (p.w <= 0.0f)
            FillRect(0.0f, 0.0f, W, H, p.color);
        else
            FillRect(x, y, x + p.w * s, y + p.h * s, p.color);
        break;
    case HP_GRADIENT: {
        auto ch = [](uint32_t c, int shift) { return (int)((c >> shift) & 255); };
        const int bands = 24;
        for (int i = 0; i < bands; ++i) {
            const float f = (i + 0.5f) / bands;
            auto mix = [&](int shift) { return ch(p.color, shift) + (ch(p.color2, shift) - ch(p.color, shift)) * f; };
            FillRect(0, H * i / bands, W, H * (i + 1) / bands + 1.0f,
                     Argb((int)mix(16), (int)mix(8), (int)mix(0), (int)(mix(24) * p.scale)));
        }
        break;
    }
    case HP_TOOLTIP: Tooltip(p.text.c_str(), x, y, (float)s); break;
    case HP_PLAYER:
        if (gGta.steve)
            DrawSteve2D(x, y, p.scale * s);
        break;
    default: break;
    }
}

void GuiDrawHud() {
    if (!gGuiTex.tex || !gFontTex.tex || !gAtlasTex.tex)
        return;
    Saved2D saved;
    saved.Save();
    Begin2D();
    const int s = GuiScale();
    HudFacts f;
    f.shown = gGta.hudVisible;
    if (gGta.hudVisible && gGame.screen != SCREEN_NONE) {
        PlaceWindow();
        f.mouseX = MouseGx();
        f.mouseY = MouseGy();
    }
    CPlayerPed* ped = FindPlayerPed();
    f.hasPlayer = ped != nullptr;
    if (ped) {
        const float maxH = ped->m_fMaxHealth > 1.0f ? ped->m_fMaxHealth : 100.0f;
        f.health = ped->m_fHealth / maxH;
        f.hostArmour = ped->m_fArmour / 100.0f;
    }
    f.camera = TheCamera.GetPosition();
    f.playerName = gGta.steve ? "Steve" : "CJ";
    static std::vector<HudPiece> pieces;
    pieces.clear();
    BuildHud(f, pieces);
    for (const HudPiece& p : pieces)
        Paint(p, s);
    if (gGta.hudVisible && gGame.screen != SCREEN_NONE)
        GuiSprite(GUI_CURSOR, gGame.cursorX, gGame.cursorY, (float)std::max(1, s / 2)); // the mouse pointer
    Flush();
    saved.Restore();
}

} // namespace mc
