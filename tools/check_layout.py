"""Static layout check for the HUD.

Measures every string the firmware can draw against the box it is drawn into,
using the real TFT_eSPI glyph tables and the real icon dimensions. Run after
any change to src/config.h, src/hud_renderer.cpp or tools/gen_icons.py.
"""
import re

FD = ".pio/libdeps/hosyond-esp32-32e/TFT_eSPI/Fonts/GFXFF/"

ROW = re.compile(r"\s*\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,")


def load(name):
    """Return {codepoint: xAdvance} for a TFT_eSPI GFXFF font."""
    adv, grabbing = [], False
    for ln in open(FD + name + ".h"):
        if "Glyphs[]" in ln:
            grabbing = True
            continue
        if grabbing:
            if ln.strip().startswith("};"):
                break
            m = ROW.match(ln)
            if m:
                adv.append(int(m.group(4)))  # 4th field is xAdvance
    # First glyph in these tables is 0x20 (space).
    return {0x20 + i: a for i, a in enumerate(adv)}


FONTS = {n: load(n) for n in ["FreeSans9pt7b", "FreeSansBold9pt7b",
                              "FreeSansBold12pt7b", "FreeSansBold18pt7b"]}
# Mirrors src/hud_fonts.h.
BODY, LABEL, VALUE, DISPLAY = ("FreeSans9pt7b", "FreeSansBold9pt7b",
                               "FreeSansBold12pt7b", "FreeSansBold18pt7b")

for n, g in FONTS.items():
    print(f"loaded {n}: {len(g)} glyphs")
print()


def w(font, s):
    g = FONTS[font]
    return sum(g.get(ord(c), g[0x20]) for c in s)


def icon_dims(path="src/hud_icons.h"):
    """{icon name: (w, h)} from the generated bitmap header."""
    txt = open(path).read()
    d = {}
    for name, wh, val in re.findall(r"constexpr int16_t (\w+)(W|H) = (\d+);", txt):
        d.setdefault(name, {})[wh] = int(val)
    return {n: (v["W"], v["H"]) for n, v in d.items()}


ICONS = icon_dims()
print("icons: " + ", ".join(f"{n} {w}x{h}" for n, (w, h) in sorted(ICONS.items())))
print()

# --- constants mirrored from src/config.h ---
padX, contentR = 20, 460
contentW = contentR - padX            # 440
frameY, frameH = 8, 304

headerY, headerH = 18, 15
rule1Y = 39
titleY, titleH = 45, 32
rule2Y = 84
targetY, targetH = 90, 34
barY, barH = 132, 36
tileY, tileH, tileGap = 180, 68, 7
rule3Y = 260
footerY, footerH = 285, 15

tileInsetX, tileIconDy = 12, 9
tileValueDy, tileValueH = 38, 24
badgeH, badgePadX = 30, 9
badgeIconGap, targetIconGap = 6, 8
tagGap, badgeGap, footerIconGap = 16, 12, 6
badgeMaxW = 175
statY, statH, statGap = 160, 46, 8

# --- derived, mirrored from src/hud_renderer.cpp ---
kTileW = (contentW - 2 * tileGap) // 3   # 142
kStatW = (contentW - statGap) // 2       # 216
kStatInsetX = 12
kTargetNameX = padX + ICONS["target"][0] + targetIconGap

# Mirrors factionIcon() in hud_renderer.cpp. "Humans" is absent there too: the
# SEAF badge is text-only, like an owner with no icon.
FACTION_ICON = {"automaton": "automaton", "terminid": "terminid",
                "illuminate": "illuminate"}


def display_name(faction):
    """Mirrors factionDisplayName() — the badge draws the API's "Humans" as
    "SEAF", so that is the string whose width has to fit."""
    return "SEAF" if faction.lower().startswith("human") else faction


def badge_w(faction):
    bw = w(LABEL, display_name(faction).upper()) + 2 * badgePadX
    for prefix, icon in FACTION_ICON.items():
        if faction.lower().startswith(prefix):
            bw += ICONS[icon][0] + badgeIconGap
            break
    return min(bw, badgeMaxW)


