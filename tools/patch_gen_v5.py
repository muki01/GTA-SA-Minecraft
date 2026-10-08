"""One-off patch for mod version 0.4 (part 2, already applied): buckets, crossbow frames."""
import io

P = 'tools/gen_assets.py'
s = io.open(P, encoding='utf-8').read()


def rep(old, new, count=1):
    global s
    assert old in s, old[:80]
    s = s.replace(old, new, count)


rep('''           "trident": 25,''', '''           "trident": 25, "water_bucket": 26, "lava_bucket": 27, "crossbow": 28,''')
rep('''    if key in ("fire_charge", "wind_charge", "spyglass", "minecart", "saddle", "milk_bucket"):''',
    '''    if key in ("fire_charge", "wind_charge", "spyglass", "minecart", "saddle", "milk_bucket", "water_bucket",
               "lava_bucket"):''')
rep('''    if key == "trident":''', '''    if key == "crossbow":
        p.update(durability=465, category=5, stack=1)
    if key == "trident":''')
rep('''    tex_tile("item/fishing_rod_cast")''', '''    tex_tile("item/fishing_rod_cast")
    for i in range(3):
        tex_tile(f"item/crossbow_pulling_{i}")
    tex_tile("item/crossbow_arrow")
    tex_tile("item/crossbow_firework")''')
rep('''    special_tiles["FISHING_ROD_CAST"] = tile_index["item/fishing_rod_cast"]''',
    '''    special_tiles["FISHING_ROD_CAST"] = tile_index["item/fishing_rod_cast"]
    for i in range(3):
        special_tiles[f"CROSSBOW_PULLING_{i}"] = tile_index[f"item/crossbow_pulling_{i}"]
    special_tiles["CROSSBOW_ARROW"] = tile_index["item/crossbow_arrow"]
    special_tiles["CROSSBOW_FIREWORK"] = tile_index["item/crossbow_firework"]''')

io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
