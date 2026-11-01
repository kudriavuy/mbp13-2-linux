"""Stack macOS Touch Bar screenshots over rows of a tiny-dfr preview, to
compare: compare.py <out.png> <preview.png> <mac.png>:<row> ... (row: 0-based
row of the preview, 60 px + 12 px gap each). Each pair: macOS, ours, a
difference strip (red where they differ by more than 40)."""
import sys
from PIL import Image, ImageChops

out, preview, pairs = sys.argv[1], Image.open(sys.argv[2]).convert('RGB'), sys.argv[3:]
W, H, GAP = 2170, 60, 12
canvas = Image.new('RGB', (W, len(pairs) * (3 * H + 2 * 4 + 24)), (128, 128, 128))
y = 0
for p in pairs:
    mac, row = p.rsplit(':', 1)
    m = Image.open(mac).convert('RGB')
    o = preview.crop((0, int(row) * (H + GAP), W, int(row) * (H + GAP) + H))
    d = ImageChops.difference(m, o).convert('L').point(lambda v: 255 if v > 40 else 0)
    red = Image.new('RGB', (W, H), (0, 0, 0))
    red.paste((255, 40, 40), mask=d)
    for img in (m, o, red):
        canvas.paste(img, (0, y)); y += H + 4
    y += 16
canvas.save(out)
