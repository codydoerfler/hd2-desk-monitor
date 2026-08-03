"""Generate src/hud_header_art.h — the Major Order header bar's artwork.

One asset comes out of here:

  * headerBg — the Earth/gold-sweep background wash, RGB565 at half the
               header's pixel size. The renderer bilinear-upscales it (see
               HUDRenderer::drawHeaderBg), which is what keeps it to ~12KB
               of flash instead of the ~51KB a full-resolution strip would
               cost. The source is a soft, blurred, heavily dimmed photo, so
               there is no detail for the halving to lose.

    python3 tools/gen_header_art.py            # rewrite src/hud_header_art.h
    python3 tools/gen_header_art.py --preview  # also dump PNGs to /tmp

This is a transcription of tools/assets/header_mockup_reference.py's
draw_earth_bg_strip() / draw_header_bar(), which is the approved visual spec,
with one deliberate departure forced by resolution: the background is
composed at the mockup's own 1440x174 and then resized down, rather than
composed at device size. The mockup's geometry is partly a function of the
strip's aspect ratio — the globe's width is 5x the bar height — so composing
at 440x58 would have moved the planet's limb out from under the gold shaft
that is meant to graze it.

Asset provenance: tools/assets/earth_nasa.jpg is NASA's Blue Marble, public
domain.

This script used to also emit headerBadge/headerSkull (a disc-and-arcs badge
with a traced winged skull inside it), the mockup's title-bar mark. The title
bar was redesigned to just wear the headerBg wash instead (0b1b5ef), and
nothing has drawn the badge or skull since. headerSkull was also rendering
letterboxed — the traced skull only filled about a quarter of its 47x47 box
— so there was nothing worth keeping. Removed rather than left dead; see git
history before this point if that badge design needs reviving.
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

# --- the mockup's own render size, which its proportions are tuned to ------
MOCK_W, MOCK_H = 1440, 174

# --- palette, from the mockup ---------------------------------------------
NAVY_DARK = (14, 19, 30)
GOLD = (223, 178, 79)


# ---------------------------------------------------------------------------
#  Background wash — mockup draw_earth_bg_strip() + draw_header_bar()
# ---------------------------------------------------------------------------

def header_bg(w, h):
    """The full background wash at `w`x`h`, composed at the mockup's own size."""
    W, H = MOCK_W, MOCK_H
    img = Image.new("RGB", (W, H), NAVY_DARK)

    # --- Earth photo, faded in left-to-right -------------------------------
    # Blow the globe up to 5x the strip height so only a band across its middle
    # shows, then take that band.
    #
    # Retuned for the objective bar (0b1b5ef), which is where this wash now
    # lives; it was cut for the old full-width title bar. That bar's left end
    # carries the DEFENSE/LIBERATION word and its right end the gold expiry
    # flag, so the artwork only reads in the middle. The crop and the fade
    # below are pulled left so the globe's limb lands in that open span
    # instead of under the flag.
    earth = Image.open(os.path.join(ASSETS, "earth_nasa.jpg")).convert("RGB")
    target_h = H * 5
    scale = target_h / earth.height
    big = earth.resize((int(earth.width * scale), target_h), Image.LANCZOS)
    x0 = max(0, min(int(big.width * 0.42), big.width - W))
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
        # Nothing until 12% across (behind the status word), full strength by
        # 40%, then held — the flag covers the right end anyway.
        t = min(1.0, max(0.0, (x - W * 0.12) / (W * 0.28)))
        md.line([(x, 0), (x, H)], fill=int(255 * t))
    img.paste(strip, (0, 0), mask)

    # --- diagonal gold light shaft -----------------------------------------
    sweep = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    # Shaft moved in from 62% to 46%: at 62% it fell under the expiry flag and
    # was never seen.
    sx, sw = int(W * 0.46), int(W * 0.05)
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

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", action="store_true",
                    help="also write /tmp/header_*.png for eyeballing")
    args = ap.parse_args()

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
        print("wrote /tmp/header_bg.png")

    total = bg_bytes
    header = f"""// ---------------------------------------------------------------------------
//  hud_header_art.h — artwork for the Major Order header bar.
//
//  AUTO-GENERATED by tools/gen_header_art.py. Do not edit by hand; change the
//  geometry in that script and re-run it.
//
//  headerBg is the background wash at half the header bar's linear size;
//  HUDRenderer::drawHeaderBg() bilinear-upscales it back to {HDR_W}x{HDR_H}. Storing
//  it at full resolution would cost {HDR_W * HDR_H * 2} bytes; this costs {bg_bytes}. The art
//  is a blurred, dimmed photograph, so there is nothing at full resolution for
//  the halving to throw away.
//
//  Sizes: background {bg_bytes}B = {total} bytes.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

namespace headerart {{

{bg_block}}}  // namespace headerart
"""
    with open(OUT_H, "w") as f:
        f.write(header)
    print(f"wrote {os.path.relpath(OUT_H, os.getcwd())} "
          f"(bg {bw}x{bh} {bg_bytes}B, {total}B total)")


if __name__ == "__main__":
    main()
