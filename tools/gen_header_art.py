"""Generate src/hud_header_art.h — the Major Order header bar's artwork.

Three assets come out of here:

  * headerBadge — 1-bit, the white disc plus the triple arc sweep beneath it.
  * headerSkull — 1-bit, the skull that sits inside the disc, painted dark.
  * headerBg    — the Earth/gold-sweep background wash, RGB565 at half the
                  header's pixel size. The renderer bilinear-upscales it (see
                  HUDRenderer::drawHeaderBg), which is what keeps it to ~12KB
                  of flash instead of the ~51KB a full-resolution strip would
                  cost. The source is a soft, blurred, heavily dimmed photo, so
                  there is no detail for the halving to lose.

    python3 tools/gen_header_art.py            # rewrite src/hud_header_art.h
    python3 tools/gen_header_art.py --preview  # also dump PNGs to /tmp

Everything here is a transcription of tools/assets/header_mockup_reference.py
(draw_skull_badge / draw_earth_bg_strip / draw_header_bar), which is the
approved visual spec. Two deliberate departures, both forced by resolution:

  * The background is composed at the mockup's own 1440x174 and then resized
    down, rather than composed at device size. The mockup's geometry is partly
    a function of the strip's aspect ratio — the globe's width is 5x the bar
    height — so composing at 440x58 would have moved the planet's limb out from
    under the gold shaft that is meant to graze it.
  * The badge's arcs are redrawn at kArcWidth / kArcRadii. The mockup's stroke
    is S/60 of a 4x supersample — 0.75 device pixels here, which thresholds
    into a dashed line — and its radii are 0.12*r apart, which at a 47px badge
    is under 2px, so the three arcs hatch into one grey smear. A 1px stroke at
    0.20*r spacing keeps them reading as three arcs, which is the point of
    them.

Asset provenance: tools/assets/earth_nasa.jpg is NASA's Blue Marble, public
domain. The skull inside the disc is the winged skull in
tools/assets/crest_mask_v2.png — the same art gen_icons.py cuts the `crest`
icon from, so the badge and the crest are one mark. It supersedes the plain
traced skull the mockup used (skull_traced_mask.png, kept in assets/ but no
longer read).
"""
import argparse
import os

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")
OUT_H = os.path.join(HERE, "..", "src", "hud_header_art.h")

# --- device geometry (mirrored in layout:: in src/config.h) ----------------
HDR_W, HDR_H = 440, 58          # the header bar's footprint on screen
BG_DIV = 2                      # background stored at 1/BG_DIV, upscaled on device
BADGE = int(HDR_H * 0.82)       # 47 — mockup: badge_size = int(h*0.82)

# --- the mockup's own render size, which its proportions are tuned to ------
MOCK_W, MOCK_H = 1440, 174

# --- palette, from the mockup ---------------------------------------------
NAVY_DARK = (14, 19, 30)
GOLD = (223, 178, 79)
WHITE = (232, 234, 237)
SKULL = (20, 22, 28)

SS = 8            # badge supersample factor
THRESHOLD = 100   # coverage above which a device pixel is set (as gen_icons.py)
kArcWidth = 1.0                    # device-pixel stroke of the badge's arcs
kArcRadii = (1.18, 1.38, 1.58)     # arc radii, as multiples of the disc's


# ---------------------------------------------------------------------------
#  Skull badge — mockup draw_skull_badge()
# ---------------------------------------------------------------------------

def traced_skull(target_h, max_w=None):
    """The winged-skull mask, smoothed to the mockup's recipe.

    Source is tools/assets/crest_mask_v2.png — the same black-on-white winged
    skull gen_icons.py cuts the `crest` icon from, so the header badge and the
    crest are the one mark rather than two different skulls. It replaces the
    older plain-skull trace (skull_traced_mask.png, still in assets/ but no
    longer read from here).

    Upscale, blur, re-threshold, blur again: that rounds off the edges without
    moving the silhouette. The old trace was 68x72 and got a fixed 12x blow-up;
    this source is already 560x209, so the factor is derived instead, landing
    the working image at ~860px tall like the old one did. That keeps the blur
    radii meaning the same thing relative to the art they are smoothing.

    max_w clamps the result's width, scaling height to match — the winged skull
    is 2.68:1, so inside the badge's disc it is width-bound, not height-bound.
    """
    mask = Image.open(os.path.join(ASSETS, "crest_mask_v2.png")).convert("L")
    mask = mask.point(lambda p: 255 if p < 128 else 0)
    up = max(1, round(860 / mask.height))
    big = mask.resize((mask.width * up, mask.height * up), Image.LANCZOS)
    big = big.filter(ImageFilter.GaussianBlur(9))
    big = big.point(lambda v: 255 if v > 110 else 0)
    big = big.filter(ImageFilter.GaussianBlur(4))
    box = big.getbbox()
    if box:
        big = big.crop(box)
    new_w = max(1, int(big.width * target_h / big.height))
    if max_w is not None and new_w > max_w:
        target_h = max(1, round(target_h * max_w / new_w))
        new_w = max_w
    return big.resize((new_w, target_h), Image.LANCZOS)


