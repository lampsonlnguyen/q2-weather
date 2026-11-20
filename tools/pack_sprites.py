#!/usr/bin/env python3
"""Compile user-editable PNG assets into a compact C atlas.

No weather art is generated here: edit assets/sprites/*.png to change icons.
Only C sprite pixels, launch icon, and anti-aliased *rasterized text glyphs*
are generated at build time. Q2 itself loads no font or PNG libraries.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets" / "sprites"
BUILD = ROOT / "build"
ICONS = BUILD / "icons"
ICONS.mkdir(parents=True, exist_ok=True)

NAMES = (
    "sunny", "partly", "cloudy", "overcast", "fog", "drizzle", "rain",
    "showers", "thunder", "snow", "snowshowers", "night", "nightcloud",
    "wind", "humidity",
)
SIZE = 24


def convert(name):
    path = ASSETS / (name + ".png")
    with Image.open(path) as image:
        if image.format != "PNG":
            raise ValueError(f"{path} must be PNG")
        if not 16 <= image.width <= 256 or not 16 <= image.height <= 256:
            raise ValueError(f"{path}: image dimensions must be 16–256 px")
        image = image.convert("RGBA")
        if image.size != (SIZE, SIZE):
            image = image.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
        return image


def font_face(size):
    # Use the distro's regular sans-serif only at build time.
    font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    if not Path(font_path).is_file():
        raise RuntimeError("Install fonts-dejavu-core on the build host")
    return ImageFont.truetype(font_path, size=size)


def emit_array(out, name, icons):
    out.write(f"static const unsigned int {name}[{len(icons)}][576] = {{\n")
    for title, img in icons:
        out.write(f" {{ /* {title} */\n")
        colors = []
        for r, g, b, a in img.getdata():
            colors.append(f"0x{a:02x}{r:02x}{g:02x}{b:02x}u")
        for i in range(0, len(colors), 12):
            out.write("  " + ", ".join(colors[i:i + 12]) + ",\n")
        out.write(" },\n")
    out.write("};\n")


def emit_font(out, name, font_size, cell_w, cell_h):
    face = font_face(font_size)
    out.write(f"static const unsigned char {name}[95][{cell_h}][{cell_w}] = {{\n")
    advances = []
    for c in range(32, 127):
        ch = chr(c)
        glyph = Image.new("L", (cell_w, cell_h), 0)
        draw = ImageDraw.Draw(glyph)
        draw.text((0, -2), ch, font=face, fill=255)
        advances.append(max(3, min(cell_w, round(face.getlength(ch)))))
        out.write(" {\n")
        for y in range(cell_h):
            out.write("  {" + ",".join(str(glyph.getpixel((x, y)))
                                         for x in range(cell_w)) + "},\n")
        out.write(" },\n")
    out.write("};\n")
    out.write(f"static const unsigned char {name}_advance[95] = {{\n")
    for i in range(0, 95, 19):
        out.write("  " + ",".join(str(v) for v in advances[i:i + 19]) + ",\n")
    out.write("};\n")


def main():
    images = [(name, convert(name)) for name in NAMES]
    with (BUILD / "weather_sprites.inc").open("w", encoding="ascii") as output:
        output.write("/* Generated from assets/sprites PNGs; DO NOT EDIT. */\n")
        emit_array(output, "weather_sprite", images)
        # Two sizes of proportional, anti-aliased sans-serif text for the UI.
        emit_font(output, "weather_font_small", 12, 13, 18)
        emit_font(output, "weather_font_large", 21, 23, 30)

    # Dedicated editable launcher artwork: bright yellow sun, not muted
    # partly-cloudy forecast art. The source stays in the repository.
    with Image.open(ROOT / "assets" / "icon.png") as launcher:
        launcher.convert("RGBA").resize(
            (52, 52), Image.Resampling.LANCZOS
        ).save(ICONS / "icon.png", "PNG", optimize=True)
    print(f"Compiled {len(images)} replaceable PNG sprites and two sans-serif font sizes")


if __name__ == "__main__":
    main()
