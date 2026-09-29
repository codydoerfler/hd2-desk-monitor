# Merge the per-species task-icon work (PR #10) onto the now-restyled main

## What happened
Two separate branches touched the combined-count card independently:

1. `cyd-webpage-restyle` (already merged to main, tag v1.5.1) — restyled colors/icons/masthead
   to match a reference webpage. This branch was cut from main BEFORE PR #10 existed, so it has
   no species-specific icons on the combined card rows — each row is bare text, no leading mark.

2. `mo-task-icons` (PR #10, still open, NOT merged) — added a distinct 16x16 traced icon to each
   row of the combined-count card (Agitators/Vox Engines/Obtruders/Gatekeepers), assigned by row
   position since the API doesn't expose species identity directly. This was built on an OLDER
   main, before the restyle existed.

Cody wants BOTH: the restyled palette/masthead AND the per-row species icons, together, on the
live device. Right now the live firmware (v1.5.1) only has the restyle, no row icons — that's
the actual bug he's reporting ("looks exactly like before, no icons").

## Task
Merge `mo-task-icons` onto current `main` (which already has the restyle). A trial merge attempt
already found conflicts in:
- `src/hud_icons.h` (real conflict — both branches added/changed icon table entries)
- `docs/preview_combined.png` / `docs/preview_combineddone.png` (binary, can't text-merge —
  regenerate after resolving code conflicts, don't hand-pick one side's binary)
- `docs/CONTEXT.md` / `README.md` may have auto-merged cleanly already, double check prose still
  reads sensibly where both branches added notes

## Resolution approach
- Do NOT just pick one side. The goal is icons from `mo-task-icons` drawn in the restyled palette
  (gold/faction colors from the webpage restyle), not a reversion of either branch's work.
- `hud_icons.h`: reconcile so the icon table has both the restyle's icon set changes (whatever
  entries/format v1.5.1 introduced) AND the four new per-species marks from `mo-task-icons`
  (Agitators/Vox Engines/Obtruders/Gatekeepers) coexisting.
- `hud_renderer.cpp`'s `drawCombinedRow()`/`drawCombinedCard()`: the restyle version changed the
  row's tint logic (gold instead of blue, with a comment explaining why species aren't
  faction-tinted). `mo-task-icons`'s version added drawing the row's leading mark via `taskIcon()`.
  Keep the restyle's gold tint AND add back the leading species mark per row, so a row shows:
  [species icon] ELIMINATED  x / y                                    pct%
  with the gold color scheme, hazard/hatch texturing, etc. from the restyle intact.
- Regenerate `docs/preview_combined.png` and `docs/preview_combineddone.png` (and any other
  preview this repo regenerates as a matter of course) using this repo's existing preview
  pipeline AFTER the code conflict is resolved, so the binaries reflect the actual merged state,
  not either parent's stale render.
- Keep `mo-task-icons`'s documented limitation intact (icons assigned by row position, not true
  species identity — this was a deliberate, well-reasoned scope cut in PR #10's description,
  don't relitigate it, just carry it forward with the new palette).

## What must not regress
- Everything the restyle changed elsewhere (masthead skull emblem, faction colors on
  liberation/defense screens, hazard motif, official faction icon SV► shapes) must survive
  untouched.
- Everything PR #10's description says was verified (layout checks, count-floor test, flash
  budget) must still pass after the merge — re-run `tools/check_layout.py` and
  `tools/count_floor_test.sh`, and report final flash usage (this device is flash-constrained,
  currently at 96.3%; adding 4 icon bitmaps back in cost PR #10 +256 bytes previously, expect
  something similar here — confirm it still fits).

## Branch / PR hygiene
Do the merge work on a fresh branch off current `main` (e.g. `merge-task-icons-into-restyle`),
not by force-pushing over either existing branch. Open one PR against `main`. Stop at PR open,
do not merge — Cody wants to see the result (a preview image) before it goes out as another
release.
