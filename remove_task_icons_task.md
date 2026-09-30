# Remove the per-species task-icon marks from the combined-count card

## What to remove
PR #10 (merged via #12) added a leading 16x16 icon mark to each row of the combined-count card
(the "ELIMINATED x / y" rows shown when a Major Order has multiple galaxy-wide kill targets).
Four marks exist: Agitators (skull), Vox Engine (broadcast mast), Obtruder (twin blades),
Gatekeeper (legged figure) — assigned by row POSITION, not real species identity, because the
API does not expose species identity for these tasks. This was a known, documented limitation
(can put the wrong mark on the wrong row if task order or composition differs from today's).

Cody wants these four marks deleted from the project entirely. Remove:
- The drawing of the mark in `drawCombinedRow()` in `src/hud_renderer.cpp` — rows go back to
  plain "ELIMINATED  x / y" captions with no leading icon, same layout as the v1.5.1 restyle
  (gold palette, hazard hatching, etc. all stay — only the per-row icon goes).
- The four icon bitmap definitions/table entries in `src/hud_icons.h` (or wherever they live
  after the merge) — Agitators/Vox Engine/Obtruder/Gatekeeper.
- `taskIcon()` or equivalent lookup function that assigns a mark by row position — delete it,
  don't just make it return nothing, since it should no longer be reachable code.
- Any generator code/assets specific to these four marks in `tools/gen_icons.py` (check for
  TASK_AGITATORS, TASK_VOX_ENGINE, TASK_OBTRUDER, TASK_GATEKEEPER style constants and their
  hand-traced grid definitions) and any reference art committed under `tools/assets/` that was
  ONLY used to trace these four icons — remove it if unused elsewhere.
- Any mention of these marks/limitation in README.md / CONTEXT.md documentation — remove the
  paragraphs describing this feature, since the feature is going away.

## What must NOT change
- Everything else from the v1.5.1/v1.5.2 restyle stays: gold accent palette, hazard-stripe
  motif, hatched progress-bar fill, Helldiver skull masthead chip, official faction icons and
  their color tinting on liberation/defense screens, faction SVG-derived bitmaps.
- Row layout/columns (caption / percentage / track) stay as they are minus the icon column —
  check whether removing the icon changes the caption's available width calc (`capW` etc. in
  `drawCombinedRow()`) and adjust so captions use the freed-up space rather than leaving a gap.
- Data fetching, parsing, taskPercent()/countWords()/count-floor logic — untouched.

## Verification
- `python3 tools/check_layout.py` passes.
- `./tools/count_floor_test.sh` passes (or repo's equivalent script name — check what exists).
- Regenerate ALL preview PNGs this repo normally regenerates (`./tools/preview.sh` or equivalent
  — check what PR #12's description referenced) so `docs/preview_combined.png` and
  `docs/preview_combineddone.png` actually show rows with no leading icon. Confirm by comparing
  before/after — only the combined scenes should differ, every other preview should be
  byte-identical (same check prior PRs in this repo have done).
- `python3 -m platformio run -e hosyond-esp32-32e` builds clean. Report final flash usage —
  removing 4 icon bitmaps should free some bytes back, confirm and report the delta.

## Branch / PR hygiene
New branch off current `main` (e.g. `remove-task-icons`). Open one PR. Stop at PR open, do not
merge — Cody reviews and merges himself.
