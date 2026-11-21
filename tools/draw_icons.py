#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Draw Weather's condition covers: <out>/<name>.jpg (build/icons by default; `make` runs it).

Q2 Pod shows a card app's row cover 64 px square (patch/addon.c ART_PX), so each tile is drawn
at 64 px and shows pixel for pixel. A cover is a JPEG with no transparency, so every tile is a
small square of sky behind its glyph, like an album cover, and reads the same on the light and
dark halves of every theme. Original drawings; needs Pillow. Run from the repository root:

    python3 tools/draw_icons.py [out]
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

PX = 64
SS = 8  # supersampling
S = PX * SS
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join("build", "icons")

SUN = (255, 204, 51)
SUN_RAY = (255, 214, 92)
CLOUD = (255, 255, 255)
CLOUD_GREY = (214, 222, 232)
CLOUD_DARK = (150, 160, 176)
RAIN = (156, 214, 255)
BOLT = (255, 214, 64)

SKIES = {
    "clear": ((31, 118, 224), (95, 178, 245)),
    "cloud": ((94, 114, 138), (150, 168, 188)),
    "wet": ((52, 72, 96), (100, 124, 150)),
    "snow": ((110, 140, 178), (176, 198, 224)),
    "storm": ((34, 36, 58), (78, 80, 116)),
    "fog": ((120, 132, 146), (172, 180, 190)),
}


def u(v):
    return int(round(v * SS))


def sky(name):
    top, bottom = SKIES[name]
    im = Image.new("RGB", (S, S))
    d = ImageDraw.Draw(im)
    for y in range(S):
        t = y / (S - 1)
        d.line([(0, y), (S, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)))
    return im


def sun(d, cx, cy, r, rays=True):
    if rays:
        for k in range(8):
            a = k * math.pi / 4
            x0, y0 = cx + math.cos(a) * (r + 3.5), cy + math.sin(a) * (r + 3.5)
            x1, y1 = cx + math.cos(a) * (r + 8), cy + math.sin(a) * (r + 8)
            d.line([(u(x0), u(y0)), (u(x1), u(y1))], fill=SUN_RAY, width=u(3))
            for x, y in ((x0, y0), (x1, y1)):
                d.ellipse([u(x - 1.5), u(y - 1.5), u(x + 1.5), u(y + 1.5)], fill=SUN_RAY)
    d.ellipse([u(cx - r), u(cy - r), u(cx + r), u(cy + r)], fill=SUN)


def cloud(d, x, y, w, color):
    """A cloud whose flat base runs from x to x + w at height y."""
    h = w * 0.30
    d.rounded_rectangle([u(x), u(y - h), u(x + w), u(y)], radius=u(h / 2), fill=color)
    r1, r2 = w * 0.22, w * 0.30
    d.ellipse([u(x + w * 0.16), u(y - h - r1 * 1.1), u(x + w * 0.16 + 2 * r1),
               u(y - h - r1 * 1.1 + 2 * r1)], fill=color)
    d.ellipse([u(x + w * 0.38), u(y - h - r2 * 1.3), u(x + w * 0.38 + 2 * r2),
               u(y - h - r2 * 1.3 + 2 * r2)], fill=color)


def shadow(im, draw_fn):
    """A soft shadow under what draw_fn draws, then the drawing itself."""
    mask = Image.new("L", im.size, 0)
    draw_fn(ImageDraw.Draw(mask), 255)
    mask = mask.filter(ImageFilter.GaussianBlur(u(1.4)))
    dark = Image.new("RGB", im.size, (0, 0, 0))
    off = Image.new("L", im.size, 0)
    off.paste(mask.point(lambda v: v * 0.22), (0, u(1.2)))
    im.paste(dark, (0, 0), off)


def drops(d, xs, y, color, length=7, slant=2.5):
    for x in xs:
        d.line([(u(x), u(y)), (u(x - slant), u(y + length))], fill=color, width=u(3))
        for px, py in ((x, y), (x - slant, y + length)):
            d.ellipse([u(px - 1.5), u(py - 1.5), u(px + 1.5), u(py + 1.5)], fill=color)


