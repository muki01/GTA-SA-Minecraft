#include "GtaWorld.h"

#include <fstream>
#include <sstream>

#include "CColPoint.h"
#include "CEntity.h"
#include "CVehicle.h"
#include "CVehicleModelInfo.h"
#include "eSurfaceType.h"

#include "Entities.h"
#include "GtaTex.h"
#include "Inventory.h"
#include "Items.h"

namespace mc {

static std::vector<std::string> gNames;

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string Lower(std::string s) {
    for (auto& c : s)
        c = (char)tolower((unsigned char)c);
    return s;
}

static void ParseIde(const std::string& path) {
    std::ifstream f(path);
    if (!f)
        return;
    std::string line, section;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        std::string low = Lower(line);
        if (section.empty()) {
            if (low == "objs" || low == "tobj" || low == "anim")
                section = low;
            else if (low.find(',') == std::string::npos && low != "end")
                section = "other:" + low;
            continue;
        }
        if (low == "end") {
            section.clear();
            continue;
        }
        if (section.rfind("other:", 0) == 0)
            continue;
        size_t c1 = line.find(',');
        if (c1 == std::string::npos)
            continue;
        size_t c2 = line.find(',', c1 + 1);
        int id = atoi(line.substr(0, c1).c_str());
        std::string name = Trim(line.substr(c1 + 1, (c2 == std::string::npos ? line.size() : c2) - c1 - 1));
        if (id > 0 && id < 30000) {
            if ((int)gNames.size() <= id)
                gNames.resize(id + 1);
            gNames[id] = Lower(name);
        }
    }
}

void LoadGtaModelNames() {
    std::ifstream f(GameDir() + "\\data\\gta.dat");
    std::string line;
    int files = 0;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.size() > 4 && Lower(line.substr(0, 4)) == "ide ") {
            ParseIde(GameDir() + "\\" + Trim(line.substr(4)));
            files++;
        }
    }
    int named = 0;
    for (auto& n : gNames)
        if (!n.empty())
            named++;
    Log("Model names: %d IDE files, %d names", files, named);
}

const char* GtaModelName(int modelId) {
    if (modelId < 0 || modelId >= (int)gNames.size())
        return "";
    return gNames[modelId].c_str();
}

static bool Has(const std::string& s, const char* const* words) {
    for (; *words; ++words)
        if (s.find(*words) != std::string::npos)
            return true;
    return false;
}

bool IsPlantModel(int modelId) {
    static std::vector<int8_t> cache;
    if (modelId < 0)
        return false;
    if ((int)cache.size() <= modelId)
        cache.resize(modelId + 1, -1);
    int8_t& c = cache[modelId];
    if (c < 0) {
        static const char* const kPlant[] = { "pine", "palm", "fir_", "_fir", "cedar", "birch", "cypress", "redwood",
                                              "willow", "trunk", "joshua", "bush", "shrub", "hedge", "fern", "flower",
                                              "cactus", "veg_", "weed", "reed", nullptr };
        const std::string n = GtaModelName(modelId);
        bool plant = Has(n, kPlant);
        // "tree", but not the "tree" of "street"
        for (size_t at = n.find("tree"); !plant && at != std::string::npos; at = n.find("tree", at + 1))
            plant = at == 0 || n[at - 1] != 's';
        c = plant ? 1 : 0;
    }
    return c == 1;
}

