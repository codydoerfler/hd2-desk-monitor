# Project Context — Read This First

This file exists so any assistant (Claude Code, Claude desktop, a fresh
session, whatever) can get fully oriented on this project without needing
prior chat history. Read this, then README.md for full technical detail.

## What this is

A standalone desk display that shows the live Helldivers 2 Major Order
(target planet, liberation %, time remaining, player counts) on a small
ESP32-driven color LCD. Read-only, no touch/buttons, WiFi + USB-C powered.
Polls the community API at api.helldivers2.dev every 5 minutes and renders
a Super Earth command-terminal style HUD.

## Where it lives

- Repo: `~/dev/hd2-desk-monitor` on the Mac Mini (Tailscale 100.64.112.105,
  SSH user `codydoerfler`). Not yet pushed to GitHub as of this writing —
  check `git remote -v` before assuming a remote exists.
- Board: PlatformIO project, env `hosyond-esp32-32e`.
- Hardware: Hosyond 4.0" ESP32-32E display module (LCDwiki E32R40T),
  ESP32-D0WD-V3, ST7796S panel, 480x320 landscape, no PSRAM, 4MB flash.
  XPT2046 touch is wired but unused in v1.

## Source layout

- `src/main.cpp` — app entry, WiFi setup (WiFiManager), poll loop.
- `src/hd2_api.cpp` / `.h` — HTTP calls to api.helldivers2.dev, JSON parsing.
- `src/hd2_model.h` — data model for assignments/war/planets.
- `src/hud_renderer.cpp` / `.h` — all drawing code, the actual HUD layout
  (largest file, ~1200 lines). This is where screen states live: idle,
  liberation, defense, invasion, stale/offline, boot screen.
- `src/hud_icons.h` — generated 1-bit icon bitmap tables (glyphs for stat
  tiles, crest/skull mark, shield icon, hazard chips, etc).
- `src/hud_faction_icons.h` — generated full-colour (RGB565) faction badges
  for Automaton/Terminid/Illuminate. Separate from hud_icons.h because these
  three carry their own colour rather than being 1-bit masks tinted by the
  caller — see the file's header comment for why.
- `src/hud_biomes.h` — generated biome backdrop strips for the campaign
  screen, one per planet terrain type.
- `src/hud_header_art.h` — generated header-bar art: just the Earth/gold-sweep
  background wash now (`headerBg`). It used to also hold a disc-and-arcs badge
  with a traced skull (`headerBadge`/`headerSkull`) for the old title-bar
  design; that design was superseded and the dead, visibly-broken assets were
  removed rather than left around — see git history before that removal if
  reviving that look is ever wanted.
- `src/hud_fonts.h`, `src/hud_font_anton.h` — font tables.
- `src/config.h` — tunables, most also exposed as `platformio.ini` build
  flags (poll interval, API contact header, etc).

## Off-target preview tooling (important — use this, don't flash blind)

`tools/preview.sh` compiles the firmware's own renderer on the host (not the
ESP32) and rasterizes each HUD screen state to PNG. This is the primary way
to check a layout/art change before flashing real hardware. Current preview
outputs sit at repo root: `preview_boot.png`, `preview_idle.png`,
`preview_liberation.png`, `preview_defense.png`, `preview_invasion.png`,
`preview_stale.png`, `preview_campaign.png`. Regenerate after any
renderer/icon change and look at them before calling a visual change done.

There is also `docs/icon-audit/` — a standalone HTML report
(`icon_audit.html`) that lays out every icon/mark in the project at a
readable size with notes, generated as a one-off review tool while auditing
the icon set. It is not wired into `tools/preview.sh` and has no regenerate
script; treat it as a snapshot from when it was made, not a live view.

Icon/art generator scripts (Python, in `tools/`):
- `tools/gen_icons.py` — generates `src/hud_icons.h`. Contains the `crest()`
  function (scene-band corner icon) among others.
- `tools/gen_header_art.py` — generates `src/hud_header_art.h` SEPARATELY
  from gen_icons.py. Now only emits the header-bar background wash
  (`headerBg`) — see the source layout note above on why the old title-bar
  badge/skull is gone.
- `tools/gen_faction_icons.py` — generates `src/hud_faction_icons.h`, the
  full-colour Automaton/Terminid/Illuminate badges. Source art is
  `tools/assets/faction_*_source.png` (official insignia, reduced rather
  than redrawn).
- `tools/gen_biomes.py` — generates `src/hud_biomes.h`, the campaign-screen
  terrain backdrops, from `tools/assets/biomes/*.webp`.
