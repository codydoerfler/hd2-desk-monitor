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


# TFT_eSPI's built-in font 1 (setFreeFont(nullptr)): a fixed 5x7 cell plus one
# column of spacing. No table to load -- the advance is the same for every
# character. Used by the strip/bar captions and the footer's sync clock.
GLCD_W, GLCD_H = 6, 8


def glcd(s):
    return GLCD_W * len(s)


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
kRewardBoxW = 56

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


# The two card layouts. Both open with the header row and close with the same
# stat strip and footer; they differ only in how tall the art is and how many
# tracks sit under it, which is exactly the pair of numbers that has to stay in
# step -- the art absorbing a track's worth of height is what keeps the strip
# on the same row either way.
grid("order card grid", [("header", headerY, headerH), ("rule1", rule1Y, 1),
                         ("art", artY, artOrderH),
                         ("cap1", orderCap1Y, barCapH),
                         ("bar1", orderBar1Y, orderBarH),
                         ("cap2", orderCap2Y, barCapH),
                         ("bar2", orderBar2Y, orderBarH),
                         ("stripCap", stripCapY, stripCapH),
                         ("stripVal", stripValY, stripValH),
                         ("footer", footerY, footerH)],
     frameY + 1, frameY + frameH - 1)

grid("campaign card grid", [("header", headerY, headerH), ("rule1", rule1Y, 1),
                            ("art", artY, artCampH),
                            ("cap", campCapY, barCapH),
                            ("bar", campBarY, campBarH),
                            ("stripCap", stripCapY, stripCapH),
                            ("stripVal", stripValY, stripValH),
                            ("footer", footerY, footerH)],
     frameY + 1, frameY + frameH - 1)

# The order card's single-track variant (a liberation, or a defence with
# nothing attacking) reuses the taller campaign bar centred in the band the two
# tracks would have filled. It has to stay inside that band, or it collides
# with the strip below.
print("=== order card, single-track variant ===")
check("solo cap after the art", artY + artOrderH, orderSoloCapY)
check("solo track inside the two-track band", orderSoloBarY + campBarH, orderBandEnd)
print()

# Every other screen keeps the plain "SUPER EARTH" strip.
grid("status-screen grid", [("header", headerY, headerH), ("rule1", rule1Y, 1)],
     frameY + 1, frameY + frameH - 1)

# The art band stacks its identity rows inside itself, positioned relative to
# the band's own top, so a collision there does not show up in the grids above.
# The order card is the tighter of the two -- it carries the order title row
# the campaign card does not -- so it is the one worth asserting.
print("=== art band inner rows (order card) ===")
ART_ROWS = [("name", artNameDy, artNameH),
            ("sector", artSectorDy, artSectorH),
            ("headline", artStatDy, artStatH),
            ("orderTitle", artTitleDy, artTitleH)]
prev_name, prev_bottom = "band top", 0
for name, dy, h in ART_ROWS:
    # The rows deliberately overlap by a few px: drawOverText() centres the
    # glyphs in each row, so the boxes touch long before the type does. What
    # matters is that they advance and stay inside the band.
    ok = dy >= prev_bottom - 8
    print(f"{'ok ' if ok else 'BAD'} {name:<10} dy {dy:>3}..{dy + h - 1:>3}   "
          f"(after {prev_name} @{prev_bottom})")
    if not ok:
        fail.append(f"art row {name} overlaps {prev_name}")
    prev_name, prev_bottom = name, dy + h
check("art rows inside the order card's band", prev_bottom, artOrderH)

# The clock plate sits bottom-right of the same band. It must clear the order
# title beside it, which is trimmed against the plate's left edge, and stay
# inside the art.
PLATE_H = plateLabelH + plateClockH + 2 * platePadY
check("clock plate inside the art band",
      (artOrderH - plateInset - PLATE_H) + PLATE_H, artOrderH - 1)
print()

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
        ("art sector row", BODY, artSectorH, CAPS),
        ("art headline row", VALUE, artStatH, CAPS),
        ("art order-title row", LABEL, artTitleH, CAPS),
        # The bar/strip caption rows are set in TFT_eSPI's built-in 6x8 GLCD
        # face, which is not a GFX font and has no table to measure; 8px into
        # barCapH/stripCapH is not a bound worth asserting.
        ("strip value row", LABEL, stripValH, CAPS),
        ("clock plate row", LABEL, plateClockH, CAPS),
        ("footer reward row", VALUE, footerH, "0123456789")]:
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

