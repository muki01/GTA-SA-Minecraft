#include "McMenu.h"

#include <string>

#include "CGame.h"
#include "CMenuManager.h"
#include "CPad.h"
#include "RenderWare.h"
#include "common.h"
#include "safetyhook.hpp"

#include "Config.h"
#include "Game.h"
#include "Gui.h"
#include "Gui2D.h"
#include "Input.h"
#include "Sound.h"
#include "Textures.h"

namespace mc {

namespace {
// CMenuManager screens (eMenuScreen)
enum : int {
    SCR_STATS = 0, SCR_START_GAME = 1, SCR_MAP = 5, SCR_LOAD_GAME = 9, SCR_DELETE_GAME = 10, SCR_LOAD_FIRST_SAVE = 13,
    SCR_DELETE_FINISHED = 14, SCR_OPTIONS = 33, SCR_MAIN_MENU = 34, SCR_QUIT_ASK = 35, SCR_PAUSE = 41
};
enum Page { PAGE_HOME, PAGE_WORLDS, PAGE_DELETE };

SafetyHookInline gDrawHook, gInputHook, gSaveHook, gDeleteHook;
Page gPage = PAGE_HOME;
int gLastScreen = -100;
int gSelected = -1;  // save slot 0..7
int gScroll = 0;
bool gFallback = false; // F10: GTA's own menu for the rest of the session
bool gMouseWasDown = true;
DWORD gLastRowClick = 0;
int gLastRowClicked = -1;
DWORD gOpenedAt = 0;

// ---- CMenuManager (plugin-sdk's names for these fields are misleading: offsets from the game)
uint8_t* Menu() { return reinterpret_cast<uint8_t*>(&FrontEndMenuManager); }
int Screen() { return (int8_t)Menu()[0x15D]; }
bool GameNotStarted() { return Menu()[0xE9] != 0; }       // m_bMainMenuSwitch
int MouseX() { return *reinterpret_cast<int*>(Menu() + 0xE0); } // set by the window's mouse messages
int MouseY() { return *reinterpret_cast<int*>(Menu() + 0xE4); }
void SetSlot(int slot) { Menu()[0x15F] = (uint8_t)slot; } // m_SelectedSlot
void DontDrawFrontEnd() { Menu()[0x32] = 1; }             // closes the menu / starts what was asked for
void Switch(int screen) { FrontEndMenuManager.SwitchToNewScreen((char)screen); }

// ---- save slots (CGenericGameStorage / C_PcSave)
bool SlotFilled(int i) { return reinterpret_cast<int*>(0xC16EBC)[i] == 0; }
std::string Clean(const char* p, int max) {
    std::string out;
    for (int i = 0; i < max && p[i]; ++i)
        out += (p[i] >= 32 && p[i] < 127) ? p[i] : ' ';
    return out;
}
std::string SlotName(int i) { return Clean(reinterpret_cast<const char*>(0xC16368 + i * 260), 40); }
std::string SlotDate(int i) { return Clean(reinterpret_cast<const char*>(0xC16138 + i * 70), 60); }
void PopulateSlots() { reinterpret_cast<void(__thiscall*)(void*)>(0x619140)(reinterpret_cast<void*>(0xC17034)); }

bool Active() {
    if (!gConfig.minecraftMenu || gFallback || !gMenuTex.tex || !gGuiTex.tex || !gFontTex.tex)
        return false;
    const int s = Screen();
    return s == SCR_MAIN_MENU || s == SCR_PAUSE || s == SCR_START_GAME || s == SCR_LOAD_GAME || s == SCR_DELETE_GAME;
}

// ---------------------------------------------------------------- widgets
struct Frame {
    float mx = 0, my = 0;
    bool click = false; // left button went down this frame
    float s = 1;        // GUI scale
    float W = 0, H = 0;
};
Frame gF;

// a Minecraft button: 200x20 texture, the middle cut out for other widths. Returns true when clicked.
bool Button(const char* label, float x, float y, float w, bool enabled = true) {
    const float s = gF.s, h = 20.0f * s;
    const bool hover = enabled && gF.mx >= x && gF.mx < x + w && gF.my >= y && gF.my < y + h;
    const GuiRect& r = !enabled ? MENU_BUTTON_OFF : (hover ? MENU_BUTTON_HI : MENU_BUTTON);
    const float half = std::min(w * 0.5f, 100.0f * s);
    const int px = (int)(half / s);
    ui::Image(gMenuTex, { r.x, r.y, px, r.h }, x, y, x + half, y + h);
    ui::Image(gMenuTex, { r.x + r.w - px, r.y, px, r.h }, x + w - half, y, x + w, y + h);
    if (w > half * 2.0f + 0.5f) // wider than the texture: stretch its middle
        ui::Image(gMenuTex, { r.x + 20, r.y, 160, r.h }, x + half, y, x + w - half, y + h);
    ui::LabelCentered(label, x + w * 0.5f, y + 6.0f * s, s, enabled ? (hover ? ui::Color(255, 255, 160) : 0xFFFFFFFF) : ui::Color(160, 160, 160));
    if (hover && gF.click) {
        PlaySfx(SND_CLICK, nullptr, 0.25f);
        return true;
    }
    return false;
}

// GTA's own menu background (CMenuManager::DrawBackground): black, with the artwork of the page in the top right
// corner. GTA's rectangles are in 640x448 units; here they keep their proportions on wide screens.
enum { SPR_BACK2 = 13, SPR_BACK8 = 19 }; // eFrontend sprites: load / delete game, main menu
void GtaBackground(int sprite) {
    const float W = gF.W, H = gF.H;
    ui::Rect(-1.0f, -1.0f, W + 1.0f, H + 1.0f, ui::Color(0, 0, 0));
    if (!Menu()[0x15C]) // m_bTexturesLoaded
        return;
    RwTexture* t = reinterpret_cast<RwTexture**>(Menu() + 0xF8)[sprite]; // m_aFrontEndSprites
    RwRaster* r = t ? RwTextureGetRaster(t) : nullptr;
    if (!r)
        return;
    Tex art;
    art.tex = t;
    art.w = RwRasterGetWidth(r);
    art.h = RwRasterGetHeight(r);
    const float ux = H / 480.0f, uy = H / 448.0f;
    const float w = (sprite == SPR_BACK8 ? 300.0f : 256.0f) * ux, h = (sprite == SPR_BACK8 ? 200.0f : 256.0f) * uy;
    ui::Linear(true);
    ui::Image(art, { 0, 0, art.w, art.h }, W - w, 0.0f, W, h);
    ui::Linear(false);
}

void TiledDark(float y0, float y1, const GuiRect& tile, int alpha) {
    // menu_background is a dark see-through 16x16 tile: a plain dark veil does the same
    (void)tile;
    ui::Rect(0, y0, gF.W, y1, ui::Color(0, 0, 0, alpha));
}

// ---------------------------------------------------------------- what the buttons do
void PlayWorld(int slot) {
    SetNextWorld(slot + 1, false);
    SetSlot(slot);
    Switch(SCR_LOAD_FIRST_SAVE); // GTA loads the save from here on
}

void NewWorld() {
    SetNextWorld(0, true);
    CGame::bMissionPackGame = 0;
    FrontEndMenuManager.DoSettingsBeforeStartingAGame();
    DontDrawFrontEnd();
}

void DeleteWorld(int slot) {
    SetSlot(slot);
    Switch(SCR_DELETE_FINISHED); // GTA deletes the file and says so
}

// ---------------------------------------------------------------- pages
void TitlePage() {
    const float s = gF.s, cx = gF.W * 0.5f;
    GtaBackground(SPR_BACK8);
    // the title: "GTA SA" in the Minecraft logo's stone letters, 256x64 GUI pixels
    const float lw = 256.0f * s, lh = 64.0f * s, ly = 22.0f * s;
    ui::Linear(true);
    ui::Image(gMenuTex, MENU_LOGO, cx - lw * 0.5f, ly, cx + lw * 0.5f, ly + lh);
    ui::Linear(false);
    // splash text, like the yellow one next to Minecraft's logo
    ui::LabelCentered("Minecraft modu!", cx + 100.0f * s, ly + 58.0f * s, s, ui::Color(255, 255, 0));

    const float bw = 200.0f * s, x = cx - bw * 0.5f;
    float y = gF.H * 0.25f + 48.0f * s;
    if (Button(kStr[STR_SINGLEPLAYER], x, y, bw)) {
        PopulateSlots();
        gPage = PAGE_WORLDS;
        gSelected = -1;
        gScroll = 0;
    }
    y += 24.0f * s;
    if (Button(kStr[STR_OPTIONS], x, y, bw))
        Switch(SCR_OPTIONS);
    y += 24.0f * s;
    if (Button(kStr[STR_QUIT], x, y, bw))
        Switch(SCR_QUIT_ASK);
    ui::Label("GTA SA Minecraft 0.29", 2.0f * s, gF.H - 10.0f * s, s);
    const char* right = "F10: GTA menüsü";
    ui::Label(right, gF.W - ui::TextW(right, s) - 2.0f * s, gF.H - 10.0f * s, s, ui::Color(200, 200, 200));
}

void PausePage() {
    const float s = gF.s, cx = gF.W * 0.5f;
    GtaBackground(SPR_BACK8);
    ui::LabelCentered(kStr[STR_GAME_MENU], cx, 40.0f * s, s);
    const float bw = 204.0f * s, x = cx - bw * 0.5f, hw = 98.0f * s;
    float y = gF.H * 0.25f + 8.0f * s;
    if (Button(kStr[STR_RETURN_TO_GAME], x, y, bw))
        DontDrawFrontEnd();
    y += 24.0f * s;
    if (Button(kStr[STR_STATS], x, y, hw))
        Switch(SCR_STATS);
    if (Button("Harita", x + bw - hw, y, hw))
        Switch(SCR_MAP);
    y += 24.0f * s;
    if (Button(kStr[STR_OPTIONS], x, y, hw))
        Switch(SCR_OPTIONS);
    if (Button(kStr[STR_SELECT_WORLD], x + bw - hw, y, hw)) {
        PopulateSlots();
        gPage = PAGE_WORLDS;
        gSelected = -1;
        gScroll = 0;
    }
    y += 36.0f * s;
    if (Button(kStr[STR_QUIT], x, y, bw))
        Switch(SCR_QUIT_ASK);
}

void WorldsPage() {
    const float s = gF.s, cx = gF.W * 0.5f, W = gF.W, H = gF.H;
    GtaBackground(SPR_BACK2);
    const float top = 32.0f * s, bottom = H - 64.0f * s;
    TiledDark(top, bottom, MENU_LIST_BG, 110);
    ui::Image(gMenuTex, MENU_HEADER_SEP, 0, top - 2.0f * s, W, top);
    ui::Image(gMenuTex, MENU_FOOTER_SEP, 0, bottom, W, bottom + 2.0f * s);
    ui::LabelCentered(kStr[STR_SELECT_WORLD], cx, 12.0f * s, s);

    int slots[8], n = 0;
    for (int i = 0; i < 8; ++i)
        if (SlotFilled(i))
            slots[n++] = i;
    const float rowH = 36.0f * s, rowW = 270.0f * s, x0 = cx - rowW * 0.5f;
    const int visible = std::max(1, (int)((bottom - top - 4.0f * s) / rowH));
    // wheel and arrow keys
    if (CPad::NewMouseControllerState.wheelDown)
        ++gScroll;
    if (CPad::NewMouseControllerState.wheelUp)
        --gScroll;
    int selIndex = -1;
    for (int i = 0; i < n; ++i)
        if (slots[i] == gSelected)
            selIndex = i;
    if (n > 0 && KeyPressed(VK_DOWN))
        selIndex = std::min(n - 1, selIndex + 1);
    if (n > 0 && KeyPressed(VK_UP))
        selIndex = std::max(0, selIndex - 1);
    if (selIndex >= 0) {
        gSelected = slots[selIndex];
        if (KeyPressed(VK_DOWN) || KeyPressed(VK_UP)) {
            if (selIndex < gScroll)
                gScroll = selIndex;
            if (selIndex >= gScroll + visible)
                gScroll = selIndex - visible + 1;
        }
    }
    gScroll = std::clamp(gScroll, 0, std::max(0, n - visible));

    bool play = false;
    for (int v = 0; v < visible && gScroll + v < n; ++v) {
        const int slot = slots[gScroll + v];
        const float y = top + 4.0f * s + v * rowH;
        const bool hover = gF.mx >= x0 && gF.mx < x0 + rowW && gF.my >= y && gF.my < y + rowH - 4.0f * s;
        if (slot == gSelected) {
            ui::Rect(x0 - 2.0f * s, y - 2.0f * s, x0 + rowW + 2.0f * s, y + 34.0f * s, ui::Color(128, 128, 128));
            ui::Rect(x0 - 1.0f * s, y - 1.0f * s, x0 + rowW + 1.0f * s, y + 33.0f * s, ui::Color(0, 0, 0));
        }
        ui::Image(gMenuTex, MENU_PACK, x0, y, x0 + 32.0f * s, y + 32.0f * s);
        if (hover) {
            ui::Rect(x0, y, x0 + 32.0f * s, y + 32.0f * s, ui::Color(160, 160, 160, 110));
            ui::Image(gMenuTex, gF.mx < x0 + 32.0f * s ? MENU_JOIN_HI : MENU_JOIN, x0, y, x0 + 32.0f * s, y + 32.0f * s);
        }
        char line[160];
        ui::Label(SlotName(slot).c_str(), x0 + 35.0f * s, y + 1.0f * s, s);
        snprintf(line, sizeof(line), "GTASAsf%d (%s)", slot + 1, SlotDate(slot).c_str());
        ui::Label(line, x0 + 35.0f * s, y + 12.0f * s, s, ui::Color(128, 128, 128), false);
        snprintf(line, sizeof(line), "%s%s", kStr[STR_SURVIVAL], WorldFileExists(slot + 1) ? ", Minecraft dünyası kayıtlı" : "");
        ui::Label(line, x0 + 35.0f * s, y + 22.0f * s, s, ui::Color(128, 128, 128), false);
        if (hover && gF.click) {
            const DWORD now = GetTickCount();
            if (gF.mx < x0 + 32.0f * s || (gLastRowClicked == slot && now - gLastRowClick < 450))
                play = true; // the arrow on the picture, or a double click
            gSelected = slot;
            gLastRowClicked = slot;
            gLastRowClick = now;
            PlaySfx(SND_CLICK, nullptr, 0.25f);
        }
    }
    if (n == 0)
        ui::LabelCentered("Kayıtlı dünya yok", cx, (top + bottom) * 0.5f - 4.0f * s, s, ui::Color(170, 170, 170));
    if (n > visible) {
        // scroll bar
        const float bx = x0 + rowW + 8.0f * s, bh = bottom - top;
        ui::Rect(bx, top, bx + 6.0f * s, bottom, ui::Color(0, 0, 0, 160));
        const float th = bh * visible / n, ty = top + (bh - th) * gScroll / std::max(1, n - visible);
        ui::Rect(bx, ty, bx + 6.0f * s, ty + th, ui::Color(192, 192, 192));
    }

    const float bw = 150.0f * s;
    const bool have = gSelected >= 0 && SlotFilled(gSelected);
    if (Button(kStr[STR_PLAY_WORLD], cx - 154.0f * s, H - 52.0f * s, bw, have) || (have && KeyPressed(VK_RETURN)))
        play = true;
    if (Button(kStr[STR_CREATE_WORLD], cx + 4.0f * s, H - 52.0f * s, bw))
        NewWorld();
    if (Button(kStr[STR_DELETE], cx - 154.0f * s, H - 28.0f * s, bw, have))
        gPage = PAGE_DELETE;
    if (Button(kStr[STR_CANCEL], cx + 4.0f * s, H - 28.0f * s, bw) || KeyPressed(VK_ESCAPE))
        gPage = PAGE_HOME;
    if (play && have)
        PlayWorld(gSelected);
}

void DeletePage() {
    const float s = gF.s, cx = gF.W * 0.5f;
    GtaBackground(SPR_BACK2);
    if (gSelected < 0 || !SlotFilled(gSelected)) {
        gPage = PAGE_WORLDS;
        return;
    }
    ui::LabelCentered(kStr[STR_DELETE_QUESTION], cx, gF.H * 0.25f, s);
    // "'%s' ..." from the language file
    char line[200];
    std::string fmt = kStr[STR_DELETE_WARNING];
    const size_t at = fmt.find("%s");
    if (at != std::string::npos)
        fmt.replace(at, 2, SlotName(gSelected));
    snprintf(line, sizeof(line), "%s", fmt.c_str());
    ui::LabelCentered(line, cx, gF.H * 0.25f + 16.0f * s, s);
    const float bw = 150.0f * s, y = gF.H * 0.25f + 60.0f * s;
    if (Button(kStr[STR_DELETE], cx - 154.0f * s, y, bw)) {
        const int slot = gSelected;
        gSelected = -1;
        gPage = PAGE_WORLDS;
        DeleteWorld(slot);
    }
    if (Button(kStr[STR_CANCEL], cx + 4.0f * s, y, bw) || KeyPressed(VK_ESCAPE))
        gPage = PAGE_WORLDS;
}

void DrawMenu() {
    int screen = Screen();
    // GTA's own "start game" pages are ours: back to the home screen with the world list open
    if (screen == SCR_START_GAME || screen == SCR_LOAD_GAME || screen == SCR_DELETE_GAME) {
        Switch(GameNotStarted() ? SCR_MAIN_MENU : SCR_PAUSE);
        screen = Screen();
        PopulateSlots();
        gPage = PAGE_WORLDS;
        gLastScreen = screen;
    }
    if (screen != gLastScreen) {
        gLastScreen = screen;
        gPage = PAGE_HOME;
        gMouseWasDown = true; // the click that brought us here is not for us
        gOpenedAt = GetTickCount();
    }
    PollKeys();
    if (KeyPressed(VK_F10)) {
        gFallback = true;
        return;
    }
    gF.W = (float)RsGlobal.maximumWidth;
    gF.H = (float)RsGlobal.maximumHeight;
    gF.s = (float)GuiScale();
    gF.mx = (float)std::clamp(MouseX(), 0, RsGlobal.maximumWidth);
    gF.my = (float)std::clamp(MouseY(), 0, RsGlobal.maximumHeight);
    const bool down = CPad::NewMouseControllerState.lmb != 0;
    gF.click = down && !gMouseWasDown && GetTickCount() - gOpenedAt > 250;
    gMouseWasDown = down;

    ui::Begin();
    switch (gPage) {
    case PAGE_WORLDS: WorldsPage(); break;
    case PAGE_DELETE: DeletePage(); break;
    default:
        if (screen == SCR_PAUSE)
            PausePage();
        else
            TitlePage();
        break;
    }
    ui::Cursor(gF.mx, gF.my, std::max(1.0f, gF.s * 0.5f));
    ui::End();
}

// ---------------------------------------------------------------- hooks
void __fastcall HookDrawBackground(void* self, void*) {
    if (Active()) {
        DrawMenu();
        if (!gFallback)
            return;
    } else {
        gLastScreen = -100;
    }
    gDrawHook.thiscall<void>(self);
}

void __fastcall HookUserInput(void* self, void*) {
    if (Active())
        return; // our screens take the mouse and the keys themselves (while they are drawn)
    gInputHook.thiscall<void>(self);
}

unsigned int __fastcall HookSaveSlot(void* self, void*, int slot) {
    const unsigned int err = gSaveHook.thiscall<unsigned int>(self, slot);
    if (err == 0)
        WorldSavedToSlot(slot + 1);
    return err;
}

bool __fastcall HookDeleteSlot(void* self, void*, int slot) {
    const bool ok = gDeleteHook.thiscall<bool>(self, slot);
    if (ok)
        WorldSlotDeleted(slot + 1);
    return ok;
}
} // namespace

void InstallMenuHooks() {
    static bool done = false;
    if (done)
        return;
    done = true;
    gSaveHook = safetyhook::create_inline(reinterpret_cast<void*>(0x619060), reinterpret_cast<void*>(&HookSaveSlot));
    gDeleteHook = safetyhook::create_inline(reinterpret_cast<void*>(0x6190D0), reinterpret_cast<void*>(&HookDeleteSlot));
    if (gConfig.minecraftMenu) {
        gDrawHook = safetyhook::create_inline(reinterpret_cast<void*>(0x57B750), reinterpret_cast<void*>(&HookDrawBackground));
        gInputHook = safetyhook::create_inline(reinterpret_cast<void*>(0x57FD70), reinterpret_cast<void*>(&HookUserInput));
    }
    Log("Hooks: save slot %s, delete slot %s, menu draw %s, menu input %s", gSaveHook ? "ok" : "FAILED",
        gDeleteHook ? "ok" : "FAILED", gConfig.minecraftMenu ? (gDrawHook ? "ok" : "FAILED") : "off",
        gConfig.minecraftMenu ? (gInputHook ? "ok" : "FAILED") : "off");
}

} // namespace mc
