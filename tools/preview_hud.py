"""Off-target preview of the HD2 HUD.

Reimplements the TFT_eSPI drawing primitives used by src/hud_renderer.cpp
against the real GFXFF glyph bitmaps, so the layout can be eyeballed without
flashing hardware. Not part of the firmware build.

Everything it draws comes from the same generated headers the firmware links:
the icons and the header bar's skull badge from hud_icons.h / hud_header_art.h,
the Anton title face from hud_font_anton.h, and the header wash from the same
half-size RGB565 array, upscaled with the identical fixed-point bilinear. So a
change to a generator shows up here without being hand-mirrored.
"""
import re
import sys
from PIL import Image, ImageDraw

FD = ".pio/libdeps/hosyond-esp32-32e/TFT_eSPI/Fonts/GFXFF/"
ANTON_H = "src/hud_font_anton.h"


def load_font(name, path=None):
    """Parse one GFXfont out of a header. Works for TFT_eSPI's own Free Fonts
    and for the Anton faces generated into src/hud_font_anton.h, which put two
    fonts in a single file — hence matching on the name prefix."""
    txt = open(path or (FD + name + ".h")).read()
    bm = re.search(name + r"Bitmaps\[\]\s*PROGMEM\s*=\s*\{(.*?)\};", txt, re.S).group(1)
    bitmaps = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", bm)]
    gl = re.search(name + r"Glyphs\[\]\s*PROGMEM\s*=\s*\{(.*?)\};", txt, re.S).group(1)
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
F.update({n: load_font(n, ANTON_H) for n in ["Anton24px", "Anton16px"]})
BODY, LABEL, VALUE, DISPLAY = ("FreeSans9pt7b", "FreeSansBold9pt7b",
                               "FreeSansBold12pt7b", "FreeSansBold18pt7b")
HEADLINE, HEADLINE_SM = "Anton24px", "Anton16px"


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


def load_header_bg(path="src/hud_header_art.h"):
    """The half-size RGB565 header wash from src/hud_header_art.h."""
    txt = open(path).read()
    w = int(re.search(r"headerBgW = (\d+);", txt).group(1))
    h = int(re.search(r"headerBgH = (\d+);", txt).group(1))
    blob = re.search(r"headerBg\[\] PROGMEM = \{(.*?)\};", txt, re.S).group(1)
    words = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{4})", blob)]
    return w, h, words


ICONS = load_icons()
ART = load_icons("src/hud_header_art.h")
BG_W, BG_H, BG_PX = load_header_bg()


def faction_icon(faction):
    """Mirrors factionIcon() in hud_renderer.cpp — "Humans" is absent there
    too, so the SEAF badge draws text-only like an owner with no icon."""
    f = faction.lower()
    for prefix, name in (("automaton", "automaton"), ("terminid", "terminid"),
                         ("illuminate", "illuminate")):
        if f.startswith(prefix):
            return ICONS[name]
    return None


def faction_display_name(faction):
    """Mirrors factionDisplayName() — the API's "Humans" is drawn as "SEAF"."""
    return "SEAF" if faction.lower().startswith("human") else faction


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


def font_ab(f):
    """TFT_eSPI's glyph_ab — the font-wide maximum ascent, which is what a bare
    drawString() places the baseline from (textBox()/textWidth work off the
    per-string ascent instead). The slice reproduces TFT_eSPI's own off-by-one
    in setFreeFont(), which walks last-first glyphs and so skips the last one."""
    gs = F[f]["glyphs"]
    return max(-gs[k]["yo"] for k in sorted(gs)[:-1])


HEX = lambda h: tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
BG, PANEL, TRACK = HEX("0A0C10"), HEX("12161C"), HEX("1E232B")
GOLD, GOLDDIM, GOLDMUTE = HEX("DFB24F"), HEX("8C6F32"), HEX("5A4822")
TEXT, GREY, BLUE, RED, GREEN = (HEX("E6E8EB"), HEX("6E7680"), HEX("4A8CC7"),
                                HEX("C63A2F"), HEX("5CA860"))
