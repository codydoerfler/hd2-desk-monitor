"""Generate the 1-bit HUD icon bitmaps in src/hud_icons.h.

The icons are drawn here as vector shapes at an 8x supersample, box-filtered
down to the target device size and thresholded to 1 bit per pixel. That keeps
them editable — change a coordinate and re-run — rather than hand-encoded.

Output format matches TFT_eSPI::drawBitmap(): row-major, MSB first, each row
padded up to a whole byte, set bits painted in the caller's colour and clear
bits left transparent.

    python3 tools/gen_icons.py            # rewrite src/hud_icons.h
    python3 tools/gen_icons.py --preview  # also dump ASCII art to stdout

Most shapes are original simple geometry. The two exceptions are the device's
own crest and the SEAF emblem, which are reduced from the source art in
tools/assets/ by threshold-and-fit (see mask_fit) rather than redrawn.
"""
import argparse
import math
import os

from PIL import Image, ImageChops, ImageDraw

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

    def arc(self, cx, cy, rx, ry, start, end, width=1.0, v=255):
        """Stroked elliptical arc. Angles are PIL's: degrees clockwise from 3
        o'clock, so 180->360 is the upper half."""
        box = [(cx - rx) * S, (cy - ry) * S, (cx + rx) * S, (cy + ry) * S]
        self.dr.arc(box, start, end, fill=v, width=max(1, round(width * S)))

    def bits(self):
        """Downsample + threshold to a list of rows of 0/1."""
        small = self.img.resize((self.w, self.h), Image.BOX)
        px = small.load()
        return [[1 if px[x, y] >= THRESHOLD else 0 for x in range(self.w)]
                for y in range(self.h)]


# ---------------------------------------------------------------------------
#  Source-art icons
#
#  Two marks are not drawn here: the device's crest and the SEAF emblem both
#  come from artwork in tools/assets/, reduced to 1 bit by the same
#  threshold-then-fit route so neither is redrawn as approximate vector
#  geometry.
# ---------------------------------------------------------------------------

