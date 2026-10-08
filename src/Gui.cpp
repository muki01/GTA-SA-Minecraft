#include "Gui.h"

#include <string>

#include "CCamera.h"

#include "CPad.h"
#include "CPlayerPed.h"
#include "CSprite2d.h"
#include "RenderWare.h"
#include "common.h"

#include "Config.h"
#include "Effects.h"
#include "Blocks.h"
#include "Game.h"
#include "Gui2D.h"
#include "Inventory.h"
#include "Items.h"
#include "Sound.h"
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
enum SlotKind { SK_NORMAL, SK_RESULT2, SK_RESULT3, SK_FUEL, SK_OUTPUT, SK_PALETTE, SK_ARMOR, SK_OFFHAND, SK_TRASH };
enum SlotGroup { G_HOTBAR, G_MAIN, G_CONTAINER, G_GRID, G_FURNACE_IN, G_FURNACE_FUEL, G_ARMOR };

struct UiSlot {
    ItemStack* st;
    int gx, gy;
    int kind;
    int group;
    int index;
};

std::vector<UiSlot> gSlots;
ItemStack gResult2, gResult3, gTrash;
ItemStack gPalette[45];
float gWinX = 0, gWinY = 0;
int gWinW = 176, gWinH = 166;

const char* kTabNames[CAT_COUNT + 1] = {
    "Yap\xC4\xB1 Bloklar\xC4\xB1", "Renkli Bloklar", "Do\xC4\x9F" "al Bloklar", "\xC4\xB0\xC5\x9Flevsel Bloklar",
    "Ara\xC3\xA7lar ve Gere\xC3\xA7ler", "Sava\xC5\x9F", "Yiyecek ve \xC4\xB0\xC3\xA7" "ecekler", "Malzemeler",
    "Hayatta Kalma Envanteri",
};
const uint16_t kTabIcons[CAT_COUNT + 1] = { ID_BRICKS, ID_CYAN_WOOL, ID_GRASS_BLOCK, ID_CRAFTING_TABLE, ID_DIAMOND_PICKAXE,
                                            ID_DIAMOND_SWORD, ID_GOLDEN_APPLE, ID_IRON_INGOT, ID_CHEST };
// position of each tab: 0..5 top row (from the left), 6..8 bottom row (8 = right end)
struct TabPos { bool top; int col; };
const TabPos kTabPos[CAT_COUNT + 1] = { { true, 0 }, { true, 1 }, { true, 2 }, { true, 3 }, { true, 4 }, { true, 5 },
                                        { false, 0 }, { false, 1 }, { false, 6 } };

FurnaceState* OpenFurnace() { return &gWorld.furnaces[gGame.openPos]; }
ChestState* OpenChest() { return &gWorld.chests[gGame.openPos]; }

void AddPlayerSlots(int x0, int mainY, int hotbarY) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 9; ++c)
            gSlots.push_back({ &gInv.slots[9 + r * 9 + c], x0 + c * 18, mainY + r * 18, SK_NORMAL, G_MAIN, 9 + r * 9 + c });
    for (int c = 0; c < 9; ++c)
        gSlots.push_back({ &gInv.slots[c], x0 + c * 18, hotbarY, SK_NORMAL, G_HOTBAR, c });
}

bool CreativeInventoryTab() { return gGame.screen == SCREEN_CREATIVE && gGame.creativeTab == CAT_COUNT; }

int PaletteRows() {
    int tab = std::min(gGame.creativeTab, (int)CAT_COUNT - 1);
    return ((int)CreativeItems(tab).size() + 8) / 9;
}

const GuiRect& FurnaceWindow() {
    int b = gWorld.GetBlock(gGame.openPos.x, gGame.openPos.y, gGame.openPos.z);
    if (b == ID_BLAST_FURNACE)
        return GUI_WIN_BLAST;
    if (b == ID_SMOKER)
        return GUI_WIN_SMOKER;
    return GUI_WIN_FURNACE;
}

