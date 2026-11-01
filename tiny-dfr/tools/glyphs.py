"""Bounding boxes of the bright marks (icons, text) inside each key of a row:
glyphs.py <png>[:row] ... (row of a preview, 60 px + 12 px gap)."""
import sys
from PIL import Image
for arg in sys.argv[1:]:
    path, _, row = arg.partition(':')
    im = Image.open(path).convert('RGB')
    y0 = int(row or 0) * 72
    px = im.load()
    keys, s = [], None
    for x in range(im.width):        # keys: runs of non-black at y0+30 ... use y0+2
        on = max(px[x, y0 + 30]) > 12
        if on and s is None: s = x
        if not on and s is not None: keys.append((s, x - 1)); s = None
    if s is not None: keys.append((s, im.width - 1))
    out = []
    for a, b in keys:
        pts = [(x, y) for y in range(y0, y0 + 60) for x in range(a, b + 1) if min(px[x, y]) > 170]
        if not pts or b - a < 20: continue
        xs = [p[0] for p in pts]; ys = [p[1] - y0 for p in pts]
        out.append(f"[{a}-{b}] {max(xs)-min(xs)+1}x{max(ys)-min(ys)+1}@y{min(ys)}")
    print(arg.split('/')[-1]); print('  ' + '  '.join(out))
