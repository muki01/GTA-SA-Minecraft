#include "Input.h"

#include "CPad.h"
#include "RenderWare.h"

#include "Config.h"
#include "ModCommon.h"

namespace mc {

namespace {
bool gPrev[256] = {};
bool gNow[256] = {};

bool WindowFocused() {
    HWND w = RsGlobal.ps ? RsGlobal.ps->window : nullptr;
    return w && GetForegroundWindow() == w;
}

// ---- Minecraft's actions on this keyboard
int gBinding[ACT_COUNT] = {}; // Windows virtual key of every action
bool gBound = false;

// what a key of the core's list is called on Windows
int VkOf(Key k) {
    if (k >= KEY_0 && k <= KEY_9)
        return '0' + (k - KEY_0);
    if (k >= KEY_A && k <= KEY_Z)
        return 'A' + (k - KEY_A);
    if (k >= KEY_F1 && k <= KEY_F12)
        return VK_F1 + (k - KEY_F1);
    switch (k) {
    case KEY_MOUSE_LEFT: return VK_LBUTTON;
    case KEY_MOUSE_RIGHT: return VK_RBUTTON;
    case KEY_MOUSE_MIDDLE: return VK_MBUTTON;
    case KEY_SPACE: return VK_SPACE;
    case KEY_ESCAPE: return VK_ESCAPE;
    case KEY_ENTER: return VK_RETURN;
    case KEY_SHIFT: return VK_SHIFT;
    case KEY_LSHIFT: return VK_LSHIFT;
    case KEY_CTRL: return VK_CONTROL;
    case KEY_LCTRL: return VK_LCONTROL;
    default: return 0;
    }
}

// Minecraft's default keys, except where MinecraftSA.ini names another one
void BindActions() {
    for (int a = 0; a < ACT_COUNT; ++a)
        gBinding[a] = VkOf(kActions[a].key);
    const struct { Action action; int vk; } fromIni[] = {
        { ACT_INVENTORY, gConfig.keyInventory }, { ACT_DROP, gConfig.keyDrop },     { ACT_GAME_MODE, gConfig.keyGameMode },
        { ACT_PERSPECTIVE, gConfig.keyCamera },  { ACT_SWAP_HANDS, gConfig.keySwap },
    };
    for (const auto& b : fromIni)
        if (b.vk > 0 && b.vk < 256)
            gBinding[b.action] = b.vk;
}

void FillControls() {
    if (!gBound) {
        BindActions();
        gBound = true;
    }
    // the mouse is GTA's own reading of it
    const auto& mouse = CPad::NewMouseControllerState;
    const auto& before = CPad::OldMouseControllerState;
    for (int a = 0; a < ACT_COUNT; ++a) {
        const int vk = gBinding[a];
        bool down, pressed;
        switch (vk) {
        case VK_LBUTTON:
            down = mouse.lmb != 0;
            pressed = mouse.lmb && !before.lmb;
            break;
        case VK_RBUTTON:
            down = mouse.rmb != 0;
            pressed = mouse.rmb && !before.rmb;
            break;
        case VK_MBUTTON:
            down = mouse.mmb != 0;
            pressed = mouse.mmb && !before.mmb;
            break;
        default:
            down = vk > 0 && vk < 256 && gNow[vk];
            pressed = down && !gPrev[vk];
            break;
        }
        gControls.Set((Action)a, down, pressed);
    }
    gControls.scrollUp = mouse.wheelUp != 0;
    gControls.scrollDown = mouse.wheelDown != 0;
}
} // namespace

void PollKeys() {
    bool focus = WindowFocused();
    for (int k = 1; k < 256; ++k) {
        gPrev[k] = gNow[k];
        gNow[k] = focus && (GetAsyncKeyState(k) & 0x8000) != 0;
    }
    FillControls();
}

bool KeyPressed(int vk) { return vk > 0 && vk < 256 && gNow[vk] && !gPrev[vk]; }
} // namespace mc
