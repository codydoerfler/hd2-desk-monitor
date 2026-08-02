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
- `src/hud_icons.h` — generated icon bitmap tables (glyphs for stat tiles,
  faction badges, crest/skull mark, shield icon, etc).
- `src/hud_header_art.h` — generated header/title-bar art (Major Order card
  badge, separate from hud_icons.h — easy to miss when updating marks).
- `src/hud_fonts.h`, `src/hud_font_anton.h` — font tables.
- `src/config.h` — tunables, most also exposed as `platformio.ini` build
  flags (poll interval, API contact header, etc).

## Off-target preview tooling (important — use this, don't flash blind)

`tools/preview.sh` compiles the firmware's own renderer on the host (not the
ESP32) and rasterizes each HUD screen state to PNG. This is the primary way
to check a layout/art change before flashing real hardware. Current preview
outputs sit at repo root: `preview_boot.png`, `preview_idle.png`,
`preview_liberation.png`, `preview_defense.png`, `preview_invasion.png`,
`preview_stale.png`. Regenerate after any renderer/icon change and look at
them before calling a visual change done.

Icon/art generator scripts (Python, in `tools/`):
- `tools/gen_icons.py` — generates `src/hud_icons.h`. Contains the `crest()`
  function (scene-band corner icon) among others.
- `tools/gen_header_art.py` — generates `src/hud_header_art.h` SEPARATELY
  from gen_icons.py. Contains `traced_skull()` for the Major Order card
  title-bar badge (`headerBadge`/`headerSkull`). **Do not assume one script
  covers all icon/mark changes — there are three independent
  skull-like-mark locations in this codebase, see below.**
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
  project's previous skull/crest mark in all three places it appears:
  1. Scene-band corner icon (`crest()` in `tools/gen_icons.py`)
  2. Major Order card title-bar badge (`traced_skull()` in
     `tools/gen_header_art.py` — separate script from #1, easy to miss)
  3. Boot screen — actually swapped OUT here in favor of the SEAF emblem
     per explicit request ("use the super earth logo on the startup
     screen"), so boot screen shows SEAF emblem, not the skull.
  `icons::shield` (Defense objective bar) and `icons::skull` (kill-count
  stat tile, a plain no-wings skull) are visually similar but were
  confirmed unrelated and intentionally left alone — don't touch them
  when asked to change "the skull icon" without double-checking which one
  is meant.

## Working conventions for this project

- Board target is fixed: `hosyond-esp32-32e` in `platformio.ini`. Don't
  change the board/pin mapping without being asked — it's already verified
  against physical hardware.
- Verify builds with `python3 -m platformio run` before considering a
  change done. Watch RAM/flash usage in the build output (device has no
  PSRAM, headroom is limited).
- Regenerate and visually check `preview_*.png` via `tools/preview.sh` for
  any change touching `hud_renderer.cpp`, `hud_icons.h`, or
  `hud_header_art.h`, before saying a visual change is complete.
- API usage: community API at api.helldivers2.dev requires `X-Super-Client`
  and `X-Super-Contact` headers (400 without them). Rate limit is 5
  requests / 10s; poll interval is 300s (5 min) and spends 3 requests per
  poll, don't lower it much. The `HD2_CONTACT_HEADER` build flag in
  `platformio.ini` still has a placeholder TODO — replace with a real
  contact before running long-term against the public API.
- This device is not yet flashed to real hardware as of the last commit
  noted above — check with the user before assuming firmware is live on a
  physical unit vs. still in host-preview/dev stage.
