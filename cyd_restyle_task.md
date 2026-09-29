# Restyle CYD desk display to match the web monitor's visual identity

## Context
This repo (hd2-desk-monitor) drives a physical ESP32 desk display (TFT_eSPI, hosyond-esp32-32e
board). Cody also has a browser-based Helldivers 2 Major Order monitor (deployed separately as a
static webpage) that got a full visual redesign. He wants the physical device's on-screen look
brought in line with that webpage's identity — NOT a literal HTML port, but the same visual
language translated into this device's native TFT_eSPI rendering.

Reference screenshot of the target look: `tools/assets/webpage_reference/target_look.jpg`
Reference webpage source (for exact colors/copy/layout ideas, not for direct reuse):
`tools/assets/reference_webpage.html`

## What "match" means here
Look at target_look.jpg closely. Key identity elements to bring onto the device:

1. **Official faction icons** — the webpage uses the real Helldivers 2 wiki icons (traced SVGs),
   not this repo's current icon set. Source SVGs are at `tools/assets/official_icons/`:
   - `terminid.svg` — orange claw shape, fill `#ffb901`
   - `automaton.svg` — red angular star/wing shape, fill `#ff6161`
   - `illuminate.svg` — purple ringed moon/circle, fill `#cd8aea`
   - `helldiver.svg` — the pentagon-shaped Helldiver skull (yellow `#ffe800`), used as the main
     masthead emblem in the header

2. **Color identity** — dark near-black background, yellow/gold accent (`#ffe600`-ish) as the
   primary UI accent instead of the current blue progress bars, with faction-specific accent
   colors on each objective row (orange Terminids / red Automatons / purple Illuminate) instead
   of the current uniform blue fill.

3. **Header/masthead** — the webpage's masthead has: Helldiver skull emblem on the left, stacked
   title ("MAJOR ORDER MONITOR" style wordmark), a small tagline under it, and status/telemetry
   text. Translate this into the device's existing header band, keeping the emblem simple enough
   to read at the device's actual pixel budget (see current `factionIcon()`/`gen_icons.py`
   precedent — icons here are small bitmaps, not vector at scale).

4. **Hazard-stripe / military-tactical texture** — the webpage uses diagonal yellow/black hazard
   stripes and bracket/corner accents as a recurring motif. Bring in a restrained version of this
   (e.g. a hazard-stripe strip somewhere in the header or as a section divider) — restrained
   because this is a small low-res display, don't let texture fights with legibility.

5. **Section labeling language** — the webpage explicitly labels each objective row with the
   faction name AND an objective descriptor (this device already does something similar per the
   `ELIMINATED` labels in the current screenshot — keep functioning labels, just re-skin them to
   match the new palette/icon set).

## What must NOT change
- Do not touch any data-fetching, parsing, or API logic (hd2_api.cpp, model/task decode, faction
  string matching in `factionColor()`/`factionAccent()`/`factionIcon()` — those functions may be
  RESTYLED (new colors, new icon bitmaps) but their inputs/outputs and call sites must keep
  working exactly as before).
- Do not touch touch-calibration screens, boot/OTA overlay screens, or Major Order full-card
  overlay logic unless the icon/color change naturally flows through shared helpers they call —
  if so, that's fine, just don't scope-creep into redesigning those screens' layouts.
- Do not change flash-critical asset budgets recklessly: flash is CURRENTLY NEAR THE CEILING on
  this board (past sessions have hit ~96.7% of 4MB with only ~65KB headroom). Before adding new
  bitmap icon assets, check current usage with `python3 -m platformio run` and report the final
  flash percentage. If the four new faction icon bitmaps + skull emblem don't fit at a
  reasonable resolution, palette-reduce / simplify rather than skip them — this repo has prior art
  for this exact problem (see git log entries about "flatten if needed" and reduced icon sizes
  when detail didn't survive small-size legibility).
- Do not invent fictional faction colors/icons — use exactly the SVGs provided in
  `tools/assets/official_icons/` as the source of truth for shape and hue, adapted only as needed
  for the device's bitmap/palette pipeline (this repo's existing icon generator is
  `tools/gen_icons.py` — follow its existing pattern for turning source art into device bitmaps
  rather than inventing a new pipeline).

## Deliverable
- New/updated icon bitmaps generated from the official SVGs, wired into `factionIcon()` (and the
  Helldiver skull into wherever the current emblem/masthead icon is drawn — check
  `tools/gen_header_art.py` per prior session notes, there may be a SEPARATE header badge
  generator from `gen_icons.py`, don't miss it).
- `factionColor()`/`factionAccent()`/progress-bar fill colors updated to the faction hues above
  instead of the current uniform blue.
- Header/masthead visually re-skinned toward the yellow/gold + hazard-stripe identity shown in
  target_look.jpg, within this device's existing header layout (don't redesign the whole screen
  geometry, restyle within it).
- Regenerate preview PNGs (this repo has an existing preview-render script/precedent — reuse it)
  so the result can be reviewed as an image, same as prior sessions in this repo's git history.
- Report final flash usage percentage.
- Branch off main, PR only — do NOT merge. Stop at PR open (visual/on-device-behavior change,
  same review bar as prior touch-screen and boot-overlay work in this repo).
