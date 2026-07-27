"""Off-target preview of the HD2 HUD.

Reimplements the TFT_eSPI drawing primitives used by src/hud_renderer.cpp
against the real GFXFF glyph bitmaps, so the layout can be eyeballed without
flashing hardware. Not part of the firmware build.
"""
import re
import sys
from PIL import Image, ImageDraw

FD = ".pio/libdeps/hosyond-esp32-32e/TFT_eSPI/Fonts/GFXFF/"


def load_font(name):
    txt = open(FD + name + ".h").read()
    bm = re.search(r"Bitmaps\[\]\s*PROGMEM\s*=\s*\{(.*?)\};", txt, re.S).group(1)
    bitmaps = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", bm)]
    gl = re.search(r"Glyphs\[\]\s*PROGMEM\s*=\s*\{(.*?)\};", txt, re.S).group(1)
    glyphs = []
    for ln in gl.splitlines():
        m = re.match(r"\s*\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,"
                     r"\s*(-?\d+)\s*,\s*(-?\d+)\s*\}", ln)
        if m:
            o, w, h, xa, xo, yo = (int(g) for g in m.groups())
            glyphs.append(dict(off=o, w=w, h=h, xa=xa, xo=xo, yo=yo))
    return {"bitmaps": bitmaps, "glyphs": {0x20 + i: g for i, g in enumerate(glyphs)}}


F = {n: load_font(n) for n in ["FreeSans9pt7b", "FreeSansBold9pt7b",
                               "FreeSansBold12pt7b", "FreeSansBold18pt7b"]}
BODY, LABEL, VALUE, DISPLAY = ("FreeSans9pt7b", "FreeSansBold9pt7b",
                               "FreeSansBold12pt7b", "FreeSansBold18pt7b")


