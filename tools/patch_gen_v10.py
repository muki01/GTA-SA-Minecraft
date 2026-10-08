"""One-off patch for mod version 0.8 (already applied): title screen textures (menu.png) and menu / death screen texts."""
import io

P = 'tools/gen_assets.py'
s = io.open(P, encoding='utf-8').read()


def rep(old, new, count=1):
    global s
    assert old in s, old[:80]
    s = s.replace(old, new, count)


rep('''# ======================================================================= font
FONT_COLS, FONT_ROWS = 32, 16''', '''# ======================================================================= title screen (menu.png)
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
menu_add("LOGO", gimg("gui/title/minecraft").resize((512, 128), Image.LANCZOS), 0, 258)
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
])

# ======================================================================= font
FONT_COLS, FONT_ROWS = 32, 16''')

rep('''    w(f"// entity.png layout\\nconstexpr int ENT_TEX_W = {ENT_W}, ENT_TEX_H = {ENT_H};\\n")''',
    '''    w(f"// menu.png layout (title screen)\\nconstexpr int MENU_TEX_W = {MENU_W}, MENU_TEX_H = {MENU_H};\\n")
    for k, (x, y, ww, hh) in menu_rects.items():
        w(f"constexpr GuiRect MENU_{k} = {{ {x}, {y}, {ww}, {hh} }};\\n")
    w("enum Str { " + ", ".join("STR_" + k for k in MENU_STRINGS) + ", STR_COUNT };\\n")
    w("inline const char* const kStr[STR_COUNT] = {\\n")
    for k, key in MENU_STRINGS.items():
        w("    " + cstr(LANG.get(key) or LANG_EN.get(key) or k) + ",\\n")
    w("};\\n\\n")
    w(f"// entity.png layout\\nconstexpr int ENT_TEX_W = {ENT_W}, ENT_TEX_H = {ENT_H};\\n")''')

io.open(P, 'w', encoding='utf-8').write(s)
print('patched')
