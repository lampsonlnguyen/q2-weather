#!/usr/bin/env python3
"""Generate tiny original pixel-weather icons for Q2 Pod's native art rows.

Source is simple Pillow geometry; Q2 never decodes PNG or runs Python.
"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "icons"
OUT.mkdir(parents=True, exist_ok=True)
SKY = (18, 31, 52)
WHITE = (234, 246, 255)
SHADE = (151, 195, 225)
DARK = (97, 130, 172)
GOLD = (255, 203, 75)
ORANGE = (251, 155, 48)
RAIN = (80, 198, 249)
SNOW = (211, 236, 255)


def base():
    image = Image.new("RGB", (24, 24), SKY)
    return image, ImageDraw.Draw(image)


def sun(draw, x=11, y=9):
    draw.ellipse((x - 4, y - 4, x + 4, y + 4), fill=ORANGE)
    draw.ellipse((x - 3, y - 3, x + 3, y + 3), fill=GOLD)
    for dx, dy in ((0, -7), (0, 7), (-7, 0), (7, 0),
                   (-5, -5), (5, -5), (-5, 5), (5, 5)):
        draw.point((x + dx, y + dy), fill=GOLD)


def cloud(draw, dark=False, x=3, y=9):
    light = DARK if dark else WHITE
    shadow = (61, 82, 118) if dark else SHADE
    draw.rectangle((x, y + 5, x + 18, y + 10), fill=shadow)
    draw.rectangle((x + 2, y + 3, x + 15, y + 9), fill=light)
    draw.rectangle((x + 5, y + 1, x + 11, y + 8), fill=light)
    draw.rectangle((x + 10, y + 2, x + 16, y + 8), fill=light)
    draw.line((x + 2, y + 10, x + 17, y + 10), fill=shadow)


def drops(draw, heavy=False, snow=False):
    for x, y in ((7, 18), (13, 17), (19, 19)):
        if snow:
            draw.line((x - 1, y, x + 1, y), fill=SNOW)
            draw.line((x, y - 1, x, y + 1), fill=SNOW)
        else:
            draw.line((x, y, x - (2 if heavy else 1), min(23, y + 3)),
                      fill=RAIN, width=2 if heavy else 1)
    if heavy:
        draw.line((3, 19, 2, 22), fill=RAIN, width=2)


def moon(draw):
    draw.ellipse((7, 2, 19, 14), fill=GOLD)
    draw.ellipse((11, 0, 21, 11), fill=SKY)
    for x, y in ((4, 5), (20, 17), (7, 19)):
        draw.point((x, y), fill=SNOW)


def make(name):
    image, draw = base()
    if name in ("sunny", "partly", "showers"):
        sun(draw, 10 if name != "sunny" else 12, 9)
    if name in ("partly", "cloudy", "overcast", "fog", "drizzle",
                "rain", "showers", "thunder", "snow", "snowshowers", "nightcloud"):
        cloud(draw, dark=name in ("overcast", "thunder"))
    if name in ("drizzle", "rain", "showers", "thunder"):
        drops(draw, heavy=name in ("showers", "thunder"))
    if name in ("snow", "snowshowers"):
        drops(draw, snow=True)
    if name == "snowshowers":
        draw.point((2, 3), fill=SNOW)
    if name == "fog":
        draw.line((2, 19, 21, 19), fill=SHADE, width=2)
        draw.line((5, 22, 18, 22), fill=SHADE)
    if name == "thunder":
        draw.polygon([(12, 15), (9, 20), (13, 19), (11, 23),
                      (18, 16), (14, 17)], fill=GOLD)
    if name in ("night", "nightcloud"):
        moon(draw)
    if name == "nightcloud":
        cloud(draw, x=4, y=10)
    if name == "wind":
        for y, x in ((8, 4), (12, 2), (17, 6)):
            draw.line((x, y, 20, y), fill=RAIN, width=2)
        draw.point((18, 6), fill=WHITE)
    if name == "humidity":
        draw.polygon([(12, 3), (5, 13), (5, 17), (8, 20), (15, 20),
                      (19, 16), (18, 12)], fill=RAIN)
        draw.line((9, 13, 12, 8), fill=WHITE, width=2)
    return image.resize((96, 96), Image.Resampling.NEAREST)


NAMES = ("sunny", "partly", "cloudy", "overcast", "fog", "drizzle",
         "rain", "showers", "thunder", "snow", "snowshowers",
         "night", "nightcloud", "wind", "humidity")
for name in NAMES:
    make(name).save(OUT / f"{name}.jpg", "JPEG", quality=87, subsampling=0, optimize=True)

# Native Library icon; generated separately so every theme gets an app icon.
app_icon = make("partly")
app_icon.resize((52, 52), Image.Resampling.NEAREST).save(
    OUT / "icon.png", "PNG", optimize=True
)
app_icon.resize((40, 40), Image.Resampling.NEAREST).save(
    OUT / "icon-40.png", "PNG", optimize=True
)
print(f"Generated {len(NAMES)} weather art sprites and two launcher icons")
