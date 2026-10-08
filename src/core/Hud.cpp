#include "Hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "BlockRules.h"
#include "GameState.h"
#include "Inventory.h"
#include "Items.h"
#include "Screens.h"
#include "Survival.h"

namespace mc {

namespace {
uint32_t Argb(int r, int g, int b, int a = 255) {
    return ((uint32_t)(a & 255) << 24) | ((uint32_t)(r & 255) << 16) | ((uint32_t)(g & 255) << 8) | (uint32_t)(b & 255);
}

struct Pieces {
    std::vector<HudPiece>& out;
    HudPiece& Add(int kind, int anchor, float x, float y) {
        out.emplace_back();
        HudPiece& p = out.back();
        p.kind = kind;
        p.anchor = anchor;
        p.x = x;
        p.y = y;
        return p;
    }
    void Sprite(int anchor, const GuiRect& r, float x, float y, uint32_t color = 0xFFFFFFFF) {
        HudPiece& p = Add(HP_SPRITE, anchor, x, y);
        p.src = r;
        p.color = color;
    }
    void Stack(int anchor, const ItemStack* st, float x, float y) { Add(HP_STACK, anchor, x, y).stack = st; }
    void Icon(int anchor, uint16_t id, float x, float y) { Add(HP_ICON, anchor, x, y).item = id; }
    HudPiece& Text(int anchor, const std::string& text, float x, float y, uint32_t color, bool shadow, int align) {
        HudPiece& p = Add(HP_TEXT, anchor, x, y);
        p.text = text;
        p.color = color;
        p.shadow = shadow;
        p.align = align;
        return p;
    }
    void Fill(int anchor, float x, float y, float w, float h, uint32_t color) {
        HudPiece& p = Add(HP_FILL, anchor, x, y);
        p.w = w;
        p.h = h;
        p.color = color;
    }
    void FillScreen(uint32_t color) { Fill(AT_TOP_LEFT, 0, 0, 0, 0, color); }
    void Tooltip(const char* text) { Add(HP_TOOLTIP, AT_CURSOR, 0, 0).text = text; }
};

// ---------------------------------------------------------------- the HUD
void Effects(Pieces& P) {
    int row = 0;
    for (int e = 0; e < EFFECT_COUNT; ++e) {
        const EffectState& st = gSurvival.effects[e];
        if (st.time <= 0.0f)
            continue;
        const float bx = -25.0f, by = (float)(1 + row * 26);
        // the icon blinks when the effect is about to end
        const bool blink = st.time < 10.0f && std::fmod(st.time, 0.5f) < 0.25f;
        P.Sprite(AT_TOP_RIGHT, GUI_EFFECT_BG, bx, by);
        P.Sprite(AT_TOP_RIGHT, kEffectIcons[e], bx + 3, by + 3, blink ? Argb(255, 255, 255, 110) : 0xFFFFFFFF);
        static const char* const kRoman[] = { "", " II", " III", " IV", " V", " VI" };
        char name[64], left[16];
        snprintf(name, sizeof(name), "%s%s", kEffectNames[e], kRoman[std::clamp(st.amp, 0, 5)]);
        const int secs = (int)std::ceil(st.time);
        snprintf(left, sizeof(left), "%d:%02d", secs / 60, secs % 60);
        P.Text(AT_TOP_RIGHT, name, bx - 3, by + 3, 0xFFFFFFFF, true, 2);
        P.Text(AT_TOP_RIGHT, left, bx - 3, by + 13, Argb(170, 170, 170), true, 2);
        ++row;
    }
}

void HotbarAndStats(Pieces& P, const HudFacts& f) {
    const float hx = -91.0f;
    P.Sprite(AT_BOTTOM, GUI_HOTBAR, hx, -22);
    P.Sprite(AT_BOTTOM, GUI_SELECTION, hx + gInv.selected * 20 - 1, -23);
    for (int i = 0; i < 9; ++i)
        P.Stack(AT_BOTTOM, &gInv.slots[i], hx + 3 + i * 20, -19);
    if (!gInv.offhand.Empty()) {
        P.Sprite(AT_BOTTOM, GUI_OFFHAND, hx - 29, -23);
        P.Stack(AT_BOTTOM, &gInv.offhand, hx - 26, -19);
    }

    if (gGame.gameMode == MODE_SURVIVAL) {
        P.Sprite(AT_BOTTOM, GUI_XP_BG, hx, -29);
        const int fill = (int)(Clamp(gSurvival.xpProgress, 0.0f, 1.0f) * 183.0f);
        if (fill > 0) {
            GuiRect bar = GUI_XP_PROGRESS;
            bar.w = std::min(fill, 182);
            P.Sprite(AT_BOTTOM, bar, hx, -29);
        }
        if (gSurvival.xpLevel > 0) {
            // Gui.renderExperienceLevel: green number with a black outline
            P.Text(AT_BOTTOM, std::to_string(gSurvival.xpLevel), 0, -35, Argb(128, 255, 32), false, 1).outline = true;
        }
        if (f.hasPlayer) {
            int hp = (int)std::ceil(Clamp(f.health, 0.0f, 1.0f) * 20.0f);
            float y = -39.0f;
            bool shake = hp <= 4;
            const bool poisoned = HasEffect(EFFECT_POISON);
            for (int i = 0; i < 10; ++i) {
                float x = hx + i * 8;
                float yy = y + (shake ? (float)((rand() % 3) - 1) : 0.0f);
                P.Sprite(AT_BOTTOM, GUI_HEART_CONTAINER, x, yy);
                if (hp >= i * 2 + 2)
                    P.Sprite(AT_BOTTOM, poisoned ? GUI_HEART_POISONED_FULL : GUI_HEART_FULL, x, yy);
                else if (hp == i * 2 + 1)
                    P.Sprite(AT_BOTTOM, poisoned ? GUI_HEART_POISONED_HALF : GUI_HEART_HALF, x, yy);
            }
            // absorption: yellow hearts in rows above the red ones (the armour moves up)
            const int absorb = (int)std::ceil(gSurvival.absorption);
            const int absorbRows = (absorb + 19) / 20;
            for (int i = 0; i * 2 < absorb; ++i) {
                float x = hx + (i % 10) * 8, yy = y - (10 + (i / 10) * 10);
                P.Sprite(AT_BOTTOM, GUI_HEART_CONTAINER, x, yy);
                P.Sprite(AT_BOTTOM, absorb >= i * 2 + 2 ? GUI_HEART_ABSORBING_FULL : GUI_HEART_ABSORBING_HALF, x, yy);
            }
            y -= absorbRows * 10;
            int armor = ArmorPoints();
            armor = std::max(armor, (int)std::round(Clamp(f.hostArmour, 0.0f, 1.0f) * 20.0f));
            if (armor > 0)
                for (int i = 0; i < 10; ++i) {
                    const GuiRect& r = armor >= i * 2 + 2 ? GUI_ARMOR_FULL : armor == i * 2 + 1 ? GUI_ARMOR_HALF : GUI_ARMOR_EMPTY;
                    P.Sprite(AT_BOTTOM, r, hx + i * 8, y - 10);
                }
            y = -39.0f;
            int food = (int)std::ceil(gSurvival.food);
            const bool hungry = HasEffect(EFFECT_HUNGER);
            for (int i = 0; i < 10; ++i) {
                float x = hx + (182 - 9 - i * 8);
                float yy = y + (gSurvival.saturation <= 0.0f && food <= 6 ? (float)((rand() % 3) - 1) : 0.0f);
                P.Sprite(AT_BOTTOM, hungry ? GUI_FOOD_EMPTY_HUNGER : GUI_FOOD_EMPTY, x, yy);
                if (food >= i * 2 + 2)
                    P.Sprite(AT_BOTTOM, hungry ? GUI_FOOD_FULL_HUNGER : GUI_FOOD_FULL, x, yy);
                else if (food == i * 2 + 1)
                    P.Sprite(AT_BOTTOM, hungry ? GUI_FOOD_HALF_HUNGER : GUI_FOOD_HALF, x, yy);
            }
            // air: ten bubbles above the food while under water (Gui.renderAirBubbles)
            if (gSurvival.air < kMaxAir) {
                const float air = std::max(0.0f, gSurvival.air);
                const int full = (int)std::ceil((air - 2.0f) * 10.0f / kMaxAir);
                const int bursting = (int)std::ceil(air * 10.0f / kMaxAir) - full;
                for (int i = 0; i < full + bursting; ++i)
                    P.Sprite(AT_BOTTOM, i < full ? GUI_AIR : GUI_AIR_BURSTING, hx + (182 - 9 - i * 8), y - 10);
            }
        }
    }

    if (gGame.selectedNameTimer > 0.0f && !gInv.Held().Empty()) {
        int a = (int)Clamp(gGame.selectedNameTimer * 255.0f, 0.0f, 255.0f);
        P.Text(AT_BOTTOM, ItemName(gInv.Held().id), 0, -(float)(gGame.gameMode == MODE_SURVIVAL ? 59 : 45), Argb(255, 255, 255, a), true, 1);
    }
}

// ---------------------------------------------------------------- the open screen
const GuiRect& FurnaceWindow() {
    int b = gWorld.GetBlock(gGame.openPos.x, gGame.openPos.y, gGame.openPos.z);
    if (b == ID_BLAST_FURNACE)
        return GUI_WIN_BLAST;
    if (b == ID_SMOKER)
        return GUI_WIN_SMOKER;
    return GUI_WIN_FURNACE;
}

void Tab(Pieces& P, int t, bool selected) {
    float x, y, w, h;
    TabBox(t, x, y, w, h);
    const TabPos& p = kTabPos[t];
    int idx = p.col + 1;
    if (!p.top && p.col == 6)
        idx = 7;
    static const GuiRect* topSel[7] = { &GUI_TAB_TOP_SELECTED_1, &GUI_TAB_TOP_SELECTED_2, &GUI_TAB_TOP_SELECTED_3, &GUI_TAB_TOP_SELECTED_4,
                                        &GUI_TAB_TOP_SELECTED_5, &GUI_TAB_TOP_SELECTED_6, &GUI_TAB_TOP_SELECTED_7 };
    static const GuiRect* topUn[7] = { &GUI_TAB_TOP_UNSELECTED_1, &GUI_TAB_TOP_UNSELECTED_2, &GUI_TAB_TOP_UNSELECTED_3, &GUI_TAB_TOP_UNSELECTED_4,
                                       &GUI_TAB_TOP_UNSELECTED_5, &GUI_TAB_TOP_UNSELECTED_6, &GUI_TAB_TOP_UNSELECTED_7 };
    static const GuiRect* botSel[7] = { &GUI_TAB_BOTTOM_SELECTED_1, &GUI_TAB_BOTTOM_SELECTED_2, &GUI_TAB_BOTTOM_SELECTED_3, &GUI_TAB_BOTTOM_SELECTED_4,
                                        &GUI_TAB_BOTTOM_SELECTED_5, &GUI_TAB_BOTTOM_SELECTED_6, &GUI_TAB_BOTTOM_SELECTED_7 };
    static const GuiRect* botUn[7] = { &GUI_TAB_BOTTOM_UNSELECTED_1, &GUI_TAB_BOTTOM_UNSELECTED_2, &GUI_TAB_BOTTOM_UNSELECTED_3, &GUI_TAB_BOTTOM_UNSELECTED_4,
                                       &GUI_TAB_BOTTOM_UNSELECTED_5, &GUI_TAB_BOTTOM_UNSELECTED_6, &GUI_TAB_BOTTOM_UNSELECTED_7 };
    int i = std::clamp(idx, 1, 7) - 1;
    const GuiRect* r = p.top ? (selected ? topSel[i] : topUn[i]) : (selected ? botSel[i] : botUn[i]);
    P.Sprite(AT_WINDOW, *r, x, y);
    P.Icon(AT_WINDOW, kTabIcons[t], x + 5, y + (p.top ? 9 : 7));
}

void OpenScreenPieces(Pieces& P, const HudFacts& f) {
    P.FillScreen(Argb(16, 16, 16, 150)); // Minecraft darkens the world behind menus
    const uint32_t labelCol = Argb(64, 64, 64);

    if (gGame.screen == SCREEN_CREATIVE)
        for (int t = 0; t <= CAT_COUNT; ++t)
            if (t != gGame.creativeTab)
                Tab(P, t, false);

    const GuiRect* win = &GUI_WIN_INVENTORY;
    switch (gGame.screen) {
    case SCREEN_CRAFTING: win = &GUI_WIN_CRAFTING; break;
    case SCREEN_FURNACE: win = &FurnaceWindow(); break;
    case SCREEN_CHEST: win = &GUI_WIN_CHEST; break;
    case SCREEN_CREATIVE: win = CreativeInventoryTab() ? &GUI_WIN_CREATIVE_INV : &GUI_WIN_CREATIVE; break;
    default: break;
    }
    P.Sprite(AT_WINDOW, *win, 0, 0);
    if (gGame.screen == SCREEN_CREATIVE)
        Tab(P, gGame.creativeTab, true);

    switch (gGame.screen) {
    case SCREEN_INVENTORY:
        P.Text(AT_WINDOW, ScreenTitle(), 97, 8, labelCol, false, 0);
        P.Add(HP_PLAYER, AT_WINDOW, 51, 10);
        break;
    case SCREEN_CREATIVE:
        if (CreativeInventoryTab()) {
            P.Add(HP_PLAYER, AT_WINDOW, 89, 6).scale = 0.65f;
        } else {
            P.Text(AT_WINDOW, ScreenTitle(), 8, 6, labelCol, false, 0);
            int rows = PaletteRows();
            int maxScroll = std::max(1, rows - 5);
            float t = (float)gGame.creativeScroll / maxScroll;
            P.Sprite(AT_WINDOW, rows > 5 ? GUI_SCROLLER : GUI_SCROLLER_OFF, 175, 18 + t * (112 - 15));
        }
        break;
    case SCREEN_CHEST:
        P.Text(AT_WINDOW, ScreenTitle(), 8, 6, labelCol, false, 0);
        P.Text(AT_WINDOW, "Envanter", 8, 73, labelCol, false, 0);
        break;
    default:
        P.Text(AT_WINDOW, ScreenTitle(), 8, 6, labelCol, false, 0);
        P.Text(AT_WINDOW, "Envanter", 8, 72, labelCol, false, 0);
        break;
    }

    if (gGame.screen == SCREEN_FURNACE) {
        FurnaceState* fs = OpenFurnace();
        if (fs->burnTime > 0 && fs->burnTimeTotal > 0) {
            int h = (int)std::ceil(14.0f * fs->burnTime / fs->burnTimeTotal);
            P.Sprite(AT_WINDOW, GuiRect{ GUI_LIT.x, GUI_LIT.y + 14 - h, 14, h }, 56, (float)(36 + 14 - h));
        }
        if (fs->cookTime > 0) {
            int b = gWorld.GetBlock(gGame.openPos.x, gGame.openPos.y, gGame.openPos.z);
            int total = 200;
            CookResult(CookKindForBlock(b), fs->input.id, &total);
            int w = (int)std::ceil(24.0f * fs->cookTime / std::max(1, total));
            P.Sprite(AT_WINDOW, GuiRect{ GUI_BURN.x, GUI_BURN.y, w, 16 }, 79, 34);
        }
    }

    UiSlot* hover = SlotAt(f.mouseX, f.mouseY);
    for (auto& sl : gSlots) {
        if (sl.st->Empty()) {
            if (sl.kind == SK_ARMOR) {
                static const GuiRect* icons[4] = { &GUI_SLOT_HELMET, &GUI_SLOT_CHESTPLATE, &GUI_SLOT_LEGGINGS, &GUI_SLOT_BOOTS };
                P.Sprite(AT_WINDOW, *icons[sl.index], (float)sl.gx, (float)sl.gy);
            } else if (sl.kind == SK_OFFHAND) {
                P.Sprite(AT_WINDOW, GUI_SLOT_SHIELD, (float)sl.gx, (float)sl.gy);
            }
        }
        P.Stack(AT_WINDOW, sl.st, (float)sl.gx, (float)sl.gy);
        if (&sl == hover)
            P.Fill(AT_WINDOW, (float)sl.gx, (float)sl.gy, 16, 16, Argb(255, 255, 255, 128));
    }

    if (!gInv.cursor.Empty()) {
        P.Stack(AT_CURSOR, &gInv.cursor, -8, -8);
    } else if (hover && !hover->st->Empty()) {
        P.Tooltip(ItemName(hover->st->id));
    } else {
        int tab = TabAt(f.mouseX, f.mouseY);
        if (tab >= 0)
            P.Tooltip(kTabNames[tab]);
    }
}

// ---------------------------------------------------------------- you died
// DeathScreen: red veil, "You died!", the cause, the score.
void DeathScreen(Pieces& P, const HudFacts& f) {
    const float t = Clamp(gGame.deathTime / 1.0f, 0.0f, 1.0f);
    HudPiece& veil = P.Add(HP_GRADIENT, AT_TOP_LEFT, 0, 0); // fillGradient(0x60500000, 0xA0803030)
    veil.color = 0x60500000;
    veil.color2 = 0xA0803030;
    veil.scale = t;
    P.Text(AT_TOP, kStr[STR_YOU_DIED], 0, 60, 0xFFFFFFFF, true, 1).scale = 2.0f;
    // "%1$s died" with the player's name
    std::string msg = kStr[std::clamp(gGame.deathCause, (int)STR_DEATH_GENERIC, (int)STR_DEATH_STARVE)];
    const size_t at = msg.find("%1$s");
    if (at != std::string::npos)
        msg.replace(at, 4, f.playerName);
    P.Text(AT_TOP, msg, 0, 85, 0xFFFFFFFF, true, 1);
    char score[96], num[32];
    snprintf(num, sizeof(num), "%d", gSurvival.xpTotal);
    std::string sc = kStr[STR_SCORE];
    const size_t at2 = sc.find("%s");
    if (at2 != std::string::npos)
        sc.erase(at2, 2);
    snprintf(score, sizeof(score), "%s", sc.c_str());
    HudPiece& line = P.Text(AT_TOP, score, 0, 100, 0xFFFFFFFF, true, 1);
    line.text2 = num;
    line.color2 = Argb(255, 255, 85);
    if (gGame.deathTime > 1.0f) {
        // the host brings the player back by itself: the button shows what is about to happen
        HudPiece& b = P.Add(HP_SPRITE, AT_QUARTER, -100, 72);
        b.src = MENU_BUTTON_HI;
        b.texture = HT_MENU;
        P.Text(AT_QUARTER, kStr[STR_RESPAWN], 0, 78, Argb(255, 255, 160), true, 1);
    }
}
} // namespace

void BuildHud(const HudFacts& f, std::vector<HudPiece>& out) {
    Pieces P{ out };
    if (f.shown) {
        // the camera inside our water or lava (Minecraft's fog under water is blue, in lava almost opaque orange)
        const int fluid = FluidAt(f.camera);
        if (fluid == ID_WATER)
            P.FillScreen(Argb(0x2A, 0x50, 0xC8, 90));
        else if (fluid == ID_LAVA)
            P.FillScreen(Argb(0xE0, 0x50, 0x00, 215));
        Effects(P);
        if (gGame.screen == SCREEN_NONE && gGame.cameraMode != CAM_THIRD_FRONT) {
            HudPiece& c = P.Add(HP_SPRITE, AT_CENTRE, -7.5f, -7.5f);
            c.src = GUI_CROSSHAIR;
            c.invert = true;
        }
        HotbarAndStats(P, f);
        if (gGame.screen != SCREEN_NONE)
            OpenScreenPieces(P, f);
        if (gGame.deathTime >= 0.0f)
            DeathScreen(P, f);
    }
    if (gGame.messageTimer > 0.0f && !gGame.message.empty()) {
        int a = (int)Clamp(gGame.messageTimer * 255.0f, 0.0f, 255.0f);
        P.Text(AT_TOP, gGame.message, 0, 40, Argb(255, 255, 85, a), true, 1);
    }
}

} // namespace mc
