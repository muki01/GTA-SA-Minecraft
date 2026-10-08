"""One-off patch: extends gen_assets.py for v0.3 (display transforms, mobs, wand, fishing, sounds)."""
import io
import os

p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gen_assets.py")
s = io.open(p, encoding="utf-8").read()


def rep(old, new, count=1):
    global s
    assert old in s, "pattern not found:\n" + old[:200]
    s = s.replace(old, new, count)


# ---------------------------------------------------------------- items: specials, wand, spawn eggs
rep('''SPECIAL = {"bow": 1, "arrow": 2, "spectral_arrow": 2, "flint_and_steel": 3, "fire_charge": 3, "firework_rocket": 4,
           "elytra": 5, "snowball": 6, "egg": 7, "ender_pearl": 8}
EXCLUDE_ITEMS = ("spawn_egg", "debug_stick", "knowledge_book", "structure_void", "air", "barrier", "light",
                 "bundle", "filled_map", "command_block_minecart", "jigsaw", "test_")''',
    '''SPECIAL = {"bow": 1, "arrow": 2, "spectral_arrow": 2, "flint_and_steel": 3, "fire_charge": 3, "firework_rocket": 4,
           "elytra": 5, "snowball": 6, "egg": 7, "ender_pearl": 8, "debug_stick": 9, "cow_spawn_egg": 10,
           "pig_spawn_egg": 11, "sheep_spawn_egg": 12, "chicken_spawn_egg": 13, "fishing_rod": 14, "shears": 15,
           "bucket": 16}
KEEP_SPAWN_EGGS = ("cow_spawn_egg", "pig_spawn_egg", "sheep_spawn_egg", "chicken_spawn_egg")
EXCLUDE_ITEMS = ("spawn_egg", "knowledge_book", "structure_void", "air", "barrier", "light",
                 "bundle", "filled_map", "command_block_minecart", "jigsaw", "test_")
NAME_OVERRIDE = {"debug_stick": "B\\u00fcy\\u00fcl\\u00fc Sopa"}
GLINT = ("debug_stick", "enchanted_golden_apple", "enchanted_book", "nether_star", "experience_bottle")''')

rep('''        if key in blocks or any(x in key for x in EXCLUDE_ITEMS):
            continue
        if f"item.minecraft.{key}" not in LANG_EN:
            continue''',
    '''        if key in blocks or (any(x in key for x in EXCLUDE_ITEMS) and key not in KEEP_SPAWN_EGGS):
            continue
        if f"item.minecraft.{key}" not in LANG_EN:
            continue''')

rep('''        it = dict(key=key, name=tr_name("item", key), tile=tile)
        it.update(item_props(key))
        items[key] = it
    # extra frames
    for i in range(3):
        tex_tile(f"item/bow_pulling_{i}")''',
    '''        it = dict(key=key, name=NAME_OVERRIDE.get(key, tr_name("item", key)), tile=tile)
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
    tex_tile("item/fishing_rod_cast")''')

# remember the model of every block for its display transforms
rep('''def add_block(key, kind, tex, render=None, extra=None):
    b = dict(key=key, name=tr_name("block", key), kind=kind, tex=tex)''',
    '''def add_block(key, kind, tex, render=None, extra=None):
    b = dict(key=key, name=tr_name("block", key), kind=kind, tex=tex, model="block/" + key)''')

# ---------------------------------------------------------------- particles
rep('''             "flash", "flame", "critical_hit", "big_smoke_0", "big_smoke_4", "big_smoke_8", "heart", "angry"]:''',
    '''             "flash", "flame", "critical_hit", "big_smoke_0", "big_smoke_4", "big_smoke_8", "heart", "angry",
             "splash_0", "splash_1", "splash_2", "splash_3", "bubble", "enchanted_hit", "glint"]:''')

rep('''    for k in ("generic_0", "spark_0", "flash", "flame", "critical_hit", "big_smoke_0", "heart"):''',
    '''    special_tiles["FISHING_ROD_CAST"] = tile_index["item/fishing_rod_cast"]
    for k in ("generic_0", "spark_0", "flash", "flame", "critical_hit", "big_smoke_0", "heart", "splash_0", "splash_1",
              "splash_2", "splash_3", "bubble", "enchanted_hit"):''')

# ---------------------------------------------------------------- entity atlas
rep('''ent = Image.new("RGBA", (256, 128), (0, 0, 0, 0))
ent.paste(gimg("entity/player/wide/steve"), (0, 0))
ent.paste(gimg("entity/equipment/wings/elytra"), (64, 0))
ent.paste(gimg("entity/projectiles/arrow"), (128, 0))
ent.paste(gimg("misc/shadow"), (0, 64))
ent.save(os.path.join(OUT, "entity.png"))''',
    '''ENT_W, ENT_H = 512, 256
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
ent.save(os.path.join(OUT, "entity.png"))''')

rep('''    w("// entity.png layout\\nconstexpr int ENT_TEX_W = 256, ENT_TEX_H = 128;\\n")
    w("constexpr GuiRect ENT_STEVE = { 0, 0, 64, 64 };\\nconstexpr GuiRect ENT_ELYTRA = { 64, 0, 64, 32 };\\n")
    w("constexpr GuiRect ENT_ARROW = { 128, 0, 32, 32 };\\nconstexpr GuiRect ENT_SHADOW = { 0, 64, 64, 64 };\\n")''',
    '''    w(f"// entity.png layout\\nconstexpr int ENT_TEX_W = {ENT_W}, ENT_TEX_H = {ENT_H};\\n")
    for k, (x, y, ww, hh) in ent_rects.items():
        w(f"constexpr GuiRect ENT_{k} = {{ {x}, {y}, {ww}, {hh} }};\\n")''')

# ---------------------------------------------------------------- sounds
rep('''    ("PLACE_WOOL", "block.wool.place"), ("PLACE_METAL", "block.metal.place"), ("PLACE_SNOW", "block.snow.place"),''',
    '''    ("PLACE_WOOL", "block.wool.place"), ("PLACE_METAL", "block.metal.place"), ("PLACE_SNOW", "block.snow.place"),
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
    ("KNOCKBACK", "entity.player.attack.knockback"), ("POP", "entity.chicken.egg"),''')

# ---------------------------------------------------------------- display transforms + glint in the tables
rep('''def cname(k):
    return "ID_" + k.upper()
''',
    '''def cname(k):
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
''')

rep('''          f'{it["armor_slot"]}, {it["armor_pts"]}, {it["special"]}, {it["category"]} }},\\n')
    w("};\\n\\n")''',
    '''          f'{it["armor_slot"]}, {it["armor_pts"]}, {it["special"]}, {it["category"]}, {it["glint"]} }},\\n')
    w("};\\n\\n")
    w(f"const ItemDisplay kDisplays[{len(display_sets)}] = {{\\n")
    for d in display_sets:
        g, fp, tp = d[0:9], d[9:18], d[18:27]

        def tf(v):
            return "{ { %s }, { %s }, { %s } }" % (", ".join("%gf" % x for x in v[0:3]), ", ".join("%gf" % x for x in v[3:6]),
                                                 ", ".join("%gf" % x for x in v[6:9]))
        w(f"    {{ {tf(g)}, {tf(fp)}, {tf(tp)} }},\\n")
    w("};\\n")
    w(f"const uint8_t kItemDisplay[{len(item_display)}] = {{\\n")
    for i in range(0, len(item_display), 32):
        w("    " + ", ".join(str(x) for x in item_display[i:i + 32]) + ",\\n")
    w("};\\n\\n")''')

io.open(p, "w", encoding="utf-8").write(s)
print("gen_assets.py patched")
