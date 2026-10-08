#pragma once

#include "ModCommon.h"

struct RwTexture;
struct RwRaster;

namespace mc {

struct Tex {
    RwTexture* tex = nullptr;
    int w = 0, h = 0;
    RwRaster* Raster() const;
};

extern Tex gAtlasTex; // blocks, items, cracks
extern Tex gGuiTex;   // hotbar, windows, icons
extern Tex gFontTex;  // pixel font
extern Tex gEntityTex; // steve skin, elytra, arrow, shadow
extern Tex gMenuTex;   // title screen: panorama, logo, buttons (optional)

// alpha channel of atlas.png kept on the CPU (item sprites get their extruded edges from it)
extern std::vector<uint8_t> gAtlasAlpha;
bool AtlasPixelOpaque(int tile, int px, int py);

bool LoadTextures();
void UnloadTextures();

// atlas UVs for a 16px tile (small inset avoids bleeding with point sampling)
struct TileUV { float u0, v0, u1, v1; };
TileUV AtlasTileUV(int tile);

} // namespace mc
