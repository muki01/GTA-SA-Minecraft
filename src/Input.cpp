#include "Input.h"

#include "CPad.h"
#include "RenderWare.h"

#include "ModCommon.h"

namespace mc {

namespace {
bool gPrev[256] = {};
bool gNow[256] = {};

bool WindowFocused() {
    HWND w = RsGlobal.ps ? RsGlobal.ps->window : nullptr;
    return w && GetForegroundWindow() == w;
}
} // namespace

void PollKeys() {
    bool focus = WindowFocused();
    for (int k = 1; k < 256; ++k) {
        gPrev[k] = gNow[k];
        gNow[k] = focus && (GetAsyncKeyState(k) & 0x8000) != 0;
    }
}

bool KeyPressed(int vk) { return vk > 0 && vk < 256 && gNow[vk] && !gPrev[vk]; }
bool KeyDown(int vk) { return vk > 0 && vk < 256 && gNow[vk]; }

bool MouseLeft() { return CPad::NewMouseControllerState.lmb != 0; }
bool MouseLeftPressed() { return CPad::NewMouseControllerState.lmb && !CPad::OldMouseControllerState.lmb; }
bool MouseRight() { return CPad::NewMouseControllerState.rmb != 0; }
bool MouseRightPressed() { return CPad::NewMouseControllerState.rmb && !CPad::OldMouseControllerState.rmb; }
bool MouseRightReleased() { return !CPad::NewMouseControllerState.rmb && CPad::OldMouseControllerState.rmb; }

} // namespace mc