def mask_fit(c, mask, even_w=False):
    """Scale a 1-bit mask to fit the canvas' supersampled slot and centre it.

    The mask keeps its own aspect ratio — it is letterboxed inside the slot
    rather than stretched to the slot's exact dimensions, since the icon's
    declared w:h in ICONS is a layout box, not a statement about the art.
    Canvas.bits() then box-filters and thresholds it like any drawn shape.

    `even_w` rounds the scaled width to an even number of supersamples so the
    centring divides exactly. Without it a mask that lands on an odd width sits
    half a supersample off centre, and the box filter then reads a shade more
    coverage down one side than the other — which on art that is symmetric to
    begin with shows up as an icon whose two halves do not match. Only the task
    medallions ask for it; the marks that predate it are asymmetric anyway and
    would only shift if it were made unconditional.
    """
    src_w, src_h = mask.size
    slot_w, slot_h = c.w * S, c.h * S

    scale = min(slot_w / src_w, slot_h / src_h)
    new_w, new_h = max(1, round(src_w * scale)), max(1, round(src_h * scale))
    if even_w:
        new_w = max(2, round(new_w / 2) * 2)
        if new_w > slot_w:
            new_w -= 2
    resized = mask.resize((new_w, new_h), Image.LANCZOS)

    c.img.paste(resized, ((slot_w - new_w) // 2, (slot_h - new_h) // 2))


# The SEAF emblem source is a flag photo: a blue-grey field (luminance ~115)
# behind a white device (~255) and a gold laurel (~195), letterboxed with black
# bars (~0). One cut halfway between the field and the laurel therefore lifts
# the whole emblem — laurel included — and drops both field and bars.
SEAF_CUT = 155


def seaf(c):
    """Super Earth's SEAF emblem, from tools/assets/seaf_emblem_source.jpg.

    The mark is all but square (647x620 of content), so in the wider
    centrepiece slot it sits centred with the slack left either side. Its fine
    detail — the globe's grid lines, the laurel, the stars — only survives the
    1-bit reduction at the 72x39 centrepiece size, which is why that is the
    only size cut from it.
    """
    src = Image.open(os.path.join(HERE, "assets", "seaf_emblem_source.jpg"))
    m = src.convert("L").point(lambda p: 255 if p >= SEAF_CUT else 0)
    # getbbox() drops the black bars and the field margin in one step, so the
    # icon is cut from the emblem itself rather than from the photo's framing.
    mask_fit(c, m.crop(m.getbbox()))


def crest(c):
    """The monitor's own mark: a winged skull, reduced from the reference art
    the user supplied (tools/assets/crest_mask_v2.png — a plain black-on-white
    silhouette), not redrawn as vector geometry.

    The source is ~2.68:1 (560x209), which need not match this icon's own w:h
    slot; mask_fit() letterboxes rather than stretches.
    """
    mask = Image.open(os.path.join(HERE, "assets", "crest_mask_v2.png")).convert("L")
    mask_fit(c, mask.point(lambda p: 255 if p < 128 else 0))


# The Helldivers II wordmark the user supplied directly: black field, solid
# yellow mark. A mid threshold on luminance alone separates them cleanly, no
# colour masking needed. Boot screen only -- the idle screen keeps the SEAF
# emblem, so this is a distinct icon rather than a swap of emblemLarge.
HD2_LOGO_CUT = 128


def hd2_logo(c):
    """The Helldivers II title mark (II + wordmark + skull), reduced from the
    user-supplied source art in tools/assets/hd2_logo_source.jpg, not redrawn.

    Boot-screen centrepiece only.
    """
    src = Image.open(os.path.join(HERE, "assets", "hd2_logo_source.jpg")).convert("L")
    m = src.point(lambda p: 255 if p >= HD2_LOGO_CUT else 0)
    mask_fit(c, m.crop(m.getbbox()))


# ---------------------------------------------------------------------------
#  Major Order task medallions
#
#  The four marks the in-game Major Order screen sets beside each objective row
#  of a multi-target kill order — Agitators, Vox Engines, Obtruders,
#  Gatekeepers. Traced from tools/assets/task_icon_*_source.png, which are
#  crops of the same reference screenshot the combined card itself was built
#  from, by the same threshold-and-fit route as seaf() and crest(); they are
#  not redrawn as vector geometry.
#
#  Two things have to happen to a crop before mask_fit() can take it.
#
#  The medallion's ring goes. There is no room for a badge-in-a-ring beside a
#  row of 6x8 text at the height the card gives a row, and the ring is the one
#  part of the art that is identical on all four marks, so it is the part
#  carrying no information here. It is cut by connected component rather than
#  by a circular crop: every blob whose centroid sits outside RING_KEEP of the
#  medallion's radius is ring — or the neighbouring row divider a crop caught —
#  and is dropped, which keeps the marks' own detached pieces, the Vox Engine's
#  side ticks and the Gatekeeper's shoulders, that one circle big enough to
#  clear the skull would have swept up along with the ring.
#
#  Then each crop is averaged against its own mirror. Every one of the four
#  marks is drawn symmetric, but the crops are ~26px of a compressed screenshot
#  upscaled, so the two halves disagree pixel for pixel; folding the crop over
#  its axis cancels that rather than baking one half's jpeg artefacts into the
#  icon. It is the by-eye cleanup the source needs, expressed as one operation.

# Medallion geometry, measured off the crops: centre and ring radius in source
# pixels, then the luminance a pixel has to reach to count as part of the mark.
# The cuts differ because the marks do: the Vox Engine's antenna is the
# faintest stroke in the set and a cut sized for the Agitators skull loses it.
#
#     name: (cx, cy, ring radius, luminance cut)
TASK_MEDALLIONS = {
    "taskAgitators":  (144, 129, 100, 90),
    "taskVoxEngine":  (144, 128, 100, 70),
    "taskObtruder":   (145, 119, 111, 90),
    "taskGatekeeper": (145, 171, 111, 70),
}

# Source file per mark, keyed the same way.
TASK_SOURCES = {
    "taskAgitators":  "task_icon_agitators_source.png",
    "taskVoxEngine":  "task_icon_voxengine_source.png",
    "taskObtruder":   "task_icon_obtruder_source.png",
    "taskGatekeeper": "task_icon_gatekeeper_source.png",
}

# How far out a blob's centroid may sit, as a fraction of the ring radius, and
# still count as part of the mark. The ring's own fragments centre on 1.0 by
# definition; the outermost piece of any of the four marks centres on 0.62.
RING_KEEP = 0.72


def _blobs(px, w, h):
    """8-connected components of a 0/255 mask, as lists of (x, y)."""
    seen = bytearray(w * h)
    out = []
    for y0 in range(h):
        for x0 in range(w):
            if not px[x0, y0] or seen[y0 * w + x0]:
                continue
            stack, pts = [(x0, y0)], []
            seen[y0 * w + x0] = 1
            while stack:
                x, y = stack.pop()
                pts.append((x, y))
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = x + dx, y + dy
                        if (0 <= nx < w and 0 <= ny < h
                                and not seen[ny * w + nx] and px[nx, ny]):
                            seen[ny * w + nx] = 1
                            stack.append((nx, ny))
            out.append(pts)
    return out


def task_mark(name):
    """The inner symbol of one task medallion, as a tight 1-bit mask."""
    cx, cy, radius, cut = TASK_MEDALLIONS[name]
    src = Image.open(os.path.join(HERE, "assets", TASK_SOURCES[name])).convert("L")

    # A window a little wider than the ring, so the fold has the whole
    # medallion to work with and nothing of the row above or below it.
    r = int(radius * 1.10)
    win = src.crop((cx - r, cy - r, cx + r, cy + r))
    win = ImageChops.add(win.point(lambda p: p // 2),
                         win.transpose(Image.FLIP_LEFT_RIGHT).point(lambda p: p // 2))

    m = win.point(lambda p: 255 if p >= cut else 0)
    w, h = m.size
    px = m.load()

    keep = Image.new("L", (w, h), 0)
    kp = keep.load()
    for blob in _blobs(px, w, h):
        mx = sum(p[0] for p in blob) / len(blob)
        my = sum(p[1] for p in blob) / len(blob)
        if ((mx - r) ** 2 + (my - r) ** 2) ** 0.5 <= RING_KEEP * radius:
            for x, y in blob:
                kp[x, y] = 255

    bbox = keep.getbbox()
    return keep.crop(bbox) if bbox else keep


def task_icon(name):
    """Draw fn for one task medallion, for the ICONS table.

    Each mark is letterboxed into its own slot rather than scaled by a rule
    shared across the four, so the small ones are not left illegible: the Vox
    Engine mark occupies about half of its medallion and the Agitators skull
    nearly all of one, and holding that ratio at 16px would leave the first a
    smudge. What the row needs from these is 'which of the four is this', not a
    faithful record of how much of a ring each one filled.
    """
    return lambda c: mask_fit(c, task_mark(name), even_w=True)



# ---------------------------------------------------------------------------
#  Icon shapes
#
#  Each takes a Canvas sized to the icon. Shapes are written against the
#  canvas' own width/height so one definition can be emitted at several sizes.
# ---------------------------------------------------------------------------

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


# ---------------------------------------------------------------------------
#  Major Order card
#
#  The card's header names the kind of objective, its identity row carries a
#  planet disc, and its scene band shows one chip per hazard the API actually
#  reports for that planet. Everything below is drawn from the same primitives
#  as the set above.
# ---------------------------------------------------------------------------

def shield(c):
    """Defend objective: a plain shield outline."""
    W, H = c.w, c.h
    pts = [(W * 0.50, H * 0.06), (W * 0.93, H * 0.22), (W * 0.93, H * 0.50),
           (W * 0.50, H * 0.94), (W * 0.07, H * 0.50), (W * 0.07, H * 0.22)]
    c.line(pts + [pts[0]], width=max(1.0, W * 0.115))


def liberate(c):
    """Liberate objective: a pennant planted on a staff."""
    W, H = c.w, c.h
    t = max(1.0, W * 0.10)
    c.rect(W * 0.17, H * 0.04, W * 0.17 + t, H * 0.92)              # staff
    c.poly([(W * 0.27, H * 0.08), (W * 0.94, H * 0.29),
            (W * 0.27, H * 0.50)])                                  # pennant
    c.rect(W * 0.02, H * 0.86, W * 0.60, H * 0.97)                  # ground


def globe(c):
    """Planet disc for the identity row: a ring with an equator and one
    meridian, which is enough to read as a body rather than a target ring."""
    W, H = c.w, c.h
    cx, cy = W / 2.0, H / 2.0
    t = max(1.0, W * 0.075)
    c.ellipse(cx, cy, W * 0.46, H * 0.46, width=t)
    c.line([(cx - W * 0.45, cy), (cx + W * 0.45, cy)], width=t * 0.75)
    c.ellipse(cx, cy, W * 0.18, H * 0.46, width=t * 0.75)


def haz_fire(c):
    """Fire tornadoes / volcanic activity / intense heat: a flame."""
    W, H = c.w, c.h
    cx = W / 2.0
    pts = quad((cx, 0), (cx + W * 0.48, H * 0.40), (cx + W * 0.31, H * 0.76))
    pts += quad((cx + W * 0.31, H * 0.76), (cx + W * 0.31, H * 0.99), (cx, H * 0.99))
    pts += quad((cx, H * 0.99), (cx - W * 0.31, H * 0.99), (cx - W * 0.31, H * 0.76))
    pts += quad((cx - W * 0.31, H * 0.76), (cx - W * 0.36, H * 0.38), (cx, 0))
    c.poly(pts)
    c.poly([(cx, H * 0.44), (cx + W * 0.16, H * 0.72), (cx, H * 0.93),
            (cx - W * 0.16, H * 0.72)], v=0)                        # cooler core


def haz_ion(c):
    """Ion storms: a lightning bolt."""
    W, H = c.w, c.h
    c.poly([(W * 0.66, 0), (W * 0.16, H * 0.56), (W * 0.44, H * 0.56),
            (W * 0.32, H * 1.00), (W * 0.86, H * 0.40), (W * 0.54, H * 0.40)])


def haz_cold(c):
    """Blizzards / extreme cold: a six-spoke snowflake."""
    W, H = c.w, c.h
    cx, cy = W / 2.0, H / 2.0
    t = max(1.0, W * 0.10)
    for k in range(3):
        a = math.radians(60 * k)
        dx, dy = math.cos(a) * W * 0.48, math.sin(a) * H * 0.48
        c.line([(cx - dx, cy - dy), (cx + dx, cy + dy)], width=t)


def haz_meteor(c):
    """Meteor storms: a rock with two trailing streaks."""
    W, H = c.w, c.h
    t = max(1.0, W * 0.11)
    c.ellipse(W * 0.63, H * 0.67, W * 0.30, H * 0.30, fill_only=True)
    c.line([(W * 0.03, H * 0.02), (W * 0.40, H * 0.39)], width=t)
    c.line([(W * 0.38, H * 0.02), (W * 0.62, H * 0.26)], width=t)


def haz_storm(c):
    """Sandstorms / fog / rainstorms: three drifting bands."""
    W, H = c.w, c.h
    t = max(1.0, W * 0.115)
    for i, (x0, x1) in enumerate(((0.04, 0.84), (0.18, 0.98), (0.04, 0.68))):
        y = H * (0.22 + 0.28 * i)
        c.line([(W * x0, y), (W * x1, y)], width=t)


def haz_tremor(c):
    """Tremors: a seismic trace over a ground line."""
    W, H = c.w, c.h
    t = max(1.0, W * 0.10)
    c.line([(W * 0.02, H * 0.40), (W * 0.22, H * 0.40), (W * 0.35, H * 0.04),
            (W * 0.52, H * 0.76), (W * 0.66, H * 0.24), (W * 0.77, H * 0.40),
            (W * 0.98, H * 0.40)], width=t)
    c.rect(W * 0.02, H * 0.84, W * 0.98, H * 0.95)


def haz_other(c):
    """Any hazard the set has no glyph for. The API's hazard names are free
    text, so this is what keeps an unrecognised one visible instead of
    silently dropped."""
    # Solid triangle with the bang knocked out of it. An outlined triangle
    # leaves so little black at 14px that the bang's stem fuses with the apex
    # and the whole chip reads as a capital A.
    W, H = c.w, c.h
    c.poly([(W * 0.50, H * 0.02), (W * 1.00, H * 0.95), (W * 0.00, H * 0.95)])
    c.rect(W * 0.42, H * 0.38, W * 0.58, H * 0.66, v=0)
    c.rect(W * 0.42, H * 0.74, W * 0.58, H * 0.88, v=0)


def trend(c):
    """Marker for a %-per-hour readout: the same double chevron the progress
    bars cap their fill with, so the two read as one idea."""
    W, H = c.w, c.h
    c.poly([(0, 0), (W * 0.50, H * 0.5), (0, H)])
    c.poly([(W * 0.48, 0), (W * 0.98, H * 0.5), (W * 0.48, H)])


def gauge(c):
    """Verdict marker for WINNING / LOSING: a half dial with a needle.

    Kept sparse — at 16px a thicker arc, a hub and a baseplate all crowd the
    needle until the whole thing silts up into a blob.
    """
    W, H = c.w, c.h
    cx, cy = W / 2.0, H * 0.80
    t = max(1.0, W * 0.085)
    c.arc(cx, cy, W * 0.46, H * 0.60, 180, 360, width=t)
    c.line([(cx, cy), (cx + W * 0.24, cy - H * 0.34)], width=t * 1.3)
    c.rect(W * 0.16, cy + t, W * 0.84, cy + t * 2.2)


def diver(c):
    """One helldiver, head and shoulders — the card footer's player-count
    marker. The three-quarter pair in `divers` is too tall for that row."""
    W, H = c.w, c.h
    c.ellipse(W * 0.50, H * 0.22, W * 0.30, H * 0.24, fill_only=True)
    c.poly([(W * 0.02, H * 1.00), (W * 0.19, H * 0.52),
            (W * 0.81, H * 0.52), (W * 0.98, H * 1.00)])


# name, width, height, draw fn
ICONS = [
    # The only size the emblem is cut at: below ~40px its globe grid, laurel
    # and stars all silt up into a blob, so the header row and the SEAF faction
    # badge are text-only rather than carrying an illegible icon.
    ("emblemLarge", 72, 39, seaf),          # idle-screen centrepiece
    ("hd2LogoBoot", 440, 172, hd2_logo),    # boot-screen centrepiece, full width
    ("automaton",   20, 20, automaton),
    ("terminid",    20, 20, terminid),
    ("illuminate",  20, 20, illuminate),
    ("target",      20, 20, target),
    ("clock",       24, 24, clock),
    ("divers",      24, 24, divers),
    ("skull",       24, 24, skull),
    ("check",       24, 24, check),
    ("medal",       14, 14, medal),         # footer reward

    # --- Major Order card -------------------------------------------------
    ("crest",      132, 55, crest),         # the device's own mark, 2.68:1 source
    ("shield",      18, 18, shield),        # card header, defend objective
    ("liberate",    18, 18, liberate),      # card header, liberate objective
    ("globe",       28, 28, globe),         # card identity row
    ("gauge",       16, 16, gauge),         # card verdict, WINNING / LOSING
    ("trend",       12, 10, trend),         # card %/h readouts
    ("diver",       13, 15, diver),         # card footer, player count
    # One chip per hazard the API reports for the planet, matched on the
    # hazard's own name in hd2_model.h. hazOther catches the rest.
    ("hazFire",     14, 14, haz_fire),
    ("hazIon",      14, 14, haz_ion),
    ("hazCold",     14, 14, haz_cold),
    ("hazMeteor",   14, 14, haz_meteor),
    ("hazStorm",    14, 14, haz_storm),
    ("hazTremor",   14, 14, haz_tremor),
    ("hazOther",    14, 14, haz_other),

    # --- combined count card, one per objective row ------------------------
    # 16x16 is moCombCapH exactly, so a mark sits in the same box the row's
    # caption is set in and cannot reach the track below it or the row above.
    # See drawCombinedRow() for the order these are handed out in, and the
    # comment above taskIcon() for why that order is by position.
    ("taskAgitators",  16, 16, task_icon("taskAgitators")),
    ("taskVoxEngine",  16, 16, task_icon("taskVoxEngine")),
    ("taskObtruder",   16, 16, task_icon("taskObtruder")),
    ("taskGatekeeper", 16, 16, task_icon("taskGatekeeper")),
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
