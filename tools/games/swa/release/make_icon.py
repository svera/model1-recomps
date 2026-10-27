#!/usr/bin/env python3
"""The app icon: "SW" in chiselled low-poly letters on a faceted tile over a starfield and a trench
running to the horizon, drawn from scratch (no art from the game), in the style of VR's
(tools/games/vr/release/make_icon.py). Writes assets/games/swa/icon/icon.png (1024x1024, the macOS
icon grid: an 824px rounded tile with room for the shadow), SWA.icns and SWA.ico; with variant
names, icon_<variant>.png and choices.png instead.

    tools/games/swa/release/make_icon.py [variant ...]      variants: space, grey, blue
"""
import math, os, random, sys
from PIL import Image, ImageDraw, ImageFilter

SS = 4                     # supersampling
N = 1024 * SS
OUT = os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', 'assets', 'games', 'swa', 'icon')
DEFAULT = 'space'

PALETTES = {
    # tile (two corners), letter base colour, letter edge colour, trench colour
    'space': ((28, 34, 70), (2, 3, 12), (255, 214, 40), (150, 70, 0), (120, 128, 145)),
    'grey':  ((120, 126, 140), (30, 32, 40), (255, 214, 40), (150, 70, 0), (70, 74, 86)),
    'blue':  ((20, 60, 170), (4, 10, 50), (250, 250, 255), (60, 80, 160), (110, 130, 170)),
}

def shade(c, k):
    return tuple(max(0, min(255, int(v * k))) for v in c)

def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def _skew(p, s=0.0):       # upright, as the title's lettering
    x, y = p
    return (x + (512 - y) * s, y)

_FIT = None
def skew(p):
    """Scaled and centred to sit inside the tile with a margin (above the trench)."""
    global _FIT
    if _FIT is None:
        pts = [_skew(q) for st in strokes() for q in st]
        x0 = min(x for x, _ in pts); x1 = max(x for x, _ in pts)
        y0 = min(y for _, y in pts); y1 = max(y for _, y in pts)
        k = min(640 / (x1 - x0), 360 / (y1 - y0))
        _FIT = (k, 512 - (x0 + x1) / 2 * k, 420 - (y0 + y1) / 2 * k)
    k, ox, oy = _FIT
    x, y = _skew(p)
    return (x * k + ox, y * k + oy)

def strokes():
    """The letters as quads (in a 1024 design space): each stroke is chiselled separately."""
    q = []
    # S: top bar, upper left side (its outer corner cut), middle bar, lower right side, bottom bar
    q.append([(160, 300), (480, 300), (480, 375), (160, 375)])
    q.append([(80, 380), (160, 300), (160, 555), (80, 555)])
    q.append([(160, 485), (430, 485), (430, 555), (160, 555)])
    q.append([(430, 485), (510, 485), (510, 660), (430, 740)])
    q.append([(80, 665), (430, 665), (430, 740), (80, 740)])
    # W: four strokes meeting at two bottom points and a middle peak
    q.append([(560, 300), (645, 300), (700, 740), (630, 740)])
    q.append([(720, 370), (810, 370), (735, 740), (665, 740)])
    q.append([(720, 370), (810, 370), (845, 740), (775, 740)])
    q.append([(880, 300), (965, 300), (880, 740), (810, 740)])
    return q

def chisel(d, quad, base, edge, light):
    """A bevelled block: the top face (two triangles) and four sloped edges, each flat-shaded."""
    cx = sum(p[0] for p in quad) / 4
    cy = sum(p[1] for p in quad) / 4
    inner = [(cx + (x - cx) * 0.62, cy + (y - cy) * 0.62) for x, y in quad]
    faces = []
    for i in range(4):
        a, b = quad[i], quad[(i + 1) % 4]
        ia, ib = inner[i], inner[(i + 1) % 4]
        # the facet's outward direction decides its brightness
        nx, ny = (b[1] - a[1]), -(b[0] - a[0])
        ln = math.hypot(nx, ny) or 1
        lit = (nx * light[0] + ny * light[1]) / ln
        faces.append(([a, b, ib, ia], mix(shade(edge, 0.55), shade(base, 1.15), (lit + 1) / 2)))
    faces.append(([inner[0], inner[1], inner[2]], shade(base, 1.0)))
    faces.append(([inner[0], inner[2], inner[3]], shade(base, 0.9)))
    for poly, col in faces:
        d.polygon([(skew(p)[0] * SS, skew(p)[1] * SS) for p in poly], fill=col)

