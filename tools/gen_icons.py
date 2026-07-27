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
    """A supersampled 'L' canvas that takes device-pixel coordinates.

    Every primitive takes an optional `v`: 255 paints, 0 erases. Erasing is how
    the interior details (skull eye sockets, the medal's star) are cut out of a
    filled silhouette.
    """

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.img = Image.new("L", (w * S, h * S), 0)
        self.dr = ImageDraw.Draw(self.img)

    def _p(self, pts):
        return [(x * S, y * S) for x, y in pts]

    def poly(self, pts, v=255):
        self.dr.polygon(self._p(pts), fill=v)

    def line(self, pts, width=1.0, v=255):
        self.dr.line(self._p(pts), fill=v, width=max(1, round(width * S)),
                     joint="curve")

    def ellipse(self, cx, cy, rx, ry, width=0.0, fill_only=False, v=255):
        box = [(cx - rx) * S, (cy - ry) * S, (cx + rx) * S, (cy + ry) * S]
        if width and not fill_only:
            self.dr.ellipse(box, outline=v, width=max(1, round(width * S)))
        else:
            self.dr.ellipse(box, fill=v)

    def rect(self, x0, y0, x1, y1, width=0.0, v=255):
        box = [x0 * S, y0 * S, x1 * S, y1 * S]
        if width:
            self.dr.rectangle(box, outline=v, width=max(1, round(width * S)))
        else:
            self.dr.rectangle(box, fill=v)

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
    t = max(1.0, W * 0.10)
    c.rect(W * 0.14, H * 0.28, W * 0.86, H * 0.99, width=t)  # head shell
    c.rect(W * 0.28, H * 0.55, W * 0.44, H * 0.71)           # visor slot, left
    c.rect(W * 0.56, H * 0.55, W * 0.72, H * 0.71)           # visor slot, right
    c.rect(W * 0.45, H * 0.08, W * 0.55, H * 0.30)           # antenna stalk
    c.rect(W * 0.36, H * 0.00, W * 0.64, H * 0.10)           # antenna nub


def terminid(c):
    """Bug: fat abdomen, small head, mandibles and three legs a side."""
    W, H = c.w, c.h
    cx = W / 2.0
    t = max(1.0, W * 0.062)
    c.ellipse(cx, H * 0.68, W * 0.235, H * 0.235, fill_only=True)  # abdomen
    c.ellipse(cx, H * 0.32, W * 0.145, H * 0.145, fill_only=True)  # head
    for s in (1, -1):
        # mandibles, swept up and out from the head
        c.line(quad((cx + s * W * 0.11, H * 0.21), (cx + s * W * 0.18, H * 0.07),
                    (cx + s * W * 0.25, H * 0.035)), width=t)
        # legs
        c.line([(cx + s * W * 0.20, H * 0.55), (cx + s * W * 0.44, H * 0.45)], width=t)
        c.line([(cx + s * W * 0.22, H * 0.68), (cx + s * W * 0.47, H * 0.68)], width=t)
        c.line([(cx + s * W * 0.20, H * 0.81), (cx + s * W * 0.41, H * 0.94)], width=t)


def illuminate(c):
    """Eye/orb: a wide ring around a filled pupil."""
    W, H = c.w, c.h
    cx, cy = W / 2.0, H / 2.0
    c.ellipse(cx, cy, W * 0.43, H * 0.29, width=max(1.0, W * 0.085))  # outer ring
    c.ellipse(cx, cy, W * 0.145, H * 0.145, fill_only=True)           # pupil


def target(c):
    """Crosshair: a ring with four ticks and a centre pip. Replaces the word
    "TARGET" on the target row."""
    W, H = c.w, c.h
    cx, cy = W / 2.0, H / 2.0
    t = max(1.0, W * 0.085)
    c.ellipse(cx, cy, W * 0.30, H * 0.30, width=t)
    c.ellipse(cx, cy, W * 0.07, H * 0.07, fill_only=True)
    for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
        c.line([(cx + dx * W * 0.38, cy + dy * H * 0.38),
                (cx + dx * W * 0.50, cy + dy * H * 0.50)], width=t)