def badge_masks(size):
    """(disc+arcs, skull) as supersampled 'L' masks, both `size`*SS square."""
    S = size * SS
    disc = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(disc)
    cx, cy = S // 2, int(S * 0.42)
    r = int(S * 0.36)

    # Triple concentric arcs below the circle, sweeping 35..145 degrees (PIL
    # angles run clockwise from 3 o'clock, so that is the underside).
    aw = max(1, round(kArcWidth * SS))
    for f in kArcRadii:
        rr = r * f
        d.arc([cx - rr, cy - rr, cx + rr, cy + rr], start=35, end=145, fill=255,
              width=aw)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)

    # The winged skull spans 1.80r — wing tip to wing tip across the disc's
    # widest line, leaving a 10% margin inside the 2r diameter. Its height then
    # follows from the art's own 2.68:1, so it is letterboxed, never stretched.
    skull_h = int(r * 1.55)
    sk = traced_skull(skull_h, max_w=int(r * 1.80))
    skull = Image.new("L", (S, S), 0)
    skull.paste(sk, (cx - sk.width // 2, cy - sk.height // 2 - int(r * 0.04)))
    return disc, skull


def to_bits(mask, w, h):
    """Box-downsample a supersampled mask to w*h and threshold to 0/1 rows."""
    small = mask.resize((w, h), Image.BOX)
    px = small.load()
    return [[1 if px[x, y] >= THRESHOLD else 0 for x in range(w)] for y in range(h)]


# ---------------------------------------------------------------------------
#  Background wash — mockup draw_earth_bg_strip() + draw_header_bar()
# ---------------------------------------------------------------------------

def header_bg(w, h):
    """The full background wash at `w`x`h`, composed at the mockup's own size."""
    W, H = MOCK_W, MOCK_H
    img = Image.new("RGB", (W, H), NAVY_DARK)

    # --- Earth photo, faded in left-to-right -------------------------------
    # Blow the globe up to 5x the strip height so only a band across its middle
    # shows, then take that band. The globe ends up narrower than the strip, so
    # the crop runs off its right edge into black — which is the point: the
    # planet's limb lands about 60% across, right where the gold shaft falls.
    earth = Image.open(os.path.join(ASSETS, "earth_nasa.jpg")).convert("RGB")
    target_h = H * 5
    scale = target_h / earth.height
    big = earth.resize((int(earth.width * scale), target_h), Image.LANCZOS)
    x0 = max(0, min(int(big.width * 0.55), big.width - W))
    y0 = (big.height - H) // 2
    strip = big.crop((x0, y0, x0 + W, y0 + H)).filter(ImageFilter.GaussianBlur(2))
    # 55% of the photo plus 45% of a flat navy — it reads as background art
    # rather than as a photograph someone left on screen.
    strip = strip.point(lambda v: int(v * 0.55 + 0.5))
    strip = Image.merge("RGB", [
        ch.point(lambda v, a=add: min(255, v + a))
        for ch, add in zip(strip.split(), (18, 24, 38))])  # (40,55,85) * 0.45

    mask = Image.new("L", (W, H), 0)
    md = ImageDraw.Draw(mask)
    for x in range(W):
        # Nothing until 22% across, full strength by 55%.
        t = min(1.0, max(0.0, (x - W * 0.22) / (W * 0.33)))
        md.line([(x, 0), (x, H)], fill=int(255 * t))
    img.paste(strip, (0, 0), mask)

    # --- diagonal gold light shaft -----------------------------------------
    sweep = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sx, sw = int(W * 0.62), int(W * 0.05)
    ImageDraw.Draw(sweep).polygon(
        [(sx, 0), (sx + sw, 0), (sx - int(H * 0.5) + sw, H), (sx - int(H * 0.5), H)],
        fill=GOLD + (90,))
    img = Image.alpha_composite(img.convert("RGBA"), sweep).convert("RGB")

    return img.resize((w, h), Image.LANCZOS)


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


# ---------------------------------------------------------------------------
#  Emit
# ---------------------------------------------------------------------------

def pack_bits(rows, w):
    """Rows of 0/1 -> TFT_eSPI::drawBitmap() order (MSB first, byte-padded rows)."""
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


def bitmap_block(name, w, h, rows):
    data = pack_bits(rows, w)
    stride = (w + 7) // 8
    lines = ["    " + " ".join(f"0x{b:02X}," for b in data[j * stride:(j + 1) * stride])
             for j in range(h)]
    return (f"constexpr int16_t {name}W = {w};\n"
            f"constexpr int16_t {name}H = {h};\n"
            f"static const uint8_t {name}[] PROGMEM = {{\n"
            + "\n".join(lines) + "\n};\n"), len(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", action="store_true",
                    help="also write /tmp/header_*.png for eyeballing")
    args = ap.parse_args()

    # --- badge ------------------------------------------------------------
    disc, skull = badge_masks(BADGE)
    disc_rows = to_bits(disc, BADGE, BADGE)
    skull_rows = to_bits(skull, BADGE, BADGE)
    badge_block, badge_bytes = bitmap_block("headerBadge", BADGE, BADGE, disc_rows)
    skull_block, skull_bytes = bitmap_block("headerSkull", BADGE, BADGE, skull_rows)

    # --- background -------------------------------------------------------
    bw, bh = HDR_W // BG_DIV, HDR_H // BG_DIV
    bg = header_bg(bw, bh)
    px = bg.load()
    words = [rgb565(*px[x, y]) for y in range(bh) for x in range(bw)]
    lines = ["    " + " ".join(f"0x{w:04X}," for w in words[i:i + 10])
             for i in range(0, len(words), 10)]
    bg_bytes = len(words) * 2
    bg_block = (f"constexpr int16_t headerBgW = {bw};\n"
                f"constexpr int16_t headerBgH = {bh};\n"
                f"static const uint16_t headerBg[] PROGMEM = {{\n"
                + "\n".join(lines) + "\n};\n")

    if args.preview:
        bg.resize((HDR_W, HDR_H), Image.BILINEAR).save("/tmp/header_bg.png")
        # The badge as the device draws it: the disc mask in white, then the
        # skull mask over it in the near-black, on the header's navy. Emitted
        # from the packed rows, so this is the artwork that ships, not a
        # prettier intermediate. Also dumped at 8x for eyeballing 1-bit detail.
        badge = Image.new("RGB", (BADGE, BADGE), NAVY_DARK)
        bpx = badge.load()
        for y in range(BADGE):
            for x in range(BADGE):
                if disc_rows[y][x]:
                    bpx[x, y] = WHITE
                if skull_rows[y][x]:
                    bpx[x, y] = SKULL
        badge.save("/tmp/header_badge.png")
        badge.resize((BADGE * 8, BADGE * 8), Image.NEAREST).save(
            "/tmp/header_badge_8x.png")
        print("wrote /tmp/header_bg.png, /tmp/header_badge.png, "
              "/tmp/header_badge_8x.png")

    total = badge_bytes + skull_bytes + bg_bytes
    header = f"""// ---------------------------------------------------------------------------
//  hud_header_art.h — artwork for the Major Order header bar.
//
//  AUTO-GENERATED by tools/gen_header_art.py. Do not edit by hand; change the
//  geometry in that script and re-run it.
//
//  headerBadge / headerSkull are TFT_eSPI::drawBitmap() 1-bit masks, drawn in
//  that order: the disc and its arcs in white, then the skull over them in a
//  near-black. They are separate masks rather than one because a 1-bit bitmap
//  only carries one colour.
//
//  headerBg is the background wash at half the header bar's linear size;
//  HUDRenderer::drawHeaderBg() bilinear-upscales it back to {HDR_W}x{HDR_H}. Storing
//  it at full resolution would cost {HDR_W * HDR_H * 2} bytes; this costs {bg_bytes}. The art
//  is a blurred, dimmed photograph, so there is nothing at full resolution for
//  the halving to throw away.
//
//  Sizes: badge {badge_bytes}B + skull {skull_bytes}B + background {bg_bytes}B = {total} bytes.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

namespace headerart {{

{badge_block}
{skull_block}
{bg_block}}}  // namespace headerart
"""
    with open(OUT_H, "w") as f:
        f.write(header)
    print(f"wrote {os.path.relpath(OUT_H, os.getcwd())} "
          f"(badge {badge_bytes}B, skull {skull_bytes}B, bg {bw}x{bh} {bg_bytes}B, "
          f"{total}B total)")


if __name__ == "__main__":
    main()
