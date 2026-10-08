"""One-off patch for mod version 0.6 (already applied): full fluid / fire animations, fire block, 3D trident texture."""
import io

P = 'tools/gen_assets.py'
s = io.open(P, encoding='utf-8').read()


def rep(old, new, count=1):
    global s
    assert old in s, old[:80]
    s = s.replace(old, new, count)


# ---- fire is a block of its own (no item, no drops, breaks at once)
rep('''FLUIDS = ("water", "lava")


def hardness_for(n):
    if n in PLANTS:
        return 0.0''', '''FLUIDS = ("water", "lava")
FIRES = ("fire",)


def hardness_for(n):
    if n in PLANTS or n in FIRES:
        return 0.0''')
rep('''    if n in FLUIDS:
        return SURF["DEFAULT"], "stone", 2
    if "wool" in n:''', '''    if n in FLUIDS:
        return SURF["DEFAULT"], "stone", 2
    if n in FIRES:
        return SURF["DEFAULT"], "wool", 2
    if "wool" in n:''')
rep('''def loot_drops(key):
    if key in FLUIDS:
        return []''', '''def loot_drops(key):
    if key in FLUIDS or key in FIRES:
        return []''')
rep('''EMISSIVE = {"lava", ''', '''EMISSIVE = {"lava", "fire", ''')

# ---- animations: every frame, in the order the .mcmeta plays them, at full resolution
rep('''def build_extra_blocks():''', '''ANIMS = OrderedDict()  # name -> (first tile, frames, ticks per frame, frame size in px)


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


def build_extra_blocks():''')
rep('''    ws = anim_tiles("block/water_still", FLUID_FRAMES, WATER_TINT)
    wf = anim_tiles("block/water_flow", FLUID_FRAMES, WATER_TINT)
    ls = anim_tiles("block/lava_still", FLUID_FRAMES)
    lf = anim_tiles("block/lava_flow", FLUID_FRAMES)
    add_block("water", "fluid", dict(top=ws, bottom=ws, side=wf), render=2)
    add_block("lava", "fluid", dict(top=ls, bottom=ls, side=lf), render=1)''', '''    ws = anim_row("WATER_STILL", "block/water_still", WATER_TINT)
    wf = anim_big("WATER_FLOW", "block/water_flow", WATER_TINT)
    ls = anim_row("LAVA_STILL", "block/lava_still")
    lf = anim_big("LAVA_FLOW", "block/lava_flow")
    add_block("water", "fluid", dict(top=ws, bottom=ws, side=wf), render=2)
    add_block("lava", "fluid", dict(top=ls, bottom=ls, side=lf), render=1)
    f0 = anim_row("FIRE_0", "block/fire_0")
    anim_row("FIRE_1", "block/fire_1")
    add_block("fire", "fire", dict(top=f0, bottom=f0, side=f0), render=1)''')

# ---- C++ side
rep('''    w(f"constexpr int FLUID_FRAMES = {FLUID_FRAMES};\\n\\n")''', '''    w("struct AnimDef { uint16_t tile; uint8_t frames, ticks, px; };\\n")
    w("enum AnimId : uint8_t { ANIM_NONE = 0, " + ", ".join("ANIM_" + k for k in ANIMS) + ", ANIM_COUNT };\\n")
    w("constexpr AnimDef kAnims[ANIM_COUNT] = { { 0, 1, 1, 16 }, " +
      ", ".join("{ %d, %d, %d, %d }" % v for v in ANIMS.values()) + " };\\n\\n")''')
rep('''    special_tiles["LAVA_FLOW"] = blocks["lava"]["tex"]["side"]''', '''    special_tiles["LAVA_FLOW"] = blocks["lava"]["tex"]["side"]
    special_tiles["FIRE_0"] = ANIMS["FIRE_0"][0]
    special_tiles["FIRE_1"] = ANIMS["FIRE_1"][0]''')
rep('''"cross": "SHAPE_CROSS", "fluid": "SHAPE_FLUID"}''', '''"cross": "SHAPE_CROSS", "fluid": "SHAPE_FLUID",
                 "fire": "SHAPE_FIRE"}''')

# ---- the trident is a 3D model in the hands and in flight
rep('''ent_add("XP_ORB", gimg("entity/experience/experience_orb"))''', '''ent_add("XP_ORB", gimg("entity/experience/experience_orb"))
ent_add("TRIDENT", gimg("entity/trident/trident"))''')

# ---- sounds
rep('''    ("STEP_WOOL", "block.wool.step"), ("STEP_METAL", "block.metal.step"), ("STEP_SNOW", "block.snow.step"),
])''', '''    ("STEP_WOOL", "block.wool.step"), ("STEP_METAL", "block.metal.step"), ("STEP_SNOW", "block.snow.step"),
    ("FIRE_AMBIENT", "block.fire.ambient"), ("FIRE_EXTINGUISH", "block.fire.extinguish"),
    ("LAVA_AMBIENT", "block.lava.ambient"), ("BURN", "entity.player.hurt_on_fire"),
])''')

io.open(P, 'w', encoding='utf-8').write(s)
print('patched')
