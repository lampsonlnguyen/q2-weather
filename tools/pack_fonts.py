#!/usr/bin/env python3
"""Compile the build-time font glyphs into a compact C include.

No weather art is generated here: conditions are text only, inherited from
the q2-pod theme (see src/q2pod_scheme.c). Only anti-aliased sans-serif
glyphs are baked in, plus the existing launcher icon resize. Q2 itself
loads no font or PNG libraries.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
ICONS = BUILD / "icons"
ICONS.mkdir(parents=True, exist_ok=True)


def font_face(size):
    # Use the distro's regular sans-serif only at build time.
    font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    if not Path(font_path).is_file():
        raise RuntimeError("Install fonts-dejavu-core on the build host")
    return ImageFont.truetype(font_path, size=size)


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
    with (BUILD / "weather_fonts.inc").open("w", encoding="ascii") as output:
        output.write("/* Generated from DejaVu Sans; DO NOT EDIT. */\n")
        # Two sizes of proportional, anti-aliased sans-serif text for the UI.
        emit_font(output, "weather_font_small", 12, 13, 18)
        emit_font(output, "weather_font_large", 21, 23, 30)

    # Dedicated editable launcher artwork: bright yellow sun, not muted
    # partly-cloudy forecast art. The source stays in the repository.
    with Image.open(ROOT / "assets" / "icon.png") as launcher:
        launcher.convert("RGBA").resize(
            (52, 52), Image.Resampling.LANCZOS
        ).save(ICONS / "icon.png", "PNG", optimize=True)
    print("Compiled two sans-serif font sizes and the launcher icon")


if __name__ == "__main__":
    main()