def load_icons(path="src/hud_icons.h"):
    """Parse the generated 1-bit icons into {name: (w, h, rows-of-bits)}."""
    txt = open(path).read()
    dims = {}
    for name, wh, val in re.findall(r"constexpr int16_t (\w+)(W|H) = (\d+);", txt):
        dims.setdefault(name, {})[wh] = int(val)
    out = {}
    for name, blob in re.findall(
            r"static const uint8_t (\w+)\[\] PROGMEM = \{(.*?)\};", txt, re.S):
        by = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", blob)]
        w, h = dims[name]["W"], dims[name]["H"]
        stride = (w + 7) // 8
        out[name] = (w, h, [[1 if by[j * stride + i // 8] & (0x80 >> (i & 7)) else 0
                             for i in range(w)] for j in range(h)])
    return out


ICONS = load_icons()


def faction_icon(faction):
    """Mirrors factionIcon() in hud_renderer.cpp."""
    f = faction.lower()
    for prefix, name in (("automaton", "automaton"), ("terminid", "terminid"),
                         ("illuminate", "illuminate"), ("human", "emblemBadge")):
        if f.startswith(prefix):
            return ICONS[name]
    return None


def gly(f, ch):
    return F[f]["glyphs"].get(ord(ch), F[f]["glyphs"][0x20])


def text_width(f, s):
    return sum(gly(f, c)["xa"] for c in s)


def metrics(f, s):
    """(ascent, descent) the way TFT_eSPI derives them for a string."""
    asc = des = 0
    for c in s:
        g = gly(f, c)
        if g["h"] == 0:
            continue
        asc = max(asc, -g["yo"])
        des = max(des, g["h"] + g["yo"])
    return asc, des


HEX = lambda h: tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
BG, PANEL, TRACK = HEX("0A0C10"), HEX("12161C"), HEX("1E232B")
GOLD, GOLDDIM, GOLDMUTE = HEX("DFB24F"), HEX("8C6F32"), HEX("5A4822")
TEXT, GREY, BLUE, RED, GREEN = (HEX("E6E8EB"), HEX("6E7680"), HEX("4A8CC7"),
                                HEX("C63A2F"), HEX("5CA860"))

img = Image.new("RGB", (480, 320), BG)
px = img.load()
dr = ImageDraw.Draw(img)


def draw_string(s, x, y, f, color):
    """x,y = pen origin at the baseline (TFT_eSPI drawChar semantics)."""
    bits, cx = F[f]["bitmaps"], x
    for ch in s:
        g = gly(f, ch)
        bit = g["off"] * 8
        for gy in range(g["h"]):
            for gx in range(g["w"]):
                byte = bits[bit >> 3]
                if byte & (0x80 >> (bit & 7)):
                    X, Y = cx + g["xo"] + gx, y + g["yo"] + gy
                    if 0 <= X < 480 and 0 <= Y < 320:
                        px[X, Y] = color
                bit += 1
        cx += g["xa"]


def text_box(x, y, w, h, bg, f, fg, datum, s, inset=0):
    """Mirrors HUDRenderer::textBox()."""
    dr.rectangle([x, y, x + w - 1, y + h - 1], fill=bg)
    if not s:
        return
    cw = text_width(f, s)
    asc, des = metrics(f, s)
    ch = asc + des
    ax = {"TL": 0, "ML": 0, "TC": w // 2, "MC": w // 2, "TR": w, "MR": w}[datum]
    ay = {"TL": 0, "TC": 0, "TR": 0, "ML": h // 2, "MC": h // 2, "MR": h // 2}[datum]
    if datum in ("TL", "ML"):
        ax += inset
    if datum in ("TR", "MR"):
        ax -= inset
    if datum in ("TC", "MC"):
        ax -= cw // 2
    if datum in ("TR", "MR"):
        ax -= cw
    if datum in ("ML", "MC", "MR"):
        ay -= ch // 2
    draw_string(s, x + ax, y + ay + asc, f, fg)


# ---- layout constants, mirrored from src/config.h -------------------------
padX, contentR = 20, 460
contentW = contentR - padX
frameX = frameY = 8
frameW, frameH = 464, 304
bracketLen, bracketThick = 28, 3
headerY, headerH = 18, 15
rule1Y, titleY, titleH = 39, 45, 32
rule2Y, targetY, targetH = 84, 90, 34
barY, barH = 132, 36
tileY, tileH, tileGap = 180, 68, 7
rule3Y, footerY, footerH = 260, 285, 15
badgeH, badgePadX = 30, 9
headerIconGap, badgeIconGap, badgeMaxW = 6, 6, 175
targetIconGap, tagGap, badgeGap, footerIconGap = 8, 16, 12, 6
tileInsetX, tileIconDy, tileValueDy, tileValueH = 12, 9, 38, 24
kTileW = (contentW - 2 * tileGap) // 3
kHeaderTextInset = ICONS["emblem"][0] + headerIconGap
kTargetNameX = padX + ICONS["target"][0] + targetIconGap


def rect(x, y, w, h, c):
    dr.rectangle([x, y, x + w - 1, y + h - 1], outline=c)


def fill(x, y, w, h, c):
    dr.rectangle([x, y, x + w - 1, y + h - 1], fill=c)


def draw_bitmap(x, y, icon, c):
    """Mirrors TFT_eSPI::drawBitmap() — set bits painted, clear bits skipped."""
    w, h, rows = icon
    for j in range(h):
        for i in range(w):
            if rows[j][i] and 0 <= x + i < 480 and 0 <= y + j < 320:
                px[x + i, y + j] = c


def chrome():
    x0, y0 = frameX, frameY
    x1, y1 = frameX + frameW - 1, frameY + frameH - 1
    rect(x0, y0, frameW, frameH, GOLDDIM)
    L, T = bracketLen, bracketThick
    for fx, fy in ((x0, y0), (x1 - L + 1, y0), (x0, y1 - T + 1), (x1 - L + 1, y1 - T + 1)):
        fill(fx, fy, L, T, GOLD)
    for fx, fy in ((x0, y0), (x1 - T + 1, y0), (x0, y1 - L + 1), (x1 - T + 1, y1 - L + 1)):
        fill(fx, fy, T, L, GOLD)
    text_box(padX, headerY, 200, headerH, BG, LABEL, GOLD, "ML", "SUPER EARTH",
             kHeaderTextInset)
    em = ICONS["emblem"]
    draw_bitmap(padX, headerY + (headerH - em[1]) // 2, em, GOLD)
    dr.line([padX, rule1Y, padX + contentW - 1, rule1Y], fill=GOLDDIM)


def wifi(up):
    c = GREEN if up else RED
    dotR = 5
    text_box(contentR - 110, headerY, 110 - (2 * dotR + 6), headerH, BG, LABEL, c,
             "MR", "WIFI" if up else "OFFLINE")
    cx, cy = contentR - dotR, headerY + headerH // 2
    dr.ellipse([cx - dotR, cy - dotR, cx + dotR, cy + dotR], fill=c)


def tile_x(i):
    return padX + i * (kTileW + tileGap)


def tile(i, icon, value, vc=GOLD):
    """Mirrors HUDRenderer::drawTile() — icon where a label used to be."""
    x = tile_x(i)
    fill(x, tileY, kTileW, tileH, PANEL)
    rect(x, tileY, kTileW, tileH, GOLDDIM)
    draw_bitmap(x + tileInsetX, tileY + tileIconDy, ICONS[icon], GOLDDIM)
    vf = LABEL if text_width(VALUE, value) > kTileW - 2 * tileInsetX else VALUE
    text_box(x + 1, tileY + tileValueDy, kTileW - 2, tileValueH, PANEL, vf, vc, "ML",
             value, tileInsetX - 1)


def progress(pct):
    fill(padX, barY, contentW, barH, TRACK)
    fw = int((contentW - 2) * pct / 100.0 + 0.5)
    if fw > 0:
        fill(padX + 1, barY + 1, fw, barH - 2, BLUE)
    rect(padX, barY, contentW, barH, GOLDDIM)
    s = f"{int(pct + 0.5)}% LIBERATED"
    cw = text_width(DISPLAY, s)
    asc, des = metrics(DISPLAY, s)
    draw_string(s, padX + contentW // 2 - cw // 2,
                barY + barH // 2 - (asc + des) // 2 + asc, DISPLAY, TEXT)


def footer(left, left_color, reward=None):
    """Mirrors HUDRenderer::drawFooter(): local sync time on the left, a medal
    icon and the bare reward number on the right."""
    text_box(padX, footerY, contentW // 2, footerH, BG, BODY, left_color, "ML", left)
    if reward is None:
        return
    mw, mh, _ = ICONS["medal"]
    text_box(contentR - contentW // 2, footerY, contentW // 2, footerH, BG, BODY,
             GOLD, "MR", reward)
    draw_bitmap(contentR - (mw + footerIconGap + text_width(BODY, reward)),
                footerY + (footerH - mh) // 2, ICONS["medal"], GOLD)


def badge_width(faction):
    w = text_width(LABEL, faction.upper()) + 2 * badgePadX
    ic = faction_icon(faction)
    if ic:
        w += ic[0] + badgeIconGap
    return min(w, badgeMaxW)


def badge(faction):
    lab = faction.upper()
    c = GREEN if faction.lower() == "humans" else RED
    ic = faction_icon(faction)
    w = badge_width(faction)
    x = contentR - w
    y = targetY + (targetH - badgeH) // 2
    fill(x, y, w, badgeH, BG)
    rect(x, y, w, badgeH, c)
    if ic:
        text_box(x + 1, y + 1, w - 2, badgeH - 2, BG, LABEL, c, "ML", lab,
                 badgePadX + ic[0] + badgeIconGap - 1)
        draw_bitmap(x + badgePadX, y + (badgeH - ic[1]) // 2, ic, c)
    else:
        text_box(x + 1, y + 1, w - 2, badgeH - 2, BG, LABEL, c, "MC", lab)


# ---- live data captured from the API on 2026-07-26 ------------------------
order_title = "MAJOR ORDER"
planet_name, sector, owner = "X-45", "Ymir", "Automaton"
liberation, players, reward = 79.0, "33.4K", "40"
time_left, kills, success, online = "18h 22m", "393.6B", 91, "51.0K"

# The footer clock is UTC + the configured offset (see kUtcOffsetMinutesDefault
# in src/config.h and the "UTC offset" field in the WiFi setup portal). The
# sample here is a 14:32 UTC sync shown on a device set to UTC-6.
sync_utc_hh, sync_utc_mm, sample_offset_min = 14, 32, -6 * 60
_local = (sync_utc_hh * 60 + sync_utc_mm + sample_offset_min) % (24 * 60)
sync_local = f"{_local // 60:02d}:{_local % 60:02d}"

# Mode: "order" (default), "idle" (no active Major Order), "stale" (offline).
mode = sys.argv[1] if len(sys.argv) > 1 else "order"
# Optional 2nd arg overrides the badge faction, to eyeball each faction icon:
#   python3 tools/preview_hud.py order Terminids
if len(sys.argv) > 2:
    owner = sys.argv[2]

chrome()
wifi(mode != "stale")

if mode == "idle":
    em = ICONS["emblemLarge"]
    draw_bitmap(padX + (contentW - em[0]) // 2, 92, em, GOLDDIM)
    text_box(padX, 146, contentW, 28, BG, VALUE, GOLD, "MC", "NO ACTIVE MAJOR ORDER")
    tile(0, "divers", online)
    tile(1, "skull", kills)
    tile(2, "check", f"{success}%")
    dr.line([padX, rule3Y, padX + contentW - 1, rule3Y], fill=GOLDDIM)
    footer(f"SYNCED {sync_local}", GREY)
    img.resize((960, 640), Image.NEAREST).save(f"preview_{mode}.png")
    print(f"wrote preview_{mode}.png")
    raise SystemExit

text_box(padX, titleY, contentW, titleH, BG, DISPLAY, GOLD, "ML", order_title)
dr.line([padX, rule2Y, padX + contentW - 1, rule2Y], fill=GOLDDIM)

tg = ICONS["target"]
draw_bitmap(padX, targetY + (targetH - tg[1]) // 2, tg, GOLD)
badge(owner)
rowR = contentR - badge_width(owner) - badgeGap
availW = rowR - kTargetNameX
nf = DISPLAY if text_width(DISPLAY, planet_name) <= availW else VALUE
nameW = text_width(nf, planet_name)
text_box(kTargetNameX, targetY, availW, targetH, BG, nf, TEXT, "ML", planet_name)
tag = sector.upper()
tagW = text_width(BODY, tag)
if nameW + tagGap + tagW <= availW:
    text_box(rowR - tagW, targetY, tagW, targetH, BG, BODY, GREY, "MR", tag)
progress(liberation)

tile(0, "clock", time_left)
tile(1, "divers", players)
tile(2, "skull", kills)

dr.line([padX, rule3Y, padX + contentW - 1, rule3Y], fill=GOLDDIM)
if mode == "stale":
    footer(f"STALE - LAST {sync_local}", GOLDMUTE, reward)
else:
    footer(f"SYNCED {sync_local}", GREY, reward)

if len(sys.argv) > 2:
    out = f"preview_{owner.lower()}.png"
else:
    out = "preview.png" if mode == "order" else f"preview_{mode}.png"
img.resize((960, 640), Image.NEAREST).save(out)
print(f"wrote {out}")
