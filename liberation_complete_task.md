# Bug: a liberated planet's task card shows 0.0% LIBERATED instead of 100%

## Symptom
Cody confirmed on the physical device, after the count-task-progress-floor
fix (v1.4.1) already shipped: a Major Order liberation task
(`taskType == kTaskTypeLiberate`, goal 1) for a planet that has actually
been fully liberated still shows "0.0% LIBERATED" instead of 100%.

This is a DIFFERENT bug from the one just fixed in
count_progress_zero_task.md / PR #7 (merged, v1.4.1). That fix only
touched count-style tasks (goal > 1, `taskIsCount()` true — extraction,
eradicate, operations). This bug is in the liberate/defend path, which
never goes through `taskPercent()`/`taskIsCount()` at all.

## Root cause (confirmed against the live community API)
Liberation percentage for a liberate-style task is NOT read from the
assignment's own `progress`/`goal` fields. It comes from the *planet's*
health, fetched separately from `/api/v1/planets/{index}`:

    src/hd2_api.cpp (fetchPlanet, ~line 302-305):
      out.liberation = 100.0f * (1.0f - (float)out.health / (float)out.maxHealth);

That formula assumes health counts down from maxHealth as the planet is
pushed, reaching 0 at full liberation — true WHILE the fight is active.
But once a planet finishes flipping to human control, the live API stops
reporting it as a combat target and instead reports it fully healed:
`currentOwner: "Humans"`, `health == maxHealth` (confirmed live right now
against planet index 172, GAR HAREN — currentOwner Humans,
health:1000000, maxHealth:1000000, event:null). Health is a live siege
gauge, not a liberation record, so a resolved planet naturally reads as
"full health" again, which this formula misreads as 0% liberated instead
of 100%.

This is the mirror image of the count-task bug just fixed: there, a
resolved task's own numeric field reset to 0 when it should hold high.
Here, a resolved planet's health field resets to full when it should
still read as fully taken.

## Fix requirement
The order task itself already knows independently whether it is done:
`OrderTask.complete` is computed in hd2_api.cpp from the assignment
payload's own `progress`/`goal` (`ot.complete = ot.progress >= ot.goal`),
which for a liberate task is a completely separate signal from the
planet's health fetch and is NOT subject to this bug (progress/goal for
liberate tasks are the 0/1 or 1/1 values straight off the assignments
endpoint, already verified live and correct — task shows progress:1 for
a planet the API's own planets endpoint now reports as healed/human
owned).

So: wherever a liberation percentage is displayed or used for a task
that `taskIsLiberation(t->taskType)` is true for, if `t->complete` is
true, treat/display liberation as 100% regardless of what the fresh
planet health fetch says. Do this at render/use time (or by clamping the
value at the point it is combined with the task, whichever is the
cleaner single-point fix given how `p.liberation` currently flows) — do
NOT weaken or touch `fetchPlanet()`'s general health-based liberation
math, which is correct and necessary for planets that are mid-fight
(including ones NOT tied to a liberate-style Major Order task, e.g. the
campaign carousel cards, which have no task/complete flag at all and
must keep using the raw health-based calculation exactly as today).

Concretely, likely touch points (verify against current source, this is
guidance not a literal diff):
- `src/hud_renderer.cpp` ~line 1123-1124 (headline "%.1f%% LIBERATED"),
  ~line 1179-1181 (the liberation bar row), and ~line 1317-1326 (the
  no-order/idle or clock variant that also prints "%.4f%% LIBERATED") —
  each of these already has `t` or the relevant task in scope where they
  branch on `taskIsLiberation`; add the `t->complete` check there.
- `src/hud_renderer.cpp` ~line 258 (`cardRates()`'s rate calc feeding off
  `p.liberation`) — decide whether a completed task's rate readout should
  freeze at 0%/h ("MET"-style) rather than compute a rate off health that
  just jumped from e.g. 94% to 0%; use judgment, but do not let this path
  reintroduce a large spurious rate spike the way the count-task fix had
  to guard against for its own rate readout.
- Signature strings that feed repaint dirty-checking (e.g.
  `targetSignature()` around line 392/433, which encode `p.liberation`)
  should encode whatever the corrected DISPLAYED value is, not the raw
  fetched one, so the card actually repaints when the fix changes what's
  shown.

Do not touch: `fetchPlanet()`/`fetchCampaigns()`'s own health-based
liberation math (correct for in-progress fights and for campaign cards
with no task), the count-task floor fix from PR #7 (separate, already
correct, do not re-touch `taskPercent()`/`taskIsCount()`), the
defend-task path (`kTaskTypeDefend`, unrelated — defend already reads
`t.complete` correctly per `objectiveStatus()`), or the overlay/dismiss
logic.

## Verification
- Build clean with `python3 -m platformio run`.
- This repo now has `tools/count_floor_test.sh` (added in PR #7) as
  precedent for a synthetic no-hardware test harness compiling real model
  code against the preview host shim. Consider whether a similarly
  scoped small test/preview case is worth adding for this fix (a
  liberate task with `complete=true` and a planet payload at full
  health/human-owned should render 100%, not 0%) — use judgment on
  whether to extend that harness or the preview renderer, whichever is
  the smaller change; do not feel obligated to build new test
  infrastructure if the existing preview scenes can cover it with a
  fixture.
- Regenerate `tools/preview.sh` output if any changed rendering shows up
  in existing preview scenes.
- Confirm normal in-progress liberation (health counting down, task not
  yet complete) still renders unaffected — this must only correct the
  post-completion case, not change how a live push looks.

## Process
- Branch off main: `liberation-complete-floor`
- PR only, do not merge — Cody reviews first, same as the count-task fix.
- Update the shared Obsidian vault project note with a short summary at
  the end, per standing convention (same note used for the count-task
  fix and prior hd2-desk-monitor sessions).