// ---------------------------------------------------------------- materials
namespace {
GtaMaterial M(uint16_t block, uint8_t kind = GM_BLOCK, float hardness = -2.0f) {
    GtaMaterial m;
    m.block = block;
    m.kind = kind;
    m.hardness = hardness;
    return m;
}

GtaMaterial ForSurface(int s) {
    switch (s) {
    // grass
    case SURFACE_GRASS_SHORT_LUSH: case SURFACE_GRASS_MEDIUM_LUSH: case SURFACE_GRASS_LONG_LUSH:
    case SURFACE_GRASS_SHORT_DRY: case SURFACE_GRASS_MEDIUM_DRY: case SURFACE_GRASS_LONG_DRY:
    case SURFACE_GOLFGRASS_ROUGH: case SURFACE_GOLFGRASS_SMOOTH: case SURFACE_STEEP_SLIDYGRASS:
    case SURFACE_FLOWERBED: case SURFACE_MEADOW:
    case SURFACE_P_GRASS_SHORT: case SURFACE_P_GRASS_MEADOW: case SURFACE_P_GRASS_DRY:
    case SURFACE_P_WOODLAND: case SURFACE_P_WOODDENSE: case SURFACE_P_FLOWERBED:
    case SURFACE_P_BUSHY: case SURFACE_P_BUSHYMIX: case SURFACE_P_BUSHYDRY: case SURFACE_P_BUSHYMID:
    case SURFACE_P_GRASSWEEFLOWERS: case SURFACE_P_GRASSDRYTALL: case SURFACE_P_GRASSLUSHTALL:
    case SURFACE_P_GRASSGRNMIX: case SURFACE_P_GRASSBRNMIX: case SURFACE_P_GRASSLOW:
    case SURFACE_P_GRASSROCKY: case SURFACE_P_GRASSSMALLTREES: case SURFACE_P_GRASSWEEDS:
    case SURFACE_P_SPARSEFLOWERS: case SURFACE_P_AIRPORTGRND:
    case SURFACE_P_GRASSLIGHT: case SURFACE_P_GRASSLIGHTER: case SURFACE_P_GRASSLIGHTER2:
    case SURFACE_P_GRASSMID1: case SURFACE_P_GRASSMID2: case SURFACE_P_GRASSDARK:
    case SURFACE_P_GRASSDARK2: case SURFACE_P_GRASSDIRTMIX: case SURFACE_PARKGRASS:
        return M(ID_GRASS_BLOCK, GM_GRASS);
    case SURFACE_CORNFIELD: case SURFACE_P_CORNFIELD:
        return M(ID_HAY_BLOCK, GM_CROP, 0.4f);
    case SURFACE_VEGETATION: case SURFACE_HEDGE:
        return M(ID_OAK_LEAVES, GM_LEAVES);
    // dirt / mud
    case SURFACE_WASTEGROUND: case SURFACE_WOODLANDGROUND: case SURFACE_DIRT: case SURFACE_DIRTTRACK:
    case SURFACE_P_ROADSIDE: case SURFACE_P_ROADSIDEDES: case SURFACE_P_WASTEGROUND: case SURFACE_P_DIRTROCKY:
    case SURFACE_P_DIRTWEEDS: case SURFACE_P_FORESTSTUMPS: case SURFACE_P_FORESTSTICKS: case SURFACE_P_FORRESTLEAVES:
    case SURFACE_P_FORRESTDRY:
        return M(ID_DIRT);
    case SURFACE_MUD_WET: case SURFACE_MUD_DRY: case SURFACE_P_MARSH:
        return M(ID_MUD);
    // sand
    case SURFACE_SAND_DEEP: case SURFACE_SAND_MEDIUM: case SURFACE_SAND_COMPACT: case SURFACE_SAND_MORE:
    case SURFACE_SAND_BEACH: case SURFACE_P_SAND: case SURFACE_P_SAND_DENSE: case SURFACE_P_SAND_COMPACT:
    case SURFACE_P_SANDBEACH: case SURFACE_P_UNDERWATERLUSH: case SURFACE_P_UNDERWATERBARREN:
    case SURFACE_P_UNDERWATERCORAL: case SURFACE_P_UNDERWATERDEEP: case SURFACE_P_SEAWEED: case SURFACE_P_RIVEREDGE:
    case SURFACE_P_CACTUSDENSE:
        return M(ID_SAND);
    case SURFACE_SAND_ARID: case SURFACE_P_SAND_ARID: case SURFACE_P_SAND_ROCKY:
        return M(ID_RED_SAND);
    case SURFACE_P_DESERTROCKS:
        return M(ID_TERRACOTTA);
    // river beds hide clay, like in Minecraft
    case SURFACE_WATER_RIVERBED: case SURFACE_WATER_SHALLOW: case SURFACE_P_RIVERBED: case SURFACE_P_RIVERBEDSTONE:
    case SURFACE_P_RIVERBEDSHALLOW: case SURFACE_P_RIVERBEDWEEDS:
        return M(ID_CLAY);
    case SURFACE_GRAVEL: case SURFACE_P_RUBBLE:
        return M(ID_GRAVEL);
    case SURFACE_RAILTRACK:
        return M(ID_GRAVEL, GM_RAIL);
    // roads, pavements, concrete
    case SURFACE_TARMAC: case SURFACE_TARMAC_FUCKED: case SURFACE_TARMAC_REALLYFUCKED:
        return M(ID_BLACKSTONE);
    case SURFACE_PAVEMENT: case SURFACE_PAVEMENT_FUCKED: case SURFACE_STAIRSSTONE:
        return M(ID_STONE_BRICKS);
    case SURFACE_FUCKED_CONCRETE: case SURFACE_PAINTED_GROUND: case SURFACE_CONCRETE_BEACH: case SURFACE_P_CONCRETE:
    case SURFACE_P_BUILDINGSITE: case SURFACE_P_DOCKLANDS: case SURFACE_P_INDUSTRIAL: case SURFACE_P_INDUSTJETTY:
    case SURFACE_P_CONCRETELITTER: case SURFACE_P_ALLEYRUBISH: case SURFACE_P_POOLSIDE: case SURFACE_FLOORCONCRETE:
        return M(ID_LIGHT_GRAY_CONCRETE);
    case SURFACE_P_KIRCHENFLOOR: case SURFACE_P_711FLOOR: case SURFACE_P_FASTFOODFLOOR:
        return M(ID_QUARTZ_BLOCK);
    case SURFACE_P_SKANKYFLOOR:
        return M(ID_WHITE_TERRACOTTA);
    // natural rock (and anything unknown on buildings)
    case SURFACE_DEFAULT: case SURFACE_ROCK_DRY: case SURFACE_ROCK_WET: case SURFACE_ROCK_CLIFF:
    case SURFACE_STEEP_CLIFF: case SURFACE_P_MOUNTAIN: case SURFACE_TRANSPARENT_STONE:
        return M(ID_STONE, GM_ROCK);
    // wood
    case SURFACE_WOOD_CRATES: case SURFACE_WOOD_SOLID: case SURFACE_WOOD_THIN: case SURFACE_WOOD_BENCH:
    case SURFACE_FLOORBOARD: case SURFACE_STAIRSWOOD: case SURFACE_P_BEDROOMFLOOR: case SURFACE_P_LIVINGRMFLOOR:
    case SURFACE_P_CORRIDORFLOOR: case SURFACE_DOOR:
        return M(ID_OAK_PLANKS);
    case SURFACE_WOOD_PICKET_FENCE: case SURFACE_WOOD_SLATTED_FENCE: case SURFACE_WOOD_RANCH_FENCE:
        return M(ID_OAK_PLANKS, GM_WOOD_FENCE, 1.0f);
    // furniture
    case SURFACE_P_OFFICEDESK:
        return M(ID_OAK_PLANKS, GM_DESK, 1.0f);
    case SURFACE_P_711SHELF1: case SURFACE_P_711SHELF2: case SURFACE_P_711SHELF3:
        return M(ID_OAK_PLANKS, GM_SHOP, 1.0f);
    case SURFACE_P_RESTUARANTTABLE: case SURFACE_P_BARTABLE:
        return M(ID_OAK_PLANKS, GM_TABLE, 1.0f);
    // glass
    case SURFACE_GLASS: case SURFACE_GLASS_WINDOWS_LARGE: case SURFACE_GLASS_WINDOWS_SMALL:
    case SURFACE_UNBREAKABLE_GLASS:
        return M(ID_GLASS, GM_GLASS);
    // cloth
    case SURFACE_CARPET: case SURFACE_STAIRSCARPET: case SURFACE_TRANSPARENT_CLOTH:
        return M(ID_WHITE_WOOL);
    case SURFACE_HAY_BALE:
        return M(ID_HAY_BLOCK);
    // metal
    case SURFACE_GARAGE_DOOR: case SURFACE_THICK_METAL_PLATE: case SURFACE_SCAFFOLD_POLE: case SURFACE_METAL_GATE:
    case SURFACE_METAL_CHAIN_FENCE: case SURFACE_GIRDER: case SURFACE_SURFACE_FIRE_HYDRANT: case SURFACE_CONTAINER:
    case SURFACE_WHEELBASE: case SURFACE_CAR: case SURFACE_CAR_PANEL: case SURFACE_CAR_MOVINGCOMPONENT:
    case SURFACE_STAIRSMETAL: case SURFACE_FLOORMETAL: case SURFACE_THIN_METAL_SHEET: case SURFACE_METAL_BARREL:
    case SURFACE_METAL_DUMPSTER: case SURFACE_P_JUNKYARDPILES: case SURFACE_P_JUNKYARDGRND:
        return M(ID_IRON_BLOCK, GM_METAL);
    case SURFACE_LAMP_POST:
        return M(ID_IRON_BLOCK, GM_LAMP);
    case SURFACE_NEWS_VENDOR:
        return M(ID_IRON_BLOCK, GM_PAPER, 2.0f);
    case SURFACE_CARDBOARDBOX:
        return M(ID_OAK_PLANKS, GM_PAPER, 0.6f);
    case SURFACE_BIN_BAG: case SURFACE_P_DUMP:
        return M(ID_BLACK_CONCRETE_POWDER, GM_TRASH, 0.4f);
    // plastic and rubber
    case SURFACE_RUBBER:
        return M(ID_BLACK_CONCRETE, GM_RUBBER, 0.8f);
    case SURFACE_PLASTIC: case SURFACE_PLASTICBARRIER:
        return M(ID_WHITE_CONCRETE, GM_BLOCK, 1.0f);
    case SURFACE_PLASTIC_CONE:
        return M(ID_ORANGE_CONCRETE, GM_BLOCK, 0.6f);
    case SURFACE_PLASTIC_DUMPSTER:
        return M(ID_GREEN_CONCRETE, GM_BLOCK, 1.0f);
    case SURFACE_GORE:
        return M(ID_NETHERRACK, GM_GORE, 0.4f);
    default:
        return M(ID_AIR, GM_NONE); // peds, water...
    }
}

uint16_t LogFor(const std::string& n) {
    static const char* const kPalm[] = { "palm", nullptr };
    static const char* const kSpruce[] = { "pine", "fir_", "_fir", "cedar", "redwood", "spruce", nullptr };
    static const char* const kBirch[] = { "birch", "aspen", nullptr };
    static const char* const kAcacia[] = { "joshua", "desert", "cact", "yucca", nullptr };
    if (Has(n, kPalm))
        return ID_JUNGLE_LOG;
    if (Has(n, kSpruce))
        return ID_SPRUCE_LOG;
    if (Has(n, kBirch))
        return ID_BIRCH_LOG;
    if (Has(n, kAcacia))
        return ID_ACACIA_LOG;
    return ID_OAK_LOG;
}

uint16_t LeavesFor(const std::string& n) {
    uint16_t log = LogFor(n);
    return log == ID_JUNGLE_LOG ? ID_JUNGLE_LEAVES
         : log == ID_SPRUCE_LOG ? ID_SPRUCE_LEAVES
         : log == ID_BIRCH_LOG  ? ID_BIRCH_LEAVES
         : log == ID_ACACIA_LOG ? ID_ACACIA_LEAVES
                                : ID_OAK_LEAVES;
}

// buildings and props whose name tells more than their collision surface
bool ForName(const std::string& n, bool isObject, GtaMaterial& out) {
    static const char* const kTree[] = { "tree", "pine", "palm", "fir_", "_fir", "oak", "cedar", "birch",
                                         "cypress", "redwood", "willow", "trunk", "joshua", "log_", nullptr };
    static const char* const kBush[] = { "bush", "shrub", "hedge", "fern", "plant", "flower", "cactus", "veg_",
                                         "weed", "grass", nullptr };
    static const char* const kVend[] = { "vend", "snack", "soda", "burg", "pizza", "food", "donut", "cluck", nullptr };
    static const char* const kLamp[] = { "lamp", "light", "streetlght", "trafficlight", "neon", nullptr };
    static const char* const kTrash[] = { "bin_", "_bin", "dump", "trash", "rubbish", "litter", "bag", nullptr };
    static const char* const kTyre[] = { "tyre", "tire", "wheel", nullptr };
    static const char* const kBrick[] = { "brick", nullptr };
    static const char* const kWood[] = { "wood", "shack", "hut", "barn", "cabin", "pier", "jetty", "boardwalk",
                                         "plank", "crate", "pallet", "bench", "fence", "shed", nullptr };
    static const char* const kMetal[] = { "metal", "steel", "tank", "crane", "contain", "pipe", "girder", "silo",
                                          "iron", "barrel", "scaff", "pylon", "hydrant", "phone", "post", nullptr };
    static const char* const kRock[] = { "rock", "cliff", "boulder", "cave", "quarry", "stone", "mount", nullptr };
    static const char* const kPaper[] = { "box", "card", "paper", "news", nullptr };
    static const char* const kCone[] = { "cone", nullptr };
    if (Has(n, kTree)) {
        out = M(LogFor(n));
        return true;
    }
    if (Has(n, kBush)) {
        out = M(LeavesFor(n), GM_LEAVES);
        return true;
    }
    if (!isObject) {
        if (Has(n, kBrick)) {
            out = M(ID_BRICKS);
            return true;
        }
        if (Has(n, kRock)) {
            out = M(ID_STONE, GM_ROCK);
            return true;
        }
        return false;
    }
    // props
    if (Has(n, kVend)) {
        out = M(ID_IRON_BLOCK, GM_SHOP, 2.0f);
        return true;
    }
    if (Has(n, kLamp)) {
        out = M(ID_IRON_BLOCK, GM_LAMP);
        return true;
    }
    if (Has(n, kTrash)) {
        out = M(ID_BLACK_CONCRETE_POWDER, GM_TRASH, 0.6f);
        return true;
    }
    if (Has(n, kTyre)) {
        out = M(ID_BLACK_CONCRETE, GM_RUBBER, 0.8f);
        return true;
    }
    if (Has(n, kCone)) {
        out = M(ID_ORANGE_CONCRETE, GM_BLOCK, 0.6f);
        return true;
    }
    if (Has(n, kPaper)) {
        out = M(ID_OAK_PLANKS, GM_PAPER, 0.6f);
        return true;
    }
    if (Has(n, kWood)) {
        out = M(ID_OAK_PLANKS, Has(n, kTrash) ? GM_TRASH : GM_WOOD_FENCE, 1.2f);
        return true;
    }
    if (Has(n, kMetal)) {
        out = M(ID_IRON_BLOCK, GM_METAL);
        return true;
    }
    return false;
}

void Drop(const CVector& at, uint16_t id, int lo, int hi) {
    int n = lo + (hi > lo ? rand() % (hi - lo + 1) : 0);
    if (n > 0 && IsValidItem(id))
        SpawnDropItem(at, id, n);
}

void DropOne(const CVector& at, std::initializer_list<uint16_t> ids) {
    if (ids.size() == 0)
        return;
    uint16_t id = *(ids.begin() + rand() % ids.size());
    Drop(at, id, 1, 1);
}
} // namespace

