# hd2-desk-monitor — touch, event overlays, SD card, and a progress-rendering bug fix

Read CONTEXT.md and README.md first. This is a mature, opinionated codebase
with strong existing conventions (see hd2_model.h, hud_renderer.cpp) — match
its style, comment density, and architecture rather than bolting things on.

There are five pieces of work below. Do them in the order listed — the bug
fix and SD card work are foundational to the rest, touch is the highest risk
item, and the two overlay screens depend on touch + SD both being in place.

## 0. Branch

Work on a new branch off main, e.g. `touch-and-events`. Do not merge
yourself — this touches hardware calibration (touch), a new physical
peripheral (SD card wiring), and visual bug fixes Cody needs to see rendered
before trusting them. Stop at PR open. Regenerate and commit
tools/preview.sh output (or an SD-aware equivalent, see part 3) for every
visual change so Cody can review without flashing hardware first.

## 1. Bug fix: count-style Major Order tasks render as "UNKNOWN" / "OBJECTIVE COMPLETE"

Cody reported a live bug via photo: the device showed "UNKNOWN" as the
planet name, "OBJECTIVE COMPLETE" as the status line, and "AWAITING
TELEMETRY" in the progress bar area, for an order that in fact has three
live count-style objectives (a Companion-app screenshot of the same order,
same LIBCON tier, same "Ends in: 3d 18h", confirms this): an "Operation"
completion count on one planet, a kill count on another, and a
sample-extraction count on a third — none of which are the liberate/defend
task types this renderer currently special-cases.

Root cause (already confirmed by reading the code, don't re-derive from
scratch): `taskIsLiberation()` in hd2_model.h only returns true for
`kTaskTypeLiberate` (raw API type 11). `objectiveStatus()` in
hud_renderer.cpp only special-cases `kTaskTypeDefend` (type 13); every other
task type — which includes at least the kill-count and extraction-count
types visible in the bug report — falls through to a generic branch that was
written for "a type we don't recognize, be honest about not inventing a
metric," not as the primary code path. `drawBarRow`/`drawIdleTrack`
selection in the per-task solo track section (around hud_renderer.cpp
~955-975) has the same gap: only liberate gets a real progress bar, only
defend-with-no-assault gets the "NO ASSAULT IN PROGRESS"/"AWAITING
TELEMETRY" idle track, everything else has no branch at all and presumably
falls through to whatever the idle track default is (hence "AWAITING
TELEMETRY" showing for a task that has very much started).

Fix:
- Confirm exact raw `type` values for the count-style tasks by hitting
  `GET /api/v1/assignments` directly (send the required X-Super-Client /
  X-Super-Contact headers) and inspecting a live order with kill/extraction
  objectives, or by checking the community API's OpenAPI schema / GitHub
  repo (github.com/helldivers-2/api) for the task type enum if the live API
  is down (it returned 503 as of this writing — retry, it's usually up).
  Do not guess numeric values.
- Each task's `progress` field is a running total against a target count
  embedded in the task's own `values`/`valueTypes` (per the API's existing
  documented shape — hd2_api.cpp / hd2_model.h may already partially parse
  this, check before adding a duplicate field). Add whatever's missing to
  OrderTask to carry the target count through, not just the binary
  `complete` flag.
- Add a real rendering path for count-style tasks: current/target as a
  number (formatCompact() already exists in hd2_api.cpp for large numbers,
  reuse it), a percent, and a progress bar via the existing drawBarRow()
  machinery — same visual language as the liberation bar, not a new style.
  Companion's screenshot is the reference for what data is meaningfully
  available (running total, percent, "end result" projection, time to
  complete) — match what THIS panel's layout and data budget can support,
  it does not need to replicate Companion's full multi-line layout, but it
  must show real progress instead of a static label.
- Figure out why the planet name showed "UNKNOWN" for these tasks —
  likely these task types either don't carry a `planetIndex` the same way,
  or carry one that points at a planet not being resolved for
  non-liberate/defend types. If a task type is genuinely not planet-scoped,
  don't force a planet name — but if it IS planet-scoped (the bug report
  shows named planets in Companion for the same tasks: Uvp Gamma, Senge 23,
  Uvp Alpha) then the planet lookup is being skipped for these types and
  needs to run the same as it does for liberate/defend.