def tile(img, c1, c2, trench):
    """A rounded tile of flat-shaded triangles, darker toward one corner, with stars and a trench."""
    mask = Image.new('L', (N, N), 0)
    m = 100 * SS
    ImageDraw.Draw(mask).rounded_rectangle((m, m, N - m, N - m), radius=185 * SS, fill=255)
    layer = Image.new('RGB', (N, N))
    d = ImageDraw.Draw(layer)
    rnd = random.Random(7)
    step = 128 * SS
    pts = {}
    for gy in range(0, N + step, step):
        for gx in range(0, N + step, step):
            j = 0 if gx in (0, N) or gy in (0, N) else step * 0.32
            pts[gx, gy] = (gx + rnd.uniform(-j, j), gy + rnd.uniform(-j, j))
    for gy in range(0, N, step):
        for gx in range(0, N, step):
            a, b, c, e = pts[gx, gy], pts[gx + step, gy], pts[gx + step, gy + step], pts[gx, gy + step]
            for tri in ((a, b, c), (a, c, e)):
                cx = sum(p[0] for p in tri) / 3 / N
                cy = sum(p[1] for p in tri) / 3 / N
                col = mix(c1, c2, min(1, max(0, (cx + cy) / 2 + rnd.uniform(-0.08, 0.08))))
                d.polygon(tri, fill=shade(col, rnd.uniform(0.88, 1.12)))
    # stars: small four-pointed sparkles and dots
    for _ in range(70):
        x, y, r = rnd.uniform(110, 914), rnd.uniform(110, 700), rnd.choice((2, 2, 3, 3, 4, 6))
        b = rnd.randint(170, 255)
        if r >= 6:
            d.polygon([((x + dx) * SS, (y + dy) * SS) for dx, dy in ((0, -3 * r), (r * 0.5, 0), (0, 3 * r), (-r * 0.5, 0))], fill=(b, b, b))
            d.polygon([((x + dx) * SS, (y + dy) * SS) for dx, dy in ((-3 * r, 0), (0, r * 0.5), (3 * r, 0), (0, -r * 0.5))], fill=(b, b, b))
        else:
            d.ellipse(((x - r / 2) * SS, (y - r / 2) * SS, (x + r / 2) * SS, (y + r / 2) * SS), fill=(b, b, b))
    # the trench: a floor and two walls running to a vanishing point, panelled in flat greys
    vx, vy = 512, 650
    B = N / SS
    def toward(p, t):                                  # from the vanishing point (t 0) to p (t 1)
        return vx + (p[0] - vx) * t, vy + (p[1] - vy) * t
    floor = ((360, B), (664, B))                       # the floor's edges at the tile's bottom
    rim = ((40, 760), (984, 760))                      # the walls' top edges at the tile's sides
    ts = [0.08 + 0.92 * (k / 8) ** 1.5 for k in range(9)]
    for i in range(len(ts) - 1):
        t, u = ts[i], ts[i + 1]
        odd = i % 2
        for side, k in ((0, 0.78), (1, 0.58)):          # the left wall lit, the right in shadow
            f, r = floor[side], rim[side]
            low = (r[0], B + (B - r[1]) * 0.6)            # the wall's foot below the tile
            p = [toward(r, t), toward(r, u), toward(f, u), toward(f, t)]
            d.polygon([(x * SS, y * SS) for x, y in p], fill=shade(trench, k * (0.88 if odd else 1.04)))
        p = [toward(floor[0], t), toward(floor[1], t), toward(floor[1], u), toward(floor[0], u)]
        d.polygon([(x * SS, y * SS) for x, y in p], fill=shade(trench, 0.36 * (0.85 if odd else 1)))
    for r in rim:                                      # the surface's lit edge along each wall top
        a, b = toward(r, ts[0]), r
        d.line([(a[0] * SS, a[1] * SS), (b[0] * SS, b[1] * SS)], fill=shade(trench, 1.6), width=5 * SS)
    # a soft drop shadow under the tile
    shadow = Image.new('L', (N, N), 0)
    ImageDraw.Draw(shadow).rounded_rectangle((m, m + 14 * SS, N - m, N - m + 14 * SS), radius=185 * SS, fill=110)
    shadow = shadow.filter(ImageFilter.GaussianBlur(18 * SS))
    img.paste((0, 0, 0, 255), (0, 0), shadow)
    img.paste(layer, (0, 0), mask)

def make(name):
    c1, c2, base, edge, trench = PALETTES[name]
    img = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    tile(img, c1, c2, trench)
    # the letters, with a dark outline (the same shapes, grown) for contrast on the tile
    d = ImageDraw.Draw(img)
    for q in strokes():
        cx = sum(p[0] for p in q) / 4; cy = sum(p[1] for p in q) / 4
        grown = [(x + (12 if x > cx else -12), y + (12 if y > cy else -12)) for x, y in q]
        d.polygon([(skew(p)[0] * SS, (skew(p)[1] + 10) * SS) for p in grown], fill=(0, 0, 0, 170))
    for q in strokes():
        chisel(d, q, base, edge, (-0.55, -0.83))
    return img.resize((1024, 1024), Image.LANCZOS)

if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    if len(sys.argv) > 1:
        ims = [make(n) for n in sys.argv[1:]]
        for n, im in zip(sys.argv[1:], ims): im.save(os.path.join(OUT, f'icon_{n}.png'))
        sheet = Image.new('RGB', (360 * len(ims) + 20, 400), (236, 236, 240))
        for i, im in enumerate(ims):
            small = im.resize((340, 340), Image.LANCZOS)
            sheet.paste(small, (20 + i * 360, 30), small)
        sheet.save(os.path.join(OUT, 'choices.png'))
    else:
        im = make(DEFAULT)
        im.save(os.path.join(OUT, 'icon.png'))
        im.save(os.path.join(OUT, 'SWA.ico'), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
        im.save(os.path.join(OUT, 'SWA.icns'))
