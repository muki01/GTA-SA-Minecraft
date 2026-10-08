"""One-off patch for mod version 0.7 (already applied): status effects (icons, hearts, names), air bubbles."""
import io

P = 'tools/gen_assets.py'
s = io.open(P, encoding='utf-8').read()


def rep(old, new, count=1):
    global s
    assert old in s, old[:80]
    s = s.replace(old, new, count)


rep('''gui_add("CURSOR", cur)''', '''# status effects (order = enum Effect in the mod) and the HUD pieces that show them
EFFECTS = ("absorption", "regeneration", "fire_resistance", "resistance", "hunger", "poison")
for name, rel in [("HEART_ABSORBING_FULL", "hud/heart/absorbing_full"), ("HEART_ABSORBING_HALF", "hud/heart/absorbing_half"),
                  ("HEART_POISONED_FULL", "hud/heart/poisoned_full"), ("HEART_POISONED_HALF", "hud/heart/poisoned_half"),
                  ("FOOD_EMPTY_HUNGER", "hud/food_empty_hunger"), ("FOOD_FULL_HUNGER", "hud/food_full_hunger"),
                  ("FOOD_HALF_HUNGER", "hud/food_half_hunger"), ("AIR_BURSTING", "hud/air_bursting"),
                  ("EFFECT_BG", "hud/effect_background")]:
    gui_add(name, gimg(S + rel))
for e in EFFECTS:
    gui_add("EFFECT_" + e.upper(), gimg("mob_effect/" + e))
gui_add("CURSOR", cur)''')

rep('''    w(f"\\nconstexpr int FONT_COLS = {FONT_COLS};\\nconstexpr int FONT_ROWS = {FONT_ROWS};\\n")''',
    '''    w("\\nenum Effect : uint8_t { " + ", ".join("EFFECT_" + e.upper() for e in EFFECTS) + ", EFFECT_COUNT };\\n")
    w("constexpr GuiRect kEffectIcons[EFFECT_COUNT] = { " + ", ".join("GUI_EFFECT_" + e.upper() for e in EFFECTS) + " };\\n")
    w("inline const char* const kEffectNames[EFFECT_COUNT] = { " +
      ", ".join(cstr(LANG.get("effect.minecraft." + e) or LANG_EN.get("effect.minecraft." + e) or e) for e in EFFECTS) + " };\\n")
    w(f"\\nconstexpr int FONT_COLS = {FONT_COLS};\\nconstexpr int FONT_ROWS = {FONT_ROWS};\\n")''')

io.open(P, 'w', encoding='utf-8').write(s)
print('patched')