void BuildSlots() {
    gSlots.clear();
    int s = GuiScale();
    int W = RsGlobal.maximumWidth, H = RsGlobal.maximumHeight;
    switch (gGame.screen) {
    case SCREEN_INVENTORY:
        gWinW = 176; gWinH = 166;
        AddPlayerSlots(8, 84, 142);
        for (int i = 0; i < 4; ++i)
            gSlots.push_back({ &gInv.armor[i], 8, 8 + i * 18, SK_ARMOR, G_ARMOR, i });
        gSlots.push_back({ &gInv.offhand, 77, 62, SK_OFFHAND, G_ARMOR, 0 });
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c)
                gSlots.push_back({ &gInv.craft[r * 2 + c], 98 + c * 18, 18 + r * 18, SK_NORMAL, G_GRID, r * 2 + c });
        gResult2 = MatchRecipe(gInv.craft, 2, 2);
        gSlots.push_back({ &gResult2, 154, 28, SK_RESULT2, G_CONTAINER, 0 });
        break;
    case SCREEN_CRAFTING:
        gWinW = 176; gWinH = 166;
        AddPlayerSlots(8, 84, 142);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                gSlots.push_back({ &gInv.craft3[r * 3 + c], 30 + c * 18, 17 + r * 18, SK_NORMAL, G_GRID, r * 3 + c });
        gResult3 = MatchRecipe(gInv.craft3, 3, 3);
        gSlots.push_back({ &gResult3, 124, 35, SK_RESULT3, G_CONTAINER, 0 });
        break;
    case SCREEN_FURNACE: {
        gWinW = 176; gWinH = 166;
        AddPlayerSlots(8, 84, 142);
        FurnaceState* f = OpenFurnace();
        gSlots.push_back({ &f->input, 56, 17, SK_NORMAL, G_FURNACE_IN, 0 });
        gSlots.push_back({ &f->fuel, 56, 53, SK_FUEL, G_FURNACE_FUEL, 0 });
        gSlots.push_back({ &f->output, 116, 35, SK_OUTPUT, G_CONTAINER, 0 });
        break;
    }
    case SCREEN_CHEST: {
        gWinW = 176; gWinH = 167;
        AddPlayerSlots(8, 85, 143);
        ChestState* c = OpenChest();
        for (int r = 0; r < 3; ++r)
            for (int col = 0; col < 9; ++col)
                gSlots.push_back({ &c->slots[r * 9 + col], 8 + col * 18, 18 + r * 18, SK_NORMAL, G_CONTAINER, r * 9 + col });
        break;
    }
    case SCREEN_CREATIVE: {
        gWinW = 195; gWinH = 136;
        if (CreativeInventoryTab()) {
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 9; ++c)
                    gSlots.push_back({ &gInv.slots[9 + r * 9 + c], 9 + c * 18, 54 + r * 18, SK_NORMAL, G_MAIN, 9 + r * 9 + c });
            const int ax[4] = { 54, 54, 108, 108 }, ay[4] = { 6, 33, 6, 33 };
            for (int i = 0; i < 4; ++i)
                gSlots.push_back({ &gInv.armor[i], ax[i], ay[i], SK_ARMOR, G_ARMOR, i });
            gSlots.push_back({ &gInv.offhand, 35, 20, SK_OFFHAND, G_ARMOR, 0 });
            gTrash.Clear();
            gSlots.push_back({ &gTrash, 173, 112, SK_TRASH, G_CONTAINER, 0 });
        } else {
            const auto& items = CreativeItems(gGame.creativeTab);
            int maxScroll = std::max(0, PaletteRows() - 5);
            gGame.creativeScroll = std::clamp(gGame.creativeScroll, 0, maxScroll);
            for (int r = 0; r < 5; ++r)
                for (int c = 0; c < 9; ++c) {
                    int i = (gGame.creativeScroll + r) * 9 + c;
                    ItemStack& p = gPalette[r * 9 + c];
                    p.Clear();
                    if (i < (int)items.size()) {
                        p.id = items[i];
                        p.count = 1;
                    }
                    gSlots.push_back({ &p, 9 + c * 18, 18 + r * 18, SK_PALETTE, G_CONTAINER, r * 9 + c });
                }
        }
        for (int c = 0; c < 9; ++c)
            gSlots.push_back({ &gInv.slots[c], 9 + c * 18, 112, SK_NORMAL, G_HOTBAR, c });
        break;
    }
    default:
        break;
    }
    gWinX = (float)((W - gWinW * s) / 2);
    gWinY = (float)((H - gWinH * s) / 2);
}

UiSlot* SlotUnderMouse() {
    int s = GuiScale();
    float gx = (gGame.cursorX - gWinX) / s, gy = (gGame.cursorY - gWinY) / s;
    for (auto& sl : gSlots)
        if (gx >= sl.gx - 1 && gx < sl.gx + 17 && gy >= sl.gy - 1 && gy < sl.gy + 17)
            return &sl;
    return nullptr;
}

// screen rectangle of a creative tab
void TabRect(int tab, float& x, float& y, float& w, float& h) {
    int s = GuiScale();
    const TabPos& p = kTabPos[tab];
    x = gWinX + (p.col == 6 ? gWinW - 26 : p.col * 27) * s; // the last column sits at the right edge
    y = p.top ? gWinY - 28 * s : gWinY + (gWinH - 4) * s;
    w = 26.0f * s;
    h = 32.0f * s;
}

int TabUnderMouse() {
    if (gGame.screen != SCREEN_CREATIVE)
        return -1;
    for (int t = 0; t <= CAT_COUNT; ++t) {
        float x, y, w, h;
        TabRect(t, x, y, w, h);
        if (gGame.cursorX >= x && gGame.cursorX < x + w && gGame.cursorY >= y && gGame.cursorY < y + h)
            return t;
    }
    return -1;
}