def flakes(d, pts, color, r=3.2):
    for x, y in pts:
        for k in range(3):
            a = k * math.pi / 3 + math.pi / 6
            dx, dy = math.cos(a) * r, math.sin(a) * r
            d.line([(u(x - dx), u(y - dy)), (u(x + dx), u(y + dy))], fill=color, width=u(1.6))


def tile(name, kind):
    im = sky(kind)
    d = ImageDraw.Draw(im)
    return im, d


def make():
    os.makedirs(OUT, exist_ok=True)
    icons = {}

    im, d = tile("sunny", "clear")
    sun(d, 32, 32, 11)
    icons["sunny"] = im

    im, d = tile("partly", "clear")
    sun(d, 24, 24, 9)
    shadow(im, lambda m, c: cloud(m, 18, 46, 36, c))
    cloud(d, 18, 46, 36, CLOUD)
    icons["partly"] = im

    im, d = tile("cloudy", "cloud")
    cloud(d, 8, 36, 32, CLOUD_GREY)
    shadow(im, lambda m, c: cloud(m, 16, 48, 40, c))
    cloud(d, 16, 48, 40, CLOUD)
    icons["cloudy"] = im

    im, d = tile("overcast", "cloud")
    cloud(d, 6, 34, 34, CLOUD_DARK)
    cloud(d, 22, 38, 34, CLOUD_GREY)
    shadow(im, lambda m, c: cloud(m, 12, 50, 40, c))
    cloud(d, 12, 50, 40, CLOUD)
    icons["overcast"] = im

    im, d = tile("fog", "fog")
    cloud(d, 14, 30, 36, CLOUD)
    for y, x0, x1 in ((38, 12, 52), (45, 8, 46), (52, 16, 56)):
        d.rounded_rectangle([u(x0), u(y - 1.6), u(x1), u(y + 1.6)], radius=u(1.6), fill=CLOUD)
    icons["fog"] = im

    def wet(name, kind, extra):
        im, d = tile(name, kind)
        shadow(im, lambda m, c: cloud(m, 12, 34, 40, c))
        cloud(d, 12, 34, 40, CLOUD)
        extra(im, d)
        icons[name] = im

    wet("drizzle", "wet", lambda im, d: drops(d, (22, 32, 42), 42, RAIN, length=4, slant=1.5))
    wet("rain", "wet", lambda im, d: (drops(d, (21, 31, 41), 40, RAIN),
                                      drops(d, (26, 36, 46), 50, RAIN, length=5)))
    wet("snow", "snow", lambda im, d: flakes(d, ((21, 44), (32, 48), (43, 44), (26, 55),
                                                 (38, 56)), CLOUD))

    im, d = tile("showers", "clear")
    sun(d, 22, 20, 8)
    shadow(im, lambda m, c: cloud(m, 14, 38, 40, c))
    cloud(d, 14, 38, 40, CLOUD)
    drops(d, (24, 34, 44), 44, RAIN)
    icons["showers"] = im

    im, d = tile("snowshowers", "snow")
    sun(d, 22, 20, 8)
    shadow(im, lambda m, c: cloud(m, 14, 38, 40, c))
    cloud(d, 14, 38, 40, CLOUD)
    flakes(d, ((24, 47), (35, 51), (46, 47)), CLOUD)
    icons["snowshowers"] = im

    im, d = tile("thunder", "storm")
    cloud(d, 12, 34, 40, CLOUD_GREY)
    bolt = [(35, 34), (25, 47), (31, 47), (27, 59), (40, 43), (33, 43), (38, 34)]
    d.polygon([(u(x), u(y)) for x, y in bolt], fill=BOLT)
    drops(d, (19, 47), 40, RAIN, length=5)
    icons["thunder"] = im

    for name, im in icons.items():
        im.resize((PX, PX), Image.LANCZOS).save(os.path.join(OUT, name + ".jpg"), quality=92,
                                                 optimize=True, subsampling=0)
    print(f"Drew {len(icons)} condition covers into {OUT}/")


if __name__ == "__main__":
    make()
