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

Cards are art-led: the biome plate fills the upper half with the planet's
identity set over it, progress tracks beneath, a four-value stat strip, then
the footer. An active Major Order owns the screen (its targets are the only
carousel pages); with no order, the five busiest liberation campaigns take
over. Pages advance on a 7s timer. It also drives the board's speaker on a
new Major Order and after an OTA update.

## Where it lives

- Repo: `~/dev/hd2-desk-monitor` on the Mac Mini (Tailscale 100.64.112.105,
  SSH user `codydoerfler`), pushed to github.com/codydoerfler/hd2-desk-monitor.
  Tagging `v*` there is what ships an OTA to every unit — see the release
  notes in README.
- Board: PlatformIO project, env `hosyond-esp32-32e`.
- Hardware: Hosyond 4.0" ESP32-32E display module (LCDwiki E32R40T),
  ESP32-D0WD-V3, ST7796S panel, 480x320 landscape, no PSRAM, 4MB flash.
  XPT2046 touch is wired but unused -- swipe navigation was built and
  reverted (calibration would not hold on this panel), so the carousel is
  timed. Audio: GPIO26 internal DAC -> FM8002E amp, GPIO4 enable (active low).

## Source layout

- `src/main.cpp` — app entry, WiFi setup (WiFiManager), poll loop.
- `src/hd2_api.cpp` / `.h` — HTTP calls to api.helldivers2.dev, JSON parsing.
- `src/hd2_model.h` — data model for assignments/war/planets.
- `src/hud_renderer.cpp` / `.h` — all drawing code, the actual HUD layout
  (largest file). Screen states: order card, campaign card, idle, stale/
  offline, boot. Both card types share drawArtBand()/drawStrip()/drawBarRow();
  see the `Cards` block in config.h for the band geometry and why the art
  height differs between them.
- `src/hud_audio.cpp` / `.h` — the speaker. Amp enable + blocking 8-bit DAC
  playback; knows nothing about the model, same isolation the renderer keeps.
- `src/hud_audio_clip.h` — generated 8kHz PCM clip (~30KB PROGMEM).
- `src/hud_icons.h` — generated 1-bit icon bitmap tables (glyphs for stat
  tiles, crest/skull mark, shield icon, hazard chips, etc).
- `src/hud_faction_icons.h` — generated full-colour (RGB565) faction badges
  for Automaton/Terminid/Illuminate. Separate from hud_icons.h because these
  three carry their own colour rather than being 1-bit masks tinted by the
  caller — see the file's header comment for why.
- `src/hud_biomes.h` — generated biome backdrop strips for the campaign
  screen, one per planet terrain type.
- `src/hud_header_art.h` — the Earth/gold-sweep wash that backed the old
  objective bar. **No longer referenced**: the card redesign dropped the bar,
  and with it ~12.7KB of PROGMEM. The file and tools/gen_header_art.py are
  still present but unused; see git history if that look is ever wanted.
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
- `tools/gen_audio_clip.py` — generates `src/hud_audio_clip.h` from
  tools/assets/hellpods_source.mp3 via macOS `afconvert`. `--preview` dumps an
  ASCII waveform.
- `tools/check_layout.py` — layout/bounds sanity checks.
- `tools/assets/` — source art (e.g. `seaf_emblem.jpg`, skull source photos).

## Current state (as of last commit, see `git log`)

Run `git log --oneline -10` for the authoritative recent history. The most
recent work (v1.1.0) was a card redesign plus audio:

- **Cards rebuilt around the artwork.** The biome plate now fills the upper
  half with the planet's identity set *over* it (left-side scrim for
  legibility), progress tracks below, then the four-value strip and footer.
  This deleted the whole previous card stack — objective bar with its gold
  clock flag, identity row, alert ribbon, woven scene panel, verdict row —
  along with drawWash/washText/drawHatch and the header wash art. Net effect
  was flash *down* despite adding a 30KB audio clip.
- **LIBCON chip and carousel pips moved into the header row**, which every
  screen now shares. The footer is divers / reward / sync only.
- **An active Major Order owns the screen.** pageCount() returns the order's
  tasks when one is live and campaigns only when none is; the campaigns feed
  is not even fetched while an order runs. Don't "fix" this into a combined
  strip — it was tried and deliberately reverted.
- **Touch was built and reverted.** Swipe navigation with on-device
  calibration worked in principle but the XPT2046 calibration would not hold
  on this panel; the carousel is timed (7s) instead. The implementation is in
  git history if the panel is ever swapped.
- **Audio** on a new Major Order and after an OTA — see `hud_audio.*` above
  and the README's Audio section for the trigger rules (both are gated so a
  reboot doesn't replay them).

Before that, a SEAF/skull rebrand:
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
  1. Scene-band corner icon (`crest()` in `tools/gen_icons.py`) — the scene
     band itself is gone as of the card redesign, so this one is no longer
     drawn anywhere.
  2. Boot screen — actually swapped OUT here in favor of the SEAF emblem
     per explicit request ("use the super earth logo on the startup
     screen"), so boot screen shows SEAF emblem, not the skull.
  There used to be a third place — a traced version of this same skull inside
  the Major Order title-bar badge (`headerBadge`/`headerSkull`) — but that
  badge design was already superseded by a background-wash-only title bar
  before this rebrand, the badge/skull generation was just never pruned. It
  has since been removed (see the `hud_header_art.h` note above); don't go
  looking for a third skull location, there are only two live ones now.
  `icons::shield` (the old Defense objective bar, now unreferenced) and
  `icons::skull` (kill-count
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
- Flash headroom is genuinely tight: OTA needs two app slots, so
  `min_spiffs.csv` caps the image at 1.875 MiB and the build sits at ~96% of
  that (~70KB spare). Compiled-in art is what fills it. If a change overflows
  the slot, the fix is a custom partition CSV growing **both** app slots
  equally — not reverting to `huge_app.csv`, which would remove OTA.
- Regenerate and visually check `preview_*.png` via `tools/preview.sh` for
  any change touching `hud_renderer.cpp` or the generated art headers, and run
  `python3 tools/check_layout.py`, before saying a visual change is complete.
  The preview caught two real collisions in the card redesign (a clipped clock
  plate and a plate overlapping the headline) that the constants alone did not
  make obvious.
- API usage: community API at api.helldivers2.dev requires `X-Super-Client`
  and `X-Super-Contact` headers (400 without them). Rate limit is 5
  requests / 10s; poll interval is 300s (5 min) and spends up to 6 requests
  per poll, don't lower it much. `HD2_CONTACT_HEADER` now points at this
  repo (it was a placeholder until v1.1.0).
- This device IS flashed and connected to real hardware (confirmed 2026-08-02:
  a CH340 USB-serial adapter, VID:PID 1A86:7523, was present at
  `/dev/cu.usbserial-210` on the Mac Mini). Don't assume based on git history
  alone whether a unit is live — flashing doesn't require a commit. If in
  doubt, check `pio device list` for a connected USB-serial adapter and ask
  the user directly.