def clock(c):
    """Time remaining: a ring with two hands at ~10:10-ish, i.e. one up and one
    out to the right so both stay distinct at 24px."""
    W, H = c.w, c.h
    cx, cy = W / 2.0, H / 2.0
    t = max(1.0, W * 0.095)
    c.ellipse(cx, cy, W * 0.44, H * 0.44, width=t)
    c.line([(cx, cy), (cx, cy - H * 0.27)], width=t)             # minute hand
    c.line([(cx, cy), (cx + W * 0.19, cy + H * 0.08)], width=t)  # hour hand
    c.ellipse(cx, cy, W * 0.06, H * 0.06, fill_only=True)        # spindle


def divers(c):
    """Player count: two head-and-shoulders figures, the front one larger. The
    back figure is cut back out of the front one so the pair reads as two
    bodies rather than one blob."""
    W, H = c.w, c.h

    W2, H2 = c.w, c.h

    def figure(cx, headY, r, top, base, wTop, wBase, v=255, g=0.0):
        """Head circle plus a tapered shoulders/torso trapezoid. `g` inflates
        the whole shape, which is how the gap between the two is cut."""
        c.ellipse(cx, headY, r + g, r + g, fill_only=True, v=v)
        c.poly([(cx - wBase - g, base + g), (cx - wTop - g, top),
                (cx + wTop + g, top), (cx + wBase + g, base + g)], v=v)

    # Back figure first, then a gap punched around the front one, then the
    # front one drawn into that gap.
    figure(W2 * 0.72, H2 * 0.26, W2 * 0.105, H2 * 0.38, H2 * 0.94,
           W2 * 0.105, W2 * 0.155)
    for v, g in ((0, 1.3), (255, 0.0)):
        figure(W2 * 0.38, H2 * 0.30, W2 * 0.130, H2 * 0.45, H2 * 1.00,
               W2 * 0.130, W2 * 0.190, v=v, g=g)


def skull(c):
    """Kill count: cranium, jaw block, sunken eye sockets and a nose notch."""
    W, H = c.w, c.h
    cx = W / 2.0
    c.ellipse(cx, H * 0.42, W * 0.40, H * 0.36, fill_only=True)  # cranium
    c.rect(W * 0.30, H * 0.60, W * 0.70, H * 0.92)               # jaw
    for s in (1, -1):
        c.ellipse(cx + s * W * 0.18, H * 0.42, W * 0.13, H * 0.14,
                  fill_only=True, v=0)                           # eye socket
    c.poly([(cx, H * 0.52), (cx + W * 0.07, H * 0.66), (cx - W * 0.07, H * 0.66)],
           v=0)                                                  # nose notch
    c.rect(cx - W * 0.015, H * 0.74, cx + W * 0.015, H * 0.92, v=0)  # teeth gap


def medal(c):
    """Reward: two ribbon bands above a disc with a four-point star cut out."""
    W, H = c.w, c.h
    cx = W / 2.0
    c.poly([(W * 0.20, 0), (W * 0.40, 0), (W * 0.58, H * 0.42), (W * 0.40, H * 0.48)])
    c.poly([(W * 0.80, 0), (W * 0.60, 0), (W * 0.42, H * 0.42), (W * 0.60, H * 0.48)])
    c.ellipse(cx, H * 0.70, W * 0.29, H * 0.29, fill_only=True)
    # Four-point star punched out of the disc, same concave shape as the
    # emblem's body so the icon set stays consistent.
    r, k = W * 0.20, W * 0.055
    c.poly([(cx, H * 0.70 - r), (cx + k, H * 0.70 - k), (cx + r, H * 0.70),
            (cx + k, H * 0.70 + k), (cx, H * 0.70 + r), (cx - k, H * 0.70 + k),
            (cx - r, H * 0.70), (cx - k, H * 0.70 - k)], v=0)


def check(c):
    """Mission success rate: a plain thick tick."""
    W, H = c.w, c.h
    c.line([(W * 0.12, H * 0.52), (W * 0.40, H * 0.80), (W * 0.90, H * 0.16)],
           width=max(1.0, W * 0.155))


# name, width, height, draw fn
ICONS = [
    ("emblem",      24, 13, super_earth),   # header row
    ("emblemBadge", 26, 14, super_earth),   # faction badge, "HUMANS"
    ("emblemLarge", 72, 39, super_earth),   # idle-screen centrepiece
    ("automaton",   20, 20, automaton),
    ("terminid",    20, 20, terminid),
    ("illuminate",  20, 20, illuminate),
    ("target",      20, 20, target),
    ("clock",       24, 24, clock),
    ("divers",      24, 24, divers),
    ("skull",       24, 24, skull),
    ("check",       24, 24, check),
    ("medal",       14, 14, medal),         # footer reward
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
