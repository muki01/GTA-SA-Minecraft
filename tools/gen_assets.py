"""
Builds the mod's runtime assets and C++ data tables from the original Minecraft assets
(minecraft-assets-26.3, supplied by the user).

Outputs
  assets/atlas.png    blocks, items, cracks, particles (16px tiles)
  assets/gui.png      HUD + container textures (packed)
  assets/font.png     Minecraft bitmap font (ASCII + Latin accents incl. Turkish)
  assets/entity.png   steve skin, elytra, arrow, shadow
  assets/sounds/*.ogg selected sound files
  src/core/generated/Assets.h       tile / gui rect / font tables
  src/core/generated/GameData.h     ids, enums
  src/core/generated/GameData.cpp   block, item, recipe and sound tables
"""
import json
import os
import shutil
from collections import OrderedDict, defaultdict

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MCA = os.path.join(ROOT, "minecraft-assets-26.3")
AM = os.path.join(MCA, "assets", "minecraft")
DM = os.path.join(MCA, "data", "minecraft")
OUT = os.path.join(ROOT, "assets")
GEN = os.path.join(ROOT, "src", "core", "generated")
os.makedirs(OUT, exist_ok=True)
os.makedirs(GEN, exist_ok=True)
os.makedirs(os.path.join(OUT, "sounds"), exist_ok=True)