GtaMaterial MaterialForSurface(int surface) { return ForSurface(surface); }

GtaMaterial MaterialFor(const CColPoint& cp, CEntity* entity) {
    const GtaMaterial surface = ForSurface(HitSurface(cp));
    if (entity) {
        const bool isObject = entity->m_nType == ENTITY_TYPE_OBJECT;
        if (surface.kind == GM_GLASS)
            return surface; // windows stay glass whatever the building is called
        std::string name = GtaModelName(entity->m_nModelIndex);
        GtaMaterial byName;
        if (!name.empty() && ForName(name, isObject, byName)) {
            // a named building keeps its real ground surface (grass on a "hill" model is still grass)
            if (!isObject && surface.kind != GM_ROCK && byName.kind == GM_ROCK)
                return surface;
            if (!isObject && byName.block == ID_BRICKS && surface.block != ID_STONE &&
                surface.block != ID_LIGHT_GRAY_CONCRETE)
                return surface;
            return byName;
        }
    }
    return surface;
}

GtaMaterial MaterialForTarget(const CColPoint& cp, CEntity* ent, const CVector& origin, const CVector& dir) {
    const GtaMaterial base = MaterialFor(cp, ent);
    if (!ent || (ent->m_nType != ENTITY_TYPE_BUILDING && ent->m_nType != ENTITY_TYPE_OBJECT) || !ent->m_pRwObject)
        return base;
    // what the collision surface cannot tell apart: walls, floors, pavements, plain "default" ground
    const int surface = HitSurface(cp);
    bool refine = false;
    if (base.kind == GM_BLOCK)
        refine = base.block == ID_STONE_BRICKS || base.block == ID_LIGHT_GRAY_CONCRETE || base.block == ID_QUARTZ_BLOCK ||
                 base.block == ID_WHITE_TERRACOTTA || base.block == ID_OAK_PLANKS || base.block == ID_WHITE_WOOL ||
                 base.block == ID_BRICKS || base.block == ID_STONE;
    else if (base.kind == GM_ROCK)
        refine = surface == SURFACE_DEFAULT;
    else if (base.kind == GM_WOOD_FENCE)
        refine = true;
    if (!refine)
        return base;
    // looking at the same spot: same answer
    static CEntity* lastEnt = nullptr;
    static Int3 lastCell;
    static int lastSurface = -1;
    static GtaMaterial last;
    const Int3 cell{ FloorI(cp.m_vecPoint.x * 2.0f), FloorI(cp.m_vecPoint.y * 2.0f), FloorI(cp.m_vecPoint.z * 2.0f) };
    if (ent == lastEnt && cell == lastCell && surface == lastSurface)
        return last;
    lastEnt = ent;
    lastCell = cell;
    lastSurface = surface;
    last = base;
    const float dist = (cp.m_vecPoint - origin).Magnitude();
    RwTexture* tex = TextureAtRay(ent, origin, dir, dist + 2.5f, dist);
    if (!tex)
        return last;
    const TexInfo& ti = TextureInfo(tex);
    if (!ti.block || !IsSolidBlock(ti.block))
        return last;
    last.block = ti.block;
    if (base.kind == GM_ROCK)
        last.kind = GM_BLOCK; // a wall, not the mountain: the block itself, no ores
    // wooden and cloth surfaces keep their kind, in the texture's colour
    if (ti.hasColor) {
        if (base.block == ID_OAK_PLANKS)
            last.block = PlanksForColor(ti.r, ti.g, ti.b);
        else if (base.block == ID_WHITE_WOOL)
            last.block = WoolForColor(ti.r, ti.g, ti.b);
    }
    return last;
}