- `tools/gen_anton_font.py` — generates the Anton display font table.
- `tools/check_layout.py` — layout/bounds sanity checks.
- `tools/assets/` — source art (e.g. `seaf_emblem.jpg`, skull source photos).

## Current state (as of last commit, see `git log`)

Run `git log --oneline -10` for the authoritative recent history. As of
writing, the most recent work was a SEAF/skull rebrand:
- Faction label "Humans" → "SEAF" in all **UI text only**. The underlying
  API string matching against the community API's actual `"Humans"` string
  was deliberately left untouched (factionColor, objectiveStatus,
  factionIcon, factionAccent all still match `human*` — do not "fix" this,
  it's correct, the API still says Humans even though we display SEAF).
- SEAF emblem (Super Earth globe + laurel, user-supplied source photo) is
  the boot screen and idle-screen centerpiece art (`emblemLarge`, 72x39
  only — it was tested and confirmed illegible at any smaller icon/badge
  size, so smaller emblem variants were removed entirely rather than kept
  as broken assets). The faction badge for SEAF is text-only, no icon, by
  design (matches the "unknown faction" fallback style).
- Winged skull (user-supplied source photo,
  `tools/assets/skull_wings_source.jpg` → `crest_mask_v2.png`) replaced the
  project's previous crest mark:
  1. Scene-band corner icon (`crest()` in `tools/gen_icons.py`)
  2. Boot screen — actually swapped OUT here in favor of the SEAF emblem
     per explicit request ("use the super earth logo on the startup
     screen"), so boot screen shows SEAF emblem, not the skull.
  There used to be a third place — a traced version of this same skull inside
  the Major Order title-bar badge (`headerBadge`/`headerSkull`) — but that
  badge design was already superseded by a background-wash-only title bar
  before this rebrand, the badge/skull generation was just never pruned. It
  has since been removed (see the `hud_header_art.h` note above); don't go
  looking for a third skull location, there are only two live ones now.
  `icons::shield` (Defense objective bar) and `icons::skull` (kill-count
  stat tile, a plain no-wings skull) are visually similar but were
  confirmed unrelated and intentionally left alone — don't touch them
  when asked to change "the skull icon" without double-checking which one
  is meant.
- Full-colour faction badges (Automaton/Terminid/Illuminate) and biome
  terrain backdrops for the campaign screen were added on top of the SEAF
  rebrand — see the `hud_faction_icons.h`/`hud_biomes.h` and
  `gen_faction_icons.py`/`gen_biomes.py` notes above. The Automaton badge
  went through one revision: an earlier draft used a drawn robot-head shape,
  caught by the icon audit as wrong (the actual insignia is a four-pointed
  star) and corrected by replacing `tools/assets/faction_automaton_source.png`
  and regenerating — `docs/icon-audit/icon_audit.html` predates that fix, so
  its "automaton is not the faction insignia" finding is stale/resolved, not
  an open issue.

## Working conventions for this project

- Board target is fixed: `hosyond-esp32-32e` in `platformio.ini`. Don't
  change the board/pin mapping without being asked — it's already verified
  against physical hardware.
- Verify builds with `python3 -m platformio run` before considering a
  change done. Watch RAM/flash usage in the build output (device has no
  PSRAM, headroom is limited).
- Flash headroom is now genuinely tight: OTA needs two app slots, so
  `min_spiffs.csv` caps the image at 1.875 MiB and the build sits at ~95% of
  that (~90KB spare). Compiled-in art is what fills it. If a change overflows
  the slot, the fix is a custom partition CSV growing **both** app slots
  equally — not reverting to `huge_app.csv`, which would remove OTA.
- Regenerate and visually check `preview_*.png` via `tools/preview.sh` for
  any change touching `hud_renderer.cpp`, `hud_icons.h`, or
  `hud_header_art.h`, before saying a visual change is complete.
- API usage: community API at api.helldivers2.dev requires `X-Super-Client`
  and `X-Super-Contact` headers (400 without them). Rate limit is 5
  requests / 10s; poll interval is 300s (5 min) and spends 3 requests per
  poll, don't lower it much. The `HD2_CONTACT_HEADER` build flag in
  `platformio.ini` still has a placeholder TODO — replace with a real
  contact before running long-term against the public API.
- This device IS flashed and connected to real hardware (confirmed 2026-08-02:
  a CH340 USB-serial adapter, VID:PID 1A86:7523, was present at
  `/dev/cu.usbserial-210` on the Mac Mini). Don't assume based on git history
  alone whether a unit is live — flashing doesn't require a commit. If in
  doubt, check `pio device list` for a connected USB-serial adapter and ask
  the user directly.
