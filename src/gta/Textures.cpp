#include "Textures.h"

#include "Image.h"
#include "Other.h"
#include "RenderWare.h"

namespace mc {

Tex gAtlasTex, gGuiTex, gFontTex, gEntityTex, gMenuTex;

RwRaster* Tex::Raster() const { return tex ? RwTextureGetRaster(tex) : nullptr; }

std::vector<uint8_t> gAtlasAlpha;

bool AtlasPixelOpaque(int tile, int px, int py) {
    if (px < 0 || py < 0 || px > 15 || py > 15 || gAtlasAlpha.empty())
        return false;
    int x = (tile % ATLAS_TILES_PER_ROW) * 16 + px, y = (tile / ATLAS_TILES_PER_ROW) * 16 + py;
    if (y >= ATLAS_SIZE)
        return false;
    return gAtlasAlpha[(size_t)y * ATLAS_SIZE + x] > 100;
}

static bool LoadPng(const char* file, Tex& out, std::vector<uint8_t>* alphaOut = nullptr) {
    std::string path = ModPath(file);
    plugin::Image* img = nullptr;
    if (!plugin::CreateImageFromFile(path, img) || !img) {
        Log("ERROR: could not load %s", path.c_str());
        return false;
    }
    RwRaster* raster = RwRasterCreate(img->width, img->height, 0, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
    if (!raster) {
        Log("ERROR: RwRasterCreate failed for %s", file);
        img->Release();
        return false;
    }
    if (alphaOut) {
        alphaOut->resize((size_t)img->width * img->height);
        for (int i = 0; i < img->width * img->height; ++i)
            (*alphaOut)[i] = img->pixels[i * 4 + 3];
    }
    RwUInt8* dst = RwRasterLock(raster, 0, rwRASTERLOCKWRITE);
    if (dst) {
        int stride = raster->stride ? raster->stride : img->width * 4;
        for (int y = 0; y < img->height; ++y) {
            const uint8_t* src = img->pixels + y * img->width * 4;
            uint8_t* row = dst + y * stride;
            for (int x = 0; x < img->width; ++x) {
                row[x * 4 + 0] = src[x * 4 + 2]; // B
                row[x * 4 + 1] = src[x * 4 + 1]; // G
                row[x * 4 + 2] = src[x * 4 + 0]; // R
                row[x * 4 + 3] = src[x * 4 + 3]; // A
            }
        }
        RwRasterUnlock(raster);
    }
    out.tex = RwTextureCreate(raster);
    if (out.tex) {
        RwTextureSetFilterMode(out.tex, rwFILTERNEAREST);
        RwTextureSetAddressing(out.tex, rwTEXTUREADDRESSCLAMP);
    }
    out.w = img->width;
    out.h = img->height;
    img->Release();
    Log("Loaded %s (%dx%d)", file, out.w, out.h);
    return out.tex != nullptr;
}

bool LoadTextures() {
    bool ok = LoadPng("atlas.png", gAtlasTex, &gAtlasAlpha);
    ok &= LoadPng("gui.png", gGuiTex);
    ok &= LoadPng("font.png", gFontTex);
    ok &= LoadPng("entity.png", gEntityTex);
    LoadPng("menu.png", gMenuTex); // without it GTA's own menu stays
    return ok;
}

static void Free(Tex& t) {
    if (t.tex) {
        RwTextureDestroy(t.tex);
        t.tex = nullptr;
    }
}

void UnloadTextures() {
    Free(gAtlasTex);
    Free(gGuiTex);
    Free(gFontTex);
    Free(gEntityTex);
    Free(gMenuTex);
}

} // namespace mc
