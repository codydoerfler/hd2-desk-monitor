"""Generate the 1-bit HUD icon bitmaps in src/hud_icons.h.

The icons are drawn here as vector shapes at an 8x supersample, box-filtered
down to the target device size and thresholded to 1 bit per pixel. That keeps
them editable — change a coordinate and re-run — rather than hand-encoded.

Output format matches TFT_eSPI::drawBitmap(): row-major, MSB first, each row
padded up to a whole byte, set bits painted in the caller's colour and clear
bits left transparent.

    python3 tools/gen_icons.py            # rewrite src/hud_icons.h
    python3 tools/gen_icons.py --preview  # also dump ASCII art to stdout

All shapes are original simple geometry, not traced from any game asset.
"""
import argparse
import os

from PIL import Image, ImageDraw

S = 8  # supersample factor
THRESHOLD = 100  # 0-255 coverage above which a device pixel is set

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(HERE, "..", "src", "hud_icons.h")


# ---------------------------------------------------------------------------
#  Drawing helpers (all coordinates are in device pixels; scaled by S here)
# ---------------------------------------------------------------------------

def quad(p0, p1, p2, n=24):
    """Sample a quadratic bezier into a point list."""
    out = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        out.append((u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
                    u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]))
    return out


class Canvas:
    """A supersampled 'L' canvas that takes device-pixel coordinates."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.img = Image.new("L", (w * S, h * S), 0)
        self.dr = ImageDraw.Draw(self.img)

    def _p(self, pts):
        return [(x * S, y * S) for x, y in pts]

    def poly(self, pts):
        self.dr.polygon(self._p(pts), fill=255)

    def line(self, pts, width=1.0):
        self.dr.line(self._p(pts), fill=255, width=max(1, round(width * S)),
                     joint="curve")

    def ellipse(self, cx, cy, rx, ry, width=0.0, fill_only=False):
        box = [(cx - rx) * S, (cy - ry) * S, (cx + rx) * S, (cy + ry) * S]
        if width and not fill_only:
            self.dr.ellipse(box, outline=255, width=max(1, round(width * S)))
        else:
            self.dr.ellipse(box, fill=255)

    def rect(self, x0, y0, x1, y1, width=0.0):
        box = [x0 * S, y0 * S, x1 * S, y1 * S]
        if width:
            self.dr.rectangle(box, outline=255, width=max(1, round(width * S)))
        else:
            self.dr.rectangle(box, fill=255)

    def bits(self):
        """Downsample + threshold to a list of rows of 0/1."""
        small = self.img.resize((self.w, self.h), Image.BOX)
        px = small.load()
        return [[1 if px[x, y] >= THRESHOLD else 0 for x in range(self.w)]
                for y in range(self.h)]


# ---------------------------------------------------------------------------
#  Icon shapes
#
#  Each takes a Canvas sized to the icon. Shapes are written against the
#  canvas' own width/height so the Super Earth emblem can be emitted at two
#  sizes from one definition.
# ---------------------------------------------------------------------------

def super_earth(c):
    """Swept-wing emblem: a four-pointed star flanked by two raked wings.

    Original geometry — a long-spiked star body with two wing sweeps. Written
    against normalised coordinates so the same definition emits both the
    header size and the small badge size.
    """
    W, H = c.w, c.h
    cx = W / 2.0

    def X(u):
        return u * W

    def Y(v):
        return v * H

    # --- central four-pointed star ----------------------------------------
    # Compact concave star: the body has to stay small or it swamps the wings
    # at 13px tall.
    c.poly([
        (cx, Y(0.02)),
        (cx + X(0.040), Y(0.36)),
        (cx + X(0.160), Y(0.52)),   # east point
        (cx + X(0.085), Y(0.70)),
        (cx, Y(1.00)),
        (cx - X(0.085), Y(0.70)),
        (cx - X(0.160), Y(0.52)),   # west point
        (cx - X(0.040), Y(0.36)),
    ])
    # The spike tips taper below one pixel, so lay a 2px bar down the axis to
    # keep them solid at both sizes.
    c.rect(cx - 1, 0, cx + 1, H)

    # --- wings: shallow crescents, tips flicked up and outboard ------------
    # Two offset beziers meeting at the tip. The pair stays a band ~3px thick
    # where it meets the star and runs near-horizontally out to the tip,
    # rather than diving in as a triangle.
    for s in (1, -1):
        tip = (cx + s * X(0.480), Y(0.24))
        pts = quad((cx + s * X(0.10), Y(0.34)), (cx + s * X(0.30), Y(0.23)), tip)
        pts += quad(tip, (cx + s * X(0.32), Y(0.50)), (cx + s * X(0.10), Y(0.62)))
        c.poly(pts)


def automaton(c):
    """Angular head: boxy shell, two-slot visor, stalk antenna with a nub."""
    W, H = c.w, c.h
    c.rect(2, 4, W - 2, H, width=1)              # head shell, 10x10
    c.rect(4, 8, 6, 10)                          # visor slot, left
    c.rect(W - 6, 8, W - 4, 10)                  # visor slot, right
    c.rect(W / 2 - 1, 1, W / 2 + 1, 5)           # antenna stalk
    c.rect(W / 2 - 2, 0, W / 2 + 2, 1)           # antenna nub


def terminid(c):
    """Bug: fat abdomen, small head, mandibles and three legs a side."""
    W, H = c.w, c.h
    cx = W / 2.0
    c.ellipse(cx, 9.5, 3.5, 3.5, fill_only=True)  # abdomen
    c.ellipse(cx, 4.5, 2, 2, fill_only=True)      # head
    for s in (1, -1):
        # mandibles, swept up and out from the head
        c.line(quad((cx + s * 1.5, 3.0), (cx + s * 2.5, 1.0),
                    (cx + s * 3.5, 0.5)), width=1)
        # legs
        c.line([(cx + s * 2.5, 7.5), (cx + s * 5.5, 6.5)], width=1)
        c.line([(cx + s * 3.0, 9.5), (cx + s * 6.0, 9.5)], width=1)
        c.line([(cx + s * 2.5, 11.5), (cx + s * 5.0, 13.0)], width=1)


def illuminate(c):
    """Eye/orb: a wide ring around a filled pupil."""
    W, H = c.w, c.h
    cx, cy = W / 2.0, H / 2.0
    c.ellipse(cx, cy, 6, 4, width=1)             # outer ring
    c.ellipse(cx, cy, 2, 2, fill_only=True)      # pupil


# name, width, height, draw fn
ICONS = [
    ("emblem",     24, 13, super_earth),
    ("emblemMini", 16,  9, super_earth),
    ("automaton",  14, 14, automaton),
    ("terminid",   14, 14, terminid),
    ("illuminate", 14, 14, illuminate),
]


# ---------------------------------------------------------------------------
#  Packing + emit
# ---------------------------------------------------------------------------

def pack(rows, w):
    """Rows of 0/1 -> drawBitmap() byte order (MSB first, byte-padded rows)."""
    stride = (w + 7) // 8
    out = []
    for row in rows:
        for b in range(stride):
            byte = 0
            for bit in range(8):
                x = b * 8 + bit
                if x < w and row[x]:
                    byte |= 0x80 >> bit
            out.append(byte)
    return out


def ascii_art(rows):
    return "\n".join("".join("#" if v else "." for v in r) for r in rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", action="store_true",
                    help="dump ASCII art of each icon to stdout")
    args = ap.parse_args()

    blocks, total = [], 0
    for name, w, h, fn in ICONS:
        c = Canvas(w, h)
        fn(c)
        rows = c.bits()
        data = pack(rows, w)
        total += len(data)
        if args.preview:
            print(f"\n{name} {w}x{h} ({len(data)} bytes)")
            print(ascii_art(rows))

        stride = (w + 7) // 8
        lines = []
        for j in range(h):
            chunk = data[j * stride:(j + 1) * stride]
            lines.append("    " + " ".join(f"0x{b:02X}," for b in chunk))
        blocks.append(
            f"constexpr int16_t {name}W = {w};\n"
            f"constexpr int16_t {name}H = {h};\n"
            f"static const uint8_t {name}[] PROGMEM = {{\n"
            + "\n".join(lines) + "\n};\n")

    body = "\n".join(blocks)
    header = f"""// ---------------------------------------------------------------------------
//  hud_icons.h — 1-bit HUD icon bitmaps.
//
//  AUTO-GENERATED by tools/gen_icons.py. Do not edit by hand; edit the shape
//  definitions in that script and re-run it.
//
//  Layout is TFT_eSPI::drawBitmap() order: row-major, MSB first, each row
//  padded to a whole byte. Set bits are painted in the caller's colour, clear
//  bits are left untouched, so the icons composite over whatever is already
//  on screen.
//
//  Total bitmap data: {total} bytes.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

namespace icons {{

{body}
}}  // namespace icons
"""
    with open(OUT_H, "w") as f:
        f.write(header)
    print(f"wrote {os.path.relpath(OUT_H, os.getcwd())} "
          f"({len(ICONS)} icons, {total} bytes of bitmap data)")


if __name__ == "__main__":
    main()
