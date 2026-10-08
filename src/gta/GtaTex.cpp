#include "GtaTex.h"

#include <d3d9.h>

#include <string>
#include <unordered_map>

#include "CEntity.h"
#include "RenderWare.h"
#include "common.h"

#include "Items.h"

namespace mc {

namespace {
// ---------------------------------------------------------------- block palettes
const uint16_t kPlanks[] = { ID_OAK_PLANKS, ID_SPRUCE_PLANKS, ID_BIRCH_PLANKS, ID_JUNGLE_PLANKS, ID_ACACIA_PLANKS,
                             ID_DARK_OAK_PLANKS, ID_MANGROVE_PLANKS, ID_CHERRY_PLANKS, ID_BAMBOO_PLANKS, ID_PALE_OAK_PLANKS };
const uint16_t kLogs[] = { ID_OAK_LOG, ID_SPRUCE_LOG, ID_BIRCH_LOG, ID_JUNGLE_LOG, ID_ACACIA_LOG, ID_DARK_OAK_LOG };
const uint16_t kBricks[] = { ID_BRICKS, ID_STONE_BRICKS, ID_NETHER_BRICKS, ID_RED_NETHER_BRICKS, ID_DEEPSLATE_BRICKS,
                             ID_END_STONE_BRICKS, ID_QUARTZ_BRICKS, ID_POLISHED_BLACKSTONE_BRICKS, ID_PRISMARINE_BRICKS,
                             ID_TUFF_BRICKS, ID_RESIN_BRICKS, ID_GRANITE, ID_TERRACOTTA, ID_BROWN_TERRACOTTA };
const uint16_t kConcrete[] = { ID_WHITE_CONCRETE, ID_ORANGE_CONCRETE, ID_MAGENTA_CONCRETE, ID_LIGHT_BLUE_CONCRETE,
                               ID_YELLOW_CONCRETE, ID_LIME_CONCRETE, ID_PINK_CONCRETE, ID_GRAY_CONCRETE,
                               ID_LIGHT_GRAY_CONCRETE, ID_CYAN_CONCRETE, ID_PURPLE_CONCRETE, ID_BLUE_CONCRETE,
                               ID_BROWN_CONCRETE, ID_GREEN_CONCRETE, ID_RED_CONCRETE, ID_BLACK_CONCRETE };
const uint16_t kWool[] = { ID_WHITE_WOOL, ID_ORANGE_WOOL, ID_MAGENTA_WOOL, ID_LIGHT_BLUE_WOOL, ID_YELLOW_WOOL, ID_LIME_WOOL,
                           ID_PINK_WOOL, ID_GRAY_WOOL, ID_LIGHT_GRAY_WOOL, ID_CYAN_WOOL, ID_PURPLE_WOOL, ID_BLUE_WOOL,
                           ID_BROWN_WOOL, ID_GREEN_WOOL, ID_RED_WOOL, ID_BLACK_WOOL };
const uint16_t kTerracotta[] = { ID_TERRACOTTA, ID_WHITE_TERRACOTTA, ID_ORANGE_TERRACOTTA, ID_MAGENTA_TERRACOTTA,
                                 ID_LIGHT_BLUE_TERRACOTTA, ID_YELLOW_TERRACOTTA, ID_LIME_TERRACOTTA, ID_PINK_TERRACOTTA,
                                 ID_GRAY_TERRACOTTA, ID_LIGHT_GRAY_TERRACOTTA, ID_CYAN_TERRACOTTA, ID_PURPLE_TERRACOTTA,
                                 ID_BLUE_TERRACOTTA, ID_BROWN_TERRACOTTA, ID_GREEN_TERRACOTTA, ID_RED_TERRACOTTA,
                                 ID_BLACK_TERRACOTTA };
const uint16_t kStones[] = { ID_STONE, ID_COBBLESTONE, ID_ANDESITE, ID_POLISHED_ANDESITE, ID_DIORITE, ID_POLISHED_DIORITE,
                             ID_GRANITE, ID_POLISHED_GRANITE, ID_DEEPSLATE, ID_POLISHED_DEEPSLATE, ID_TUFF, ID_CALCITE,
                             ID_SMOOTH_STONE, ID_BASALT, ID_BLACKSTONE, ID_SANDSTONE, ID_RED_SANDSTONE, ID_DRIPSTONE_BLOCK,
                             ID_QUARTZ_BLOCK, ID_END_STONE };
const uint16_t kMetal[] = { ID_IRON_BLOCK, ID_COPPER_BLOCK, ID_EXPOSED_COPPER, ID_WEATHERED_COPPER, ID_OXIDIZED_COPPER,
                            ID_NETHERITE_BLOCK, ID_GOLD_BLOCK, ID_POLISHED_DEEPSLATE, ID_LIGHT_GRAY_CONCRETE, ID_GRAY_CONCRETE,
                            ID_WHITE_CONCRETE, ID_SMOOTH_STONE, ID_RAW_IRON_BLOCK };
const uint16_t kGlass[] = { ID_GLASS, ID_WHITE_STAINED_GLASS, ID_LIGHT_BLUE_STAINED_GLASS, ID_CYAN_STAINED_GLASS,
                            ID_BLUE_STAINED_GLASS, ID_GRAY_STAINED_GLASS, ID_LIGHT_GRAY_STAINED_GLASS, ID_BLACK_STAINED_GLASS,
                            ID_BROWN_STAINED_GLASS, ID_GREEN_STAINED_GLASS };
const uint16_t kRoof[] = { ID_TERRACOTTA, ID_RED_TERRACOTTA, ID_ORANGE_TERRACOTTA, ID_BROWN_TERRACOTTA, ID_GRAY_TERRACOTTA,
                           ID_BLACK_TERRACOTTA, ID_LIGHT_GRAY_TERRACOTTA, ID_WHITE_TERRACOTTA, ID_CYAN_TERRACOTTA,
                           ID_GREEN_TERRACOTTA, ID_BLUE_TERRACOTTA, ID_BRICKS, ID_DEEPSLATE_TILES, ID_STONE_BRICKS,
                           ID_SPRUCE_PLANKS, ID_DARK_OAK_PLANKS, ID_GRAY_CONCRETE, ID_LIGHT_GRAY_CONCRETE, ID_RED_CONCRETE,
                           ID_BLACK_CONCRETE, ID_SMOOTH_STONE };
// plain walls: everything a house is plastered, painted or poured with
const uint16_t kGeneric[] = {
    ID_WHITE_CONCRETE, ID_ORANGE_CONCRETE, ID_MAGENTA_CONCRETE, ID_LIGHT_BLUE_CONCRETE, ID_YELLOW_CONCRETE, ID_LIME_CONCRETE,
    ID_PINK_CONCRETE, ID_GRAY_CONCRETE, ID_LIGHT_GRAY_CONCRETE, ID_CYAN_CONCRETE, ID_PURPLE_CONCRETE, ID_BLUE_CONCRETE,
    ID_BROWN_CONCRETE, ID_GREEN_CONCRETE, ID_RED_CONCRETE, ID_BLACK_CONCRETE,
    ID_TERRACOTTA, ID_WHITE_TERRACOTTA, ID_ORANGE_TERRACOTTA, ID_MAGENTA_TERRACOTTA, ID_LIGHT_BLUE_TERRACOTTA,
    ID_YELLOW_TERRACOTTA, ID_LIME_TERRACOTTA, ID_PINK_TERRACOTTA, ID_GRAY_TERRACOTTA, ID_LIGHT_GRAY_TERRACOTTA,
    ID_CYAN_TERRACOTTA, ID_PURPLE_TERRACOTTA, ID_BLUE_TERRACOTTA, ID_BROWN_TERRACOTTA, ID_GREEN_TERRACOTTA,
    ID_RED_TERRACOTTA, ID_BLACK_TERRACOTTA,
    ID_STONE, ID_SMOOTH_STONE, ID_ANDESITE, ID_POLISHED_ANDESITE, ID_DIORITE, ID_POLISHED_DIORITE, ID_CALCITE,
    ID_SANDSTONE, ID_SMOOTH_SANDSTONE, ID_RED_SANDSTONE, ID_SMOOTH_RED_SANDSTONE, ID_QUARTZ_BLOCK, ID_SMOOTH_QUARTZ,
    ID_DEEPSLATE, ID_POLISHED_DEEPSLATE, ID_BLACKSTONE, ID_POLISHED_BLACKSTONE, ID_TUFF, ID_POLISHED_TUFF,
    ID_END_STONE, ID_BONE_BLOCK, ID_PACKED_MUD, ID_CLAY, ID_DRIPSTONE_BLOCK
};

template <size_t N>
uint16_t Nearest(const uint16_t (&list)[N], int r, int g, int b) {
    return NearestBlock(list, (int)N, r, g, b);
}

bool Has(const std::string& s, std::initializer_list<const char*> words) {
    for (const char* w : words)
        if (s.find(w) != std::string::npos)
            return true;
    return false;
}

// ---------------------------------------------------------------- reading a texture's colour
struct RasterExt {
    IDirect3DTexture9* texture;
    PALETTEENTRY* palette; // _rwD3D9Palette: 256 entries first
};

struct Accum {
    double r = 0, g = 0, b = 0, a = 0;
    int n = 0;
    void Add(int R, int G, int B, int A) {
        const double w = A / 255.0;
        r += R * w;
        g += G * w;
        b += B * w;
        a += A;
        ++n;
    }
};

void From565(uint16_t c, int* out) {
    out[0] = ((c >> 11) & 31) * 255 / 31;
    out[1] = ((c >> 5) & 63) * 255 / 63;
    out[2] = (c & 31) * 255 / 31;
}

// one DXT colour block (8 bytes); alpha[16] may be null (DXT1: the punch-through index)
void DxtBlock(const uint8_t* p, bool dxt1, const int* alpha, Accum& acc) {
    const uint16_t c0 = (uint16_t)(p[0] | (p[1] << 8)), c1 = (uint16_t)(p[2] | (p[3] << 8));
    int col[4][3];
    From565(c0, col[0]);
    From565(c1, col[1]);
    const bool four = !dxt1 || c0 > c1;
    for (int k = 0; k < 3; ++k) {
        col[2][k] = four ? (2 * col[0][k] + col[1][k]) / 3 : (col[0][k] + col[1][k]) / 2;
        col[3][k] = four ? (col[0][k] + 2 * col[1][k]) / 3 : 0;
    }
    const uint32_t bits = (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
    for (int i = 0; i < 16; ++i) {
        const int idx = (bits >> (i * 2)) & 3;
        int a = alpha ? alpha[i] : 255;
        if (!four && idx == 3)
            a = 0;
        acc.Add(col[idx][0], col[idx][1], col[idx][2], a);
    }
}

bool SampleTexture(RwTexture* tex, TexInfo& info) {
    RwRaster* ras = tex->raster;
    if (!ras)
        return false;
    if (ras->parent && ras->parent != ras)
        ras = ras->parent;
    const int off = *reinterpret_cast<int*>(0xB4E9E0); // _RwD3D9RasterExtOffset
    if (off <= 0 || off > 256)
        return false;
    RasterExt* ext = reinterpret_cast<RasterExt*>(reinterpret_cast<uint8_t*>(ras) + off);
    IDirect3DTexture9* t = ext->texture;
    if (!t || t->GetType() != D3DRTYPE_TEXTURE)
        return false;
    const DWORD levels = t->GetLevelCount();
    if (levels == 0)
        return false;
    // a small mip level is the average already
    DWORD level = 0;
    D3DSURFACE_DESC d{};
    for (; level < levels; ++level) {
        if (FAILED(t->GetLevelDesc(level, &d)))
            return false;
        if (d.Width <= 8 || d.Height <= 8 || level + 1 == levels)
            break;
    }
    if (d.Width < 4 || d.Height < 4) {
        // DXT needs whole blocks: one level up if there is one
        if (level > 0) {
            --level;
            if (FAILED(t->GetLevelDesc(level, &d)))
                return false;
        }
    }
    D3DLOCKED_RECT lr{};
    if (FAILED(t->LockRect(level, &lr, nullptr, D3DLOCK_READONLY)) || !lr.pBits)
        return false;
    Accum acc;
    const uint8_t* base = static_cast<const uint8_t*>(lr.pBits);
    const int w = (int)d.Width, h = (int)d.Height;
    switch (d.Format) {
    case D3DFMT_DXT1:
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5: {
        const bool dxt1 = d.Format == D3DFMT_DXT1;
        const bool dxt3 = d.Format == D3DFMT_DXT2 || d.Format == D3DFMT_DXT3;
        const int bw = std::max(1, (w + 3) / 4), bh = std::max(1, (h + 3) / 4);
        const int sx = std::max(1, bw / 8), sy = std::max(1, bh / 8), size = dxt1 ? 8 : 16;
        for (int by = 0; by < bh; by += sy)
            for (int bx = 0; bx < bw; bx += sx) {
                const uint8_t* p = base + (size_t)by * lr.Pitch + (size_t)bx * size;
                if (dxt1) {
                    DxtBlock(p, true, nullptr, acc);
                    continue;
                }
                int alpha[16];
                if (dxt3) {
                    for (int i = 0; i < 16; ++i)
                        alpha[i] = ((p[i / 2] >> ((i & 1) * 4)) & 15) * 17;
                } else {
                    int a[8];
                    a[0] = p[0];
                    a[1] = p[1];
                    for (int i = 2; i < 8; ++i)
                        a[i] = a[0] > a[1] ? ((8 - i) * a[0] + (i - 1) * a[1]) / 7
                                           : (i < 6 ? ((6 - i) * a[0] + (i - 1) * a[1]) / 5 : (i == 6 ? 0 : 255));
                    uint64_t bits = 0;
                    for (int i = 0; i < 6; ++i)
                        bits |= (uint64_t)p[2 + i] << (8 * i);
                    for (int i = 0; i < 16; ++i)
                        alpha[i] = a[(bits >> (3 * i)) & 7];
                }
                DxtBlock(p + 8, false, alpha, acc);
            }
        break;
    }
    default: {
        const int sx = std::max(1, w / 16), sy = std::max(1, h / 16);
        for (int y = 0; y < h; y += sy)
            for (int x = 0; x < w; x += sx) {
                const uint8_t* row = base + (size_t)y * lr.Pitch;
                switch (d.Format) {
                case D3DFMT_A8R8G8B8: {
                    const uint8_t* p = row + x * 4;
                    acc.Add(p[2], p[1], p[0], p[3]);
                    break;
                }
                case D3DFMT_X8R8G8B8: {
                    const uint8_t* p = row + x * 4;
                    acc.Add(p[2], p[1], p[0], 255);
                    break;
                }
                case D3DFMT_R8G8B8: {
                    const uint8_t* p = row + x * 3;
                    acc.Add(p[2], p[1], p[0], 255);
                    break;
                }
                case D3DFMT_R5G6B5: {
                    int c[3];
                    From565(*reinterpret_cast<const uint16_t*>(row + x * 2), c);
                    acc.Add(c[0], c[1], c[2], 255);
                    break;
                }
                case D3DFMT_A1R5G5B5:
                case D3DFMT_X1R5G5B5: {
                    const uint16_t c = *reinterpret_cast<const uint16_t*>(row + x * 2);
                    acc.Add(((c >> 10) & 31) * 255 / 31, ((c >> 5) & 31) * 255 / 31, (c & 31) * 255 / 31,
                            d.Format == D3DFMT_X1R5G5B5 || (c & 0x8000) ? 255 : 0);
                    break;
                }
                case D3DFMT_A4R4G4B4:
                case D3DFMT_X4R4G4B4: {
                    const uint16_t c = *reinterpret_cast<const uint16_t*>(row + x * 2);
                    acc.Add(((c >> 8) & 15) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17,
                            d.Format == D3DFMT_X4R4G4B4 ? 255 : ((c >> 12) & 15) * 17);
                    break;
                }
                case D3DFMT_L8:
                    acc.Add(row[x], row[x], row[x], 255);
                    break;
                case D3DFMT_A8L8:
                    acc.Add(row[x * 2], row[x * 2], row[x * 2], row[x * 2 + 1]);
                    break;
                case D3DFMT_P8:
                    if (ext->palette) {
                        const PALETTEENTRY& e = ext->palette[row[x]];
                        acc.Add(e.peRed, e.peGreen, e.peBlue, e.peFlags);
                    }
                    break;
                default:
                    break;
                }
            }
        break;
    }
    }
    t->UnlockRect(level);
    if (acc.n == 0)
        return false;
    const double wsum = acc.a / 255.0;
    if (wsum > 1e-6) {
        info.r = (uint8_t)Clamp((float)(acc.r / wsum), 0.0f, 255.0f);
        info.g = (uint8_t)Clamp((float)(acc.g / wsum), 0.0f, 255.0f);
        info.b = (uint8_t)Clamp((float)(acc.b / wsum), 0.0f, 255.0f);
    }
    info.alpha = (uint8_t)Clamp((float)(acc.a / acc.n), 0.0f, 255.0f);
    info.hasColor = wsum > 1e-6;
    return true;
}

// ---------------------------------------------------------------- name + colour -> block
uint16_t Classify(const std::string& n, TexInfo& info) {
    const int r = info.r, g = info.g, b = info.b;
    // things that are painted on, hang in the air or glow: not blocks
    if (Has(n, { "shad", "decal", "graf", "tag_", "stain", "crack", "skid", "blood", "smoke", "cloud", "corona", "glow",
                 "flare", "light", "neon", "lamp", "particle", "water", "wave", "reflect", "mirror_fx" })) {
        info.skip = true;
        return 0;
    }
    const bool seeThrough = info.hasColor && info.alpha < 150;
    if (Has(n, { "glass", "window", "windo", "wind_", "_wind", "pane", "skylight", "glaz" }))
        return Nearest(kGlass, r, g, b);
    if (seeThrough) {
        // wire fences, railings, leaves on a card: too thin to be a wall
        if (Has(n, { "leaf", "leaves", "tree", "bush", "hedge", "palm", "plant", "foliage", "ivy" }))
            return ID_OAK_LEAVES;
        info.skip = true;
        return 0;
    }
    if (!info.hasColor) {
        // colour unknown: the name alone
        if (Has(n, { "brick" })) return ID_BRICKS;
        if (Has(n, { "wood", "plank", "board", "timber" })) return ID_OAK_PLANKS;
        if (Has(n, { "metal", "steel", "iron", "corr" })) return ID_IRON_BLOCK;
        return 0;
    }
    if (Has(n, { "brick" }))
        return Nearest(kBricks, r, g, b);
    if (Has(n, { "log", "bark", "trunk" }))
        return Nearest(kLogs, r, g, b);
    if (Has(n, { "wood", "plank", "board", "timber", "barn", "shack", "deck", "pier", "jetty", "crate", "pallet", "fence",
                 "shingle", "lumber", "panel" }))
        return Nearest(kPlanks, r, g, b);
    if (Has(n, { "leaf", "leaves", "hedge", "bush", "foliage", "ivy" }))
        return ID_OAK_LEAVES;
    if (Has(n, { "grass", "lawn", "turf" }))
        return ID_GRASS_BLOCK;
    if (Has(n, { "roof", "tile", "slate", "shngl" }))
        return Nearest(kRoof, r, g, b);
    if (Has(n, { "metal", "steel", "iron", "alum", "corr", "rust", "chrome", "tin_", "shutter", "garage", "girder", "pipe",
                 "tank", "vent", "grate", "grill", "duct", "contain", "silo", "scaff" }))
        return Nearest(kMetal, r, g, b);
    if (Has(n, { "marble" }))
        return (r + g + b) / 3 > 150 ? (uint16_t)ID_QUARTZ_BLOCK : Nearest(kStones, r, g, b);
    if (Has(n, { "stone", "rock", "cobble", "granite", "cliff", "boulder", "flag" }))
        return Nearest(kStones, r, g, b);
    if (Has(n, { "carpet", "cloth", "curtain", "canvas", "awning", "fabric", "rug", "tent", "tarp" }))
        return Nearest(kWool, r, g, b);
    if (Has(n, { "tarmac", "asphalt", "road", "street" }))
        return (r + g + b) / 3 < 70 ? (uint16_t)ID_BLACKSTONE : (uint16_t)ID_GRAY_CONCRETE;
    if (Has(n, { "sand", "beach", "dune" }))
        return r > g + 25 ? (uint16_t)ID_RED_SAND : (uint16_t)ID_SAND;
    if (Has(n, { "dirt", "mud", "soil", "earth" }))
        return ID_DIRT;
    if (Has(n, { "gravel", "pebble" }))
        return ID_GRAVEL;
    return Nearest(kGeneric, r, g, b);
}

std::unordered_map<std::string, TexInfo> gCache;
const TexInfo kNone;

// ---------------------------------------------------------------- render meshes
struct AtomicWalk {
    const TriangleFn* fn;
    bool any;
};

void WalkAtomic(RpAtomic* atomic, AtomicWalk& walk) {
    RpGeometry* geo = atomic ? atomic->geometry : nullptr;
    if (!geo || !geo->triangles || !geo->morphTarget || !geo->morphTarget->verts || geo->numTriangles <= 0)
        return;
    RwFrame* frame = RpAtomicGetFrame(atomic);
    if (!frame)
        return;
    const RwMatrix* m = RwFrameGetLTM(frame);
    if (!m)
        return;
    const RwV3d* verts = geo->morphTarget->verts;
    auto world = [&](const RwV3d& v) {
        return CVector(m->pos.x + m->right.x * v.x + m->up.x * v.y + m->at.x * v.z,
                       m->pos.y + m->right.y * v.x + m->up.y * v.y + m->at.y * v.z,
                       m->pos.z + m->right.z * v.x + m->up.z * v.y + m->at.z * v.z);
    };
    for (int i = 0; i < geo->numTriangles; ++i) {
        const RpTriangle& t = geo->triangles[i];
        if (t.vertIndex[0] >= geo->numVertices || t.vertIndex[1] >= geo->numVertices || t.vertIndex[2] >= geo->numVertices)
            continue;
        RpMaterial* mat = t.matIndex < geo->matList.numMaterials ? geo->matList.materials[t.matIndex] : nullptr;
        const uint32_t color = mat ? ((uint32_t)mat->color.alpha << 24) | ((uint32_t)mat->color.red << 16) |
                                         ((uint32_t)mat->color.green << 8) | mat->color.blue
                                   : 0xFFFFFFFFu;
        (*walk.fn)(world(verts[t.vertIndex[0]]), world(verts[t.vertIndex[1]]), world(verts[t.vertIndex[2]]),
                   mat ? mat->texture : nullptr, color);
        walk.any = true;
    }
}

RpAtomic* WalkAtomicCB(RpAtomic* atomic, void* data) {
    WalkAtomic(atomic, *static_cast<AtomicWalk*>(data));
    return atomic;
}
} // namespace

uint16_t NearestBlock(const uint16_t* blocks, int count, int r, int g, int b) {
    uint16_t best = count > 0 ? blocks[0] : (uint16_t)ID_STONE;
    int bestD = INT_MAX;
    for (int i = 0; i < count; ++i) {
        const uint32_t c = kBlockColor[blocks[i]];
        const int dr = (int)((c >> 16) & 255) - r, dg = (int)((c >> 8) & 255) - g, db = (int)(c & 255) - b;
        // the eye is most sensitive to green, least to blue
        const int d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
        if (d < bestD) {
            bestD = d;
            best = blocks[i];
        }
    }
    return best;
}

uint16_t ConcreteForColor(int r, int g, int b) { return Nearest(kConcrete, r, g, b); }
uint16_t PlanksForColor(int r, int g, int b) { return Nearest(kPlanks, r, g, b); }
uint16_t WoolForColor(int r, int g, int b) { return Nearest(kWool, r, g, b); }
uint16_t BlockForColor(int r, int g, int b) { return Nearest(kGeneric, r, g, b); }

const TexInfo& TextureInfo(RwTexture* tex) {
    if (!tex || !tex->name[0])
        return kNone;
    std::string name(tex->name, strnlen(tex->name, sizeof(tex->name)));
    for (auto& c : name)
        c = (char)tolower((unsigned char)c);
    auto it = gCache.find(name);
    if (it != gCache.end())
        return it->second;
    TexInfo info;
    SampleTexture(tex, info);
    info.block = Classify(name, info);
    if (gCache.size() > 20000)
        gCache.clear();
    static int logged = 0;
    if (logged < 60) {
        ++logged;
        Log("Texture '%s': colour %d,%d,%d alpha %d -> %s", name.c_str(), info.r, info.g, info.b, info.alpha,
            info.skip ? "(skipped)" : (info.block ? Block(info.block).key : "?"));
    }
    return gCache[name] = info;
}

bool ForEachTriangle(CEntity* e, const TriangleFn& fn) {
    if (!e || !e->m_pRwObject)
        return false;
    AtomicWalk walk{ &fn, false };
    RwObject* obj = e->m_pRwObject;
    if (obj->type == rpATOMIC)
        WalkAtomic(reinterpret_cast<RpAtomic*>(obj), walk);
    else if (obj->type == rpCLUMP)
        RpClumpForAllAtomics(reinterpret_cast<RpClump*>(obj), WalkAtomicCB, &walk);
    return walk.any;
}

RwTexture* TextureAtRay(CEntity* e, const CVector& origin, const CVector& dir, float maxDist, float nearDist) {
    RwTexture* best = nullptr;
    float bestErr = 1e9f;
    ForEachTriangle(e, [&](const CVector& a, const CVector& b, const CVector& c, RwTexture* tex, uint32_t) {
        // Moller-Trumbore
        const CVector e1 = b - a, e2 = c - a;
        const CVector p = CVector::Cross(dir, e2);
        const float det = e1.x * p.x + e1.y * p.y + e1.z * p.z;
        if (std::fabs(det) < 1e-9f)
            return;
        const float inv = 1.0f / det;
        const CVector s = origin - a;
        const float u = (s.x * p.x + s.y * p.y + s.z * p.z) * inv;
        if (u < -0.001f || u > 1.001f)
            return;
        const CVector q = CVector::Cross(s, e1);
        const float v = (dir.x * q.x + dir.y * q.y + dir.z * q.z) * inv;
        if (v < -0.001f || u + v > 1.001f)
            return;
        const float t = (e2.x * q.x + e2.y * q.y + e2.z * q.z) * inv;
        if (t < 0.0f || t > maxDist)
            return;
        if (tex && TextureInfo(tex).skip)
            return; // decals lie right on top of the real surface
        const float err = std::fabs(t - nearDist);
        if (err < bestErr) {
            bestErr = err;
            best = tex;
        }
    });
    return bestErr < 3.0f ? best : nullptr;
}

} // namespace mc