void MoveInto(ItemStack& src, const std::vector<ItemStack*>& dests) {
    if (src.Empty())
        return;
    int maxStack = MaxStack(src.id);
    for (ItemStack* d : dests) {
        if (src.count == 0)
            break;
        if (!d->Empty() && d->SameItem(src) && d->count < maxStack) {
            int mv = std::min<int>(src.count, maxStack - d->count);
            d->count += (uint8_t)mv;
            src.count -= (uint8_t)mv;
        }
    }
    for (ItemStack* d : dests) {
        if (src.count == 0)
            break;
        if (d->Empty()) {
            int mv = std::min<int>(src.count, maxStack);
            *d = src;
            d->count = (uint8_t)mv;
            src.count -= (uint8_t)mv;
        }
    }
    if (src.count == 0)
        src.Clear();
}

std::vector<ItemStack*> PlayerDests(bool hotbarFirst, bool includeHotbar = true, bool includeMain = true) {
    std::vector<ItemStack*> v;
    if (hotbarFirst && includeHotbar)
        for (int i = 0; i < 9; ++i)
            v.push_back(&gInv.slots[i]);
    if (includeMain)
        for (int i = 9; i < INV_SIZE; ++i)
            v.push_back(&gInv.slots[i]);
    if (!hotbarFirst && includeHotbar)
        for (int i = 8; i >= 0; --i)
            v.push_back(&gInv.slots[i]);
    return v;
}

bool ArmorFits(int armorIndex, uint16_t id) { return Item(id).armorSlot == armorIndex + 1; }

void QuickMove(UiSlot& sl) {
    ItemStack& st = *sl.st;
    if (st.Empty())
        return;
    if (sl.group == G_HOTBAR || sl.group == G_MAIN) {
        // armour goes on first
        int as = Item(st.id).armorSlot;
        if (as && gInv.armor[as - 1].Empty() && gGame.screen != SCREEN_CHEST) {
            bool elytra = st.id == ID_ELYTRA;
            gInv.armor[as - 1] = st;
            st.Clear();
            PlaySfx(elytra ? SND_EQUIP_ELYTRA : SND_EQUIP_GENERIC);
            return;
        }
        if (gGame.screen == SCREEN_CHEST) {
            std::vector<ItemStack*> d;
            for (auto& c : OpenChest()->slots)
                d.push_back(&c);
            MoveInto(st, d);
            return;
        }
        if (gGame.screen == SCREEN_FURNACE) {
            FurnaceState* f = OpenFurnace();
            int kind = CookKindForBlock(gWorld.GetBlock(gGame.openPos.x, gGame.openPos.y, gGame.openPos.z));
            if (CookResult(kind, st.id)) {
                MoveInto(st, { &f->input });
                return;
            }
            if (FuelTicks(st.id) > 0) {
                MoveInto(st, { &f->fuel });
                return;
            }
        }
        if (sl.group == G_HOTBAR)
            MoveInto(st, PlayerDests(false, false, true));
        else
            MoveInto(st, PlayerDests(true, true, false));
        return;
    }
    MoveInto(st, PlayerDests(false));
}

bool CanTakeResult(const ItemStack& result) {
    ItemStack& c = gInv.cursor;
    if (c.Empty())
        return true;
    return c.SameItem(result) && c.count + result.count <= MaxStack(c.id);
}

void TakeCraftResult(UiSlot& sl, bool shift) {
    ItemStack* grid = sl.kind == SK_RESULT2 ? gInv.craft : gInv.craft3;
    int w = sl.kind == SK_RESULT2 ? 2 : 3;
    for (int guard = 0; guard < 64; ++guard) {
        ItemStack res = MatchRecipe(grid, w, w);
        if (res.Empty())
            return;
        if (shift) {
            int free = 0;
            for (auto& s : gInv.slots) {
                if (s.Empty())
                    free += MaxStack(res.id);
                else if (s.SameItem(res))
                    free += MaxStack(res.id) - s.count;
            }
            if (free < res.count)
                return;
            gInv.Add(res);
            ConsumeIngredients(grid, w * w);
            continue;
        }
        if (!CanTakeResult(res))
            return;
        if (gInv.cursor.Empty())
            gInv.cursor = res;
        else
            gInv.cursor.count += res.count;
        ConsumeIngredients(grid, w * w);
        return;
    }
}

