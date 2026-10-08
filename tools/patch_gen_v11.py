"""One-off patch for mod version 0.8 (already applied): the button click of the menus."""
import io

P = 'tools/gen_assets.py'
s = io.open(P, encoding='utf-8').read()
old = '''("LAVA_AMBIENT", "block.lava.ambient"), ("BURN", "entity.player.hurt_on_fire"),'''
assert old in s
s = s.replace(old, old + '''
    ("CLICK", "ui.button.click"),''', 1)
io.open(P, 'w', encoding='utf-8').write(s)
print('patched')
