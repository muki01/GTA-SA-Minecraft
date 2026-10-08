"""One-off patch for mod version 0.7 (already applied): average colour of every block (matching GTA textures)."""
import io

P = 'tools/gen_assets.py'
s = io.open(P, encoding='utf-8').read()


def rep(old, new, count=1):
    global s
    assert old in s, old[:80]
    s = s.replace(old, new, count)


rep('''    w("\\nenum Effect : uint8_t { "''', '''    def avg_color(t):
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
    w("\\n// average colour (0xRRGGBB) of every block's side texture\\n")
    w(f"constexpr uint32_t kBlockColor[{len(cols)}] = {{\\n")
    for i in range(0, len(cols), 12):
        w("    " + ", ".join("0x%06X" % c for c in cols[i:i + 12]) + ",\\n")
    w("};\\n")
    w("\\nenum Effect : uint8_t { "''')

io.open(P, 'w', encoding='utf-8').write(s)
print('patched')