void ClickSlot(UiSlot& sl, int button, bool shift) {
    ItemStack& a = *sl.st;
    ItemStack& c = gInv.cursor;

    if (sl.kind == SK_RESULT2 || sl.kind == SK_RESULT3) {
        TakeCraftResult(sl, shift);
        return;
    }
    if (sl.kind == SK_TRASH) {
        if (shift) {
            for (auto& s : gInv.slots)
                s.Clear();
        }
        c.Clear();
        return;
    }
    if (sl.kind == SK_PALETTE) {
        if (!c.Empty()) {
            c.Clear();
            return;
        }
        if (a.Empty())
            return;
        ItemStack give = a;
        give.count = (uint8_t)(button == 0 ? MaxStack(a.id) : 1);
        if (shift)
            gInv.Add(give);
        else
            c = give;
        return;
    }
    if (shift) {
        QuickMove(sl);
        return;
    }
    if (sl.kind == SK_OUTPUT) {
        if (a.Empty())
            return;
        if (c.Empty()) {
            c = a;
            a.Clear();
        } else if (c.SameItem(a)) {
            int mv = std::min<int>(a.count, MaxStack(c.id) - c.count);
            c.count += (uint8_t)mv;
            a.count -= (uint8_t)mv;
            if (!a.count)
                a.Clear();
        }
        return;
    }
    if (sl.kind == SK_FUEL && !c.Empty() && FuelTicks(c.id) == 0)
        return;
    if (sl.kind == SK_ARMOR) {
        if (!c.Empty() && !ArmorFits(sl.index, c.id))
            return;
        if (!c.Empty() && c.count > 1)
            return;
        std::swap(a, c);
        if (!a.Empty())
            PlaySfx(a.id == ID_ELYTRA ? SND_EQUIP_ELYTRA : SND_EQUIP_GENERIC);
        return;
    }

    if (button == 0) {
        if (c.Empty()) {
            c = a;
            a.Clear();
        } else if (a.Empty()) {
            a = c;
            c.Clear();
        } else if (a.SameItem(c)) {
            int mv = std::min<int>(c.count, MaxStack(a.id) - a.count);
            a.count += (uint8_t)mv;
            c.count -= (uint8_t)mv;
            if (!c.count)
                c.Clear();
        } else {
            std::swap(a, c);
        }
    } else {
        if (c.Empty()) {
            if (!a.Empty()) {
                int take = (a.count + 1) / 2;
                c = a;
                c.count = (uint8_t)take;
                a.count -= (uint8_t)take;
                if (!a.count)
                    a.Clear();
            }
        } else if (a.Empty()) {
            a = c;
            a.count = 1;
            if (--c.count == 0)
                c.Clear();
        } else if (a.SameItem(c)) {
            if (a.count < MaxStack(a.id)) {
                a.count++;
                if (--c.count == 0)
                    c.Clear();
            }
        } else {
            std::swap(a, c);
        }
    }
}

const char* ScreenTitle() {
    switch (gGame.screen) {
    case SCREEN_INVENTORY: return "\xC3\x9Cretim";
    case SCREEN_CRAFTING: return Block(ID_CRAFTING_TABLE).name;
    case SCREEN_FURNACE: return Block(gWorld.GetBlock(gGame.openPos.x, gGame.openPos.y, gGame.openPos.z)).name;
    case SCREEN_CHEST: return Block(ID_CHEST).name;
    case SCREEN_CREATIVE: return kTabNames[std::clamp(gGame.creativeTab, 0, (int)CAT_COUNT)];
    }
    return "";
}

void DrawTab(int t, bool selected) {
    int s = GuiScale();
    float x, y, w, h;
    TabRect(t, x, y, w, h);
    const TabPos& p = kTabPos[t];
    int idx = p.col + 1;
    if (!p.top && p.col == 6)
        idx = 7;
    const GuiRect* r = nullptr;
    static const GuiRect* topSel[7] = { &GUI_TAB_TOP_SELECTED_1, &GUI_TAB_TOP_SELECTED_2, &GUI_TAB_TOP_SELECTED_3, &GUI_TAB_TOP_SELECTED_4,
                                        &GUI_TAB_TOP_SELECTED_5, &GUI_TAB_TOP_SELECTED_6, &GUI_TAB_TOP_SELECTED_7 };
    static const GuiRect* topUn[7] = { &GUI_TAB_TOP_UNSELECTED_1, &GUI_TAB_TOP_UNSELECTED_2, &GUI_TAB_TOP_UNSELECTED_3, &GUI_TAB_TOP_UNSELECTED_4,
                                       &GUI_TAB_TOP_UNSELECTED_5, &GUI_TAB_TOP_UNSELECTED_6, &GUI_TAB_TOP_UNSELECTED_7 };
    static const GuiRect* botSel[7] = { &GUI_TAB_BOTTOM_SELECTED_1, &GUI_TAB_BOTTOM_SELECTED_2, &GUI_TAB_BOTTOM_SELECTED_3, &GUI_TAB_BOTTOM_SELECTED_4,
                                        &GUI_TAB_BOTTOM_SELECTED_5, &GUI_TAB_BOTTOM_SELECTED_6, &GUI_TAB_BOTTOM_SELECTED_7 };
    static const GuiRect* botUn[7] = { &GUI_TAB_BOTTOM_UNSELECTED_1, &GUI_TAB_BOTTOM_UNSELECTED_2, &GUI_TAB_BOTTOM_UNSELECTED_3, &GUI_TAB_BOTTOM_UNSELECTED_4,
                                       &GUI_TAB_BOTTOM_UNSELECTED_5, &GUI_TAB_BOTTOM_UNSELECTED_6, &GUI_TAB_BOTTOM_UNSELECTED_7 };
    int i = std::clamp(idx, 1, 7) - 1;
    r = p.top ? (selected ? topSel[i] : topUn[i]) : (selected ? botSel[i] : botUn[i]);
    GuiSprite(*r, x, y, (float)s);
    DrawItemIcon(kTabIcons[t], x + 5 * s, y + (p.top ? 9 : 7) * s, (float)s);
}
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