fail = []


def check(label, got, limit):
    ok = got <= limit
    print(f"{'ok ' if ok else 'BAD'} {label:<52} {got:>4}px (limit {limit})")
    if not ok:
        fail.append(label)


print("=== vertical grid (rows must not collide) ===")
BANDS = [("header", headerY, headerH), ("rule1", rule1Y, 1),
         ("title", titleY, titleH), ("rule2", rule2Y, 1),
         ("target", targetY, targetH), ("bar", barY, barH),
         ("tiles", tileY, tileH), ("rule3", rule3Y, 1),
         ("footer", footerY, footerH)]
prev_name, prev_bottom = "frame top", frameY + 1
for name, y, h in BANDS:
    ok = y >= prev_bottom
    print(f"{'ok ' if ok else 'BAD'} {name:<10} y {y:>3}..{y + h - 1:>3}   "
          f"(after {prev_name} @{prev_bottom})")
    if not ok:
        fail.append(f"row {name} overlaps {prev_name}")
    prev_name, prev_bottom = name, y + h
check("footer bottom inside the frame", prev_bottom, frameY + frameH - 1)

print("\n=== title row ===")
for t in ["MAJOR ORDER", "LIBERATE MERIDIA", "DEFEND THE CREEK"]:
    check(f'"{t}" @18pt', w(DISPLAY, t), contentW)

print("\n=== target row: crosshair + planet name + sector tag + badge ===")
print(f"    crosshair {ICONS['target'][0]}x{ICONS['target'][1]} at x={padX}, "
      f"name starts at x={kTargetNameX}")
check("crosshair height in the target row", ICONS["target"][1], targetH)
NAMES = ["X-45", "Meridia", "Hellmire", "Charbal-VII", "Vernen Wells",
         "Angel's Venture", "Heeth", "Mastia"]
SECTORS = ["Ymir", "Andromeda", "Sagittarius"]
for faction in ["Automaton", "Terminids", "Illuminate", "Humans", "Unknown"]:
    bw = badge_w(faction)
    if bw >= badgeMaxW:
        fail.append(f"badge {faction} hits the {badgeMaxW}px cap")
    availW = (contentR - bw - badgeGap) - kTargetNameX
    print(f"  badge {display_name(faction).upper():<11} {bw:>3}px "
          f"-> {availW:>3}px for the name")
    for pn in NAMES:
        g18 = w(DISPLAY, pn)
        used, got = ("18pt", g18) if g18 <= availW else ("12pt", w(VALUE, pn))
        flag = "" if got <= availW else "  <-- OVERFLOW"
        if got > availW:
            fail.append(f"name '{pn}' with {faction}")
        # The sector tag is drawn only when it fits, so it can never overflow;
        # report which combinations keep it.
        tags = [s for s in SECTORS
                if got + tagGap + w(BODY, s.upper()) <= availW]
        print(f"      {pn:<16} {used} {got:>3}px{flag}"
              f"   sector tag kept for {len(tags)}/{len(SECTORS)}")

print("\n=== no-planet fallbacks (target row, full width) ===")
for s in ["PLANET DATA UNAVAILABLE", "GALAXY-WIDE OBJECTIVE"]:
    check(f'"{s}" @12pt', w(VALUE, s), contentR - kTargetNameX)

print("\n=== progress bar label (now at display size) ===")
for pct in [0, 51, 79, 100]:
    check(f'"{pct}% LIBERATED" @18pt', w(DISPLAY, f"{pct}% LIBERATED"), contentW - 2)
check('"AWAITING TELEMETRY" @18pt', w(DISPLAY, "AWAITING TELEMETRY"), contentW - 2)

print("\n=== icon tiles ===")
inner = kTileW - 2 * tileInsetX
print(f"    tile {kTileW}x{tileH} at x=" +
      ", ".join(str(padX + i * (kTileW + tileGap)) for i in range(3)) +
      f", inner width {inner}px")
