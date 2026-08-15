# Touch calibration onboarding: forced first-boot flow + persistent uncalibrated hint

## Background

Touch calibration (src/hud_touch.cpp) is per physical panel — manufacturing
variance means every unit needs its own calibration, done locally by whoever
has it in hand. It is stored in NVS (survives OTA firmware updates fine,
unrelated concern, already confirmed working correctly by design).

Today the only way to discover that calibration is opt-in — hold a finger on
the panel through power-on for ~1.2s to trigger it — is a serial log line
(main.cpp maybeRecalibrateTouch(), "uncalibrated. To calibrate: hold a finger
on the panel while powering the board on."). That is only visible to someone
with a USB serial monitor attached. This unit is going out to other people
who will never see serial output, and right now they have zero way to learn
this exists. Fix the discoverability gap without breaking the deliberate
opt-in design for people who ARE already calibrated (main.cpp's existing
comment explains why unprompted calibration on every boot would be worse than
the current gap — do not regress that reasoning, only fix first-time
discovery).

## Visuals — final direction, build this natively (no photo asset)

Cody supplied a reference composition (Helldivers 2 style "TOUCH CALIBRATION
REQUIRED" warning card) for the first-boot full-screen prompt. Reproduce it
as native TFT_eSPI vector drawing at the panel's real 480x320 landscape
resolution (config.h screenW/screenH), using the existing theme:: palette
(theme::bg near-black, theme::gold accent, theme::red for the warning
triangle) and the project's existing fonts (FONT_LABEL/FONT_VALUE/FONT_BODY
per whatever textBox() helper is already used elsewhere in hud_renderer.cpp —
reuse those, don't add a new font). Do NOT attempt to reproduce the
reference's photographic starfield/ship background or the HELLDIVERS II
logo wordmark — those are copyrighted game assets and out of scope; keep the
background plain theme::bg like the rest of the HUD. What to carry over from
the reference, redrawn natively:

  - A gold-bordered warning card/panel, roughly centered, dark fill.
  - A warning triangle + "TOUCH CALIBRATION REQUIRED" header row in gold,
    matching the weight/prominence of the reference (see attached image for
    exact proportions — header bar with a rule beneath it separating it from
    the body).
  - A crosshair-with-pointing-hand icon on the left side of the body, gold
    line art. Simple enough to draw with TFT_eSPI primitives (circles +
    lines for the crosshair, a basic hand/finger shape) or, if a pointing-
    hand glyph is impractical to hand-draw cleanly at this resolution, a
    simpler equivalent icon that reads the same at a glance (a finger-tap /
    target icon) is an acceptable substitute — keep it recognizable, don't
    over-engineer pixel art.
  - Body text on the right of the icon: an address line ("Helldiver," or
    similar in-universe framing is fine, keep it short) and 1-2 short lines
    explaining touch calibration is needed, roughly matching the reference's
    tone and length — do not just paste the literal reference copy verbatim,
    adapt if needed to fit 480x320, but keep the in-universe HD2 voice.
  - A hazard-stripe accent bar at the bottom of the card (diagonal
    gold/black stripes), matching the reference's footer treatment.

This IS the tap-to-calibrate trigger screen from item 1 below — tapping
anywhere on it (or the existing timeout) proceeds into touch::calibrate()
exactly as already scoped. The persistent uncalibrated hint (item 2) stays
small/textual, unrelated to this card design, unaffected by any of the above.

## What to build

1. **Forced first-boot calibration flow.** On a genuinely fresh unit (NVS has
   no stored calibration at all — touch::calibrated() false on the very first
   boot, not "user cleared it" or "version mismatch on an update", just true
   first-ever power-on) show a full-screen prompt automatically, no held
   finger required to trigger it: something like "Touch the screen to
   calibrate" leading straight into the existing four-corner calibrate() UI
   (touch::calibrate() already draws its own chrome, reuse it, don't build a
   parallel calibration routine). This should feel like a one-time setup step
   a normal person would expect from any touchscreen device, not a hidden
   power-user gesture. If the panel truly isn't touch-capable or nobody
   touches it (timeout), fall back gracefully to the existing
   default-calibration behavior rather than blocking boot forever — reuse
   calibrate()'s existing timeout handling. This screen's content is the
   native-drawn card described in the Visuals section above, final, not a
   placeholder.

2. **Persistent on-screen hint while still uncalibrated.** For someone who
   skips or times out the first-boot flow (or is running firmware that
   shipped before this feature existed and is somehow still uncalibrated),
   show a small, unobtrusive on-screen indicator any time touch::calibrated()
   is false, reminding them how to calibrate ("HOLD SCREEN AT POWER-ON TO
   CALIBRATE" or similar, short). Should not compete with the footer/header
   rework just merged (branch hud-cleanup, now on main — read that layout
   before adding anything, don't collide with the new compact sync-time
   glyph or the reward placement). A good candidate location is wherever
   there's now free/calm space after that footer cleanup, or a subtle badge
   near the WiFi/link indicator, your call — screenshot options if unsure
   which reads better. Must disappear immediately and permanently (until
   NVS is cleared again) the moment calibration succeeds, no lingering.

3. Keep the existing opt-in hold-at-boot recalibration path for already-
   calibrated units completely unchanged — this task only affects the
   never-calibrated state, not people who want to redo an existing
   calibration.

## Constraints

- Branch: touch-onboarding, off main (already has v1.2.0 touch/events and the
  hud-cleanup footer/header rework merged in).
- This touches touch calibration UX/timing on real hardware (the exact kind
  of change the standing "hold for review, don't merge yourself" rule exists
  for) — stop at PR open, do not merge yourself. Regenerate
  tools/preview.sh PNGs for whatever can be captured statically (the
  persistent hint state, footer layout with the hint present), note clearly
  in the PR which parts need Cody's hands-on-hardware verification (the
  forced first-boot flow itself, since it depends on NVS being genuinely
  empty and on real touch input timing).
- Don't touch anything already fixed in hud-cleanup (pips, strip columns,
  diver dedup) except to make sure this doesn't visually collide with it.