HDR_EDGE, HDR_SKULL = HEX("32362C"), HEX("14161C")

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


def draw_string_datum(s, x, y, f, color, datum):
    """Mirrors a bare TFT_eSPI drawString() — no background fill, so whatever is
    already on screen shows through the glyphs' clear pixels."""
    ab = font_ab(f)
    if datum in ("TC", "MC"):
        x -= text_width(f, s) // 2
    if datum in ("TR", "MR"):
        x -= text_width(f, s)
    y += ab
    if datum in ("ML", "MC", "MR"):
        y -= ab // 2
    draw_string(s, x, y, f, color)


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
rule1Y = 39
rule2Y, targetY, targetH = 84, 90, 34
hdrX, hdrY, hdrW, hdrH = padX, 18, contentW, 58
hdrBadgeX, hdrTitleGap, hdrWifiW, hdrWifiPad = 6, 10, 100, 10
barY, barH = 132, 36
tileY, tileH, tileGap = 180, 68, 7
rule3Y, footerY, footerH = 260, 285, 15
badgeH, badgePadX = 30, 9
badgeIconGap, badgeMaxW = 6, 175
targetIconGap, tagGap, badgeGap, footerIconGap = 8, 16, 12, 6
tileInsetX, tileIconDy, tileValueDy, tileValueH = 12, 9, 38, 24
kTileW = (contentW - 2 * tileGap) // 3
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


def frame():
    """Mirrors HUDRenderer::drawFrame()."""
    x0, y0 = frameX, frameY
    x1, y1 = frameX + frameW - 1, frameY + frameH - 1
    rect(x0, y0, frameW, frameH, GOLDDIM)
    L, T = bracketLen, bracketThick
    for fx, fy in ((x0, y0), (x1 - L + 1, y0), (x0, y1 - T + 1), (x1 - L + 1, y1 - T + 1)):
        fill(fx, fy, L, T, GOLD)
    for fx, fy in ((x0, y0), (x1 - T + 1, y0), (x0, y1 - L + 1), (x1 - T + 1, y1 - L + 1)):
        fill(fx, fy, T, L, GOLD)


def status_header():
    """Mirrors HUDRenderer::drawStatusHeader() — the plain "SUPER EARTH" strip
    every screen that is not showing a Major Order still uses. Text only: the
    emblem is illegible at header height, so it appears only at centrepiece
    size lower down."""
    text_box(padX, headerY, 200, headerH, BG, LABEL, GOLD, "ML", "SUPER EARTH")
    dr.line([padX, rule1Y, padX + contentW - 1, rule1Y], fill=GOLDDIM)