check("rightmost tile ends inside the content column",
      padX + 2 * (kTileW + tileGap) + kTileW, contentR)
for n in ("clock", "divers", "skull", "check"):
    iw, ih = ICONS[n]
    check(f'tile icon "{n}" width', tileInsetX + iw, kTileW - tileInsetX)
    check(f'tile icon "{n}" height above the value row', tileIconDy + ih, tileValueDy)
check("tile value row inside the tile", tileValueDy + tileValueH, tileH)
# Values are drawn at 12pt and drop to 9pt bold if they don't fit; 9pt is the
# floor, so that is what must fit.
for val in ["18h 22m", "2d 04h", "45m 12s", "EXPIRED", "--", "33.4K", "393.6B",
            "125.4K", "91%", "100%"]:
    g12 = w(VALUE, val)
    used, got = ("12pt", g12) if g12 <= inner else ("9pt", w(LABEL, val))
    check(f'tile value "{val}" @{used}', got, inner)

print("\n=== WiFi setup screen (two-up stat boxes) ===")
sinner = kStatW - 2 * kStatInsetX
for lbl in ["NETWORK", "PASSWORD"]:
    check(f'label "{lbl}" @9pt bold', w(LABEL, lbl), sinner)
for val in ["HD2-Monitor", "helldive"]:
    check(f'value "{val}" @12pt', w(VALUE, val), sinner)

print("\n=== footer (local clock time, no unit suffix) ===")
for s in ["SYNCED 14:32", "SYNCED 08:32", "STALE - LAST 14:32", "NO DATA YET"]:
    check(f'left "{s}"', w(BODY, s), contentW // 2)
for r in ["40", "150", "1000"]:
    check(f'right medal + "{r}"',
          ICONS["medal"][0] + footerIconGap + w(BODY, r), contentW // 2)
check("medal height in the footer row", ICONS["medal"][1], footerH)

print("\n=== centred status lines ===")
for s in ["ESTABLISHING UPLINK", "SUPER EARTH", "WIFI SETUP"]:
    check(f'"{s}" @18pt', w(DISPLAY, s), contentW)
check('"NO ACTIVE MAJOR ORDER" @12pt', w(VALUE, "NO ACTIVE MAJOR ORDER"), contentW)
check('"MAJOR ORDER MONITOR" @9pt bold', w(LABEL, "MAJOR ORDER MONITOR"), contentW)
for s in ["Connecting to WiFi...",
          "Synchronising clock...",
          "Contacting High Command...",
          "Setup timed out. Restarting...",
          "Join this network from a phone or laptop,",
          "then set WiFi and UTC offset in the page that opens.",
          "If no page opens, browse to 192.168.4.1"]:
    check(f'"{s[:44]}"', w(BODY, s), contentW)

print("\n=== centrepiece emblem ===")
elw, elh = ICONS["emblemLarge"]
check("emblemLarge width in the content column", elw, contentW)
check("emblemLarge above the idle caption (y=92)", 92 + elh, 146)
check("emblemLarge above the uplink caption (y=96)", 96 + elh, 150)
check("emblemLarge above the boot caption (y=110)", 110 + elh, 164)

print("\n=== header row (label + wifi block) ===")
se = w(LABEL, "SUPER EARTH")
check('"SUPER EARTH" @9pt bold in its box', se, 200)
check("label vs the WiFi block", padX + se, contentR - 110)
check('"OFFLINE" @9pt bold in wifi box', w(LABEL, "OFFLINE"), 110 - (2 * 5 + 6))

print("\n=== faction badge ===")
for n in sorted(set(FACTION_ICON.values())):
    check(f'badge icon "{n}" height', ICONS[n][1], badgeH - 2)
check("badge height in the target row", badgeH, targetH)

print("\n" + ("ALL LAYOUT CHECKS PASSED" if not fail else "FAILURES:\n  " + "\n  ".join(fail)))