- objectiveStatus()'s generic fallback branch should remain as a true
  last-resort for task types that are genuinely unhandled/unknown, not as
  the effective default for common, well-known task types like kill-count
  and extraction-count.
- Regenerate tools/preview.sh output against a real or synthetic order that
  exercises kill-count and extraction-count tasks (mock the API response
  if you can't get a live one with these task types active) and confirm
  visually before considering this done.

## 2. SD card support (128MB, SPI)

Cody is adding a 128MB SD card to each unit (SPI SD card module, not
SD_MMC — confirm the Hosyond board's free pins support SPI SD before
wiring; the display already occupies the HSPI group (12/13/14/15/2/27) plus
touch CS 33, so the SD card needs its own CS pin and either shares the HSPI
bus (SD cards tolerate lower clock, TFT_eSPI + SD sharing one SPI bus with
separate CS is a common and supported pattern) or use VSPI if free pins
allow — pick whichever fits the board's actual free GPIO, check the
LCDwiki E32R40T pinout doc referenced in platformio.ini's header comment,
don't assume, verify against real pin availability).

Purpose: hold artwork and sound assets for the new touch/event features
(items 3-5 below) rather than compiling them into PROGMEM — headroom is
already tight per platformio.ini's own comments (~100KB spare on a
1.875MB OTA slot), and Cody explicitly said space is no longer a constraint
once the SD card is in. Move new assets added for this task to the SD
card; do not migrate existing PROGMEM assets (hud_icons.h, hud_biomes.h,
hud_faction_icons.h, hud_audio_clip.h) unless it's trivial and safe to do
alongside this work — that's a separate cleanup, not in scope here unless
free.

- Add SD (or SD_MMC, whichever fits the wiring) library support, mount at
  boot, fail gracefully (log + fall back to "no SD" behavior, e.g. skip new
  sounds/art, keep everything else working) if no card is present or mount
  fails — existing units without a card yet must not be bricked by a
  firmware update that assumes SD is always there.
- Define a simple on-card file layout/convention for the new assets (e.g.
  `/audio/new_order.wav`, `/audio/success.wav`, `/audio/failure.wav`,
  `/art/success_bg.bin` or similar — pick a sensible convention, document
  it in README.md and CONTEXT.md, and provide the actual asset files or a
  tools/ generator script for them alongside the code, same pattern as the
  existing tools/gen_*.py scripts).
- Document card prep (format as FAT32, directory layout, how to regenerate
  assets) in README.md.

## 3. Touch: try harder this time

XPT2046 resistive touch was wired but the calibration would not hold on a
prior attempt (see CONTEXT.md "Touch was built and reverted" and git
history for the previous implementation — review it before starting, don't
reinvent from zero, the previous attempt is a real starting point even
though it was reverted). Cody's words: "I didn't try hard enough" — same
hardware, no panel swap, this needs a genuinely more robust attempt, not a
repeat of the same approach.

Things to actually try this time that may not have been tried before:
- Persist calibration data (the four/five-point touch-to-screen mapping)
  to NVS (ESP32 non-volatile storage) so it survives reboot instead of
  needing recalibration or relying on a single hardcoded mapping.
- Re-verify TOUCH_CS=33 and SPI_TOUCH_FREQUENCY=2500000 in platformio.ini
  against the actual LCDwiki E32R40T reference wiring — confirm these
  weren't part of why calibration drifted (frequency too high/low, wrong
  rotation mapping against the display's own rotation, etc).
  XPT2046 is a resistive panel; pressure/contact-time sensitivity settings
  and debounce may matter more than the calibration math itself if the
  previous attempt treated it like a capacitive panel.
- Check TFT_eSPI's own touch calibration helpers (getTouchRawZ, calibrate,
  setTouch) are being used correctly, and that raw ADC readings are sane
  (log them) before assuming the coordinate mapping is the problem.
- If, after a genuinely more thorough attempt, this remains fundamentally
  unreliable, report that clearly with specifics of what was tried and
  what failed rather than silently reverting again.