bool GuiMouseOverWindow() {
    int s = GuiScale();
    return gGame.cursorX >= gWinX && gGame.cursorY >= gWinY && gGame.cursorX < gWinX + gWinW * s &&
           gGame.cursorY < gWinY + gWinH * s;
}

void GuiProcessInput() {
    if (gGame.screen == SCREEN_NONE)
        return;
    BuildSlots();
    bool lClick = CPad::NewMouseControllerState.lmb && !CPad::OldMouseControllerState.lmb;
    bool rClick = CPad::NewMouseControllerState.rmb && !CPad::OldMouseControllerState.rmb;
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    if (gGame.screen == SCREEN_CREATIVE && !CreativeInventoryTab()) {
        if (CPad::NewMouseControllerState.wheelUp)
            gGame.creativeScroll--;
        if (CPad::NewMouseControllerState.wheelDown)
            gGame.creativeScroll++;
    }

    if (lClick || rClick) {
        int tab = TabUnderMouse();
        if (tab >= 0 && lClick) {
            gGame.creativeTab = tab;
            gGame.creativeScroll = 0;
            PlaySfx(SND_CLICK);
        } else if (UiSlot* sl = SlotUnderMouse()) {
            ClickSlot(*sl, lClick ? 0 : 1, shift);
            gWorld.dirty = true;
        } else if (!GuiMouseOverWindow() && !gInv.cursor.Empty()) {
            ItemStack drop = gInv.cursor;
            if (rClick) {
                drop.count = 1;
                if (--gInv.cursor.count == 0)
                    gInv.cursor.Clear();
            } else {
                gInv.cursor.Clear();
            }
            DropStackAtPlayer(drop, true);
        }
        BuildSlots();
    }
}

static void DrawScreen(int s) {
    BuildSlots();
    int W = RsGlobal.maximumWidth, H = RsGlobal.maximumHeight;
    FillRect(0, 0, (float)W, (float)H, Argb(16, 16, 16, 150)); // Minecraft darkens the world behind menus
    RwUInt32 labelCol = Argb(64, 64, 64);

    if (gGame.screen == SCREEN_CREATIVE)
        for (int t = 0; t <= CAT_COUNT; ++t)
            if (t != gGame.creativeTab)
                DrawTab(t, false);

    const GuiRect* win = &GUI_WIN_INVENTORY;
    switch (gGame.screen) {
    case SCREEN_CRAFTING: win = &GUI_WIN_CRAFTING; break;
    case SCREEN_FURNACE: win = &FurnaceWindow(); break;
    case SCREEN_CHEST: win = &GUI_WIN_CHEST; break;
    case SCREEN_CREATIVE: win = CreativeInventoryTab() ? &GUI_WIN_CREATIVE_INV : &GUI_WIN_CREATIVE; break;
    default: break;
    }
    GuiSprite(*win, gWinX, gWinY, (float)s);
    if (gGame.screen == SCREEN_CREATIVE)
        DrawTab(gGame.creativeTab, true);

    switch (gGame.screen) {
    case SCREEN_INVENTORY:
        Text(ScreenTitle(), gWinX + 97 * s, gWinY + 8 * s, (float)s, labelCol, false);
        if (gGame.steve)
            DrawSteve2D(gWinX + 51 * s, gWinY + 10 * s, (float)s);
        break;
    case SCREEN_CREATIVE:
        if (CreativeInventoryTab()) {
            if (gGame.steve)
                DrawSteve2D(gWinX + 89 * s, gWinY + 6 * s, s * 0.65f);
        } else {
            Text(ScreenTitle(), gWinX + 8 * s, gWinY + 6 * s, (float)s, labelCol, false);
            int rows = PaletteRows();
            int maxScroll = std::max(1, rows - 5);
            float t = (float)gGame.creativeScroll / maxScroll;
            GuiSprite(rows > 5 ? GUI_SCROLLER : GUI_SCROLLER_OFF, gWinX + 175 * s, gWinY + (18 + t * (112 - 15)) * s, (float)s);
        }
        break;
    case SCREEN_CHEST:
        Text(ScreenTitle(), gWinX + 8 * s, gWinY + 6 * s, (float)s, labelCol, false);
        Text("Envanter", gWinX + 8 * s, gWinY + 73 * s, (float)s, labelCol, false);
        break;
    default:
        Text(ScreenTitle(), gWinX + 8 * s, gWinY + 6 * s, (float)s, labelCol, false);
        Text("Envanter", gWinX + 8 * s, gWinY + 72 * s, (float)s, labelCol, false);
        break;
    }

    if (gGame.screen == SCREEN_FURNACE) {
        FurnaceState* f = OpenFurnace();
        if (f->burnTime > 0 && f->burnTimeTotal > 0) {
            int h = (int)std::ceil(14.0f * f->burnTime / f->burnTimeTotal);
            GuiSprite(GUI_LIT.x, GUI_LIT.y + 14 - h, 14, h, gWinX + 56 * s, gWinY + (36 + 14 - h) * s, (float)s);
        }
        if (f->cookTime > 0) {
            int b = gWorld.GetBlock(gGame.openPos.x, gGame.openPos.y, gGame.openPos.z);
            int total = 200;
            CookResult(CookKindForBlock(b), f->input.id, &total);
            int w = (int)std::ceil(24.0f * f->cookTime / std::max(1, total));
            GuiSprite(GUI_BURN.x, GUI_BURN.y, w, 16, gWinX + 79 * s, gWinY + 34 * s, (float)s);
        }
    }

    UiSlot* hover = SlotUnderMouse();
    for (auto& sl : gSlots) {
        float x = gWinX + sl.gx * s, y = gWinY + sl.gy * s;
        if (sl.st->Empty()) {
            if (sl.kind == SK_ARMOR) {
                static const GuiRect* icons[4] = { &GUI_SLOT_HELMET, &GUI_SLOT_CHESTPLATE, &GUI_SLOT_LEGGINGS, &GUI_SLOT_BOOTS };
                GuiSprite(*icons[sl.index], x, y, (float)s);
            } else if (sl.kind == SK_OFFHAND) {
                GuiSprite(GUI_SLOT_SHIELD, x, y, (float)s);
            }
        }
        DrawStack(*sl.st, x, y, (float)s);
        if (&sl == hover)
            FillRect(x, y, x + 16 * s, y + 16 * s, Argb(255, 255, 255, 128));
    }

    if (!gInv.cursor.Empty()) {
        DrawStack(gInv.cursor, gGame.cursorX - 8 * s, gGame.cursorY - 8 * s, (float)s);
    } else if (hover && !hover->st->Empty()) {
        Tooltip(ItemName(hover->st->id), gGame.cursorX, gGame.cursorY, (float)s);
    } else {
        int tab = TabUnderMouse();
        if (tab >= 0)
            Tooltip(kTabNames[tab], gGame.cursorX, gGame.cursorY, (float)s);
    }
    GuiSprite(GUI_CURSOR, gGame.cursorX, gGame.cursorY, (float)std::max(1, s / 2));
}

