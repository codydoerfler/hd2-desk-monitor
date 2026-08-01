"""Static layout check for the HUD.

Measures every string the firmware can draw against the box it is drawn into,
using the real TFT_eSPI glyph tables and the real icon dimensions. Run after
any change to src/config.h, src/hud_renderer.cpp or tools/gen_icons.py.
"""
import re

FD = ".pio/libdeps/hosyond-esp32-32e/TFT_eSPI/Fonts/GFXFF/"

ROW = re.compile(r"\s*\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,")


BOX = re.compile(r"\s*\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,"
                 r"\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)")


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


def load_boxes(name):
    """Return {codepoint: (height, yOffset)} -- the glyph's vertical extent."""
    out, grabbing, i = {}, False, 0
    for ln in open(FD + name + ".h"):
        if "Glyphs[]" in ln:
            grabbing = True
            continue
        if grabbing:
            if ln.strip().startswith("};"):
                break
            m = BOX.match(ln)
            if m:
                out[0x20 + i] = (int(m.group(3)), int(m.group(6)))
                i += 1
    return out


FONTS = {n: load(n) for n in ["FreeSans9pt7b", "FreeSansBold9pt7b",
                              "FreeSansBold12pt7b", "FreeSansBold18pt7b"]}
GLYPH_BOX = {n: load_boxes(n) for n in FONTS}
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

# --- constants, parsed straight out of src/config.h ---
#
# These used to be re-typed here by hand, "mirrored from src/config.h". They
# drifted: the checker was still validating titleY/targetY/barY/tileY long
# after the card was rebuilt around objY/idyY/ribY/sceneY, so it passed
# against a layout the firmware no longer had. Parsing the header means the
# constants cannot go stale.
def load_config(path="src/config.h"):
    txt = open(path).read()
    out = {}
    for m in re.finditer(r"constexpr int16_t\s+([^;]+);", txt):
        for decl in m.group(1).split(","):
            if "=" not in decl:
                continue
            name, _, val = decl.partition("=")
            name, val = name.strip(), val.strip()
            try:
                out[name] = int(val, 0)
            except ValueError:
                # An expression in terms of names already parsed.
                try:
                    out[name] = int(eval(val, {"__builtins__": {}}, out))
                except Exception:
                    pass
    return out


CFG = load_config()
globals().update(CFG)
print(f"parsed {len(CFG)} layout constants from src/config.h")
print()

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


def grid(title, bands, top, bottom):
    print(f"=== {title} (rows must not collide) ===")
    prev_name, prev_bottom = "frame top", top
    for name, y, h in bands:
        ok = y >= prev_bottom
        print(f"{'ok ' if ok else 'BAD'} {name:<10} y {y:>3}..{y + h - 1:>3}   "
              f"(after {prev_name} @{prev_bottom})")
        if not ok:
            fail.append(f"row {name} overlaps {prev_name}")
        prev_name, prev_bottom = name, y + h
    check(f"{prev_name} bottom inside the frame", prev_bottom, bottom)
    print()


# The Major Order card, which is what the display shows almost all the time.
grid("card grid", [("objective", objY, objH), ("identity", idyY, idyH),
                   ("ribbon", ribY, ribH), ("scene", sceneY, sceneH),
                   ("bars", barsY, 2 * cardBarH + barsGap),
                   ("verdict", verdictY, verdictH), ("cardRule", cardRuleY, 1),
                   ("cardFoot", cardFootY, cardFootH)],
     frameY + 1, frameY + frameH - 1)

# Every other screen keeps the plain "SUPER EARTH" strip.
grid("status-screen grid", [("header", headerY, headerH), ("rule1", rule1Y, 1)],
     frameY + 1, frameY + frameH - 1)

# The scene band stacks three rows inside itself, and they are positioned
# row-relative, so a collision there does not show up in the card grid above.
# This is what let the biome row (15px for a 22px font) clip "DESERT" and then,
# once widened, overlap the hazard chips.
print("=== scene band inner rows ===")
SCENE_ROWS = [("title", sceneTitleDy, sceneTitleH),
              ("biome", sceneBiomeDy, sceneBiomeH),
              ("chips", sceneChipDy, ICONS["hazTremor"][1])]
prev_name, prev_bottom = "band top", 0
for name, dy, h in SCENE_ROWS:
    ok = dy >= prev_bottom
    print(f"{'ok ' if ok else 'BAD'} {name:<10} dy {dy:>3}..{dy + h - 1:>3}   "
          f"(after {prev_name} @{prev_bottom})")
    if not ok:
        fail.append(f"scene row {name} overlaps {prev_name}")
    prev_name, prev_bottom = name, dy + h
check("scene rows inside the band", prev_bottom, sceneH)

# A text row shorter than the glyphs it actually draws clips them. The bound
# is per-row rather than the font's full line height, because a row that only
# ever sets capitals does not need to reserve descender space -- what matters
# is the span of the characters that row can really contain.
#
# This is the check that would have caught the biome row: 15px for glyphs
# spanning 17px, which cut the baseline row off "DESERT".
print("\n=== text rows vs the glyphs they draw ===")


def glyph_span(font, chars):
    """Total vertical extent, in px, of the tallest/deepest of `chars`."""
    rows = GLYPH_BOX[font]
    top = min(rows[ord(c)][1] for c in chars if ord(c) in rows)
    bot = max(rows[ord(c)][1] + rows[ord(c)][0] for c in chars if ord(c) in rows)
    return bot - top


CAPS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -,.'%/:"
for label, font, h, chars in [
        ("scene biome row", BODY, sceneBiomeH, CAPS),
        ("scene title row", LABEL, sceneTitleH, CAPS),
        ("identity sector row", BODY, idySectorH, CAPS),
        ("objective bar text", LABEL, objH - 2, CAPS),
        ("footer count row", BODY, cardFootH, "0123456789,")]:
    need = glyph_span(font, chars)
    ok = h >= need
    print(f"{'ok ' if ok else 'BAD'} {label:<52} {h:>4}px (needs {need})")
    if not ok:
        fail.append(label)
print()

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

blw, blh = ICONS["hd2LogoBoot"]
check("hd2LogoBoot width in the content column", blw, contentW)
check("hd2LogoBoot above the boot caption (y=48)", 48 + blh, 232)
check("boot status line inside the frame", 264 + 20, frameY + frameH)

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
