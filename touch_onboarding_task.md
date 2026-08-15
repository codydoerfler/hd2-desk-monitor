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

## Visuals — DEFERRED, do not design custom art

Cody is providing the actual graphics for the first-boot calibration prompt
himself (in progress, not delivered yet). Do NOT invest effort drawing custom
chrome, icons, or polished layout for that full-screen prompt. Build the
logic/plumbing/state-machine now with placeholder text only (plain
textBox/println-style, whatever is fastest to write), structured so the
placeholder is trivially swappable for a real image later — e.g. a single
clearly-named draw function/call site for the prompt's visual content, not
visuals scattered inline across the flow logic. Leave a `// TODO: replace
with Cody's supplied art` comment at that call site. The persistent
uncalibrated hint (item 2) is small/textual by nature and not part of what
Cody is designing — that one can be finished normally, text is fine and
expected there, no placeholder needed.

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
   calibrate()'s existing timeout handling. Per the deferred-visuals note
   above, this screen's content is placeholder text for now, not final art.

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