GtaMaterial MaterialForVehicle(CVehicle* veh) {
    if (!veh)
        return M(ID_AIR, GM_NONE);
    switch (veh->m_nVehicleSubClass) {
    case VEHICLE_BMX:
        return M(ID_IRON_BLOCK, GM_VEHICLE, 1.5f);
    case VEHICLE_BIKE: case VEHICLE_QUAD:
        return M(ID_IRON_BLOCK, GM_VEHICLE, 3.0f);
    case VEHICLE_BOAT:
        return M(ID_OAK_PLANKS, GM_VEHICLE, 3.0f);
    case VEHICLE_HELI: case VEHICLE_PLANE:
        return M(ID_IRON_BLOCK, GM_VEHICLE, 8.0f);
    case VEHICLE_TRAIN:
        return M(ID_AIR, GM_NONE);
    default:
        return M(ID_IRON_BLOCK, GM_VEHICLE, 5.0f);
    }
}

int VirtualBlockFor(const CColPoint& cp, CEntity* entity) { return MaterialFor(cp, entity).block; }

void SpawnMaterialDrops(const GtaMaterial& m, const CVector& at, int tier, CVehicle* veh) {
    const float r = Rand01();
    switch (m.kind) {
    case GM_BLOCK:
        SpawnBlockDrops(m.block, at);
        break;
    case GM_ROCK: {
        // the GTA underground hides ores
        struct Ore { float p; int tier; uint16_t id; int min, max; };
        const Ore ores[] = { { 0.003f, TIER_IRON, ID_DIAMOND, 1, 1 }, { 0.006f, TIER_IRON, ID_EMERALD, 1, 1 },
                             { 0.016f, TIER_IRON, ID_RAW_GOLD, 1, 1 }, { 0.026f, TIER_IRON, ID_REDSTONE, 4, 5 },
                             { 0.036f, TIER_STONE, ID_LAPIS_LAZULI, 4, 8 }, { 0.066f, TIER_STONE, ID_RAW_IRON, 1, 1 },
                             { 0.096f, TIER_STONE, ID_RAW_COPPER, 2, 4 }, { 0.16f, TIER_WOOD, ID_COAL, 1, 1 } };
        for (auto& o : ores)
            if (r < o.p && tier >= o.tier) {
                Drop(at, o.id, o.min, o.max);
                return;
            }
        SpawnBlockDrops(ID_STONE, at);
        break;
    }
    case GM_GRASS:
        Drop(at, ID_DIRT, 1, 1);
        if (r < 0.10f)
            Drop(at, ID_WHEAT_SEEDS, 1, 1);
        else if (r < 0.13f)
            Drop(at, ID_CARROT, 1, 1);
        else if (r < 0.16f)
            Drop(at, ID_POTATO, 1, 1);
        break;
    case GM_LEAVES: {
        const ItemStack& held = gInv.Held();
        if (!held.Empty() && Item(held.id).special == SP_SHEARS) {
            Drop(at, m.block, 1, 1);
            break;
        }
        if (r < 0.08f)
            Drop(at, ID_APPLE, 1, 1);
        else if (r < 0.22f)
            Drop(at, ID_STICK, 1, 2);
        else if (r < 0.40f) // a sapling of the same tree: plant it and it grows
            Drop(at, m.block == ID_SPRUCE_LEAVES   ? ID_SPRUCE_SAPLING
                   : m.block == ID_BIRCH_LEAVES    ? ID_BIRCH_SAPLING
                   : m.block == ID_JUNGLE_LEAVES   ? ID_JUNGLE_SAPLING
                   : m.block == ID_ACACIA_LEAVES   ? ID_ACACIA_SAPLING
                   : m.block == ID_DARK_OAK_LEAVES ? ID_DARK_OAK_SAPLING
                                                   : ID_OAK_SAPLING, 1, 1);
        break;
    }
    case GM_GLASS:
        Drop(at, ID_GLASS, 1, 1);
        break;
    case GM_METAL:
        Drop(at, ID_IRON_NUGGET, 2, 6);
        if (r < 0.25f)
            Drop(at, ID_IRON_INGOT, 1, 1);
        break;
    case GM_LAMP:
        Drop(at, ID_IRON_NUGGET, 3, 6);
        if (r < 0.5f)
            Drop(at, ID_GLOWSTONE_DUST, 1, 3);
        break;
    case GM_PAPER:
        Drop(at, ID_PAPER, 1, 3);
        if (r < 0.15f)
            Drop(at, ID_BOOK, 1, 1);
        break;
    case GM_TRASH:
        for (int i = 0, n = 1 + rand() % 2; i < n; ++i)
            DropOne(at, { ID_ROTTEN_FLESH, ID_BONE, ID_PAPER, ID_STRING, ID_BOWL, ID_STICK, ID_GLASS_BOTTLE });
        if (r < 0.05f)
            Drop(at, ID_LEATHER, 1, 1);
        break;
    case GM_SHOP:
        for (int i = 0, n = 1 + rand() % 2; i < n; ++i)
            DropOne(at, { ID_BREAD, ID_APPLE, ID_COOKIE, ID_BAKED_POTATO, ID_CARROT, ID_MELON_SLICE, ID_SWEET_BERRIES,
                          ID_PUMPKIN_PIE, ID_HONEY_BOTTLE });
        if (r < 0.03f)
            Drop(at, ID_GOLDEN_CARROT, 1, 1);
        break;
    case GM_DESK:
        Drop(at, ID_PAPER, 1, 4);
        if (r < 0.25f)
            Drop(at, ID_BOOK, 1, 1);
        else if (r < 0.35f)
            Drop(at, ID_INK_SAC, 1, 1);
        break;
    case GM_TABLE:
        DropOne(at, { ID_BOWL, ID_COOKED_BEEF, ID_COOKED_CHICKEN, ID_BREAD, ID_COOKED_PORKCHOP, ID_GLASS_BOTTLE,
                      ID_MUSHROOM_STEW });
        break;
    case GM_CROP:
        Drop(at, ID_WHEAT, 1, 2);
        Drop(at, ID_WHEAT_SEEDS, 0, 2);
        break;
    case GM_GORE:
        Drop(at, ID_ROTTEN_FLESH, 1, 2);
        if (r < 0.3f)
            Drop(at, ID_BONE, 1, 1);
        break;
    case GM_RAIL:
        Drop(at, ID_IRON_NUGGET, 1, 3);
        Drop(at, ID_STICK, 1, 2);
        break;
    case GM_RUBBER:
        Drop(at, ID_SLIME_BALL, 1, 2);
        break;
    case GM_WOOD_FENCE:
        Drop(at, ID_STICK, 2, 4);
        if (r < 0.3f)
            Drop(at, ID_OAK_PLANKS, 1, 2);
        break;
    case GM_VEHICLE: {
        const int sub = veh ? (int)veh->m_nVehicleSubClass : (int)VEHICLE_AUTOMOBILE;
        switch (sub) {
        case VEHICLE_BMX:
            Drop(at, ID_IRON_NUGGET, 4, 8);
            Drop(at, ID_STRING, 1, 2);
            Drop(at, ID_LEATHER, 0, 1);
            break;
        case VEHICLE_BIKE: case VEHICLE_QUAD:
            Drop(at, ID_IRON_INGOT, 1, 3);
            Drop(at, ID_LEATHER, 1, 1);
            Drop(at, ID_REDSTONE, 1, 2);
            Drop(at, ID_COAL, 0, 1);
            break;
        case VEHICLE_BOAT:
            Drop(at, ID_OAK_PLANKS, 4, 8);
            Drop(at, ID_IRON_INGOT, 1, 2);
            Drop(at, ID_STRING, 1, 2);
            break;
        case VEHICLE_HELI: case VEHICLE_PLANE:
            Drop(at, ID_IRON_INGOT, 6, 12);
            Drop(at, ID_REDSTONE, 4, 8);
            Drop(at, ID_GLASS, 2, 4);
            if (r < 0.3f)
                Drop(at, ID_GOLD_INGOT, 1, 2);
            break;
        default:
            Drop(at, ID_IRON_INGOT, 3, 6);
            Drop(at, ID_GLASS, 1, 3);
            Drop(at, ID_REDSTONE, 2, 5);
            Drop(at, ID_LEATHER, 1, 3);
            Drop(at, ID_COAL, 1, 2);
            if (r < 0.05f)
                Drop(at, ID_GOLD_INGOT, 1, 1);
            break;
        }
        // the paintwork: concrete in the vehicle's colour
        if (veh && CVehicleModelInfo::ms_vehicleColourTable) {
            const CRGBA& c = CVehicleModelInfo::ms_vehicleColourTable[veh->m_nPrimaryColor & 127];
            Drop(at, ConcreteForColor(c.r, c.g, c.b), 1, 2);
        }
        break;
    }
    default:
        break;
    }
}

} // namespace mc
