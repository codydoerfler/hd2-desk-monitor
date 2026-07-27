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
IW = {n: wh[0] for n, wh in ICONS.items()}
print("icons: " + ", ".join(f"{n} {w}x{h}" for n, (w, h) in sorted(ICONS.items())))
print()

# --- constants mirrored from src/config.h ---
padX, contentR = 20, 460
contentW = contentR - padX            # 440
badgePadX, statGap = 9, 8
kStatW = (contentW - statGap) // 2    # 216
kStatInsetX = 12
headerIconGap, badgeIconGap, badgeMaxW = 6, 5, 170
kHeaderTextInset = IW["emblem"] + headerIconGap

# Mirrors factionIcon() in hud_renderer.cpp.
FACTION_ICON = {"automaton": "automaton", "terminid": "terminid",
                "illuminate": "illuminate", "human": "emblemMini"}


def badge_w(faction):
    bw = w("FreeSansBold9pt7b", faction.upper()) + 2 * badgePadX
    for prefix, icon in FACTION_ICON.items():
        if faction.lower().startswith(prefix):
            bw += IW[icon] + badgeIconGap
            break
    return min(bw, badgeMaxW)

fail = []


def check(label, got, limit):
    ok = got <= limit
    print(f"{'ok ' if ok else 'BAD'} {label:<50} {got:>4}px (limit {limit})")
    if not ok:
        fail.append(label)


print("=== title + briefing ===")
check("MAJOR ORDER @18pt", w("FreeSansBold18pt7b", "MAJOR ORDER"), contentW)

brief = "Liberate the designated planet and secure the superweapon intel."
words, lines, cur = brief.split(), [], ""
for x in words:
    t = (cur + " " + x).strip()
    if w("FreeSans9pt7b", t) <= contentW:
        cur = t
    else:
        lines.append(cur)
        cur = x
if cur:
    lines.append(cur)
print(f"    real briefing wraps to {len(lines)} line(s), max 2:")
for L in lines:
    print(f"      [{w('FreeSans9pt7b', L):>3}px] {L}")
if len(lines) > 2:
    fail.append("briefing >2 lines")

print("\n=== target row: planet name vs faction badge (badges now carry an icon) ===")
nameX = padX + 80
NAMES = ["X-45", "Meridia", "Hellmire", "Charbal-VII", "Vernen Wells",
         "Angel's Venture", "Heeth", "Mastia"]
for faction in ["Automaton", "Terminids", "Illuminate", "Humans", "Unknown"]:
    bw = badge_w(faction)
    if bw >= badgeMaxW:
        fail.append(f"badge {faction} hits the {badgeMaxW}px cap")
    nameW = (contentR - bw - 10) - nameX
    print(f"  badge {faction.upper():<11} {bw:>3}px -> {nameW:>3}px for the name")
    for pn in NAMES:
        g18 = w("FreeSansBold18pt7b", pn)
        used, got = ("18pt", g18) if g18 <= nameW else ("12pt", w("FreeSansBold12pt7b", pn))
        flag = "" if got <= nameW else "  <-- OVERFLOW"
        if got > nameW:
            fail.append(f"name '{pn}' with {faction}")
        print(f"      {pn:<16} {used} {got:>3}px{flag}")

print("\n=== progress bar label ===")
for pct in [0, 51, 79, 100]:
    check(f'"{pct}% LIBERATED" @12pt', w("FreeSansBold12pt7b", f"{pct}% LIBERATED"), contentW - 2)
check('"AWAITING TELEMETRY" @12pt', w("FreeSansBold12pt7b", "AWAITING TELEMETRY"), contentW - 2)

print("\n=== stat boxes (inner width) ===")
inner = kStatW - 2 * kStatInsetX
for lbl in ["TIME LEFT", "PLAYERS", "NETWORK", "PASSWORD"]:
    check(f'label "{lbl}"', w("FreeSansBold9pt7b", lbl), inner)
for val in ["18h 22m", "2d 04h", "45m 12s", "EXPIRED", "34.9K",
            "HD2-Monitor", "helldive"]:
    check(f'value "{val}"', w("FreeSansBold12pt7b", val), inner)

print("\n=== war stats row (3 columns) ===")
colW = contentW // 3
for lbl, val in [("KILLS", "393.6B"), ("SUCCESS", "91%"), ("ONLINE", "51.0K")]:
    lw = w("FreeSansBold9pt7b", lbl) + 8
    avail = colW - lw - 6
    got = w("FreeSansBold9pt7b", val)
    ok = got <= avail
    print(f"{'ok ' if ok else 'BAD'} col {lbl:<16} label {lw:>3}px, value {got:>3}px, "
          f"{avail:>4}px available")
    if not ok:
        fail.append(f"war col {lbl}")

print("\n=== target subtext (sector + biome) ===")
for s in ["Ymir SECTOR  -  Basic Swamp",
          "Andromeda SECTOR  -  Highlands Frozen Wasteland"]:
    check(f'"{s[:36]}"', w("FreeSans9pt7b", s), contentW)

print("\n=== footer ===")
for s in ["STALE - LAST 14:32 UTC", "SYNCED 14:32 UTC", "NO DATA YET"]:
    check(f'left "{s}"', w("FreeSans9pt7b", s), contentW // 2)
check('right "REWARD 40 MEDALS"', w("FreeSans9pt7b", "REWARD 40 MEDALS"), contentW // 2)

print("\n=== centred status lines ===")
for s in ["NO ACTIVE", "MAJOR ORDER", "ESTABLISHING UPLINK", "SUPER EARTH", "WIFI SETUP"]:
    check(f'"{s}" @18pt', w("FreeSansBold18pt7b", s), contentW)
for s in ["Stand by for orders from Super Earth High Command.",
          "Managed democracy does not sleep.",
          "Contacting Super Earth High Command...",
          "Join this network from a phone or laptop,",
          "then pick your WiFi in the page that opens.",
          "If no page opens, browse to 192.168.4.1"]:
    check(f'"{s[:38]}"', w("FreeSans9pt7b", s), contentW)
check('"MAJOR ORDER MONITOR" @9pt bold', w("FreeSansBold9pt7b", "MAJOR ORDER MONITOR"), contentW)
check('"PLANET DATA UNAVAILABLE" @12pt', w("FreeSansBold12pt7b", "PLANET DATA UNAVAILABLE"), contentR - (padX+76))
check('"GALAXY-WIDE OBJECTIVE" @12pt', w("FreeSansBold12pt7b", "GALAXY-WIDE OBJECTIVE"), contentR - (padX+76))

print("\n=== header row (emblem + label) ===")
se = w("FreeSansBold9pt7b", "SUPER EARTH")
check('"SUPER EARTH" @9pt bold in its box', se, 200 - kHeaderTextInset)
check("emblem + label vs the WiFi block", padX + kHeaderTextInset + se, contentR - 110)
check('"OFFLINE" @9pt bold in wifi box', w("FreeSansBold9pt7b", "OFFLINE"), 110 - (2 * 5 + 6))
check("emblem height in the 15px header row", ICONS["emblem"][1], 15)
for n in ("automaton", "terminid", "illuminate", "emblemMini"):
    check(f'badge icon "{n}" height', ICONS[n][1], 22 - 2)

print("\n" + ("ALL LAYOUT CHECKS PASSED" if not fail else "FAILURES:\n  " + "\n  ".join(fail)))
