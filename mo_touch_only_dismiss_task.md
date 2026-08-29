# Bug: Major Order overlay auto-dismisses on a timeout instead of staying up until touched

## Symptom
Cody reports that when a new Major Order comes in (or the monitor reboots for
any reason), the full-screen "NEW MAJOR ORDER" overlay is not staying up until
he dismisses it by touch, as intended. It is going away on its own.

## Root cause
In `src/main.cpp`, the overlay dismissal logic in `loop()` has two ways to
clear an overlay:

```cpp
const bool acknowledged = (gesture != touch::kNone);
const bool timedOut = (uint32_t)(nowMs - overlayShownMs) >= kOverlayFallbackMs;
if (acknowledged || timedOut) {
  ...
  model.overlay = kOverlayNone;
  ...
}
```

`kOverlayFallbackMs` is defined as `HD2_POLL_INTERVAL_S * 1000UL`, i.e. 300,000ms
(5 minutes). This means ANY overlay (new order, success, failure) silently
auto-dismisses after 5 minutes even if nobody has touched the screen. This
directly contradicts the intended behavior: the new-MO overlay (and the
boot-triggered announcement added in the mo-boot-overlay PR, #5) is supposed to
stay up until dismissed by touch, full stop. If Cody isn't standing at the
desk when a new order drops (or when the unit reboots), the overlay clears
itself before he ever sees it — which is exactly the bug he's reporting.

This was a deliberate fallback added defensively (in case a broken touch
panel would leave the unit stuck forever showing a stale overlay), but it's
now confirmed to be actively wrong for the intended UX. Cody wants strictly
touch-only dismissal.

## Fix required
Remove the timeout-based auto-dismiss for overlays entirely. The overlay
(`kOverlayNewOrder`, `kOverlaySuccess`, `kOverlayFailure` — all of them, this
is one shared dismissal path) must remain on screen indefinitely until a
touch/swipe gesture is received. Do not introduce any other automatic
dismissal condition (e.g. next poll, next order change, elapsed time).

Concretely:
- Remove `kOverlayFallbackMs`, `overlayShownMs`'s use for timeout comparison,
  and the `timedOut` branch/variable in the dismissal block in `loop()`.
- `overlayShownMs` may still be needed elsewhere (e.g. for sound timing) —
  check before deleting it outright; only remove the timeout-comparison use.
- Dismissal becomes: `if (acknowledged) { ... }` only.
- Check whether `overlayShownMs` becomes fully unused after this change; if
  so remove it too, and re-check that nothing else (e.g. hud rendering) reads
  it for e.g. a progress bar / countdown visual, since removing that visual
  cue (if one exists) is in scope too if it currently implies the overlay
  will time out.
- Do not touch the trigger logic (orderIsNew, announceOnNextPoll,
  announceBootOrder, verdict classification) — this task is scoped purely to
  the dismissal condition once an overlay is already showing.
- Update any comments that explained/justified the old timeout behavior so
  they reflect the corrected touch-only design (the existing comment block
  above the dismissal code discusses "acknowledged || timedOut" and needs to
  be rewritten to describe the new touch-only behavior, not just have code
  deleted out from under it).

## Verification
- `pio run` must succeed cleanly, report final flash usage percentage.
- Confirm via code read (this can't be tested on real hardware from your
  side) that there is no remaining path that clears `model.overlay` other
  than a touch gesture.
- Regenerate preview PNGs if the existing tooling in this repo does that as
  part of a change (check for a script like check_layout.py or similar used
  in prior PRs before skipping this).

## Process
- Branch off main: `mo-overlay-touch-only-dismiss`.
- Commit this task file at repo root (follow the existing convention in this
  repo of committing task files alongside the PR, seen in prior PRs like
  mo_overlay_art_task.md, touch_screens_restyle_task.md).
- Open a PR. Do NOT merge — this changes on-device interrupt/dismissal
  behavior, Cody wants to review.
- Write a concise Obsidian vault summary of the root cause and fix
  (per the existing ~/.claude/CLAUDE.md convention for this repo).
