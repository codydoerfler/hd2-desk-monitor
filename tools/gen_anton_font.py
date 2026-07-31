"""Generate src/hud_font_anton.h — Anton as Adafruit-GFX bitmap fonts.

The HUD's other type comes from the Free Fonts that ship inside TFT_eSPI, so
the header title is emitted in exactly that format (a GFXfont struct plus a
glyph table and one continuous bitstream) and is used through the same
setFreeFont() path. Nothing about the renderer has to know it is a custom face.

Only U+0020..U+005A is emitted — space, punctuation, digits and A-Z. The HUD
sets every headline through upper(), so the lowercase half of the face would be
dead flash. That is ~59 glyphs per size instead of 95.

    python3 tools/gen_anton_font.py            # rewrite src/hud_font_anton.h
    python3 tools/gen_anton_font.py --preview  # also dump ASCII art to stdout

Anton is Christian Robertson's, under the SIL Open Font License 1.1, which
permits embedding. tools/assets/Anton-Regular.ttf is the upstream Google Fonts
release.
"""
import argparse
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
TTF = os.path.join(HERE, "assets", "Anton-Regular.ttf")
OUT_H = os.path.join(HERE, "..", "src", "hud_font_anton.h")

FIRST, LAST = 0x20, 0x5A  # space .. 'Z'

# Coverage above which a pixel is set. Anton is a heavy condensed face, so a
# fairly high cut keeps the stems crisp instead of furring them at the edges.
THRESHOLD = 120

# (C identifier, pixel size). The small size is the drop-down used when a
# Major Order title is too long to set at full size.
FACES = [("Anton24px", 24), ("Anton16px", 16)]


def render_glyph(font, ch, ascent):
    """Ink bitmap of `ch` plus its GFX offsets, measured against the baseline.

    Returns (rows, w, h, xOffset, yOffset, xAdvance), where rows is a list of
    rows of 0/1 and the offsets are the distance from the pen position (on the
    baseline) to the bitmap's top-left corner.
    """
    size = font.size
    pad = size * 2
    canvas = Image.new("L", (size * 4, size * 4), 0)
    ImageDraw.Draw(canvas).text((pad, pad), ch, font=font, fill=255)

    xadv = int(round(font.getlength(ch)))

    # Threshold first, then take the bbox, so the reported box is the box that
    # actually gets encoded.
    inked = canvas.point(lambda v: 255 if v >= THRESHOLD else 0)
    box = inked.getbbox()
    if box is None:  # space, and anything else that renders blank
        return [], 0, 0, 0, 0, xadv

    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    px = inked.crop(box).load()
    rows = [[1 if px[x, y] else 0 for x in range(w)] for y in range(h)]
    # PIL's default text anchor puts the ascender line at the draw position, so
    # the baseline sits at pad + ascent.
    return rows, w, h, x0 - pad, y0 - (pad + ascent), xadv


def pack(rows, w, h):
    """GFX glyph encoding: one continuous MSB-first bitstream, rows not padded,
    the glyph as a whole padded out to a byte boundary."""
    out, acc, nbits = [], 0, 0
    for y in range(h):
        for x in range(w):
            acc = (acc << 1) | rows[y][x]
            nbits += 1
            if nbits == 8:
                out.append(acc)
                acc, nbits = 0, 0
    if nbits:
        out.append(acc << (8 - nbits))
    return out


def ascii_art(rows):
    return "\n".join("".join("#" if v else "." for v in r) for r in rows)


def build(name, size, preview=False):
    font = ImageFont.truetype(TTF, size)
    ascent, descent = font.getmetrics()

    bitmaps, glyphs = [], []
    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        rows, w, h, xo, yo, xa = render_glyph(font, ch, ascent)
        off = len(bitmaps)
        bitmaps += pack(rows, w, h)
        glyphs.append((off, w, h, xa, xo, yo, ch))
        if preview and rows:
            print(f"\n{name} '{ch}' {w}x{h} xa={xa} xo={xo} yo={yo}")
            print(ascii_art(rows))

    lines = []
    for i in range(0, len(bitmaps), 12):
        lines.append("    " + " ".join(f"0x{b:02X}," for b in bitmaps[i:i + 12]))
    bm = "\n".join(lines)

    gl = "\n".join(
        f"    {{{off:5d}, {w:3d}, {h:3d}, {xa:3d}, {xo:4d}, {yo:4d}}},"
        f"   // 0x{ord(ch):02X} '{ch if ch != ' ' else 'space'}'"
        for off, w, h, xa, xo, yo, ch in glyphs)

    block = (
        f"// --- {name}: Anton at {size}px, {ascent}px ascent / {descent}px descent ---\n"
        f"static const uint8_t {name}Bitmaps[] PROGMEM = {{\n{bm}\n}};\n\n"
        f"static const GFXglyph {name}Glyphs[] PROGMEM = {{\n{gl}\n}};\n\n"
        f"static const GFXfont {name} PROGMEM = {{\n"
        f"    (uint8_t *){name}Bitmaps,\n"
        f"    (GFXglyph *){name}Glyphs,\n"
        f"    0x{FIRST:02X}, 0x{LAST:02X}, {ascent + descent}}};\n")
    return block, len(bitmaps) + len(glyphs) * 8


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", action="store_true",
                    help="dump ASCII art of every glyph to stdout")
    args = ap.parse_args()

    blocks, total = [], 0
    for name, size in FACES:
        block, n = build(name, size, args.preview)
        blocks.append(block)
        total += n

    header = f"""// ---------------------------------------------------------------------------
//  hud_font_anton.h — Anton, as Adafruit-GFX bitmap fonts.
//
//  AUTO-GENERATED by tools/gen_anton_font.py. Do not edit by hand; change the
//  sizes or the glyph range in that script and re-run it.
//
//  Anton (Christian Robertson) is licensed under the SIL Open Font License
//  1.1, which permits embedding. The source face is
//  tools/assets/Anton-Regular.ttf.
//
//  The face is cut to U+{FIRST:04X}..U+{LAST:04X} — space, punctuation, digits and A-Z.
//  Every headline the HUD sets goes through upper(), so lowercase would be
//  dead flash.
//
//  Format is exactly the one TFT_eSPI's own Free Fonts use, so these are
//  passed to setFreeFont() like any other GFXfont.
//
//  Total font data: {total} bytes.
// ---------------------------------------------------------------------------
#pragma once

#include <TFT_eSPI.h>

{chr(10).join(blocks)}"""

    with open(OUT_H, "w") as f:
        f.write(header)
    print(f"wrote {os.path.relpath(OUT_H, os.getcwd())} "
          f"({len(FACES)} sizes, {total} bytes of font data)")


if __name__ == "__main__":
    main()