static void DrawCrosshair(int s) {
    int W = RsGlobal.maximumWidth, H = RsGlobal.maximumHeight;
    Flush();
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDINVDESTCOLOR);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCCOLOR);
    GuiSprite(GUI_CROSSHAIR, (float)((W - 15 * s) / 2), (float)((H - 15 * s) / 2), (float)s);
    Flush();
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}

static void DrawHotbarAndStats(int s) {
    int W = RsGlobal.maximumWidth, H = RsGlobal.maximumHeight;
    float hx = (float)(W / 2 - 91 * s), hy = (float)(H - 22 * s);
    GuiSprite(GUI_HOTBAR, hx, hy, (float)s);
    GuiSprite(GUI_SELECTION, hx + (gInv.selected * 20 - 1) * s, hy - 1 * s, (float)s);
    for (int i = 0; i < 9; ++i)
        DrawStack(gInv.slots[i], hx + (3 + i * 20) * s, hy + 3 * s, (float)s);
    if (!gInv.offhand.Empty()) {
        GuiSprite(GUI_OFFHAND, hx - 29 * s, hy - 1 * s, (float)s);
        DrawStack(gInv.offhand, hx - 26 * s, hy + 3 * s, (float)s);
    }

    if (gGame.gameMode == MODE_SURVIVAL) {
        GuiSprite(GUI_XP_BG, hx, (float)(H - 29 * s), (float)s);
        const int fill = (int)(Clamp(gGame.xpProgress, 0.0f, 1.0f) * 183.0f);
        if (fill > 0)
            GuiSprite(GUI_XP_PROGRESS.x, GUI_XP_PROGRESS.y, std::min(fill, 182), GUI_XP_PROGRESS.h, hx, (float)(H - 29 * s), (float)s);
        if (gGame.xpLevel > 0) {
            // Gui.renderExperienceLevel: green number with a black outline
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", gGame.xpLevel);
            const float tw = TextWidth(buf, (float)s);
            const float tx = (W - tw) * 0.5f, ty = (float)(H - 35 * s);
            Text(buf, tx + s, ty, (float)s, 0xFF000000, false);
            Text(buf, tx - s, ty, (float)s, 0xFF000000, false);
            Text(buf, tx, ty + s, (float)s, 0xFF000000, false);
            Text(buf, tx, ty - s, (float)s, 0xFF000000, false);
            Text(buf, tx, ty, (float)s, Argb(128, 255, 32), false);
        }
        CPlayerPed* ped = FindPlayerPed();
        if (ped) {
            float maxH = ped->m_fMaxHealth > 1.0f ? ped->m_fMaxHealth : 100.0f;
            int hp = (int)std::ceil(Clamp(ped->m_fHealth / maxH, 0.0f, 1.0f) * 20.0f);
            float y = (float)(H - 39 * s);
            bool shake = hp <= 4;
            const bool poisoned = HasEffect(EFFECT_POISON);
            for (int i = 0; i < 10; ++i) {
                float x = hx + i * 8 * s;
                float yy = y + (shake ? ((rand() % 3) - 1) * s : 0);
                GuiSprite(GUI_HEART_CONTAINER, x, yy, (float)s);
                if (hp >= i * 2 + 2)
                    GuiSprite(poisoned ? GUI_HEART_POISONED_FULL : GUI_HEART_FULL, x, yy, (float)s);
                else if (hp == i * 2 + 1)
                    GuiSprite(poisoned ? GUI_HEART_POISONED_HALF : GUI_HEART_HALF, x, yy, (float)s);
            }
            // absorption: yellow hearts in rows above the red ones (the armour moves up)
            const int absorb = (int)std::ceil(gGame.absorption);
            const int absorbRows = (absorb + 19) / 20;
            for (int i = 0; i * 2 < absorb; ++i) {
                float x = hx + (i % 10) * 8 * s, yy = y - (10 + (i / 10) * 10) * s;
                GuiSprite(GUI_HEART_CONTAINER, x, yy, (float)s);
                GuiSprite(absorb >= i * 2 + 2 ? GUI_HEART_ABSORBING_FULL : GUI_HEART_ABSORBING_HALF, x, yy, (float)s);
            }
            y -= absorbRows * 10 * s;
            int armor = 0;
            for (auto& a : gInv.armor)
                if (!a.Empty())
                    armor += Item(a.id).armorPoints;
            armor = std::max(armor, (int)std::round(Clamp(ped->m_fArmour / 100.0f, 0.0f, 1.0f) * 20.0f));
            if (armor > 0)
                for (int i = 0; i < 10; ++i) {
                    float x = hx + i * 8 * s;
                    const GuiRect& r = armor >= i * 2 + 2 ? GUI_ARMOR_FULL : armor == i * 2 + 1 ? GUI_ARMOR_HALF : GUI_ARMOR_EMPTY;
                    GuiSprite(r, x, y - 10 * s, (float)s);
                }
            y = (float)(H - 39 * s);
            int food = (int)std::ceil(gGame.food);
            const bool hungry = HasEffect(EFFECT_HUNGER);
            for (int i = 0; i < 10; ++i) {
                float x = hx + (182 - 9 - i * 8) * s;
                float yy = y + (gGame.saturation <= 0.0f && food <= 6 ? ((rand() % 3) - 1) * s : 0);
                GuiSprite(hungry ? GUI_FOOD_EMPTY_HUNGER : GUI_FOOD_EMPTY, x, yy, (float)s);
                if (food >= i * 2 + 2)
                    GuiSprite(hungry ? GUI_FOOD_FULL_HUNGER : GUI_FOOD_FULL, x, yy, (float)s);
                else if (food == i * 2 + 1)
                    GuiSprite(hungry ? GUI_FOOD_HALF_HUNGER : GUI_FOOD_HALF, x, yy, (float)s);
            }
            // air: ten bubbles above the food while under water (Gui.renderAirBubbles)
            if (gGame.air < kMaxAir) {
                const float air = std::max(0.0f, gGame.air);
                const int full = (int)std::ceil((air - 2.0f) * 10.0f / kMaxAir);
                const int bursting = (int)std::ceil(air * 10.0f / kMaxAir) - full;
                for (int i = 0; i < full + bursting; ++i)
                    GuiSprite(i < full ? GUI_AIR : GUI_AIR_BURSTING, hx + (182 - 9 - i * 8) * s, y - 10 * s, (float)s);
            }
        }
    }

    if (gGame.selectedNameTimer > 0.0f && !gInv.Held().Empty()) {
        const char* name = ItemName(gInv.Held().id);
        int a = (int)Clamp(gGame.selectedNameTimer * 255.0f, 0.0f, 255.0f);
        float tw = TextWidth(name, (float)s);
        float y = (float)(H - (gGame.gameMode == MODE_SURVIVAL ? 59 : 45) * s);
        Text(name, (W - tw) * 0.5f, y, (float)s, Argb(255, 255, 255, a));
    }
}