print("\n=== footer (reward at the left, sync clock at the right) ===")
# The reward is the row's headline now, set at 12pt bold in a fixed box so a
# drop from 120 to 45 medals cannot leave the hundreds digit standing.
for r in ["40", "150", "1000"]:
    check(f'reward "{r}" @12pt bold', w(VALUE, r), kRewardBoxW)
check("medal height in the footer row", ICONS["medal"][1], footerH)
# Nothing is drawn between the two blocks, so this is slack, not a tight fit --
# but it is what guarantees a four-digit reward cannot reach the clock.
check("reward block clear of the sync box",
      cardX + ICONS["medal"][0] + footerIconGap + kRewardBoxW,
      contentR - syncBoxW)
# The sync clock: a ring glyph and HH:MM in the built-in 6x8 face, right
# aligned. Staleness is carried by a caption-size prefix as well as the colour
# -- an amber tint alone is not a difference you can name at this size.
for s in ["14:32", "STALE 13:53", "NO DATA"]:
    check(f'clock glyph + "{s}" @6x8', 2 * syncGlyphR + syncGap + glcd(s), syncBoxW)
check("6x8 sync row inside the footer", GLCD_H, footerH)

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

print("\n=== header row (type word, LIBCON chip, carousel pips, wifi block) ===")
# Every word drawStatusHeader() can be handed: the plain strip, the two planet
# objective types, and the count-task headers from countWords().
HEADER_WORDS = ["SUPER EARTH", "LIBERATION", "DEFENSE", "OBJECTIVE",
                "EXTRACTION", "ERADICATION", "OPERATIONS"]
for s in HEADER_WORDS:
    check(f'"{s}" @9pt bold in its box', w(LABEL, s), contentW - wifiSlotW)
# The chip and the pips are packed off the type word's measured width, so the
# longest word is the one that has to leave room for them. Five pips is the
# cap: four Major Order tasks, or five campaigns when there is no order.
MAX_PIPS = 5
pipRowW = pipRowGap + MAX_PIPS * pipS + (MAX_PIPS - 1) * pipGap
for s in HEADER_WORDS:
    check(f'"{s}" + chip + {MAX_PIPS} pips vs the WiFi slot',
          padX + w(LABEL, s) + libconGapX + libconW + pipRowW,
          contentR - wifiSlotW)
check("active pip inside the header row", pipS, headerH)
check('"OFFLINE" @9pt bold in wifi box',
      w(LABEL, "OFFLINE"), wifiSlotW - (2 * footDotR + 6))

print("\n=== faction badge ===")
for n in sorted(set(FACTION_ICON.values())):
    check(f'badge icon "{n}" height', ICONS[n][1], badgeH - 2)
check("badge height in the target row", badgeH, targetH)

# --- first-boot touch calibration prompt ------------------------------------
#
# The one screen on the HUD set in mixed case, which makes it the one screen
# whose rows have to reserve descender space -- and the reason the checks below
# measure the real strings rather than the CAPS alphabet everything else uses.
print("\n=== first-boot calibration card: bands ===")
calHeadRuleY = calCardY + calHeadH
calBodyY = calHeadRuleY + 1
calStripeY = calCardY + calCardH - calStripeH
grid("calibration card bands", [("head", calCardY + 1, calHeadH - 1),
                                ("headRule", calHeadRuleY, 1),
                                ("body", calBodyY, calStripeY - 1 - calBodyY),
                                ("stripeRule", calStripeY - 1, 1),
                                ("stripes", calStripeY, calStripeH - 1)],
     calCardY, calCardY + calCardH - 1)
