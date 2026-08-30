# Bug: count-style Major Order task shows 0.0% once it completes

## Symptom
Cody (device owner) observes that when a count-style task within a Major
Order (goal > 1 — extraction/eradicate/operations types, see
`taskIsCount()` in src/hd2_model.h) finishes, its card/headline flips to
showing `0.0%` instead of holding at 100% / a completed state. This
happens on the live device, not just in theory.

## Root cause (confirmed against the live community API)
`hd2_api.cpp` parses `ot.progress` fresh every poll straight from the
assignment payload's `progress[i]` array entry, and `ot.goal` fresh from
`values`/`valueTypes`. Nothing caches or carries forward the last-known
progress. `taskPercent()` in `hd2_model.h` is a pure function of
`ot.progress / ot.goal`.

The community API (api.helldivers2.dev) is known to reshuffle/reset a
count task's numeric fields around the point it resolves — a completed
task can report back with `progress` reset to 0 (or otherwise
non-monotonic) while `goal` is still intact, or vice versa. Since this
client treats every poll's payload as ground truth with no floor, that
reads as the task's progress falling off a cliff to 0% right when it
should read 100%/complete.

This is NOT the same bug as the 2026-08-15 fix (that one was about
`progress > 0` alone wrongly implying completion, and about unknown task
types not getting proper progress/labels — both already fixed and correct
today). This is specifically: a task that WAS legitimately progressing
and reaches real completion, then regresses to 0% on a later poll because
the upstream field reset.

## Fix requirement
Once a count-style task (`taskIsCount(t)` true) is observed complete
(`t.complete == true`, i.e. `progress >= goal` on some poll), never let
its displayed percentage/progress regress below 100% / below its last
known good value for the remainder of that same Major Order's lifetime
(keyed by order id, same invariant already used by `model.history` /
`model.historyOrderId` — reuse that pattern, don't invent a second one).

Concretely:
- Track, per task slot, the highest `progress` (or percent) legitimately
  observed for that specific task (matched by order id + task index +
  same `goal`/`taskType`, same guard already used in
  `rollRateHistory()`'s count-progress loop in main.cpp — a task edited
  in place or a goal revised mid-order should NOT inherit the old high
  water mark).
- When computing what to render (headline, progress bar, caption "X / Y",
  ETA), if the freshly parsed value would go backwards from that high
  water mark, use the high water mark instead. Do not silently drop or
  hide the discrepancy — a code comment explaining why is fine, this is a
  known upstream API quirk, not a client bug to hide.
- Do not touch: the existing `taskIsCount`/`objectiveStatus`/goal>1
  detection logic, the liberate/defend paths, the overlay/touch-dismiss
  logic (already fixed and merged), or the count-progress *rate* tracking
  in `rollRateHistory()` (that's for the %/h readout, separate from this
  floor-the-displayed-value fix — though check whether it needs the same
  floor fed into it so the rate readout doesn't show a huge negative
  spike at the same moment; use judgment, but the primary displayed
  percentage/caption/complete-state must never regress).
- Reset the high-water-mark tracking whenever the Major Order id changes,
  same lifecycle as `model.historyOrderId`.

## Verification
- Build clean with `python3 -m platformio run`.
- Regenerate any preview tooling (`tools/preview.sh`) if it renders count
  task cards, to confirm the card no longer shows a false 0.0% for a
  scenario you can simulate by feeding two synthetic polls (first with
  progress near goal, second with progress reset to 0 for the same task
  index/goal/order id) — write a small standalone test/harness if the
  repo doesn't already have one that can drive `HudModel` state across
  two polls without real hardware. Look for existing test infrastructure
  first before adding new tooling.
- Confirm normal forward progress (0% climbing to 100% across real polls)
  is unaffected — this must only clamp regressions, not freeze legitimate
  low readings at the start of a fresh order.

## Process
- Branch off main: `count-task-progress-floor`
- Commit with a clear message referencing this being a display-floor fix,
  not a re-litigation of the 2026-08-15 unknown-task-type fix.
- PR only, do not merge — Cody wants to review since this changes how
  progress state persists across polls.
- Update the shared Obsidian vault project note
  (Projects/hd2-desk-monitor or similar under the CLD vault, follow
  whatever convention prior sessions used) with a short summary at the
  end, per standing convention.
