"""One-off patch that upgrades tools/gen_assets.py for mod version 0.4 (already applied)."""
import io

P = 'tools/gen_assets.py'
s = io.open(P, encoding='utf-8').read()


def rep(old, new, count=1):
    global s
    assert old in s, old[:80]
    s = s.replace(old, new, count)


# ---- new item behaviours
rep('''SPECIAL = {"bow": 1, "arrow": 2, "spectral_arrow": 2, "flint_and_steel": 3, "fire_charge": 3, "firework_rocket": 4,''',
    '''SPECIAL = {"bow": 1, "arrow": 2, "spectral_arrow": 2, "flint_and_steel": 3, "fire_charge": 17, "firework_rocket": 4,
           "wind_charge": 18, "spyglass": 19, "totem_of_undying": 20, "minecart": 22, "saddle": 23, "milk_bucket": 24,
           "trident": 25,''')
rep('''    if key in FOOD:
        p.update(food=FOOD[key][0], sat=FOOD[key][1], category=6)
    return p''', '''    if key in FOOD:
        p.update(food=FOOD[key][0], sat=FOOD[key][1], category=6)
    if key.endswith("_boat") or key.endswith("_raft"):
        p.update(special=21, category=4, stack=1)
    if key in ("fire_charge", "wind_charge", "spyglass", "minecart", "saddle", "milk_bucket"):
        p["category"] = 4
    if key == "totem_of_undying":
        p["category"] = 5
    if key == "trident":
        p.update(damage=9.0, aspeed=1.1, durability=250, category=5, stack=1)
    if key == "wind_charge":
        p["stack"] = 64
    return p''')

# ---- entity atlas: armour layers, pig saddle
rep('''ENT_W, ENT_H = 512, 256''', '''ENT_W, ENT_H = 512, 512''')
rep('''white = Image.new("RGBA", (8, 8), (255, 255, 255, 255))
ent_add("WHITE", white)''', '''white = Image.new("RGBA", (8, 8), (255, 255, 255, 255))
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
ent_add("PIG_SADDLE", gimg("entity/equipment/pig_saddle/saddle"))''')
rep('''    for k, (x, y, ww, hh) in ent_rects.items():
        w(f"constexpr GuiRect ENT_{k} = {{ {x}, {y}, {ww}, {hh} }};\\n")''',
    '''    for k, (x, y, ww, hh) in ent_rects.items():
        w(f"constexpr GuiRect ENT_{k} = {{ {x}, {y}, {ww}, {hh} }};\\n")
    w(f"\\n// armour materials: item keys start with these names\\nconstexpr int NUM_ARMOR_MATS = {len(ARMOR_TEX)};\\n")
    w("constexpr const char* ARMOR_MAT_KEYS[NUM_ARMOR_MATS] = { " + ", ".join(f'"{m}_"' for m in ARMOR_TEX) + " };\\n")
    w("constexpr GuiRect ENT_ARMOR_OUTER[NUM_ARMOR_MATS] = { " + ", ".join(f"ENT_ARMOR_{m.upper()}_1" for m in ARMOR_TEX) + " };\\n")
    w("constexpr GuiRect ENT_ARMOR_INNER[NUM_ARMOR_MATS] = { " + ", ".join(f"ENT_ARMOR_{m.upper()}_2" for m in ARMOR_TEX) + " };\\n")''')

# ---- sounds
rep('''    ("KNOCKBACK", "entity.player.attack.knockback"), ("POP", "entity.chicken.egg"),
])''', '''    ("KNOCKBACK", "entity.player.attack.knockback"), ("POP", "entity.chicken.egg"),
    ("CROSSBOW_SHOOT", "item.crossbow.shoot"), ("THUNDER", "entity.lightning_bolt.thunder"),
    ("LIGHTNING_IMPACT", "entity.lightning_bolt.impact"), ("FIREBALL", "entity.ghast.shoot"),
    ("WIND_THROW", "entity.wind_charge.throw"), ("WIND_BURST", "entity.wind_charge.wind_burst"),
    ("TOTEM", "item.totem.use"), ("DRINK", "entity.generic.drink"), ("VILLAGER_YES", "entity.villager.yes"),
    ("VILLAGER_NO", "entity.villager.no"), ("VILLAGER_TRADE", "entity.villager.trade"),
    ("SPYGLASS", "item.spyglass.use"), ("SADDLE", "entity.pig.saddle"), ("SPLASH", "entity.player.splash"),
    ("SWIM", "entity.player.swim"), ("TRIDENT_THROW", "item.trident.throw"), ("TRIDENT_HIT", "item.trident.hit_ground"),
    ("PLAYER_DEATH", "entity.player.death"), ("LEVELUP", "entity.player.levelup"), ("NOTE", "block.note_block.harp"),
    ("EQUIP_IRON", "item.armor.equip_iron"), ("ANVIL", "block.anvil.land"),
])''')

io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
