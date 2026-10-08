"""One-off patch for mod version 0.5 (already applied): plants, fluids, fixed item id range, XP, footsteps."""
import io

P = 'tools/gen_assets.py'
s = io.open(P, encoding='utf-8').read()


def rep(old, new, count=1):
    global s
    assert old in s, old[:80]
    s = s.replace(old, new, count)


# ---- potted plants are not blocks we want
rep('''EXCLUDE_PREFIX = ("infested_", "waxed_", "test_")''', '''EXCLUDE_PREFIX = ("infested_", "waxed_", "test_", "potted_")''')

# ---- plants: no hardness, grass sounds
rep('''def hardness_for(n):
    rules = [''', '''PLANTS = ("oak_sapling", "spruce_sapling", "birch_sapling", "jungle_sapling", "acacia_sapling", "dark_oak_sapling",
          "cherry_sapling", "dandelion", "poppy", "blue_orchid", "allium", "azure_bluet", "red_tulip", "orange_tulip",
          "white_tulip", "pink_tulip", "oxeye_daisy", "cornflower", "lily_of_the_valley", "short_grass", "fern",
          "dead_bush", "brown_mushroom", "red_mushroom")
FLUIDS = ("water", "lava")


def hardness_for(n):
    if n in PLANTS:
        return 0.0
    if n in FLUIDS:
        return -1.0
    rules = [''')
rep('''def classify(n):
    """surface, sound group, creative category"""''', '''def classify(n):
    """surface, sound group, creative category"""
    if n in PLANTS:
        return SURF["GRASS"], "grass", 2
    if n in FLUIDS:
        return SURF["DEFAULT"], "stone", 2''')
rep('''def loot_drops(key):
    path = os.path.join(DM, "loot_table", "blocks", key + ".json")''', '''def loot_drops(key):
    if key in FLUIDS:
        return []
    path = os.path.join(DM, "loot_table", "blocks", key + ".json")''')

# ---- extra blocks, appended after the cube blocks so existing block ids never move
rep('''# ======================================================================= block properties''', '''WATER_TINT = (0x3F, 0x76, 0xE4)
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
    ws = anim_tiles("block/water_still", FLUID_FRAMES, WATER_TINT)
    wf = anim_tiles("block/water_flow", FLUID_FRAMES, WATER_TINT)
    ls = anim_tiles("block/lava_still", FLUID_FRAMES)
    lf = anim_tiles("block/lava_flow", FLUID_FRAMES)
    add_block("water", "fluid", dict(top=ws, bottom=ws, side=wf), render=2)
    add_block("lava", "fluid", dict(top=ls, bottom=ls, side=lf), render=1)
    for k in ("glint", "lava", "bubble", "drip_fall"):
        tex_tile("particle/" + k)


# ======================================================================= block properties''')
rep('''EMISSIVE = {"glowstone",''', '''EMISSIVE = {"lava", "glowstone",''')
rep('''build_blocks()
finalize_blocks()''', '''build_blocks()
build_extra_blocks()
finalize_blocks()''')

# ---- items start at a fixed id: new blocks never shift item ids again
rep('''FIRST_ITEM = len(block_keys) + 1''', '''FIRST_ITEM = 1024  # fixed: block ids 1..1023, item ids from here on
assert len(block_keys) + 1 <= FIRST_ITEM''')

rep('''    kind_enum = {"all": "SHAPE_CUBE", "bottom_top": "SHAPE_CUBE", "column": "SHAPE_COLUMN", "facing": "SHAPE_FACING",
                 "cube6": "SHAPE_FACING"}''', '''    kind_enum = {"all": "SHAPE_CUBE", "bottom_top": "SHAPE_CUBE", "column": "SHAPE_COLUMN", "facing": "SHAPE_FACING",
                 "cube6": "SHAPE_FACING", "cross": "SHAPE_CROSS", "fluid": "SHAPE_FLUID"}''')

# ---- tiles, entity, sounds
rep('''    special_tiles["CROSSBOW_ARROW"] = tile_index["item/crossbow_arrow"]''', '''    special_tiles["CROSSBOW_ARROW"] = tile_index["item/crossbow_arrow"]
    special_tiles["WATER_STILL"] = blocks["water"]["tex"]["top"]
    special_tiles["WATER_FLOW"] = blocks["water"]["tex"]["side"]
    special_tiles["LAVA_STILL"] = blocks["lava"]["tex"]["top"]
    special_tiles["LAVA_FLOW"] = blocks["lava"]["tex"]["side"]''')
rep('''    for k in ("generic_0", "spark_0", "flash", "flame", "critical_hit", "big_smoke_0", "heart", "splash_0", "splash_1",
              "splash_2", "splash_3", "bubble", "enchanted_hit"):''', '''    for k in ("generic_0", "spark_0", "flash", "flame", "critical_hit", "big_smoke_0", "heart", "splash_0", "splash_1",
              "splash_2", "splash_3", "bubble", "enchanted_hit", "glint", "lava", "drip_fall"):''')
rep('''    w(f"constexpr int ATLAS_SIZE = {ATLAS_PX};\\nconstexpr int ATLAS_TILES_PER_ROW = {ATLAS_TILES};\\n\\n")''',
    '''    w(f"constexpr int ATLAS_SIZE = {ATLAS_PX};\\nconstexpr int ATLAS_TILES_PER_ROW = {ATLAS_TILES};\\n")
    w(f"constexpr int FLUID_FRAMES = {FLUID_FRAMES};\\n\\n")''')
rep('''ent_add("PIG_SADDLE", gimg("entity/equipment/pig_saddle/saddle"))''', '''ent_add("PIG_SADDLE", gimg("entity/equipment/pig_saddle/saddle"))
ent_add("XP_ORB", gimg("entity/experience/experience_orb"))''')
rep('''    ("EQUIP_IRON", "item.armor.equip_iron"), ("ANVIL", "block.anvil.land"),
])''', '''    ("EQUIP_IRON", "item.armor.equip_iron"), ("ANVIL", "block.anvil.land"),
    ("XP_ORB", "entity.experience_orb.pickup"), ("BUCKET_FILL", "item.bucket.fill"),
    ("BUCKET_EMPTY", "item.bucket.empty"), ("BUCKET_FILL_LAVA", "item.bucket.fill_lava"),
    ("BUCKET_EMPTY_LAVA", "item.bucket.empty_lava"), ("LAVA_POP", "block.lava.pop"),
    ("LAVA_EXTINGUISH", "block.lava.extinguish"), ("BONE_MEAL", "item.bone_meal.use"),
    ("FALL_SMALL", "entity.player.small_fall"), ("FALL_BIG", "entity.player.big_fall"),
    ("STEP_STONE", "block.stone.step"), ("STEP_WOOD", "block.wood.step"), ("STEP_GRAVEL", "block.gravel.step"),
    ("STEP_GRASS", "block.grass.step"), ("STEP_SAND", "block.sand.step"), ("STEP_GLASS", "block.glass.step"),
    ("STEP_WOOL", "block.wool.step"), ("STEP_METAL", "block.metal.step"), ("STEP_SNOW", "block.snow.step"),
])''')
rep('''           "trident": 25, "water_bucket": 26, "lava_bucket": 27, "crossbow": 28,''',
    '''           "trident": 25, "water_bucket": 26, "lava_bucket": 27, "crossbow": 28, "bone_meal": 29,''')

io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