// ================================================================ you died
// DeathScreen: red veil, "You died!", the cause, the score. GTA brings the player back on its own.
static void DrawDeathScreen(int s) {
    const float W = (float)RsGlobal.maximumWidth, H = (float)RsGlobal.maximumHeight;
    const float t = Clamp(gGame.deathTime / 1.0f, 0.0f, 1.0f);
    // fillGradient(0x60500000, 0xA0803030)
    const int bands = 24;
    for (int i = 0; i < bands; ++i) {
        const float f = (i + 0.5f) / bands;
        const int a = (int)((0x60 + (0xA0 - 0x60) * f) * t), r = (int)(0x50 + (0x80 - 0x50) * f), g = (int)(0x30 * f);
        FillRect(0, H * i / bands, W, H * (i + 1) / bands + 1.0f, Argb(r, g, g, a));
    }
    const float cx = W * 0.5f;
    const char* title = kStr[STR_YOU_DIED];
    Text(title, cx - TextWidth(title, s * 2.0f) * 0.5f, 30.0f * s * 2.0f, s * 2.0f);
    // "%1$s died" with the player's name
    std::string msg = kStr[std::clamp(gGame.deathCause, (int)STR_DEATH_GENERIC, (int)STR_DEATH_STARVE)];
    const size_t at = msg.find("%1$s");
    if (at != std::string::npos)
        msg.replace(at, 4, gGame.steve ? "Steve" : "CJ");
    Text(msg.c_str(), cx - TextWidth(msg.c_str(), (float)s) * 0.5f, 85.0f * s, (float)s);
    char score[96], num[32];
    snprintf(num, sizeof(num), "%d", gGame.xpTotal);
    std::string sc = kStr[STR_SCORE];
    const size_t at2 = sc.find("%s");
    if (at2 != std::string::npos)
        sc.erase(at2, 2);
    snprintf(score, sizeof(score), "%s", sc.c_str());
    const float sw = TextWidth(score, (float)s) + TextWidth(num, (float)s);
    Text(score, cx - sw * 0.5f, 100.0f * s, (float)s);
    Text(num, cx - sw * 0.5f + TextWidth(score, (float)s) + s, 100.0f * s, (float)s, Argb(255, 255, 85));
    if (gGame.deathTime > 1.0f) {
        // GTA respawns the player by itself: the button shows what is about to happen
        const char* b = kStr[STR_RESPAWN];
        const float bw = 200.0f * s, bx = cx - bw * 0.5f, by = H * 0.25f + 72.0f * s;
        if (gMenuTex.tex) {
            Sprite(gMenuTex, MENU_BUTTON_HI.x, MENU_BUTTON_HI.y, 200, 20, bx, by, (float)s);
        } else {
            FillRect(bx, by, bx + bw, by + 20.0f * s, Argb(0, 0, 0, 160));
        }
        Text(b, cx - TextWidth(b, (float)s) * 0.5f, by + 6.0f * s, (float)s, Argb(255, 255, 160));
    }
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

void GuiDrawHud() {
    if (!gGuiTex.tex || !gFontTex.tex || !gAtlasTex.tex)
        return;
    Saved2D saved;
    saved.Save();
    Begin2D();
    int s = GuiScale();
    int W = RsGlobal.maximumWidth;

    // the camera inside our water or lava (Minecraft's fog under water is blue, in lava almost opaque orange)
    if (gGame.enabled && gGame.inWorld) {
        const int fluid = FluidAt(TheCamera.GetPosition());
        if (fluid == ID_WATER)
            FillRect(0.0f, 0.0f, (float)W, (float)RsGlobal.maximumHeight, Argb(0x2A, 0x50, 0xC8, 90));
        else if (fluid == ID_LAVA)
            FillRect(0.0f, 0.0f, (float)W, (float)RsGlobal.maximumHeight, Argb(0xE0, 0x50, 0x00, 215));
    }

    if (gGame.hudVisible) {
        int row = 0;
        for (int e = 0; e < EFFECT_COUNT; ++e) {
            const EffectState& st = gEffects[e];
            if (st.time <= 0.0f)
                continue;
            const float bx = (float)(W - 25 * s), by = (float)((1 + row * 26) * s);
            // the icon blinks when the effect is about to end
            const bool blink = st.time < 10.0f && std::fmod(st.time, 0.5f) < 0.25f;
            GuiSprite(GUI_EFFECT_BG, bx, by, (float)s);
            GuiSprite(kEffectIcons[e], bx + 3 * s, by + 3 * s, (float)s, blink ? Argb(255, 255, 255, 110) : 0xFFFFFFFF);
            static const char* const kRoman[] = { "", " II", " III", " IV", " V", " VI" };
            char name[64], left[16];
            snprintf(name, sizeof(name), "%s%s", kEffectNames[e], kRoman[std::clamp(st.amp, 0, 5)]);
            const int secs = (int)std::ceil(st.time);
            snprintf(left, sizeof(left), "%d:%02d", secs / 60, secs % 60);
            Text(name, bx - 3 * s - TextWidth(name, (float)s), by + 3 * s, (float)s);
            Text(left, bx - 3 * s - TextWidth(left, (float)s), by + 13 * s, (float)s, Argb(170, 170, 170));
            ++row;
        }
        if (gGame.screen == SCREEN_NONE && gGame.cameraMode != CAM_THIRD_FRONT)
            DrawCrosshair(s);
        DrawHotbarAndStats(s);
        if (gGame.screen != SCREEN_NONE)
            DrawScreen(s);
    }
    if (gGame.enabled && gGame.inWorld && gGame.deathTime >= 0.0f)
        DrawDeathScreen(s);
    if (gGame.messageTimer > 0.0f && !gGame.message.empty()) {
        int a = (int)Clamp(gGame.messageTimer * 255.0f, 0.0f, 255.0f);
        float tw = TextWidth(gGame.message.c_str(), (float)s);
        Text(gGame.message.c_str(), (W - tw) * 0.5f, 40.0f * s, (float)s, Argb(255, 255, 85, a));
    }
    Flush();
    saved.Restore();
}

} // namespace mc