## 4. Swipe left/right on cards, timer stays running

Once touch works: add left/right swipe gesture detection to advance/go
back through the card carousel (order tasks, or campaign cards when no
order is active). Cody's explicit decision: keep the existing 7s
auto-advance timer running as a fallback (per README/CONTEXT — pages
advance on a 7s timer today), touch does not replace it. Any swipe
interaction should reset the auto-advance timer (so a manual swipe isn't
immediately undone a moment later by the timer firing), same pattern most
carousel UIs use.

## 5. MO completion overlay (success/failure), dismissed by touch

The community API has no explicit result/outcome field or event for a
Major Order ending — confirmed by reading hd2_api.cpp/hd2_model.h and the
API's own response shape; it only reports currently-active assignments,
and a completed or expired order simply stops appearing in
`GET /api/v1/assignments`. There is no historical/outcome endpoint.

Design (agreed with Cody, implement as specified, this is not open for
reinterpretation):
- Track the last-known state of the current order's tasks across polls
  (already close to being available via `model.order`/`orderIsNew` logic
  in main.cpp — extend rather than duplicate).
  - If all tasks were `complete == true` at last observation, and the next
    poll's assignments response no longer contains that order id ->
    classify as SUCCESS.
  - If the order's own `expiration` timestamp has passed (or the order
    disappears from the feed with tasks still incomplete) -> classify as
    FAILURE.
- Show a full-screen overlay announcing the result (SUCCESS in the
  project's green, FAILURE in its red — match existing theme:: color
  usage) when this is detected. This overlay takes over the display,
  interrupting the normal card carousel/timer.
- Dismissed by touch (tap anywhere). Until touched, it stays up — this is
  meant to be seen, not auto-dismissed. If touch turns out to be
  unreliable after part 3's effort, have a documented fallback dismiss
  path (e.g. also dismiss after N seconds, or on the next successful poll)
  so the device can't get stuck showing a stale overlay forever — but
  touch-dismiss is the primary interaction, don't design around touch
  failing.
- Play a sound from the SD card (item 2's asset convention) when this
  overlay appears — separate clips for success vs failure. Follow the
  existing audio-trigger gating pattern in main.cpp (alertPending-style,
  queued and fired after the frame draws, gated so a reboot doesn't
  replay a stale event — see the existing new-Major-Order audio trigger
  for the pattern to copy).

## 6. New MO announcement overlay, dismissed by touch

Currently `orderIsNew` (main.cpp) only triggers audio, no dedicated visual
state — the new order is just drawn as the regular card on the next
repaint. Add a full-screen announcement overlay (distinct look from the
success/failure overlay in item 5 — this is "here's what's new," not a
verdict) shown when `orderIsNew` fires, using the order's title/briefing
as the primary content. Same interaction pattern as item 5: full-screen,
dismissed by touch (tap anywhere), same non-touch fallback safety net,
plays its own SD-card sound clip (distinct from success/failure clips),
same audio-trigger gating pattern as the existing code.

After dismissal (or fallback timeout), fall back to the normal card
carousel showing the new order's first task.

## General

- Follow existing code conventions closely: the codebase has extensive,
  precise comments explaining *why*, not just what — match that density
  and tone, don't write terser comments than the surrounding code.
- Verify build via `python3 -m platformio run` after every meaningful
  change, not just at the end.
- Regenerate tools/preview.sh outputs for every visual change (existing
  preview_*.png files at repo root, plus new ones for the two new overlay
  screens) and actually look at them before calling any visual work done.
- Update README.md and CONTEXT.md to reflect all of this once done —
  CONTEXT.md in particular currently says "Touch was built and reverted"
  and "read-only, no touch/buttons" in its opening summary; both need to
  change, and the SD card / new screen states need documenting in the
  Source layout and Current state sections.
- Stop at PR open, do not merge. Report clearly: whether touch calibration
  actually held this time (with specifics), whether SD wiring needed a
  pin change from what's currently free, and whether the count-style task
  bug fix was verified against a live API order or only a synthetic one
  (and why, if live wasn't available).