def jload(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def strip_ns(s):
    return s.split(":")[-1]


LANG = jload(os.path.join(AM, "lang", "tr_tr.json"))
LANG_EN = jload(os.path.join(AM, "lang", "en_us.json"))


def tr_name(kind, key):
    k = f"{kind}.minecraft.{key}"
    return LANG.get(k) or LANG_EN.get(k) or key.replace("_", " ").title()


def en_name(kind, key):
    return LANG_EN.get(f"{kind}.minecraft.{key}") or key.replace("_", " ").title()


def cstr(s):
    """C++ string literal with UTF-8 bytes escaped."""
    out = []
    for b in s.encode("utf-8"):
        if b < 0x20 or b >= 0x7F or b in (0x22, 0x5C):
            out.append("\\x%02X" % b)
        else:
            out.append(chr(b))
    # split after hex escapes so following hex-looking chars are not swallowed
    res = ""
    for i, piece in enumerate(out):
        res += piece
        if piece.startswith("\\x") and i + 1 < len(out) and not out[i + 1].startswith("\\x") and out[i + 1] in "0123456789abcdefABCDEF":
            res += '" "'
    return '"' + res + '"'


# ======================================================================= textures
def load_png(rel, size=16):
    rel = strip_ns(rel)
    path = os.path.join(AM, "textures", rel + ".png")
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    if h > w and h % w == 0:  # animated strip: first frame
        im = im.crop((0, 0, w, w))
    if size and im.size != (size, size):
        im = im.resize((size, size), Image.NEAREST)
    return np.array(im, dtype=np.uint8)


def tinted(arr, rgb):
    a = arr.astype(np.float32)
    a[..., 0] *= rgb[0] / 255.0
    a[..., 1] *= rgb[1] / 255.0
    a[..., 2] *= rgb[2] / 255.0
    return a.clip(0, 255).astype(np.uint8)


def over(base, top):
    b = base.astype(np.float32) / 255.0
    t = top.astype(np.float32) / 255.0
    ta = t[..., 3:4]
    out = b.copy()
    out[..., :3] = t[..., :3] * ta + b[..., :3] * (1 - ta)
    out[..., 3:4] = ta + b[..., 3:4] * (1 - ta)
    return (out * 255).clip(0, 255).astype(np.uint8)


GRASS_TINT = (0x91, 0xBD, 0x59)
FOLIAGE_TINT = (0x77, 0xAB, 0x2F)
LEAF_TINTS = {
    "oak_leaves": FOLIAGE_TINT, "jungle_leaves": FOLIAGE_TINT, "acacia_leaves": FOLIAGE_TINT,
    "dark_oak_leaves": FOLIAGE_TINT, "mangrove_leaves": (0x92, 0xC1, 0x35),
    "spruce_leaves": (0x61, 0x99, 0x61), "birch_leaves": (0x80, 0xA7, 0x55),
}

ATLAS_PX = 1024
ATLAS_TILES = ATLAS_PX // 16
tiles = []          # list of np arrays
tile_index = {}     # key -> index


def add_tile(key, arr):
    if key in tile_index:
        return tile_index[key]
    tile_index[key] = len(tiles)
    tiles.append(arr)
    return tile_index[key]


def tex_tile(rel, tint=None):
    key = strip_ns(rel) + ("" if tint is None else "#%02x%02x%02x" % tint)
    if key in tile_index:
        return tile_index[key]
    arr = load_png(rel)
    if tint is not None:
        arr = tinted(arr, tint)
    return add_tile(key, arr)


def alpha_class(arr):
    a = arr[..., 3]
    if np.any((a > 0) & (a < 255)):
        return 2  # translucent
    if np.any(a == 0):
        return 1  # cutout
    return 0


# ======================================================================= tags
def resolve_tag(kind, name, seen=None):
    seen = seen or set()
    if name in seen:
        return set()
    seen.add(name)
    path = os.path.join(DM, "tags", kind, strip_ns(name) + ".json")
    if not os.path.exists(path):
        return set()
    out = set()
    for v in jload(path).get("values", []):
        if isinstance(v, dict):
            v = v.get("id", "")
        if v.startswith("#"):
            out |= resolve_tag(kind, v[1:], seen)
        else:
            out.add(strip_ns(v))
    return out


MINEABLE = {t: resolve_tag("block", "mineable/" + t) for t in ("pickaxe", "axe", "shovel", "hoe")}
NEEDS = {2: resolve_tag("block", "needs_stone_tool"), 3: resolve_tag("block", "needs_iron_tool"),
         4: resolve_tag("block", "needs_diamond_tool")}

# ======================================================================= blocks
TEMPLATES = {
    "block/cube_all": "all", "block/leaves": "all", "block/cube_mirrored_all": "all",
    "block/template_glazed_terracotta": "glazed",
    "block/cube_column": "column", "block/cube_column_horizontal": "column", "block/cube_column_uv_locked_x": "column",
    "block/cube_column_uv_locked_y": "column", "block/cube_column_uv_locked_z": "column", "block/cube_column_mirrored": "column",
    "block/cube_bottom_top": "bottom_top", "block/orientable": "orientable",
    "block/orientable_with_bottom": "orientable_bottom", "block/cube": "cube6",
    "block/stairs": "stairs", "block/inner_stairs": "stairs", "block/outer_stairs": "stairs",
    "block/slab": "slab", "block/slab_top": "slab",
}
EXCLUDE_PREFIX = ("infested_", "waxed_", "test_", "potted_")
EXCLUDE_EXACT = {"barrier", "light", "structure_block", "jigsaw", "command_block", "chain_command_block",
                 "repeating_command_block", "moving_piston", "petrified_oak_slab", "structure_void", "air",
                 "cave_air", "void_air", "spawner", "trial_spawner", "vault", "reinforced_deepslate",
                 "budding_amethyst", "frosted_ice", "dried_ghast", "creaking_heart"}


def resolve_model(model_name):
    textures = {}
    name = strip_ns(model_name)
    for _ in range(12):
        path = os.path.join(AM, "models", name + ".json")
        if not os.path.exists(path):
            return None, textures
        m = jload(path)
        for k, v in m.get("textures", {}).items():
            textures.setdefault(k, v)
        par = m.get("parent")
        if not par:
            return None, textures
        par = strip_ns(par)
        if par in TEMPLATES:
            return TEMPLATES[par], textures
        name = par
    return None, textures


def deref(textures, key):
    v = textures.get(key)
    for _ in range(5):
        if isinstance(v, dict):  # newer format: {"sprite": "...", ...}
            v = v.get("sprite")
        if v is None or not v.startswith("#"):
            return v
        v = textures.get(v[1:])
    return v


def pick_variant(variants, want=None):
    want = want or {}
    best = None
    for key, m in variants.items():
        props = dict(kv.split("=") for kv in key.split(",") if "=" in kv)
        ok = True
        for k, v in {"axis": "y", "facing": "north", "lit": "false", "snowy": "false", **want}.items():
            if k in props and props[k] != v:
                ok = False
        if ok:
            best = m
            break
    if best is None:
        best = next(iter(variants.values()))
    if isinstance(best, list):
        best = best[0]
    return best["model"]


PLANTS = ("oak_sapling", "spruce_sapling", "birch_sapling", "jungle_sapling", "acacia_sapling", "dark_oak_sapling",
          "cherry_sapling", "dandelion", "poppy", "blue_orchid", "allium", "azure_bluet", "red_tulip", "orange_tulip",
          "white_tulip", "pink_tulip", "oxeye_daisy", "cornflower", "lily_of_the_valley", "short_grass", "fern",
          "dead_bush", "brown_mushroom", "red_mushroom")
FLUIDS = ("water", "lava")
FIRES = ("fire",)


def hardness_for(n):
    if n in PLANTS or n in FIRES:
        return 0.0
    if n in FLUIDS:
        return -1.0
    rules = [
        (("bedrock", "end_portal_frame"), -1.0), (("obsidian", "netherite_block", "respawn_anchor"), 50.0),
        (("ancient_debris",), 30.0), (("ender_chest",), 22.5), (("deepslate_",), 4.5), (("_ore",), 3.0),
        (("deepslate",), 3.0), (("end_stone",), 3.0), (("netherrack",), 0.4),
        (("iron_block", "diamond_block", "emerald_block", "copper_block", "raw_", "cut_copper", "lapis_block", "redstone_block", "coal_block", "copper_grate", "chiseled_copper", "exposed_copper", "weathered_copper", "oxidized_copper"), 5.0),
        (("gold_block",), 3.0), (("glowstone", "sea_lantern", "redstone_lamp"), 0.3), (("lodestone",), 3.5),
        (("quartz", "sandstone"), 0.8), (("calcite",), 0.75), (("stone_bricks",), 1.5),
        (("cobblestone", "bricks", "brick"), 2.0),
        (("stone", "andesite", "diorite", "granite", "prismarine", "purpur", "blackstone", "tuff", "mud_brick", "amethyst"), 1.5),
        (("terracotta",), 1.25), (("concrete_powder",), 0.5), (("concrete",), 1.8), (("basalt",), 1.25),
        (("quartz", "sandstone"), 0.8), (("calcite",), 0.75), (("packed_mud",), 1.0),
        (("crafting_table", "chest", "barrel", "cartography", "fletching", "smithing", "loom"), 2.5),
        (("bookshelf",), 1.5), (("planks", "_log", "_wood", "_stem", "hyphae", "bamboo_block", "bamboo_mosaic"), 2.0),
        (("furnace", "smoker", "dispenser", "dropper"), 3.5),
        (("grass_block", "mycelium", "gravel", "clay", "farmland"), 0.6),
        (("dirt", "podzol", "sand", "mud", "soul_soil", "hay_block", "dried_kelp", "target", "magma", "honeycomb"), 0.5),
        (("wool",), 0.8), (("glass",), 0.3), (("blue_ice",), 2.8), (("ice",), 0.5), (("leaves",), 0.2),
        (("snow",), 0.2), (("sponge",), 0.6), (("pumpkin", "melon", "jack_o_lantern", "nether_wart_block", "warped_wart", "shroomlight"), 1.0),
        (("glowstone", "sea_lantern", "redstone_lamp"), 0.3), (("bone_block",), 2.0), (("coral",), 1.5),
        (("mushroom",), 0.2), (("tnt",), 0.0), (("note_block",), 0.8), (("jukebox",), 2.0), (("sculk",), 0.2),
        (("moss",), 0.1), (("beehive",), 0.6), (("bee_nest",), 0.3), (("froglight",), 0.3), (("observer",), 3.0),
        (("lodestone",), 3.5), (("slime", "honey_block"), 0.0),
    ]
    for keys, h in rules:
        if any(k in n for k in keys):
            return h
    return None


SURF = dict(DEFAULT=0, TARMAC=1, PAVEMENT=4, GRAVEL=6, GRASS=9, HEDGE=41, MUD=25, DIRT=26, SAND=30, ROCK=35,
            WOOD=43, GLASS=45, METAL=51, CARPET=71)
SOUND_GROUPS = ["stone", "wood", "gravel", "grass", "sand", "glass", "wool", "metal", "snow"]


WOOD_TYPES = ("oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry", "pale_oak", "poplar",
              "bamboo", "crimson", "warped")


def classify(n):
    """surface, sound group, creative category"""
    if n.endswith("_bed") or n.endswith("_bed_head"):
        return SURF["DEFAULT"], "wood", 1
    if n == "iron_bars":
        return SURF["DEFAULT"], "metal", 0
    if n.endswith("_stairs") or n.endswith("_slab") or n.endswith("_fence") or n.endswith("_wall"):
        if any(n.startswith(w + "_") for w in WOOD_TYPES):
            return SURF["WOOD"], "wood", 0
        return SURF["ROCK"], "stone", 0
    if n in PLANTS:
        return SURF["GRASS"], "grass", 2
    if n in FLUIDS:
        return SURF["DEFAULT"], "stone", 2
    if n in FIRES:
        return SURF["DEFAULT"], "wool", 2
    if "wool" in n:
        return SURF["CARPET"], "wool", 1
    if "glass" in n:
        return SURF["GLASS"], "glass", 1 if ("stained" in n or "tinted" in n) else 0
    if "concrete" in n:
        return (SURF["SAND"], "sand", 1) if "powder" in n else (SURF["PAVEMENT"], "stone", 1)
    if "terracotta" in n:
        return SURF["ROCK"], "stone", 1
    if "leaves" in n:
        return SURF["HEDGE"], "grass", 2
    if n in ("grass_block", "mycelium", "podzol", "moss_block", "sculk") or "nylium" in n:
        return SURF["GRASS"], "grass", 2
    if "dirt" in n or n in ("farmland", "mud", "packed_mud", "soul_soil", "rooted_dirt"):
        return SURF["DIRT"], "gravel", 2
    if "sand" in n and "sandstone" not in n:
        return SURF["SAND"], "sand", 2
    if n in ("gravel", "clay", "soul_sand"):
        return SURF["GRAVEL"] if n == "gravel" else SURF["MUD"], "gravel", 2
    if "snow" in n or "ice" in n:
        return SURF["SAND"] if "snow" in n else SURF["TARMAC"], "snow" if "snow" in n else "glass", 2
    if any(k in n for k in ("planks", "_log", "_wood", "_stem", "hyphae", "bookshelf", "crafting_table", "chest",
                            "barrel", "table", "loom", "beehive", "bee_nest", "note_block", "jukebox", "bamboo")):
        return SURF["WOOD"], "wood", 0 if any(k in n for k in ("planks", "log", "wood", "stem", "hyphae", "bamboo_block", "bamboo_mosaic")) else 3
    if any(k in n for k in ("iron_block", "gold_block", "diamond_block", "emerald_block", "netherite", "copper", "raw_", "lapis_block", "redstone_block")):
        return SURF["METAL"], "metal", 0
    if "_ore" in n or n in ("ancient_debris",):
        return SURF["ROCK"], "stone", 2
    if any(k in n for k in ("furnace", "smoker", "dispenser", "dropper", "observer", "lamp", "tnt", "target", "lodestone")):
        return SURF["ROCK"], "stone", 3
    if any(k in n for k in ("pumpkin", "melon", "hay", "mushroom", "wart", "coral", "sponge", "dried_kelp", "shroomlight", "froglight", "bone_block", "magma", "obsidian")):
        return SURF["ROCK"], "stone", 2
    return SURF["ROCK"], "stone", 0


EMISSIVE = {"lava", "fire", "glowstone", "sea_lantern", "shroomlight", "jack_o_lantern", "ochre_froglight", "verdant_froglight",
            "pearlescent_froglight", "crying_obsidian", "magma_block", "beacon", "redstone_lamp"}

blocks = OrderedDict()  # key -> dict


def add_block(key, kind, tex, render=None, extra=None):
    b = dict(key=key, name=tr_name("block", key), name_en=en_name("block", key), kind=kind, tex=tex, model="block/" + key)
    arrs = [tiles[t] for t in tex.values() if t is not None]
    b["render"] = render if render is not None else max(alpha_class(a) for a in arrs)
    if extra:
        b.update(extra)
    blocks[key] = b


def build_blocks():
    bs_dir = os.path.join(AM, "blockstates")
    for f in sorted(os.listdir(bs_dir)):
        if not f.endswith(".json") or f.startswith("_"):
            continue
        key = f[:-5]
        if key.startswith(EXCLUDE_PREFIX) or key in EXCLUDE_EXACT:
            continue
        if key == "grass_block":
            side = over(load_png("block/grass_block_side"), tinted(load_png("block/grass_block_side_overlay"), GRASS_TINT))
            add_block(key, "bottom_top", dict(top=tex_tile("block/grass_block_top", GRASS_TINT), bottom=tex_tile("block/dirt"),
                                             side=add_tile("grass_block_side_composite", side)), render=0)
            continue
        bs = jload(os.path.join(bs_dir, f))
        # beds: two blocks, the foot (the item) and the head (only ever placed with it)
        if key.endswith("_bed"):
            try:
                _, ft = resolve_model("block/" + key + "_foot")
                _, ht = resolve_model("block/" + key + "_head")
                down, north = tex_tile("block/bed_down"), tex_tile("block/bed_head_north")
                add_block(key, "bed", dict(east=tex_tile(deref(ft, "east")), west=tex_tile(deref(ft, "west")), north=north,
                                           south=tex_tile(deref(ft, "south")), top=tex_tile(deref(ft, "up")), bottom=down))
                he = tex_tile(deref(ht, "east"))
                add_block(key + "_head", "bed_head", dict(east=he, west=tex_tile(deref(ht, "west")), north=north, south=he,
                                                          top=tex_tile(deref(ht, "up")), bottom=down),
                          extra=dict(name=tr_name("block", key), name_en=en_name("block", key)))
            except (FileNotFoundError, TypeError, AttributeError):
                pass
            continue
        # panes, bars, fences and walls (multipart: they join their neighbours): the textures of their post
        joined = "pane" if key.endswith("_pane") or key == "iron_bars" else "fence" if key.endswith("_fence") else \
            "wall" if key.endswith("_wall") else None
        if joined:
            _, tx = resolve_model("block/" + key + "_post")
            try:
                if joined == "pane":
                    side = tex_tile(deref(tx, "pane") or deref(tx, "bars"))
                    edge = tex_tile(deref(tx, "edge"))
                    add_block(key, "pane", dict(top=edge, bottom=edge, side=side))
                    blocks[key]["model"] = "item/" + key
                else:
                    t = tex_tile(deref(tx, "texture" if joined == "fence" else "wall"))
                    add_block(key, joined, dict(top=t, bottom=t, side=t))
                    blocks[key]["model"] = "block/" + key + "_inventory"
            except (FileNotFoundError, TypeError, AttributeError):
                pass
            continue
        if "variants" not in bs:
            continue
        model = pick_variant(bs["variants"])
        kind, texs = resolve_model(model)
        if kind is None:
            continue
        tint = LEAF_TINTS.get(key)
        try:
            if kind in ("all", "glazed"):
                t = tex_tile(deref(texs, "all" if kind == "all" else "pattern"), tint)
                add_block(key, "all", dict(top=t, bottom=t, side=t))
            elif kind == "column":
                add_block(key, "column", dict(top=tex_tile(deref(texs, "end")), bottom=tex_tile(deref(texs, "end")),
                                             side=tex_tile(deref(texs, "side"))))
            elif kind == "bottom_top":
                add_block(key, "bottom_top", dict(top=tex_tile(deref(texs, "top")), bottom=tex_tile(deref(texs, "bottom")),
                                                 side=tex_tile(deref(texs, "side"))))
            elif kind in ("orientable", "orientable_bottom"):
                top = tex_tile(deref(texs, "top"))
                bottom = tex_tile(deref(texs, "bottom")) if kind == "orientable_bottom" else top
                front = tex_tile(deref(texs, "front"))
                lit_front = None
                lit_model = None
                for vk, vm in bs["variants"].items():
                    if "lit=true" in vk and "facing=north" in vk:
                        lit_model = (vm[0] if isinstance(vm, list) else vm)["model"]
                if lit_model:
                    _, lt = resolve_model(lit_model)
                    if deref(lt, "front"):
                        lit_front = tex_tile(deref(lt, "front"))
                add_block(key, "facing", dict(top=top, bottom=bottom, side=tex_tile(deref(texs, "side")), front=front,
                                             front_lit=lit_front))
            elif kind == "cube6":
                n_, s_, e_, w_ = (tex_tile(deref(texs, k)) for k in ("north", "south", "east", "west"))
                add_block(key, "cube6", dict(top=tex_tile(deref(texs, "up")), bottom=tex_tile(deref(texs, "down")),
                                            north=n_, south=s_, east=e_, west=w_))
            elif kind in ("stairs", "slab"):
                add_block(key, kind, dict(top=tex_tile(deref(texs, "top")), bottom=tex_tile(deref(texs, "bottom")),
                                         side=tex_tile(deref(texs, "side"))))
        except FileNotFoundError:
            continue

    # chest: composed from the entity texture
    ch = np.array(Image.open(os.path.join(AM, "textures/entity/chest/normal.png")).convert("RGBA"))

    def strip(x0, y0, h):
        return ch[y0:y0 + h, x0:x0 + 14]

    def face(lid, base, lock=False):
        f_ = np.concatenate([lid, base], axis=0)  # 14 x 15
        if lock:
            f_ = f_.copy()
            f_[2:6, 6:8] = ch[1:5, 1:3]
        return np.array(Image.fromarray(f_).resize((16, 16), Image.NEAREST))

    front = add_tile("chest_front", face(strip(42, 14, 5), strip(42, 33, 10), True))
    side = add_tile("chest_side", face(strip(0, 14, 5), strip(0, 33, 10)))
    top = add_tile("chest_top", np.array(Image.fromarray(ch[0:14, 14:28]).resize((16, 16), Image.NEAREST)))
    add_block("chest", "facing", dict(top=top, bottom=top, side=side, front=front, front_lit=None), render=0)


WATER_TINT = (0x3F, 0x76, 0xE4)
FLUID_FRAMES = 8


def anim_tiles(rel, n, tint=None):
    """n frames of an animated strip, side by side in one atlas row"""
    im = Image.open(os.path.join(AM, "textures", strip_ns(rel) + ".png")).convert("RGBA")
    w, h = im.size
    frames = max(1, h // w)
    while len(tiles) % ATLAS_TILES + n > ATLAS_TILES:
        add_tile("pad_%d" % len(tiles), np.zeros((16, 16, 4), dtype=np.uint8))
    base = len(tiles)
    for i in range(n):
        f = (i * frames) // n
        fr = im.crop((0, f * w, w, f * w + w)).resize((16, 16), Image.NEAREST)
        arr = np.array(fr, dtype=np.uint8)
        if tint is not None:
            arr = tinted(arr, tint)
        add_tile("%s#frame%d" % (strip_ns(rel), i), arr)
    return base


ANIMS = OrderedDict()  # name -> (first tile, frames, ticks per frame, frame size in px)


def anim_meta(rel):
    path = os.path.join(AM, "textures", strip_ns(rel) + ".png")
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    frames = max(1, h // w)
    anim = {}
    if os.path.exists(path + ".mcmeta"):
        anim = jload(path + ".mcmeta").get("animation", {})
    order = [f if isinstance(f, int) else f["index"] for f in anim.get("frames", [])] or list(range(frames))
    return im, w, order, int(anim.get("frametime", 1))


def pad_row():
    while len(tiles) % ATLAS_TILES != 0:
        add_tile("pad_%d" % len(tiles), np.zeros((16, 16, 4), dtype=np.uint8))


def anim_row(name, rel, tint=None):
    """a 16 px animation: its frames side by side in one atlas row"""
    im, w, order, ft = anim_meta(rel)
    assert w == 16 and len(order) <= ATLAS_TILES, rel
    pad_row()
    base = len(tiles)
    for i, f in enumerate(order):
        arr = np.array(im.crop((0, f * w, w, f * w + w)), dtype=np.uint8)
        if tint is not None:
            arr = tinted(arr, tint)
        add_tile("%s#a%d" % (strip_ns(rel), i), arr)
    ANIMS[name] = (base, len(order), ft, 16)
    return base


def anim_big(name, rel, tint=None):
    """a 32 px animation (flowing fluids): each frame takes 2x2 tiles, two atlas rows"""
    im, w, order, ft = anim_meta(rel)
    assert w == 32 and 2 * len(order) <= ATLAS_TILES, rel
    pad_row()
    base = len(tiles)
    for half in (0, 1):
        for i, f in enumerate(order):
            arr = np.array(im.crop((0, f * w, w, f * w + w)), dtype=np.uint8)
            if tint is not None:
                arr = tinted(arr, tint)
            for col in (0, 1):
                add_tile("%s#b%d_%d_%d" % (strip_ns(rel), half, i, col),
                         np.ascontiguousarray(arr[half * 16:half * 16 + 16, col * 16:col * 16 + 16]))
        pad_row()
    ANIMS[name] = (base, len(order), ft, 32)
    return base


def build_extra_blocks():
    for key in PLANTS:
        path = os.path.join(AM, "models", "block", key + ".json")
        if not os.path.exists(path):
            continue
        m = jload(path)
        par = strip_ns(m.get("parent", ""))
        tint = GRASS_TINT if par == "block/tinted_cross" else None
        tex = deref(m.get("textures", {}), "cross")
        if not tex:
            continue
        t = tex_tile(tex, tint)
        add_block(key, "cross", dict(top=t, bottom=t, side=t), render=1)
        blocks[key]["model"] = "item/" + key
    ws = anim_row("WATER_STILL", "block/water_still", WATER_TINT)
    wf = anim_big("WATER_FLOW", "block/water_flow", WATER_TINT)
    ls = anim_row("LAVA_STILL", "block/lava_still")
    lf = anim_big("LAVA_FLOW", "block/lava_flow")
    add_block("water", "fluid", dict(top=ws, bottom=ws, side=wf), render=2)
    add_block("lava", "fluid", dict(top=ls, bottom=ls, side=lf), render=1)
    f0 = anim_row("FIRE_0", "block/fire_0")
    anim_row("FIRE_1", "block/fire_1")
    add_block("fire", "fire", dict(top=f0, bottom=f0, side=f0), render=1)
    for k in ("glint", "lava", "bubble", "drip_fall"):
        tex_tile("particle/" + k)


# ======================================================================= block properties
def loot_drops(key):
    if key in FLUIDS or key in FIRES:
        return []
    path = os.path.join(DM, "loot_table", "blocks", key + ".json")
    if not os.path.exists(path):
        return [(key, 1, 1, 1.0, 0)]
    lt = jload(path)

    def silky(cond):
        s = json.dumps(cond)
        return "silk_touch" in s or "can_shear" in s

    def chance_of(cond):
        if not cond:
            return 1.0
        if isinstance(cond, str):
            return 1.0
        t = cond.get("type", "")
        if t.endswith("table_bonus"):
            return float(cond["chances"][0])
        if t.endswith("random_chance"):
            return float(cond.get("chance", 1.0))
        if t.endswith("all_of"):
            c = 1.0
            for term in cond.get("terms", []):
                c *= chance_of(term)
            return c
        return 1.0

    def count_of(entry):
        lo = hi = 1
        mods = entry.get("modifier", entry.get("functions", []))
        if isinstance(mods, dict):
            mods = [mods]
        if not isinstance(mods, list):
            mods = []
        for m in mods:
            if not isinstance(m, dict):
                continue
            # (a count that depends on the block's state - a double slab, more candles - is not the usual one)
            if strip_ns(m.get("type", m.get("function", ""))) == "set_count" and not m.get("conditions") and not m.get("condition"):
                c = m.get("count")
                if isinstance(c, (int, float)):
                    lo = hi = int(c)
                elif isinstance(c, dict):
                    lo = int(c.get("min", 1))
                    hi = int(c.get("max", lo))
        return lo, hi

    def pick(entry):
        """list of (item, lo, hi, chance); an 'alternatives' entry may give a chance item plus its fallback"""
        t = strip_ns(entry.get("type", ""))
        if t == "item":
            if silky(entry.get("condition", entry.get("conditions"))):
                return []
            lo, hi = count_of(entry)
            return [(strip_ns(entry["name"]), lo, hi, chance_of(entry.get("condition")))]
        if t == "alternatives":
            res = []
            for c in entry.get("children", []):
                if silky(c.get("condition", c.get("conditions"))):
                    continue
                r = pick(c)
                res += r
                if r and all(x[3] >= 1.0 for x in r):
                    break
            return res
        return []

    out = []
    group = 0
    for pool in lt.get("pools", []):
        if silky(pool.get("condition", pool.get("conditions"))) and "inverted" not in json.dumps(pool.get("condition", {})):
            continue
        for e in pool.get("entries", []):
            r = pick(e)
            if r:
                g = 0
                if len(r) > 1:
                    group += 1
                    g = group
                out += [(it, lo, hi, ch, g) for (it, lo, hi, ch) in r]
                break
    return out[:3]


def finalize_blocks():
    for key, b in blocks.items():
        base = key[:-5] if key.endswith("_bed_head") else key  # a bed's head is the bed
        h = hardness_for(base)
        tool = 0
        for i, t in enumerate(("pickaxe", "axe", "shovel", "hoe")):
            if base in MINEABLE[t]:
                tool = i + 1
                break
        if h is None:
            h = {1: 1.5, 2: 2.0, 3: 0.5, 4: 0.5}.get(tool, 1.0)
        tier = 0
        for t in (4, 3, 2):
            if base in NEEDS[t]:
                tier = t
                break
        requires = tool == 1 and key not in ("ice", "packed_ice", "blue_ice") and h >= 0.5
        if requires and tier == 0:
            tier = 1
        surface, sound, cat = classify(base)
        b.update(hardness=h, tool=tool, tier=tier, requires=requires, surface=surface,
                 sound=SOUND_GROUPS.index(sound), category=cat, emissive=key in EMISSIVE,
                 drops=loot_drops(base))


# ======================================================================= items
TOOL_TIERS = {"wooden": (1, 2.0, 59), "stone": (2, 4.0, 131), "copper": (2, 5.0, 190), "iron": (3, 6.0, 250),
              "golden": (1, 12.0, 32), "diamond": (4, 8.0, 1561), "netherite": (4, 9.0, 2031)}
SWORD_DMG = {"wooden": 4, "golden": 4, "stone": 5, "copper": 5, "iron": 6, "diamond": 7, "netherite": 8}
AXE_DMG = {"wooden": 7, "golden": 7, "stone": 9, "copper": 9, "iron": 9, "diamond": 9, "netherite": 10}
ARMOR = {"chainmail": ((2, 5, 4, 1), 15), "iron": ((2, 6, 5, 2), 15), "golden": ((2, 5, 3, 1), 7),
         "diamond": ((3, 8, 6, 3), 33), "netherite": ((3, 8, 6, 3), 37), "copper": ((2, 4, 3, 1), 11),
         "turtle": ((2, 0, 0, 0), 25)}
ARMOR_SLOTS = ["helmet", "chestplate", "leggings", "boots"]
ARMOR_BASE = (11, 16, 15, 13)
FOOD = {"apple": (4, 2.4), "golden_apple": (4, 9.6), "enchanted_golden_apple": (4, 9.6), "bread": (5, 6.0),
        "beef": (3, 1.8), "cooked_beef": (8, 12.8), "porkchop": (3, 1.8), "cooked_porkchop": (8, 12.8),
        "chicken": (2, 1.2), "cooked_chicken": (6, 7.2), "mutton": (2, 1.2), "cooked_mutton": (6, 9.6),
        "rabbit": (3, 1.8), "cooked_rabbit": (5, 6.0), "cod": (2, 0.4), "cooked_cod": (5, 6.0), "salmon": (2, 0.4),
        "cooked_salmon": (6, 9.6), "carrot": (3, 3.6), "golden_carrot": (6, 14.4), "potato": (1, 0.6),
        "baked_potato": (5, 6.0), "poisonous_potato": (2, 1.2), "beetroot": (1, 1.2), "melon_slice": (2, 1.2),
        "sweet_berries": (2, 0.4), "glow_berries": (2, 0.4), "cookie": (2, 0.4), "pumpkin_pie": (8, 4.8),
        "rotten_flesh": (4, 0.8), "spider_eye": (2, 3.2), "dried_kelp": (1, 0.6), "honey_bottle": (6, 1.2),
        "mushroom_stew": (6, 7.2), "beetroot_soup": (6, 7.2), "rabbit_stew": (10, 12.0), "chorus_fruit": (4, 2.4),
        "tropical_fish": (1, 0.2), "pufferfish": (1, 0.2), "suspicious_stew": (6, 7.2)}
FUEL = {"coal": 1600, "charcoal": 1600, "blaze_rod": 2400, "lava_bucket": 20000, "stick": 100, "bowl": 100,
        "bamboo": 50, "scaffolding": 50, "dried_kelp_block": 4001, "coal_block": 16000, "bow": 300,
        "crossbow": 300, "fishing_rod": 300, "wooden_pickaxe": 200, "wooden_axe": 200, "wooden_shovel": 200,
        "wooden_sword": 200, "wooden_hoe": 200}
STACK16 = ("ender_pearl", "snowball", "egg", "bucket", "honey_bottle", "armor_stand", "_banner", "_sign")
STACK1 = ("_sword", "_pickaxe", "_axe", "_shovel", "_hoe", "_helmet", "_chestplate", "_leggings", "_boots", "elytra",
          "bow", "crossbow", "flint_and_steel", "shears", "fishing_rod", "_bucket", "saddle", "minecart", "_boat",
          "_raft", "music_disc", "totem", "enchanted_book", "written_book", "writable_book", "mace", "_on_a_stick",
          "brush", "spyglass", "_horse_armor", "mushroom_stew", "beetroot_soup", "rabbit_stew", "suspicious_stew",
          "potion", "cake", "knowledge_book", "bundle", "wolf_armor", "trident", "shield", "goat_horn")
SPECIAL = {"bow": 1, "arrow": 2, "spectral_arrow": 2, "flint_and_steel": 3, "fire_charge": 17, "firework_rocket": 4,
           "wind_charge": 18, "spyglass": 19, "totem_of_undying": 20, "minecart": 22, "saddle": 23, "milk_bucket": 24,
           "trident": 25, "water_bucket": 26, "lava_bucket": 27, "crossbow": 28, "bone_meal": 29,
           "elytra": 5, "snowball": 6, "egg": 7, "ender_pearl": 8, "debug_stick": 9, "cow_spawn_egg": 10,
           "pig_spawn_egg": 11, "sheep_spawn_egg": 12, "chicken_spawn_egg": 13, "fishing_rod": 14, "shears": 15,
           "bucket": 16, "creeper_spawn_egg": 30, "warden_spawn_egg": 31}
KEEP_SPAWN_EGGS = ("cow_spawn_egg", "pig_spawn_egg", "sheep_spawn_egg", "chicken_spawn_egg", "creeper_spawn_egg", "warden_spawn_egg")
EXCLUDE_ITEMS = ("spawn_egg", "knowledge_book", "structure_void", "air", "barrier", "light",
                 "bundle", "filled_map", "command_block_minecart", "jigsaw", "test_")
NAME_OVERRIDE = {"debug_stick": "B\u00fcy\u00fcl\u00fc Sopa"}
GLINT = ("debug_stick", "enchanted_golden_apple", "enchanted_book", "nether_star", "experience_bottle")

items = OrderedDict()


def item_props(key):
    p = dict(stack=64, tool=0, tier=0, speed=1.0, durability=0, food=0, sat=0.0, fuel=FUEL.get(key, 0),
             damage=1.0, aspeed=4.0, armor_slot=0, armor_pts=0, special=SPECIAL.get(key, 0), category=7)
    for s in STACK16:
        if s in key:
            p["stack"] = 16
    for s in STACK1:
        if key.endswith(s) or s == key or (s.startswith("_") and s in key):
            p["stack"] = 1
    for mat, (tier, speed, dur) in TOOL_TIERS.items():
        for i, kind in enumerate(("pickaxe", "axe", "shovel", "hoe", "sword")):
            if key == f"{mat}_{kind}":
                p.update(tool=i + 1, tier=tier, speed=speed if kind != "sword" else 1.5, durability=dur, category=4,
                         stack=1)
                base = {"pickaxe": 1, "axe": 0, "shovel": 1.5, "hoe": 1, "sword": 0}[kind]
                if kind == "sword":
                    p.update(damage=SWORD_DMG[mat], aspeed=1.6, category=5)
                elif kind == "axe":
                    p.update(damage=AXE_DMG[mat], aspeed=0.9)
                elif kind == "pickaxe":
                    p.update(damage=1 + tier, aspeed=1.2)
                else:
                    p.update(damage=base + tier, aspeed=1.0)
                if kind == "sword":
                    p["tool"] = 5
    for mat, (pts, mult) in ARMOR.items():
        for i, slot in enumerate(ARMOR_SLOTS):
            if key == f"{mat}_{slot}":
                p.update(armor_slot=i + 1, armor_pts=pts[i], durability=ARMOR_BASE[i] * mult, stack=1, category=5)
    if key == "elytra":
        p.update(armor_slot=2, durability=432, stack=1, category=4)
    if key == "bow":
        p.update(durability=384, category=5)
    if key in ("arrow", "spectral_arrow"):
        p["category"] = 5
    if key == "flint_and_steel":
        p.update(durability=64, category=4)
    if key == "firework_rocket":
        p["category"] = 4
    if key in FOOD:
        p.update(food=FOOD[key][0], sat=FOOD[key][1], category=6)
    if key.endswith("_boat") or key.endswith("_raft"):
        p.update(special=21, category=4, stack=1)
    if key in ("fire_charge", "wind_charge", "spyglass", "minecart", "saddle", "milk_bucket", "water_bucket",
               "lava_bucket"):
        p["category"] = 4
    if key == "totem_of_undying":
        p["category"] = 5
    if key == "crossbow":
        p.update(durability=465, category=5, stack=1)
    if key == "trident":
        p.update(damage=9.0, aspeed=1.1, durability=250, category=5, stack=1)
    if key == "wind_charge":
        p["stack"] = 64
    return p


def build_items():
    md = os.path.join(AM, "models", "item")
    for f in sorted(os.listdir(md)):
        if not f.endswith(".json") or f.startswith("_"):
            continue
        key = f[:-5]
        if key in blocks or (any(x in key for x in EXCLUDE_ITEMS) and key not in KEEP_SPAWN_EGGS):
            continue
        if f"item.minecraft.{key}" not in LANG_EN:
            continue
        m = jload(os.path.join(md, f))
        par = strip_ns(m.get("parent", ""))
        tx = m.get("textures", {})
        if par not in ("item/generated", "item/handheld", "item/handheld_rod", "item/handheld_mace") or "layer0" not in tx or "layer1" in tx:
            continue
        try:
            tile = tex_tile(tx["layer0"])
        except FileNotFoundError:
            continue
        it = dict(key=key, name=NAME_OVERRIDE.get(key, tr_name("item", key)), name_en=en_name("item", key), tile=tile)
        it.update(item_props(key))
        it["glint"] = 1 if key in GLINT else 0
        it["model"] = "item/" + key
        if key in KEEP_SPAWN_EGGS or key == "debug_stick":
            it["category"] = 4
        if key == "debug_stick":
            it["stack"] = 1
        if key == "fishing_rod":
            it.update(durability=64, category=4)
        if key == "shears":
            it.update(durability=238, category=4)
        items[key] = it
    # extra frames
    for i in range(3):
        tex_tile(f"item/bow_pulling_{i}")
    tex_tile("item/fishing_rod_cast")
    for i in range(3):
        tex_tile(f"item/crossbow_pulling_{i}")
    tex_tile("item/crossbow_arrow")
    tex_tile("item/crossbow_firework")


# ======================================================================= recipes
def ingredient_items(ing):
    if isinstance(ing, list):
        out = set()
        for x in ing:
            out |= ingredient_items(x)
        return out
    if isinstance(ing, dict):
        if "tag" in ing:
            return resolve_tag("item", ing["tag"])
        if "item" in ing:
            return {strip_ns(ing["item"])}
        return set()
    if ing.startswith("#"):
        return resolve_tag("item", ing[1:])
    return {strip_ns(ing)}


# ======================================================================= main
build_blocks()
build_extra_blocks()
finalize_blocks()
build_items()

# ======================================================================= the creative tabs
# Minecraft's tabs (CreativeModeTabs): the redstone blocks and the spawn eggs have tabs of their own, and inside a tab
# things come grouped: wood by wood type, stone by its family (stairs, slab, wall after it), colours in Minecraft's order,
# tools and armour by material. (The game's own lists are code, not assets: this follows them as far as it can.)
CAT_NATURAL, CAT_FOOD, CAT_INGREDIENTS, CAT_REDSTONE, CAT_SPAWN_EGGS = 2, 6, 7, 8, 9
REDSTONE_WORDS = ("repeater", "comparator", "piston", "observer", "dispenser", "dropper", "crafter", "hopper", "lever",
                  "daylight_detector", "target", "tripwire_hook", "trapped_chest", "note_block", "sculk_sensor",
                  "lightning_rod", "rail", "minecart", "slime_block", "honey_block", "lectern")
REDSTONE_EXACT = {"redstone", "redstone_torch", "redstone_block", "redstone_lamp", "tnt", "stone_button",
                  "stone_pressure_plate", "light_weighted_pressure_plate", "heavy_weighted_pressure_plate", "iron_door",
                  "iron_trapdoor"}
COLORS = ["white", "light_gray", "gray", "black", "brown", "red", "orange", "yellow", "lime", "green", "cyan",
          "light_blue", "blue", "purple", "magenta", "pink"]
COLORED_KINDS = ["wool", "wool_stairs", "wool_slab", "carpet", "terracotta", "concrete", "concrete_stairs", "concrete_slab",
                 "concrete_powder", "glazed_terracotta", "glass", "tinted_glass",
                 "stained_glass", "glass_pane", "stained_glass_pane", "shulker_box", "bed", "candle", "banner", "dye",
                 "bundle", "harness"]
WOOD_PARTS = ["log", "stem", "block", "wood", "hyphae", "stripped_log", "stripped_stem", "stripped_block", "stripped_wood",
              "stripped_hyphae", "planks", "mosaic", "stairs", "mosaic_stairs", "slab", "mosaic_slab", "fence", "fence_gate",
              "door", "trapdoor", "pressure_plate", "button", "sign", "hanging_sign", "boat", "chest_boat", "raft",
              "chest_raft", "leaves", "sapling", "propagule"]
STONE_FAMILIES = ["stone", "cobblestone", "mossy_cobblestone", "stone_brick", "mossy_stone_brick", "granite", "diorite",
                  "andesite", "deepslate", "cobbled_deepslate", "deepslate_brick", "deepslate_tile", "tuff", "tuff_brick",
                  "brick", "packed_mud", "mud_brick", "resin_brick", "sandstone", "red_sandstone", "prismarine",
                  "prismarine_brick", "dark_prismarine", "nether_brick", "red_nether_brick", "basalt", "blackstone",
                  "blackstone_brick", "end_stone", "end_stone_brick", "purpur", "quartz", "quartz_brick", "amethyst",
                  "copper", "cut_copper", "iron", "gold", "emerald", "lapis", "diamond", "netherite", "sulfur", "cinnabar"]
STONE_MODS = ["smooth", "polished", "chiseled", "cracked", "cut", "exposed", "weathered", "oxidized"]
SHAPE_SUFFIXES = ["stairs", "slab", "wall", "fence", "fence_gate", "door", "trapdoor", "pressure_plate", "button",
                  "pillar", "bars", "chain", "grate", "bulb"]
MATERIALS = ["wooden", "leather", "stone", "chainmail", "copper", "iron", "golden", "diamond", "netherite", "turtle"]
TOOL_KINDS, WEAPON_KINDS, ARMOR_KINDS = ["shovel", "pickaxe", "axe", "hoe"], ["sword", "spear"], ["helmet", "chestplate", "leggings", "boots"]
NATURAL_FIRST = ["grass_block", "podzol", "mycelium", "dirt_path", "dirt", "coarse_dirt", "rooted_dirt", "farmland", "mud",
                 "clay", "gravel", "sand", "red_sand", "ice", "packed_ice", "blue_ice", "snow_block", "snow", "moss_block",
                 "moss_carpet", "pale_moss_block", "pale_moss_carpet", "stone", "deepslate", "granite", "diorite", "andesite",
                 "calcite", "tuff", "dripstone_block", "pointed_dripstone", "obsidian", "crying_obsidian", "netherrack",
                 "crimson_nylium", "warped_nylium", "soul_sand", "soul_soil", "bone_block", "blackstone", "basalt",
                 "smooth_basalt", "end_stone", "coal_ore", "deepslate_coal_ore", "iron_ore", "deepslate_iron_ore",
                 "copper_ore", "deepslate_copper_ore", "gold_ore", "deepslate_gold_ore", "redstone_ore",
                 "deepslate_redstone_ore", "emerald_ore", "deepslate_emerald_ore", "lapis_ore", "deepslate_lapis_ore",
                 "diamond_ore", "deepslate_diamond_ore", "nether_gold_ore", "nether_quartz_ore", "ancient_debris",
                 "raw_iron_block", "raw_copper_block", "raw_gold_block", "glowstone", "amethyst_block", "budding_amethyst",
                 "small_amethyst_bud", "medium_amethyst_bud", "large_amethyst_bud", "amethyst_cluster"]
FOOD_ORDER = ["apple", "golden_apple", "enchanted_golden_apple", "melon_slice", "sweet_berries", "glow_berries",
              "chorus_fruit", "carrot", "golden_carrot", "potato", "baked_potato", "poisonous_potato", "beetroot",
              "dried_kelp", "beef", "cooked_beef", "porkchop", "cooked_porkchop", "mutton", "cooked_mutton", "chicken",
              "cooked_chicken", "rabbit", "cooked_rabbit", "cod", "cooked_cod", "salmon", "cooked_salmon", "tropical_fish",
              "pufferfish", "bread", "cookie", "cake", "pumpkin_pie", "rotten_flesh", "spider_eye", "mushroom_stew",
              "beetroot_soup", "rabbit_stew", "suspicious_stew", "milk_bucket", "honey_bottle"]
INGREDIENT_FIRST = ["coal", "charcoal", "raw_iron", "raw_copper", "raw_gold", "emerald", "lapis_lazuli", "diamond",
                    "ancient_debris", "quartz", "amethyst_shard", "iron_nugget", "gold_nugget", "copper_nugget",
                    "iron_ingot", "copper_ingot", "gold_ingot", "netherite_scrap", "netherite_ingot", "stick", "flint",
                    "wheat", "bone", "bone_meal", "string", "feather", "snowball", "egg", "leather", "rabbit_hide",
                    "honeycomb", "resin_clump", "ink_sac", "glow_ink_sac", "turtle_scute", "armadillo_scute", "slime_ball",
                    "clay_ball", "prismarine_shard", "prismarine_crystals", "nautilus_shell", "heart_of_the_sea",
                    "fire_charge", "blaze_rod", "breeze_rod", "heavy_core", "nether_star", "ender_pearl", "ender_eye",
                    "shulker_shell", "popped_chorus_fruit", "echo_shard"]


CAT_COLORED, CAT_FUNCTIONAL, CAT_TOOLS, CAT_COMBAT = 1, 3, 4, 5
TOOLS_EXTRA = {"bucket", "brush", "lead", "name_tag", "goat_horn", "carrot_on_a_stick", "warped_fungus_on_a_stick", "map",
               "compass", "recovery_compass", "clock", "bundle"}
COMBAT_EXTRA = {"mace", "end_crystal", "shield", "wolf_armor"}
FUNCTIONAL_EXTRA = {"armor_stand", "item_frame", "glow_item_frame", "painting", "flower_pot", "brewing_stand", "cauldron",
                    "decorated_pot", "respawn_anchor", "sea_lantern", "ender_chest", "enchanting_table", "anvil", "bell"}
NATURAL_EXTRA = {"bedrock", "calcite", "netherrack", "pale_moss_block", "sculk", "sculk_catalyst", "jack_o_lantern",
                 "cocoa_beans", "nether_wart", "pitcher_pod", "potent_sulfur", "sulfur", "cinnabar"}


def creative_tab(key, cat):
    """Minecraft's tab for it (cat: what classify / the item's kind said)"""
    if key.endswith("_spawn_egg"):
        return CAT_SPAWN_EGGS
    if key in REDSTONE_EXACT or any(w_ in key for w_ in REDSTONE_WORDS):
        return CAT_REDSTONE
    if key == "milk_bucket":
        return CAT_FOOD
    if key in TOOLS_EXTRA or key.endswith("_bucket") or key.endswith("_map") or key.endswith("_harness"):
        return CAT_TOOLS
    if key in COMBAT_EXTRA or key.endswith("_spear") or key.endswith("_horse_armor") or key.endswith("_nautilus_armor"):
        return CAT_COMBAT
    if key in FUNCTIONAL_EXTRA:
        return CAT_FUNCTIONAL
    if key in NATURAL_EXTRA or key.endswith("_seeds") or (key in NATURAL_FIRST and not stone_of(key)):
        return CAT_NATURAL
    if key in ("glass", "tinted_glass", "glass_pane") or any(
            key.startswith(c + "_") and ("_wool_" in key or "_concrete_" in key) for c in COLORS):  # (their stairs, slabs)
        return CAT_COLORED
    return cat


def wood_of(key):
    stripped = key.startswith("stripped_")
    k = key[9:] if stripped else key
    for w_ in sorted(WOOD_TYPES, key=len, reverse=True):
        if k.startswith(w_ + "_"):
            part = k[len(w_) + 1:]
            return w_, ("stripped_" + part) if stripped else part
    return None, None


def stone_of(key):
    """(family, its modifiers, its shape) for blocks of a stone or metal family"""
    k, mods, shape = key, [], ""
    changed = True
    while changed:
        changed = False
        for m_ in STONE_MODS:
            if k.startswith(m_ + "_") and k[len(m_) + 1:]:
                mods.append(STONE_MODS.index(m_) + 1)
                k = k[len(m_) + 1:]
                changed = True
                break
    for s_ in sorted(SHAPE_SUFFIXES, key=len, reverse=True):
        if k.endswith("_" + s_):
            shape, k = s_, k[:-len(s_) - 1]
            break
    if k.endswith("_block"):
        k = k[:-6]
    if k.endswith("bricks") or k.endswith("tiles"):
        k = k[:-1]
    if k == "smooth_stone":
        k, mods = "stone", mods + [1]
    if k not in STONE_FAMILIES:
        return None
    return STONE_FAMILIES.index(k), tuple(mods), SHAPE_SUFFIXES.index(shape) + 1 if shape else 0


def creative_rank(key, cat):
    if cat == CAT_NATURAL and key in NATURAL_FIRST:
        return (0, NATURAL_FIRST.index(key))
    if cat == CAT_FOOD and key in FOOD_ORDER:
        return (0, FOOD_ORDER.index(key))
    if cat == CAT_INGREDIENTS and key in INGREDIENT_FIRST:
        return (0, INGREDIENT_FIRST.index(key))
    wood, part = wood_of(key)
    if wood:
        wi, pi = WOOD_TYPES.index(wood), WOOD_PARTS.index(part) if part in WOOD_PARTS else len(WOOD_PARTS)
        return (1, pi, wi) if cat == CAT_NATURAL else (1, wi, pi)  # (the natural tab: all logs, then all leaves...)
    st = stone_of(key)
    if st:
        return (2,) + st
    for c in sorted(COLORS, key=len, reverse=True):
        if key.startswith(c + "_"):
            rest = key[len(c) + 1:]
            return (3, COLORED_KINDS.index(rest) if rest in COLORED_KINDS else len(COLORED_KINDS), rest, COLORS.index(c))
    if key in COLORED_KINDS:
        return (3, COLORED_KINDS.index(key), key, -1)
    for m_ in MATERIALS:
        if key.startswith(m_ + "_"):
            part = key[len(m_) + 1:]
            mi = MATERIALS.index(m_)
            if part in TOOL_KINDS:
                return (4, 0, mi, TOOL_KINDS.index(part))
            if part in WEAPON_KINDS:
                return (4, 1, WEAPON_KINDS.index(part), mi)
            if part in ARMOR_KINDS:
                return (4, 2, mi, ARMOR_KINDS.index(part))
            if part == "horse_armor":
                return (4, 3, mi, 0)
    for gi, suf in enumerate(("_pottery_sherd", "_smithing_template", "_banner_pattern")):
        if key.endswith(suf):
            return (10, gi, key)  # (at the end of the ingredients, each kind together)
    return (9, key)


for k_, b_ in blocks.items():
    b_["category"] = creative_tab(k_, b_["category"])
for k_, it_ in items.items():
    it_["category"] = creative_tab(k_, it_.get("category", CAT_INGREDIENTS))

# Ids are saved in the worlds (blocks in the chunks, items in inventories and chests): they must never move. The
# order they had is kept in tools/ids_frozen.json; what is new goes after it.
FROZEN = os.path.join(ROOT, "tools", "ids_frozen.json")
if os.path.exists(FROZEN):
    fz = jload(FROZEN)
    lost = [k for k in fz["blocks"] if k not in blocks] + [k for k in fz["items"] if k not in items]
    assert not lost, "these had ids and are gone: " + ", ".join(lost)
    block_keys = fz["blocks"] + [k for k in blocks if k not in set(fz["blocks"])]
    item_keys = fz["items"] + [k for k in items if k not in set(fz["items"])]
    blocks = OrderedDict((k, blocks[k]) for k in block_keys)
    items = OrderedDict((k, items[k]) for k in item_keys)
else:
    block_keys = list(blocks.keys())
    item_keys = list(items.keys())
with open(FROZEN, "w", encoding="utf-8") as f:
    json.dump({"blocks": block_keys, "items": item_keys}, f, indent=0)
ID = {}
for i, k in enumerate(block_keys):
    ID[k] = i + 1
FIRST_ITEM = 1024  # fixed: block ids 1..1023, item ids from here on
assert len(block_keys) + 1 <= FIRST_ITEM
for i, k in enumerate(item_keys):
    ID[k] = FIRST_ITEM + i

ing_sets = []
ing_index = {}


def ing_id(ing):
    s = sorted(ID[x] for x in ingredient_items(ing) if x in ID)
    if not s:
        return None
    t = tuple(s)
    if t not in ing_index:
        ing_index[t] = len(ing_sets)
        ing_sets.append(t)
    return ing_index[t]


shaped, shapeless, cooking = [], [], []
for f in sorted(os.listdir(os.path.join(DM, "recipe"))):
    if not f.endswith(".json") or f.startswith("_"):
        continue
    r = jload(os.path.join(DM, "recipe", f))
    t = strip_ns(r.get("type", ""))
    res = r.get("result")
    if not res:
        continue
    rid = strip_ns(res["id"] if isinstance(res, dict) else res)
    if rid not in ID:
        continue
    count = res.get("count", 1) if isinstance(res, dict) else 1
    if t == "crafting_shaped":
        pat = r["pattern"]
        h, w = len(pat), max(len(p) for p in pat)
        cells = []
        ok = True
        for row in pat:
            for x in range(w):
                ch = row[x] if x < len(row) else " "
                if ch == " ":
                    cells.append(-1)
                    continue
                ii = ing_id(r["key"][ch])
                if ii is None:
                    ok = False
                cells.append(-1 if ii is None else ii)
        if ok:
            shaped.append((w, h, cells, ID[rid], count))
    elif t == "crafting_shapeless":
        ings = [ing_id(x) for x in r["ingredients"]]
        if all(i is not None for i in ings) and len(ings) <= 9:
            shapeless.append((ings, ID[rid], count))
    elif t in ("smelting", "blasting", "smoking"):
        ii = ing_id(r["ingredient"])
        if ii is not None:
            kind = {"smelting": 0, "blasting": 1, "smoking": 2}[t]
            cooking.append((kind, ii, ID[rid], int(r.get("cookingtime", 200 if kind == 0 else 100))))

# ---------------------------------------------------------------- particles & misc tiles
for i in range(10):
    tex_tile(f"block/destroy_stage_{i}")
for name in ["generic_0", "generic_1", "generic_2", "generic_3", "generic_4", "generic_5", "generic_6", "generic_7",
             "spark_0", "spark_1", "spark_2", "spark_3", "spark_4", "spark_5", "spark_6", "spark_7",
             "flash", "flame", "critical_hit", "big_smoke_0", "big_smoke_4", "big_smoke_8", "heart", "angry",
             "splash_0", "splash_1", "splash_2", "splash_3", "bubble", "enchanted_hit", "glint"]:
    try:
        tex_tile("particle/" + name)
    except FileNotFoundError:
        pass

assert len(tiles) <= ATLAS_TILES * ATLAS_TILES, len(tiles)
atlas = np.zeros((ATLAS_PX, ATLAS_PX, 4), dtype=np.uint8)
for i, arr in enumerate(tiles):
    x, y = (i % ATLAS_TILES) * 16, (i // ATLAS_TILES) * 16
    atlas[y:y + 16, x:x + 16] = arr
Image.fromarray(atlas, "RGBA").save(os.path.join(OUT, "atlas.png"))

# ======================================================================= GUI atlas
GUI_W, GUI_H = 1024, 1024
gui = Image.new("RGBA", (GUI_W, GUI_H), (0, 0, 0, 0))
gui_rects = OrderedDict()
shelf = dict(x=0, y=0, h=0)


def gui_add(name, im):
    w, h = im.size
    if shelf["x"] + w > GUI_W:
        shelf["x"] = 0
        shelf["y"] += shelf["h"] + 1
        shelf["h"] = 0
    x, y = shelf["x"], shelf["y"]
    gui.paste(im, (x, y))
    gui_rects[name] = (x, y, w, h)
    shelf["x"] += w + 1
    shelf["h"] = max(shelf["h"], h)


def gimg(rel, crop=None):
    im = Image.open(os.path.join(AM, "textures", rel + ".png")).convert("RGBA")
    return im.crop(crop) if crop else im


gui_add("WIN_INVENTORY", gimg("gui/container/inventory", (0, 0, 176, 166)))
gui_add("WIN_CRAFTING", gimg("gui/container/crafting_table", (0, 0, 176, 166)))
gui_add("WIN_FURNACE", gimg("gui/container/furnace", (0, 0, 176, 166)))
gui_add("WIN_SMOKER", gimg("gui/container/smoker", (0, 0, 176, 166)))
gui_add("WIN_BLAST", gimg("gui/container/blast_furnace", (0, 0, 176, 166)))
chest_top = gimg("gui/container/generic_54", (0, 0, 176, 71))
chest_bot = gimg("gui/container/generic_54", (0, 126, 176, 222))
chest = Image.new("RGBA", (176, 167))
chest.paste(chest_top, (0, 0))
chest.paste(chest_bot, (0, 71))
gui_add("WIN_CHEST", chest)
gui_add("WIN_CREATIVE", gimg("gui/container/creative_inventory/tab_items", (0, 0, 195, 136)))
gui_add("WIN_CREATIVE_INV", gimg("gui/container/creative_inventory/tab_inventory", (0, 0, 195, 136)))
S = "gui/sprites/"
for name, rel in [("HOTBAR", "hud/hotbar"), ("SELECTION", "hud/hotbar_selection"), ("CROSSHAIR", "hud/crosshair"),
                  ("HEART_CONTAINER", "hud/heart/container"), ("HEART_FULL", "hud/heart/full"),
                  ("HEART_HALF", "hud/heart/half"), ("FOOD_EMPTY", "hud/food_empty"), ("FOOD_FULL", "hud/food_full"),
                  ("FOOD_HALF", "hud/food_half"), ("ARMOR_EMPTY", "hud/armor_empty"), ("ARMOR_FULL", "hud/armor_full"),
                  ("ARMOR_HALF", "hud/armor_half"), ("AIR", "hud/air"), ("XP_BG", "hud/experience_bar_background"),
                  ("XP_PROGRESS", "hud/experience_bar_progress"), ("BURN", "container/furnace/burn_progress"),
                  ("LIT", "container/furnace/lit_progress"), ("SCROLLER", "container/creative_inventory/scroller"),
                  ("SCROLLER_OFF", "container/creative_inventory/scroller_disabled"),
                  ("SLOT_HELMET", "container/slot/helmet"), ("SLOT_CHESTPLATE", "container/slot/chestplate"),
                  ("SLOT_LEGGINGS", "container/slot/leggings"), ("SLOT_BOOTS", "container/slot/boots"),
                  ("SLOT_SHIELD", "container/slot/shield"), ("OFFHAND", "hud/hotbar_offhand_left")]:
    try:
        gui_add(name, gimg(S + rel))
    except FileNotFoundError:
        print("missing gui sprite", rel)
for i in range(1, 8):
    for state in ("top_selected", "top_unselected", "bottom_selected", "bottom_unselected"):
        gui_add(f"TAB_{state.upper()}_{i}", gimg(S + f"container/creative_inventory/tab_{state}_{i}"))
# cursor + white pixel
cur_rows = ["X..........", "XX.........", "XWX........", "XWWX.......", "XWWWX......", "XWWWWX.....", "XWWWWWX....",
            "XWWWWWWX...", "XWWWWWWWX..", "XWWWWWWWWX.", "XWWWWWXXXXX", "XWWXWWX....", "XWX.XWWX...", "XX..XWWX...",
            "X....XWWX..", ".....XWWX..", "......XX..."]
cur = Image.new("RGBA", (11, 17))
for y, row in enumerate(cur_rows):
    for x, c in enumerate(row):
        if c != ".":
            cur.putpixel((x, y), (0, 0, 0, 255) if c == "X" else (255, 255, 255, 255))
# status effects (order = enum Effect in the mod) and the HUD pieces that show them
EFFECTS = ("absorption", "regeneration", "fire_resistance", "resistance", "hunger", "poison")
for name, rel in [("HEART_ABSORBING_FULL", "hud/heart/absorbing_full"), ("HEART_ABSORBING_HALF", "hud/heart/absorbing_half"),
                  ("HEART_POISONED_FULL", "hud/heart/poisoned_full"), ("HEART_POISONED_HALF", "hud/heart/poisoned_half"),
                  ("FOOD_EMPTY_HUNGER", "hud/food_empty_hunger"), ("FOOD_FULL_HUNGER", "hud/food_full_hunger"),
                  ("FOOD_HALF_HUNGER", "hud/food_half_hunger"), ("AIR_BURSTING", "hud/air_bursting"),
                  ("EFFECT_BG", "hud/effect_background")]:
    gui_add(name, gimg(S + rel))
for e in EFFECTS:
    gui_add("EFFECT_" + e.upper(), gimg("mob_effect/" + e))
gui_add("CURSOR", cur)
gui_add("WHITE", Image.new("RGBA", (4, 4), (255, 255, 255, 255)))
gui.save(os.path.join(OUT, "gui.png"))

def make_logo():
    """'GTA SA' in the style of the Minecraft title logo: stone letters, a dark extruded underside, black rim."""
    glyphs = {
        "G": ["####", "#...", "#.##", "#..#", "####"],
        "T": ["#####", "..#..", "..#..", "..#..", "..#.."],
        "A": ["####", "#..#", "####", "#..#", "#..#"],
        "S": ["####", "#...", "####", "...#", "####"],
    }
    C, GAP, DEPTH, RIM = 40, 15, 38, 6
    W, H = 1024, 256
    word = "GTA SA"
    widths = [C if ch == " " else len(glyphs[ch][0]) * C for ch in word]
    total = sum(widths) + GAP * (len(word) - 1)
    x = (W - total) // 2
    top = RIM + 2
    face = np.zeros((H, W), bool)
    for ch, w in zip(word, widths):
        if ch != " ":
            for r, row in enumerate(glyphs[ch]):
                for c, cell in enumerate(row):
                    if cell == "#":
                        face[top + r * C:top + (r + 1) * C, x + c * C:x + (c + 1) * C] = True
        x += w + GAP
    # stone texture, one texel = 5 px, lit like the logo's face
    stone = np.asarray(gimg("block/stone").convert("L"), dtype=np.float32)
    texel = C // 8
    yy, xx = np.mgrid[0:H, 0:W]
    lum = stone[(yy // texel) % 16, (xx // texel) % 16]
    lum = lum / lum.mean()
    logo = np.asarray(gimg("gui/title/minecraft"), dtype=np.float32)
    bright = logo[..., 3] > 200
    lumL = logo[..., :3].mean(axis=2)
    sel = bright & (lumL > 150)
    base = logo[sel][:, :3].mean(axis=0) if sel.any() else np.array([186, 177, 175], np.float32)
    rgb = np.zeros((H, W, 3), np.float32)
    alpha = np.zeros((H, W), np.float32)
    # rim: everything dilated
    body = face.copy()
    for k in range(1, DEPTH + 1):
        body[k:] |= face[:-k]
    rim = body.copy()
    for dy in range(-RIM, RIM + 1):
        for dx in range(-RIM, RIM + 1):
            if dx * dx + dy * dy <= RIM * RIM:
                rim |= np.roll(np.roll(body, dy, axis=0), dx, axis=1)
    alpha[rim] = 255
    # underside, darker the deeper
    for k in range(DEPTH, 0, -1):
        layer = np.zeros_like(face)
        layer[k:] = face[:-k]
        t = k / DEPTH
        shade = (0.58 - 0.30 * t)
        rgb[layer] = (base * shade)[None, :] * lum[layer][:, None]
    # face: a little lighter at the top of each letter, edges bevelled
    rows = ((yy - top) % C) / C
    light = 1.06 - 0.10 * ((yy - top) / (5 * C))
    col = base[None, None, :] * (lum * light)[..., None]
    rgb[face] = col[face]
    up = np.zeros_like(face); up[3:] = face[:-3]
    left = np.zeros_like(face); left[:, 3:] = face[:, :-3]
    down = np.zeros_like(face); down[:-3] = face[3:]
    right = np.zeros_like(face); right[:, :-3] = face[:, 3:]
    edge_hi = face & (~up | ~left)
    edge_lo = face & (~down | ~right) & ~edge_hi
    rgb[edge_hi] = np.minimum(rgb[edge_hi] * 1.18, 255)
    rgb[edge_lo] = rgb[edge_lo] * 0.80
    out = np.dstack([np.clip(rgb, 0, 255), alpha]).astype(np.uint8)
    return Image.fromarray(out, "RGBA").resize((512, 128), Image.LANCZOS)


# ======================================================================= title screen (menu.png)
MENU_W, MENU_H = 1024, 512
menu = Image.new("RGBA", (MENU_W, MENU_H), (0, 0, 0, 0))
menu_rects = OrderedDict()


def menu_add(name, im, x, y):
    im = im.convert("RGBA")
    menu.paste(im, (x, y))
    menu_rects[name] = (x, y, im.size[0], im.size[1])


# the four sides of the panorama cube, next to each other: a strip that scrolls by
for i in range(4):
    face = gimg("gui/title/background/panorama_%d" % i).resize((256, 256), Image.LANCZOS)
    menu.paste(face, (i * 256, 0))
menu_rects["PANORAMA"] = (0, 0, 1024, 256)
menu_add("LOGO", make_logo(), 0, 258)
menu_add("EDITION", gimg("gui/title/edition").resize((256, 32), Image.LANCZOS), 0, 388)
menu_add("BUTTON", gimg("gui/sprites/widget/button"), 516, 258)
menu_add("BUTTON_HI", gimg("gui/sprites/widget/button_highlighted"), 516, 280)
menu_add("BUTTON_OFF", gimg("gui/sprites/widget/button_disabled"), 516, 302)
menu_add("BG", gimg("gui/menu_background"), 720, 258)
menu_add("LIST_BG", gimg("gui/menu_list_background"), 740, 258)
menu_add("HEADER_SEP", gimg("gui/header_separator"), 760, 258)
menu_add("FOOTER_SEP", gimg("gui/footer_separator"), 760, 264)
menu_add("JOIN", gimg("gui/sprites/world_list/join"), 800, 258)
menu_add("JOIN_HI", gimg("gui/sprites/world_list/join_highlighted"), 836, 258)
menu_add("PACK", gimg("misc/unknown_pack").resize((32, 32), Image.LANCZOS), 872, 258)
menu.save(os.path.join(OUT, "menu.png"))

# texts of the screens the mod draws itself
MENU_STRINGS = OrderedDict([
    ("SINGLEPLAYER", "menu.singleplayer"), ("OPTIONS", "menu.options"), ("QUIT", "menu.quit"),
    ("SELECT_WORLD", "selectWorld.title"), ("PLAY_WORLD", "selectWorld.select"), ("CREATE_WORLD", "selectWorld.create"),
    ("DELETE", "selectWorld.delete"), ("CANCEL", "gui.cancel"), ("BACK", "gui.back"), ("RETURN_TO_GAME", "menu.returnToGame"),
    ("GAME_MENU", "menu.game"), ("STATS", "gui.stats"), ("SURVIVAL", "gameMode.survival"), ("CREATIVE", "gameMode.creative"),
    ("NEW_WORLD", "selectWorld.newWorld"), ("DELETE_QUESTION", "selectWorld.deleteQuestion"),
    ("DELETE_WARNING", "selectWorld.deleteWarning"), ("YOU_DIED", "deathScreen.title"), ("RESPAWN", "deathScreen.respawn"),
    ("TITLE_SCREEN", "deathScreen.titleScreen"), ("SCORE", "deathScreen.score.value"),
    ("DEATH_GENERIC", "death.attack.generic"), ("DEATH_DROWN", "death.attack.drown"), ("DEATH_LAVA", "death.attack.lava"),
    ("DEATH_FALL", "death.attack.fall"), ("DEATH_FIRE", "death.attack.onFire"), ("DEATH_EXPLOSION", "death.attack.explosion"),
    ("DEATH_STARVE", "death.attack.starve"), ("WORLDS", "selectWorld.world"),
    ("LANGUAGE", "options.language"), ("LANGUAGE_TITLE", "options.language.title"), ("DONE", "gui.done"),
    ("SET_SPAWN", "block.minecraft.set_spawn"), ("NO_SLEEP", "block.minecraft.bed.no_sleep"),
    ("NOT_SAFE", "block.minecraft.bed.not_safe"), ("NO_SPAWN", "block.minecraft.spawn.not_valid"),
    ("GAMEMODE_SET", "commands.gamemode.success.self"), ("CRAFTING", "container.crafting"),
    ("INVENTORY", "container.inventory"),
    ("TAB_BUILDING", "itemGroup.buildingBlocks"), ("TAB_COLORED", "itemGroup.coloredBlocks"),
    ("TAB_NATURAL", "itemGroup.natural"), ("TAB_FUNCTIONAL", "itemGroup.functional"), ("TAB_TOOLS", "itemGroup.tools"),
    ("TAB_COMBAT", "itemGroup.combat"), ("TAB_FOOD", "itemGroup.foodAndDrink"), ("TAB_INGREDIENTS", "itemGroup.ingredients"),
    ("TAB_REDSTONE", "itemGroup.redstone"), ("TAB_SPAWN_EGGS", "itemGroup.spawnEggs"), ("TAB_INVENTORY", "itemGroup.inventory"),
    ("VILLAGER", "entity.minecraft.villager.none"), ("FARMER", "entity.minecraft.villager.farmer"),
    ("LIBRARIAN", "entity.minecraft.villager.librarian"), ("BUTCHER", "entity.minecraft.villager.butcher"),
    ("CLERIC", "entity.minecraft.villager.cleric"), ("ARMORER", "entity.minecraft.villager.armorer"),
])

# ======================================================================= font
FONT_COLS, FONT_ROWS = 32, 16
font = Image.new("RGBA", (FONT_COLS * 16, FONT_ROWS * 16), (0, 0, 0, 0))
font_widths = [0] * (FONT_COLS * FONT_ROWS)
cp_slot = {}
ascii_img = Image.open(os.path.join(AM, "textures/font/ascii.png")).convert("RGBA")
acc_img = Image.open(os.path.join(AM, "textures/font/accented.png")).convert("RGBA")


def put_glyph(slot, src, sx, sy, cw, ch, yoff):
    g = src.crop((sx, sy, sx + cw, sy + ch))
    a = np.array(g)[..., 3]
    cols = np.where(a.max(axis=0) > 0)[0]
    width = int(cols.max()) + 1 if len(cols) else 0
    cx, cy = (slot % FONT_COLS) * 16, (slot // FONT_COLS) * 16
    font.paste(g, (cx, cy + yoff))
    font_widths[slot] = width


for c in range(32, 127):
    put_glyph(c, ascii_img, (c % 16) * 8, (c // 16) * 8, 8, 8, 4)
font_widths[32] = 3
defj = jload(os.path.join(AM, "font/include/default.json"))
acc_chars = None
for p in defj["providers"]:
    if p.get("file", "").endswith("accented.png"):
        acc_chars = p["chars"]
slot = 128
for r, row in enumerate(acc_chars):
    for col, ch in enumerate(row):
        cp = ord(ch)
        if ch == "\u0000" or not (0xA0 <= cp <= 0x17F) or slot >= FONT_COLS * FONT_ROWS:
            continue
        put_glyph(slot, acc_img, col * 9, r * 12, 9, 12, 1)
        cp_slot[cp] = slot
        slot += 1
font.save(os.path.join(OUT, "font.png"))

# ======================================================================= entity textures
ENT_W, ENT_H = 512, 512
ent = Image.new("RGBA", (ENT_W, ENT_H), (0, 0, 0, 0))
ent_rects = OrderedDict()
ent_shelf = dict(x=0, y=0, h=0)


def ent_add(name, im):
    w_, h_ = im.size
    if ent_shelf["x"] + w_ > ENT_W:
        ent_shelf["x"] = 0
        ent_shelf["y"] += ent_shelf["h"]
        ent_shelf["h"] = 0
    x, y = ent_shelf["x"], ent_shelf["y"]
    assert y + h_ <= ENT_H, "entity atlas full"
    ent.paste(im, (x, y))
    ent_rects[name] = (x, y, w_, h_)
    ent_shelf["x"] += w_
    ent_shelf["h"] = max(ent_shelf["h"], h_)


def villager_skin(profession):
    base = gimg("entity/villager/villager")
    base.alpha_composite(gimg("entity/villager/type/plains"))
    if profession:
        base.alpha_composite(gimg("entity/villager/profession/" + profession).crop((0, 0, 64, 64)))
    return base


ent_add("STEVE", gimg("entity/player/wide/steve"))
ent_add("COW", gimg("entity/cow/cow_temperate"))
ent_add("PIG", gimg("entity/pig/pig_temperate"))
for i, prof in enumerate([None, "farmer", "librarian", "butcher", "cleric", "armorer"]):
    ent_add(f"VILLAGER_{i}", villager_skin(prof))
ent_add("PILLAGER", gimg("entity/illager/pillager"))
ent_add("VINDICATOR", gimg("entity/illager/vindicator"))
ent_add("SHADOW", gimg("misc/shadow"))
ent_add("ELYTRA", gimg("entity/equipment/wings/elytra"))
ent_add("SHEEP", gimg("entity/sheep/sheep"))
ent_add("SHEEP_WOOL", gimg("entity/sheep/sheep_wool"))
ent_add("CHICKEN", gimg("entity/chicken/chicken_temperate"))
ent_add("ARROW", gimg("entity/projectiles/arrow"))
ent_add("HOOK", gimg("entity/fishing/fishing_hook"))
white = Image.new("RGBA", (8, 8), (255, 255, 255, 255))
ent_add("WHITE", white)
# armour: outer layer (helmet, chestplate, boots) and inner layer (leggings) per material
ARMOR_TEX = OrderedDict([("chainmail", "chainmail"), ("iron", "iron"), ("golden", "gold"), ("diamond", "diamond"),
                         ("netherite", "netherite"), ("copper", "copper"), ("turtle", "turtle_scute")])
for mat, fname in ARMOR_TEX.items():
    ent_add(f"ARMOR_{mat.upper()}_1", gimg("entity/equipment/humanoid/" + fname))
    inner = os.path.join(AM, "textures", "entity", "equipment", "humanoid_leggings", fname + ".png")
    if os.path.exists(inner):
        ent_add(f"ARMOR_{mat.upper()}_2", gimg("entity/equipment/humanoid_leggings/" + fname))
    else:
        ent_rects[f"ARMOR_{mat.upper()}_2"] = ent_rects[f"ARMOR_{mat.upper()}_1"]
ent_add("PIG_SADDLE", gimg("entity/equipment/pig_saddle/saddle"))
ent_add("XP_ORB", gimg("entity/experience/experience_orb"))
ent_add("TRIDENT", gimg("entity/trident/trident"))
ent_add("CREEPER", gimg("entity/creeper/creeper"))
ent_add("WARDEN", gimg("entity/warden/warden"))
ent.save(os.path.join(OUT, "entity.png"))

# ======================================================================= sounds
SJ = jload(os.path.join(AM, "sounds.json"))
SOUND_EVENTS = OrderedDict([
    ("DIG_STONE", "block.stone.break"), ("DIG_WOOD", "block.wood.break"), ("DIG_GRAVEL", "block.gravel.break"),
    ("DIG_GRASS", "block.grass.break"), ("DIG_SAND", "block.sand.break"), ("DIG_GLASS", "block.glass.break"),
    ("DIG_WOOL", "block.wool.break"), ("DIG_METAL", "block.metal.break"), ("DIG_SNOW", "block.snow.break"),
    ("HIT_STONE", "block.stone.hit"), ("HIT_WOOD", "block.wood.hit"), ("HIT_GRAVEL", "block.gravel.hit"),
    ("HIT_GRASS", "block.grass.hit"), ("HIT_SAND", "block.sand.hit"), ("HIT_GLASS", "block.glass.hit"),
    ("HIT_WOOL", "block.wool.hit"), ("HIT_METAL", "block.metal.hit"), ("HIT_SNOW", "block.snow.hit"),
    ("PICKUP", "entity.item.pickup"), ("EAT", "entity.generic.eat"), ("BURP", "entity.player.burp"),
    ("BOW_SHOOT", "entity.arrow.shoot"), ("ARROW_HIT", "entity.arrow.hit"), ("ARROW_HIT_PLAYER", "entity.arrow.hit_player"),
    ("IGNITE", "item.flintandsteel.use"), ("FUSE", "entity.tnt.primed"), ("EXPLODE", "entity.generic.explode"),
    ("FIREWORK_LAUNCH", "entity.firework_rocket.launch"), ("FIREWORK_BLAST", "entity.firework_rocket.blast"),
    ("FIREWORK_TWINKLE", "entity.firework_rocket.twinkle"), ("ELYTRA_FLYING", "item.elytra.flying"),
    ("CHEST_OPEN", "block.chest.open"), ("CHEST_CLOSE", "block.chest.close"), ("CLICK", "ui.button.click"),
    ("ATTACK_SWEEP", "entity.player.attack.sweep"), ("ATTACK_STRONG", "entity.player.attack.strong"),
    ("ATTACK_WEAK", "entity.player.attack.weak"), ("ATTACK_CRIT", "entity.player.attack.crit"),
    ("HURT", "entity.player.hurt"), ("EQUIP_ELYTRA", "item.armor.equip_elytra"),
    ("EQUIP_GENERIC", "item.armor.equip_generic"), ("TOOL_BREAK", "entity.item.break"),
    ("THROW", "entity.snowball.throw"), ("PEARL_THROW", "entity.ender_pearl.throw"),
    ("TELEPORT", "entity.enderman.teleport"), ("FIRE_CRACKLE", "block.furnace.fire_crackle"),
    ("PLACE_STONE", "block.stone.place"), ("PLACE_WOOD", "block.wood.place"), ("PLACE_GRAVEL", "block.gravel.place"),
    ("PLACE_GRASS", "block.grass.place"), ("PLACE_SAND", "block.sand.place"), ("PLACE_GLASS", "block.glass.place"),
    ("PLACE_WOOL", "block.wool.place"), ("PLACE_METAL", "block.metal.place"), ("PLACE_SNOW", "block.snow.place"),
    ("COW_SAY", "entity.cow.ambient"), ("COW_HURT", "entity.cow.hurt"), ("COW_DEATH", "entity.cow.death"),
    ("COW_MILK", "entity.cow.milk"),
    ("PIG_SAY", "entity.pig.ambient"), ("PIG_HURT", "entity.pig.hurt"), ("PIG_DEATH", "entity.pig.death"),
    ("SHEEP_SAY", "entity.sheep.ambient"), ("SHEEP_HURT", "entity.sheep.hurt"), ("SHEEP_DEATH", "entity.sheep.death"),
    ("SHEEP_SHEAR", "entity.sheep.shear"),
    ("CHICKEN_SAY", "entity.chicken.ambient"), ("CHICKEN_HURT", "entity.chicken.hurt"),
    ("CHICKEN_DEATH", "entity.chicken.death"), ("CHICKEN_EGG", "entity.chicken.egg"),
    ("VILLAGER_SAY", "entity.villager.ambient"), ("VILLAGER_HURT", "entity.villager.hurt"),
    ("VILLAGER_DEATH", "entity.villager.death"),
    ("PILLAGER_SAY", "entity.pillager.ambient"), ("PILLAGER_HURT", "entity.pillager.hurt"),
    ("PILLAGER_DEATH", "entity.pillager.death"),
    ("BOBBER_THROW", "entity.fishing_bobber.throw"), ("BOBBER_RETRIEVE", "entity.fishing_bobber.retrieve"),
    ("BOBBER_SPLASH", "entity.fishing_bobber.splash"), ("WAND", "entity.evoker.cast_spell"),
    ("KNOCKBACK", "entity.player.attack.knockback"), ("POP", "entity.chicken.egg"),
    ("CROSSBOW_SHOOT", "item.crossbow.shoot"), ("THUNDER", "entity.lightning_bolt.thunder"),
    ("LIGHTNING_IMPACT", "entity.lightning_bolt.impact"), ("FIREBALL", "entity.ghast.shoot"),
    ("WIND_THROW", "entity.wind_charge.throw"), ("WIND_BURST", "entity.wind_charge.wind_burst"),
    ("TOTEM", "item.totem.use"), ("DRINK", "entity.generic.drink"), ("VILLAGER_YES", "entity.villager.yes"),
    ("VILLAGER_NO", "entity.villager.no"), ("VILLAGER_TRADE", "entity.villager.trade"),
    ("SPYGLASS", "item.spyglass.use"), ("SADDLE", "entity.pig.saddle"), ("SPLASH", "entity.player.splash"),
    ("SWIM", "entity.player.swim"), ("TRIDENT_THROW", "item.trident.throw"), ("TRIDENT_HIT", "item.trident.hit_ground"),
    ("PLAYER_DEATH", "entity.player.death"), ("LEVELUP", "entity.player.levelup"), ("NOTE", "block.note_block.harp"),
    ("EQUIP_IRON", "item.armor.equip_iron"), ("ANVIL", "block.anvil.land"),
    ("XP_ORB", "entity.experience_orb.pickup"), ("BUCKET_FILL", "item.bucket.fill"),
    ("BUCKET_EMPTY", "item.bucket.empty"), ("BUCKET_FILL_LAVA", "item.bucket.fill_lava"),
    ("BUCKET_EMPTY_LAVA", "item.bucket.empty_lava"), ("LAVA_POP", "block.lava.pop"),
    ("LAVA_EXTINGUISH", "block.lava.extinguish"), ("BONE_MEAL", "item.bone_meal.use"),
    ("FALL_SMALL", "entity.player.small_fall"), ("FALL_BIG", "entity.player.big_fall"),
    ("STEP_STONE", "block.stone.step"), ("STEP_WOOD", "block.wood.step"), ("STEP_GRAVEL", "block.gravel.step"),
    ("STEP_GRASS", "block.grass.step"), ("STEP_SAND", "block.sand.step"), ("STEP_GLASS", "block.glass.step"),
    ("STEP_WOOL", "block.wool.step"), ("STEP_METAL", "block.metal.step"), ("STEP_SNOW", "block.snow.step"),
    ("FIRE_AMBIENT", "block.fire.ambient"), ("FIRE_EXTINGUISH", "block.fire.extinguish"),
    ("LAVA_AMBIENT", "block.lava.ambient"), ("BURN", "entity.player.hurt_on_fire"),
    ("CREEPER_PRIMED", "entity.creeper.primed"), ("CREEPER_HURT", "entity.creeper.hurt"),
    ("CREEPER_DEATH", "entity.creeper.death"),
    ("CLICK", "ui.button.click"),
    ("WARDEN_AMBIENT", "entity.warden.ambient"), ("WARDEN_HURT", "entity.warden.hurt"), ("WARDEN_DEATH", "entity.warden.death"),
    ("WARDEN_ATTACK", "entity.warden.attack_impact"), ("WARDEN_ROAR", "entity.warden.roar"),
    ("WARDEN_SONIC_CHARGE", "entity.warden.sonic_charge"), ("WARDEN_SONIC_BOOM", "entity.warden.sonic_boom"),
    ("WARDEN_HEARTBEAT", "entity.warden.heartbeat"), ("WARDEN_LISTENING", "entity.warden.listening"),
    ("WARDEN_LISTENING_ANGRY", "entity.warden.listening_angry"), ("WARDEN_SNIFF", "entity.warden.sniff"),
    ("WARDEN_DIG", "entity.warden.dig"),
])


def sound_files(event, depth=0):
    e = SJ.get(event)
    if not e or depth > 4:
        return []
    out = []
    for s in e.get("sounds", []):
        if isinstance(s, str):
            out.append((s, 1.0, 1.0))
        elif s.get("type") == "event":
            out += sound_files(s["name"], depth + 1)
        else:
            out.append((s["name"], float(s.get("volume", 1.0)), float(s.get("pitch", 1.0))))
    return out


sound_rows = []  # (enum, first, count)
sound_files_flat = []
for enum, ev in SOUND_EVENTS.items():
    files = sound_files(ev)
    first = len(sound_files_flat)
    for name, vol, pitch in files:
        name = strip_ns(name)
        src = os.path.join(AM, "sounds", name + ".ogg")
        if not os.path.exists(src):
            continue
        dst_name = name.replace("/", "_") + ".ogg"
        shutil.copyfile(src, os.path.join(OUT, "sounds", dst_name))
        sound_files_flat.append((dst_name, vol, pitch))
    sound_rows.append((enum, first, len(sound_files_flat) - first))

# ======================================================================= C++ output
def cname(k):
    return "ID_" + k.upper()


def resolve_display(model):
    """ground / first person / third person transforms, child models override their parents"""
    out = {}
    name = model
    for _ in range(12):
        path = os.path.join(AM, "models", strip_ns(name) + ".json")
        if not os.path.exists(path):
            break
        m = jload(path)
        for ctx, d in m.get("display", {}).items():
            out.setdefault(ctx, d)
        par = m.get("parent")
        if not par:
            break
        name = par
    res = []
    for ctx in ("ground", "firstperson_righthand", "thirdperson_righthand"):
        d = out.get(ctx, {})
        res += list(d.get("rotation", [0, 0, 0])) + list(d.get("translation", [0, 0, 0])) + list(d.get("scale", [1, 1, 1]))
    return tuple(round(float(v), 4) for v in res)


BLOCK_DISPLAY = resolve_display("block/block")
display_sets = []
display_index = {}
item_display = [0] * (FIRST_ITEM + len(item_keys))
for k in block_keys + item_keys:
    model = blocks[k]["model"] if k in blocks else items[k]["model"]
    d = resolve_display(model)
    if k in blocks and d == resolve_display("__none__"):
        d = BLOCK_DISPLAY
    if k in blocks and not os.path.exists(os.path.join(AM, "models", model + ".json")):
        d = BLOCK_DISPLAY
    if d not in display_index:
        display_index[d] = len(display_sets)
        display_sets.append(d)
    item_display[ID[k]] = display_index[d]


with open(os.path.join(GEN, "Assets.h"), "w", encoding="utf-8") as f:
    w = f.write
    w("// AUTO-GENERATED by tools/gen_assets.py from the Minecraft assets - do not edit\n#pragma once\n#include <cstdint>\n\n#include \"../Lang.h\"\n\nnamespace mc {\n\n")
    w(f"constexpr int ATLAS_SIZE = {ATLAS_PX};\nconstexpr int ATLAS_TILES_PER_ROW = {ATLAS_TILES};\n")
    w("struct AnimDef { uint16_t tile; uint8_t frames, ticks, px; };\n")
    w("enum AnimId : uint8_t { ANIM_NONE = 0, " + ", ".join("ANIM_" + k for k in ANIMS) + ", ANIM_COUNT };\n")
    w("constexpr AnimDef kAnims[ANIM_COUNT] = { { 0, 1, 1, 16 }, " +
      ", ".join("{ %d, %d, %d, %d }" % v for v in ANIMS.values()) + " };\n\n")
    special_tiles = {f"DESTROY_{i}": tile_index[f"block/destroy_stage_{i}"] for i in range(10)}
    special_tiles["FISHING_ROD_CAST"] = tile_index["item/fishing_rod_cast"]
    for i in range(3):
        special_tiles[f"CROSSBOW_PULLING_{i}"] = tile_index[f"item/crossbow_pulling_{i}"]
    special_tiles["CROSSBOW_ARROW"] = tile_index["item/crossbow_arrow"]
    special_tiles["WATER_STILL"] = blocks["water"]["tex"]["top"]
    special_tiles["WATER_FLOW"] = blocks["water"]["tex"]["side"]
    special_tiles["LAVA_STILL"] = blocks["lava"]["tex"]["top"]
    special_tiles["LAVA_FLOW"] = blocks["lava"]["tex"]["side"]
    special_tiles["FIRE_0"] = ANIMS["FIRE_0"][0]
    special_tiles["FIRE_1"] = ANIMS["FIRE_1"][0]
    special_tiles["CROSSBOW_FIREWORK"] = tile_index["item/crossbow_firework"]
    for k in ("generic_0", "spark_0", "flash", "flame", "critical_hit", "big_smoke_0", "heart", "splash_0", "splash_1",
              "splash_2", "splash_3", "bubble", "enchanted_hit", "glint", "lava", "drip_fall"):
        if "particle/" + k in tile_index:
            special_tiles["P_" + k.upper()] = tile_index["particle/" + k]
    for i in range(3):
        special_tiles[f"BOW_PULLING_{i}"] = tile_index[f"item/bow_pulling_{i}"]
    for i in range(8):
        if f"particle/spark_{i}" in tile_index:
            special_tiles[f"P_SPARK_{i}"] = tile_index[f"particle/spark_{i}"]
        if f"particle/generic_{i}" in tile_index:
            special_tiles[f"P_GENERIC_{i}"] = tile_index[f"particle/generic_{i}"]
    w("enum Tile : uint16_t {\n")
    for k, v in special_tiles.items():
        w(f"    TILE_{k} = {v},\n")
    w("};\n\nstruct GuiRect { int x, y, w, h; };\n")
    w(f"constexpr int GUI_TEX_W = {GUI_W};\nconstexpr int GUI_TEX_H = {GUI_H};\n")
    for k, (x, y, ww, hh) in gui_rects.items():
        w(f"constexpr GuiRect GUI_{k} = {{ {x}, {y}, {ww}, {hh} }};\n")
    def avg_color(t):
        a = tiles[t].astype(np.float64)
        wgt = a[..., 3] / 255.0
        tot = wgt.sum()
        if tot < 1e-6:
            return 0
        r, g, b = [int(round((a[..., i] * wgt).sum() / tot)) for i in range(3)]
        return (r << 16) | (g << 8) | b

    def side_tile(b):
        t = b["tex"]
        for k in ("side", "north", "east", "top"):
            if t.get(k) is not None:
                return t[k]
        return 0

    cols = [0] + [avg_color(side_tile(blocks[k])) for k in blocks]
    w("\n// average colour (0xRRGGBB) of every block's side texture\n")
    w(f"constexpr uint32_t kBlockColor[{len(cols)}] = {{\n")
    for i in range(0, len(cols), 12):
        w("    " + ", ".join("0x%06X" % c for c in cols[i:i + 12]) + ",\n")
    w("};\n")
    w("\nenum Effect : uint8_t { " + ", ".join("EFFECT_" + e.upper() for e in EFFECTS) + ", EFFECT_COUNT };\n")
    w("constexpr GuiRect kEffectIcons[EFFECT_COUNT] = { " + ", ".join("GUI_EFFECT_" + e.upper() for e in EFFECTS) + " };\n")
    w("inline const char* const kEffectNames[LANG_COUNT][EFFECT_COUNT] = {\n    { " +
      ", ".join(cstr(LANG_EN.get("effect.minecraft." + e) or e) for e in EFFECTS) + " },\n    { " +
      ", ".join(cstr(LANG.get("effect.minecraft." + e) or LANG_EN.get("effect.minecraft." + e) or e) for e in EFFECTS) + " } };\n")
    w("inline const char* EffectName(int e) { return kEffectNames[gLanguage][e]; }\n")
    w(f"\nconstexpr int FONT_COLS = {FONT_COLS};\nconstexpr int FONT_ROWS = {FONT_ROWS};\n")
    w(f"constexpr uint8_t FONT_WIDTHS[{len(font_widths)}] = {{\n")
    for i in range(0, len(font_widths), 32):
        w("    " + ", ".join(str(x) for x in font_widths[i:i + 32]) + ",\n")
    w("};\n\ninline int FontSlotForCodepoint(uint32_t cp) {\n    if (cp >= 32 && cp < 127) return (int)cp;\n    switch (cp) {\n")
    for cp, s in cp_slot.items():
        w(f"    case 0x{cp:04X}: return {s};\n")
    w("    default: return '?';\n    }\n}\n\n")
    w(f"// menu.png layout (title screen)\nconstexpr int MENU_TEX_W = {MENU_W}, MENU_TEX_H = {MENU_H};\n")
    for k, (x, y, ww, hh) in menu_rects.items():
        w(f"constexpr GuiRect MENU_{k} = {{ {x}, {y}, {ww}, {hh} }};\n")
    w("enum Str { " + ", ".join("STR_" + k for k in MENU_STRINGS) + ", STR_COUNT };\n")
    w("// [language][text]: English, Turkish\ninline const char* const kStrs[LANG_COUNT][STR_COUNT] = {\n")
    for lang in (LANG_EN, LANG):
        w("    {\n")
        for k, key in MENU_STRINGS.items():
            w("        " + cstr(lang.get(key) or LANG_EN.get(key) or k) + ",\n")
        w("    },\n")
    w("};\ninline const char* LangStr(int s) { return kStrs[gLanguage][s]; }\n\n")
    w(f"// entity.png layout\nconstexpr int ENT_TEX_W = {ENT_W}, ENT_TEX_H = {ENT_H};\n")
    for k, (x, y, ww, hh) in ent_rects.items():
        w(f"constexpr GuiRect ENT_{k} = {{ {x}, {y}, {ww}, {hh} }};\n")
    w(f"\n// armour materials: item keys start with these names\nconstexpr int NUM_ARMOR_MATS = {len(ARMOR_TEX)};\n")
    w("constexpr const char* ARMOR_MAT_KEYS[NUM_ARMOR_MATS] = { " + ", ".join(f'"{m}_"' for m in ARMOR_TEX) + " };\n")
    w("constexpr GuiRect ENT_ARMOR_OUTER[NUM_ARMOR_MATS] = { " + ", ".join(f"ENT_ARMOR_{m.upper()}_1" for m in ARMOR_TEX) + " };\n")
    w("constexpr GuiRect ENT_ARMOR_INNER[NUM_ARMOR_MATS] = { " + ", ".join(f"ENT_ARMOR_{m.upper()}_2" for m in ARMOR_TEX) + " };\n")
    w("\n} // namespace mc\n")

# the creative tabs' order (the game splits it into the tabs)
def creative_key(k_):
    cat_ = (blocks[k_] if k_ in blocks else items[k_])["category"]
    return cat_, creative_rank(k_, cat_)


creative = sorted(block_keys + item_keys, key=creative_key)
with open(os.path.join(GEN, "CreativeOrder.cpp"), "w", encoding="utf-8") as f:
    w = f.write
    w('// AUTO-GENERATED by tools/gen_assets.py - do not edit\n#include "../GameTables.h"\n\nnamespace mc {\n\n')
    w("// every block and item, in the order of Minecraft's creative tabs\n")
    w(f"const int kCreativeOrderCount = {len(creative)};\nconst uint16_t kCreativeOrder[] = {{\n")
    for i in range(0, len(creative), 16):
        w("    " + ", ".join(str(ID[k_]) for k_ in creative[i:i + 16]) + ",\n")
    w("};\n\n} // namespace mc\n")

with open(os.path.join(GEN, "GameData.h"), "w", encoding="utf-8") as f:
    w = f.write
    w("// AUTO-GENERATED by tools/gen_assets.py - do not edit\n#pragma once\n#include <cstdint>\n\nnamespace mc {\n\n")
    w(f"constexpr int NUM_BLOCKS = {len(block_keys) + 1}; // including air (0)\n")
    w(f"constexpr int FIRST_ITEM = {FIRST_ITEM};\nconstexpr int ITEM_END = {FIRST_ITEM + len(item_keys)};\n\n")
    w("constexpr uint16_t ID_AIR = 0;\n")
    for k in block_keys + item_keys:
        w(f"constexpr uint16_t {cname(k)} = {ID[k]};\n")
    w("\nenum SoundEvent : uint16_t {\n")
    for enum, _, _ in sound_rows:
        w(f"    SND_{enum},\n")
    w("    SND_COUNT\n};\n\n} // namespace mc\n")

with open(os.path.join(GEN, "GameData.cpp"), "w", encoding="utf-8") as f:
    w = f.write
    w('// AUTO-GENERATED by tools/gen_assets.py - do not edit\n#include "../GameTables.h"\n\nnamespace mc {\n\n')
    kind_enum = {"all": "SHAPE_CUBE", "bottom_top": "SHAPE_CUBE", "column": "SHAPE_COLUMN", "facing": "SHAPE_FACING",
                 "cube6": "SHAPE_FACING", "cross": "SHAPE_CROSS", "fluid": "SHAPE_FLUID",
                 "fire": "SHAPE_FIRE", "stairs": "SHAPE_STAIRS", "slab": "SHAPE_SLAB",
                 "pane": "SHAPE_PANE", "fence": "SHAPE_FENCE", "wall": "SHAPE_WALL", "bed": "SHAPE_BED",
                 "bed_head": "SHAPE_BED_HEAD"}
    w("const BlockDef kBlockDefs[NUM_BLOCKS] = {\n")
    w('    { "air", { "Air", "Hava" }, { 0, 0, 0, 0, 0, 0 }, 0xFFFF, SHAPE_CUBE, RENDER_AIR, 0.0f, 0, 0, false, 0, 0, 0, false, 0, {} },\n')
    for k in block_keys:
        b = blocks[k]
        t = b["tex"]
        if b["kind"] in ("cube6", "bed", "bed_head"):
            faces = [t["east"], t["west"], t["north"], t["south"], t["top"], t["bottom"]]
            lit = 0xFFFF
        elif b["kind"] == "facing":
            # default facing north: front on the north face
            faces = [t["side"], t["side"], t["front"], t["side"], t["top"], t["bottom"]]
            lit = t["front_lit"] if t.get("front_lit") is not None else 0xFFFF
        else:
            faces = [t["side"], t["side"], t["side"], t["side"], t["top"], t["bottom"]]
            lit = 0xFFFF
        drops = []
        for (dk, lo, hi, ch, grp) in b["drops"]:
            if dk in ID:
                drops.append(f"{{ {ID[dk]}, {lo}, {hi}, {ch:.4f}f, {grp} }}")
        drop_s = "{ " + ", ".join(drops) + " }" if drops else "{}"
        render = ["RENDER_OPAQUE", "RENDER_CUTOUT", "RENDER_TRANSLUCENT"][b["render"]]
        w(f'    {{ "{k}", {{ {cstr(b["name_en"])}, {cstr(b["name"])} }}, {{ {", ".join(str(x) for x in faces)} }}, {lit}, {kind_enum[b["kind"]]}, {render}, '
          f'{b["hardness"]:.2f}f, {b["tool"]}, {b["tier"]}, {"true" if b["requires"] else "false"}, {b["surface"]}, '
          f'{b["sound"]}, {b["category"]}, {"true" if b["emissive"] else "false"}, {len(drops)}, {drop_s} }},\n')
    w("};\n\n")
    w(f"const ItemDef kItemDefs[{max(1, len(item_keys))}] = {{\n")
    for k in item_keys:
        it = items[k]
        w(f'    {{ "{k}", {{ {cstr(it["name_en"])}, {cstr(it["name"])} }}, {it["tile"]}, {it["stack"]}, {it["tool"]}, {it["tier"]}, {it["speed"]:.2f}f, '
          f'{it["durability"]}, {it["food"]}, {it["sat"]:.2f}f, {it["fuel"]}, {it["damage"]:.2f}f, {it["aspeed"]:.2f}f, '
          f'{it["armor_slot"]}, {it["armor_pts"]}, {it["special"]}, {it["category"]}, {it["glint"]} }},\n')
    w("};\n\n")
    w(f"const ItemDisplay kDisplays[{len(display_sets)}] = {{\n")
    for d in display_sets:
        g, fp, tp = d[0:9], d[9:18], d[18:27]

        def cf(x):
            t = "%g" % x
            return (t if ("." in t or "e" in t) else t + ".0") + "f"

        def tf(v):
            return "{ { %s }, { %s }, { %s } }" % (", ".join(cf(x) for x in v[0:3]), ", ".join(cf(x) for x in v[3:6]),
                                                 ", ".join(cf(x) for x in v[6:9]))
        w(f"    {{ {tf(g)}, {tf(fp)}, {tf(tp)} }},\n")
    w("};\n")
    w(f"const uint8_t kItemDisplay[{len(item_display)}] = {{\n")
    for i in range(0, len(item_display), 32):
        w("    " + ", ".join(str(x) for x in item_display[i:i + 32]) + ",\n")
    w("};\n\n")
    flat = []
    w(f"const uint16_t kIngredientSets[{len(ing_sets)}][2] = {{\n")
    for s in ing_sets:
        w(f"    {{ {len(flat)}, {len(s)} }},\n")
        flat += list(s)
    w("};\n")
    w(f"const uint16_t kIngredientItems[{max(1, len(flat))}] = {{ {', '.join(str(x) for x in flat) or '0'} }};\n\n")
    w(f"const ShapedRecipe kShaped[{len(shaped)}] = {{\n")
    for (ww, hh, cells, res, cnt) in shaped:
        cells9 = cells + [-1] * (9 - len(cells))
        w(f"    {{ {ww}, {hh}, {{ {', '.join(str(c) for c in cells9)} }}, {res}, {cnt} }},\n")
    w("};\n")
    w(f"const ShapelessRecipe kShapeless[{len(shapeless)}] = {{\n")
    for (ings, res, cnt) in shapeless:
        ings9 = ings + [-1] * (9 - len(ings))
        w(f"    {{ {len(ings)}, {{ {', '.join(str(c) for c in ings9)} }}, {res}, {cnt} }},\n")
    w("};\n")
    w(f"const CookingRecipe kCooking[{len(cooking)}] = {{\n")
    for (kind, ii, res, tm) in cooking:
        w(f"    {{ {kind}, {ii}, {res}, {tm} }},\n")
    w("};\n")
    w(f"const int kNumShaped = {len(shaped)}, kNumShapeless = {len(shapeless)}, kNumCooking = {len(cooking)};\n")
    w(f"const int kNumIngredientSets = {len(ing_sets)};\n\n")
    w(f"const SoundFile kSoundFiles[{max(1, len(sound_files_flat))}] = {{\n")
    for name, vol, pitch in sound_files_flat:
        w(f'    {{ "{name}", {vol:.2f}f, {pitch:.2f}f }},\n')
    w("};\n")
    w(f"const SoundEventDef kSoundEvents[SND_COUNT] = {{\n")
    for enum, first, cnt in sound_rows:
        w(f"    {{ {first}, {cnt} }}, // {enum}\n")
    w("};\n\n} // namespace mc\n")

print(f"blocks: {len(block_keys)}  items: {len(item_keys)}  tiles: {len(tiles)}")
print(f"recipes: shaped {len(shaped)}, shapeless {len(shapeless)}, cooking {len(cooking)}; ingredient sets {len(ing_sets)}")
print(f"gui rects: {len(gui_rects)}  font glyphs: {sum(1 for x in font_widths if x)}  sounds: {len(sound_files_flat)} files")