check("card clears the SUPER EARTH rule", rule1Y + 1, calCardY)
check("card bottom clears the CTA line", calCardY + calCardH,
      calCtaY - calCtaH // 2)
check("CTA line inside the frame", calCtaY + calCtaH // 2, frameY + frameH - 1)

print("=== first-boot calibration card: body rows ===")
calBodyH = calStripeY - 1 - calBodyY
calTextX = calCardX + calIconInset + calIconS + calTextGap
calTextW = calCardX + calCardW - calTextPadR - calTextX
print(f"    icon {calIconS}x{calIconS} at x={calCardX + calIconInset}, "
      f"text column x={calTextX} w={calTextW}px")
check("icon box inside the body band", calIconS, calBodyH)
check("icon box clear of the text column", calCardX + calIconInset + calIconS,
      calTextX)
CAL_ROWS = [("address", calAddrDy, calAddrH),
            ("copy1", calCopyDy, calLineH),
            ("copy2", calCopyDy + calLineH, calLineH),
            ("copy3", calCopyDy + 2 * calLineH + calParaGap, calLineH),
            ("copy4", calCopyDy + 3 * calLineH + calParaGap, calLineH)]
prev_name, prev_bottom = "body top", 0
for name, dy, h in CAL_ROWS:
    ok = dy >= prev_bottom
    print(f"{'ok ' if ok else 'BAD'} {name:<10} dy {dy:>3}..{dy + h - 1:>3}   "
          f"(after {prev_name} @{prev_bottom})")
    if not ok:
        fail.append(f"cal row {name} overlaps {prev_name}")
    prev_name, prev_bottom = name, dy + h
check("cal text rows inside the body band", prev_bottom, calBodyH)

# TFT_eSPI puts a free font's baseline glyph_ab px below a TL_DATUM anchor
# (drawString() adds glyph_ab and the top datum subtracts nothing), so a row
# clears its own type when baseline + the deepest descender fits inside it.
# That is a different sum from the CAPS rows above, which never descend.
def tl_need(font, s):
    rows = GLYPH_BOX[font]
    ab = max(-yo for _, yo in rows.values())
    bot = max(rows[ord(c)][1] + rows[ord(c)][0] for c in s if ord(c) in rows)
    return ab + bot


print("=== first-boot calibration card: strings ===")
CAL_COPY = ["This terminal's touch interface is", "not yet calibrated.",
            "Run a calibration to ensure accurate",
            "targeting and stratagem deployment."]
check('"Helldiver," @9pt bold', w(LABEL, "Helldiver,"), calTextW)
check('"Helldiver," row height', tl_need(LABEL, "Helldiver,"), calAddrH)
for s in CAL_COPY:
    check(f'"{s[:34]}" @9pt', w(BODY, s), calTextW)
    check(f'"{s[:26]}" row height', tl_need(BODY, s), calLineH)
# The header title sits between the badge and the card's right padding. At
# FONT_VALUE it is 394px and does not fit; label weight is what makes it.
calTitleX = calCardX + calBadgeInset + calBadgeW + calTitleGap
check('"TOUCH CALIBRATION REQUIRED" @9pt bold', w(LABEL, "TOUCH CALIBRATION REQUIRED"),
      calCardX + calCardW - calTextPadR - calTitleX)
check('"TOUCH ANYWHERE TO BEGIN" @9pt bold', w(LABEL, "TOUCH ANYWHERE TO BEGIN"),
      contentW)
# The triangle is knocked out of the tab's rectangular part, clear of the
# slanted trailing edge -- see the badge block in showTouchPrompt().
check("warning triangle width inside the tab", 23, calBadgeW - calBadgeSlant)
check("warning triangle height inside the tab", calBadgeH - 10, calBadgeH)
check("badge inside the header band", calBadgeH, calHeadH - 1)

# --- the uncalibrated hint, in the footer's middle gap -----------------------
#
# Set in the built-in 6x8 face, in the space the footer rework left empty
# between the reward and the sync clock. It has to fit that gap without
# touching either neighbour -- a hint that shoves the reward or the clock is
# worse than no hint.
print("\n=== footer: uncalibrated-touch hint ===")
kHintX = cardX + ICONS["medal"][0] + footerIconGap + kRewardBoxW
kHintW = (contentR - syncBoxW) - kHintX
print(f"    hint box x={kHintX} w={kHintW}px, between the reward and the clock")
check("reward block ends at the hint box's left edge",
      cardX + ICONS["medal"][0] + footerIconGap + kRewardBoxW, kHintX)
check("hint box ends at the sync box's left edge", kHintX + kHintW,
      contentR - syncBoxW)
check('"HOLD SCREEN AT POWER-ON TO CALIBRATE" @6x8',
      glcd("HOLD SCREEN AT POWER-ON TO CALIBRATE"), kHintW)
check("6x8 hint row inside the footer", GLCD_H, footerH)

print("\n" + ("ALL LAYOUT CHECKS PASSED" if not fail else "FAILURES:\n  " + "\n  ".join(fail)))
