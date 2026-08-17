# Restyle touch calibration screens to match new reference art exactly

## Context

The first-boot touch calibration flow shipped in v1.3.0 (PR #3, merged commit
ea0399c). It drew a simplified native card in `HUDRenderer::showTouchPrompt()`
(src/hud_renderer.cpp) built from an EARLIER, simpler reference image
(`touch_calibration_reference.jpg`, still in repo root) that deliberately
omitted the HELLDIVERS II wordmark, the ship-bridge background, and any
success-state screen. Those omissions were an explicit decision at the time
(see the comment block above `drawCalReticle()` in hud_renderer.cpp and the
"Visuals" section history in git log for touch-onboarding).

Cody has now supplied two NEW, more complete reference images and said
explicitly: **"Restyle both. Do not change the artwork. I want it to look
exactly like that."** This supersedes the earlier decision to omit the
wordmark/background. Match these two images as closely as the hardware
(480x320 TFT_eSPI, limited color depth, no photo/JPEG decode budget) allows.

Reference images (repo root, already committed on main at this task's start):
- `touch_required_reference.jpg` — the "TOUCH CALIBRATION REQUIRED" screen
- `touch_success_reference.jpg` — the "TOUCH CALIBRATION SUCCESSFUL" screen
  (this screen does not exist in the codebase at all yet — new work)

## What "do not change the artwork" means here

Cody wants the composition, layout, colors, iconography, and text to match
the reference images as closely as native vector drawing on this panel can
achieve — not a reinterpretation, not "inspired by." Where the reference has
photographic/gradient detail (the starfield/ship-bridge background, the
diver silhouette on the left, the fine bloom around the HELLDIVERS II logo)
that cannot be faithfully reproduced as flash-cheap vector primitives at
480x320, use flat/simplified versions of the SAME elements in the SAME
positions and proportions (e.g. a flat dark background instead of the
photographic bridge, a simplified skull/wing wordmark instead of the exact
game logo's typography if the exact glyphs aren't practical) rather than
omitting them. The goal is "reads as this exact screen at a glance," not
technical pixel-identity. If truly forced to drop something (e.g. full
photographic bloom), say so explicitly in the PR rather than silently
simplifying without a note — Cody wants to know what didn't make it and why.

Both screens in the reference are the SAME background/wordmark/frame, only
the card content differs (warning triangle amber card vs check green card,
reticle+hand icon vs globe medallion icon, red vs green accent color
throughout). Build shared drawing helpers for the parts that are identical
between the two screens (logo/wordmark, card frame/proportions, hazard-bar
footer treatment) rather than duplicating.

## Required screen (touch_required_reference.jpg)

- Full "HELLDIVERS II" wordmark + skull/wing emblem at top of screen, above
  the card (not present at all in current implementation)
- Dark background suggesting a ship interior (flat/simplified is fine, does
  not need to be photographic)
- Amber/gold card, same general proportions as current implementation:
  - Header band: amber warning-triangle chip + "TOUCH CALIBRATION REQUIRED"
    title, both amber
  - Body: crosshair reticle with tapping hand icon on the left (current
    `drawCalReticle()` is close to this already, keep/reuse it if it still
    reads right against the new layout), "Helldiver," + calibration copy
    text on the right
  - Bottom: diagonal hazard-stripe bar (current `drawHazardBar()` reusable)

## Success screen (touch_success_reference.jpg) — new

- Same wordmark/background treatment as the required screen for visual
  consistency (shared top chrome)
- Green card instead of amber:
  - Header band: green checkmark chip + "TOUCH CALIBRATION SUCCESSFUL" title
  - Body: globe medallion icon (wreath + stars + globe, like a
    campaign/service medal) on the left, "Calibration Complete!" headline +
    body copy on the right ("Your touchscreen has been calibrated
    successfully." / a second line — Cody's screen says "Super Earth thanks
    you for your commitment to victory.", keep that or write a device-
    appropriate equivalent per the same pattern the required screen already
    uses for its own body copy, which was written for the device rather than
    lifted verbatim from the game)
  - Bottom: "FOR SUPER EARTH!" in a footer treatment matching the required
    screen's hazard bar band (green accent instead of amber/gold), can be a
    hazard-stripe bar in green or a simpler green rule + centered text if the
    stripes don't read well in green — use judgment, look at the reference

## Wiring

Currently `showTouchPrompt()` is called once from `main.cpp` when a fresh
unit needs calibration, and the existing code waits up to 30s for a touch
then proceeds into the normal calibration routine
(`maybeRecalibrateTouch()`/`touch::calibrate()`) regardless. The success
screen needs to be shown briefly after calibration completes successfully
during that SAME first-boot flow — find the current calibration completion
path and insert a call to a new `showTouchSuccess()` (or similar) for a
few seconds before continuing into the normal HUD boot. Do not change
calibration timing/logic itself, only add the success screen as a step in
the existing flow. If calibration is re-run later via the existing opt-in
hold-at-boot recal gesture (for already-calibrated units), it should also
show the same success screen on completion — check how that path currently
ends and hook it there too if it doesn't already show any confirmation.

## Constraints (existing conventions in this file, keep following them)

- Everything drawn from TFT_eSPI primitives (rects, circles, triangles,
  lines) or the existing font system — no new bitmap/PROGMEM image assets,
  per the standing flash-budget note in hud_renderer.cpp above
  `drawCalReticle()`. Current flash headroom is tight (~74KB free per the
  last touch-onboarding PR) so keep new code primitive-based like the rest
  of this file.
- Reuse `theme::` palette constants (gold/panel/bg/text/grey) plus add
  whatever green accent constant is needed for the success screen — follow
  the existing naming convention in config.h/hud_renderer.h (see how
  `theme::gold` etc. are defined and add `theme::green` or similar if not
  already present).
- Regenerate `tools/preview.sh` PNGs for every visual change, screenshot
  proof required in the PR, same as prior touch-onboarding work.
- Run `python3 tools/check_layout.py` and confirm it passes (may need new
  assertions added for the new layout, follow the existing pattern in that
  file).
- `pio run` must build clean, report flash usage before/after.

## Process

- Work on a fresh branch off latest main: `restyle-touch-screens`
- Opus, xhigh effort (this is a real visual/layout redesign, not a small fix)
- STOP AT PR, do not merge. This is a visible UI/art change Cody explicitly
  wants to review against the reference images himself before it ships.
- Read `Projects/HD2 Desk Monitor.md` in the Obsidian vault
  (~/Library/Mobile Documents/iCloud~md~obsidian/Documents/CLD/Projects/) for
  background before starting, per standing CLAUDE.md convention, and write a
  session summary back there when done.
