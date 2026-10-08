#pragma once
// 2D drawing for the screens outside the HUD (title screen, world list, pause menu). Implemented in Gui.cpp.

#include "ModCommon.h"
#include "Textures.h"

namespace mc::ui {

void Begin();              // saves the render states and sets up 2D drawing
void End();                // draws what is left and restores the states
void Linear(bool on);      // smooth (photos) or pixel-sharp (Minecraft GUI) texture filtering
RwUInt32 Color(int r, int g, int b, int a = 255);
void Rect(float x0, float y0, float x1, float y1, RwUInt32 col);
// part `src` (pixels) of a texture stretched over a rectangle
void Image(const Tex& t, const GuiRect& src, float x0, float y0, float x1, float y1, RwUInt32 col = 0xFFFFFFFF);
float TextW(const char* text, float scale);
void Label(const char* text, float x, float y, float scale, RwUInt32 col = 0xFFFFFFFF, bool shadow = true);
void LabelCentered(const char* text, float cx, float y, float scale, RwUInt32 col = 0xFFFFFFFF, bool shadow = true);
void Cursor(float x, float y, float scale);

} // namespace mc::ui