def src_coord(d, src_len, dst_len):
    """Mirrors srcCoord() in hud_renderer.cpp: 24.8 fixed point, half-pixel
    centred, clamped at the near edge."""
    return max(0, ((2 * d + 1) * src_len * 256) // (2 * dst_len) - 128)


def lerp565(a, b, t):
    """Mirrors lerp565() — interpolation in packed RGB565, channel by channel,
    so the preview inherits the same quantisation the device shows."""
    out = 0
    for shift, mask in ((11, 0x1F), (5, 0x3F), (0, 0x1F)):
        ca, cb = (a >> shift) & mask, (b >> shift) & mask
        out |= (ca + (((cb - ca) * t) >> 8)) << shift
    return out


def header_bg(x, y, w, h):
    """Mirrors HUDRenderer::drawHeaderBg() — bilinear upscale of the half-size
    wash in hud_header_art.h onto a sub-rect of the bar."""
    for j in range(h):
        fy = src_coord(y - hdrY + j, BG_H, hdrH)
        y0 = fy >> 8
        y1 = min(y0 + 1, BG_H - 1)
        ty = fy & 0xFF
        for i in range(w):
            fx = src_coord(x - hdrX + i, BG_W, hdrW)
            x0 = fx >> 8
            x1 = min(x0 + 1, BG_W - 1)
            tx = fx & 0xFF
            top = lerp565(BG_PX[y0 * BG_W + x0], BG_PX[y0 * BG_W + x1], tx)
            bot = lerp565(BG_PX[y1 * BG_W + x0], BG_PX[y1 * BG_W + x1], tx)
            c = lerp565(top, bot, ty)
            px[x + i, y + j] = (((c >> 11) & 0x1F) << 3, ((c >> 5) & 0x3F) << 2,
                                (c & 0x1F) << 3)


def header_bar(title):
    """Mirrors HUDRenderer::drawHeaderBar()."""
    header_bg(hdrX, hdrY, hdrW, hdrH)
    rect(hdrX, hdrY, hdrW, hdrH, HDR_EDGE)

    badge_ic, skull_ic = ART["headerBadge"], ART["headerSkull"]
    bx = hdrX + hdrBadgeX
    by = hdrY + (hdrH - badge_ic[1]) // 2
    draw_bitmap(bx, by, badge_ic, TEXT)
    draw_bitmap(bx, by, skull_ic, HDR_SKULL)

    x = bx + badge_ic[0] + hdrTitleGap
    availW = (hdrX + hdrW - hdrWifiW) - hdrTitleGap - x
    f = HEADLINE if text_width(HEADLINE, title) <= availW else HEADLINE_SM
    s = title
    while len(s) > 1 and text_width(f, s) > availW:
        s = s[:-1]
    draw_string_datum(s, x, hdrY + hdrH // 2, f, TEXT, "ML")


def wifi(up, in_bar):
    """Mirrors HUDRenderer::drawWifi() — inside the header bar when there is
    one, on the "SUPER EARTH" strip when there is not."""
    c = GREEN if up else RED
    dotR = 5
    label = "WIFI" if up else "OFFLINE"

    if in_bar:
        cy = hdrY + hdrH // 2
        cx = hdrX + hdrW - hdrWifiPad - dotR
        header_bg(hdrX + hdrW - hdrWifiW, hdrY + 1, hdrWifiW - 1, hdrH - 2)
        draw_string_datum(label, cx - dotR - 6, cy, LABEL, c, "MR")
    else:
        cy = headerY + headerH // 2
        cx = contentR - dotR
        text_box(contentR - 110, headerY, 110 - (2 * dotR + 6), headerH, BG, LABEL,
                 c, "MR", label)
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
    w = text_width(LABEL, faction_display_name(faction).upper()) + 2 * badgePadX
    ic = faction_icon(faction)
    if ic:
        w += ic[0] + badgeIconGap
    return min(w, badgeMaxW)


def badge(faction):
    lab = faction_display_name(faction).upper()
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

# Mode: "order" (default), "idle" (no active Major Order), "stale" (offline),
# "boot" (the startup screen, before there is any data to show).
mode = sys.argv[1] if len(sys.argv) > 1 else "order"
# Optional 2nd arg overrides the badge faction, to eyeball each faction icon:
#   python3 tools/preview_hud.py order Terminids
if len(sys.argv) > 2:
    owner = sys.argv[2]

frame()

if mode == "boot":
    # Mirrors HUDRenderer::showBoot(). No WiFi indicator: the boot screen is
    # drawn before the link state means anything.
    status_header()
    em = ICONS["hd2LogoBoot"]
    draw_bitmap(padX + (contentW - em[0]) // 2, 110, em, GOLD)
    text_box(padX, 164, contentW, 22, BG, VALUE, TEXT, "MC", "MAJOR ORDER MONITOR")
    text_box(padX, 200, contentW, 20, BG, BODY, GREY, "MC",
             "Contacting High Command...")
    img.resize((960, 640), Image.NEAREST).save("preview_boot.png")
    print("wrote preview_boot.png")
    raise SystemExit

# A live Major Order gets the full header bar; everything else keeps the strip.
if mode == "idle":
    status_header()
else:
    header_bar(order_title.upper())
wifi(mode != "stale", mode != "idle")

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

# The order title now lives in the header bar, so the body starts at the rule.
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
