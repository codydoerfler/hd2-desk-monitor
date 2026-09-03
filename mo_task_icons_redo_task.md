# Redo the task icon art: v1 does not resemble the reference

## What's wrong

PR #10 (branch mo-task-icons, commit 6e6adc4) added four task icons via
`tools/gen_icons.py` — an automated threshold + connected-component blob
extraction. Cody reviewed the rendered result against
`mo_task_icons_reference.jpg` side by side and rejected it: the shapes
produced do not resemble the medallions in the reference at all. This is not
a resolution problem — it's an extraction-method problem. The automated
threshold/blob approach throws away real shape information that a careful
manual trace would keep.

Do not reuse `gen_icons.py`'s output. Re-derive the four icons from scratch
by tracing the source crops by eye, pixel by pixel, the same way
`tools/assets/skull_wings_source.jpg` → the header skull, or
`tools/assets/faction_*_source.png` → the faction badges were traced
(check git history / any existing trace notes for those if the process
isn't obvious from the current file layout).

## Source material — read this constraint first

`mo_task_icons_reference.jpg` (477x318) is the only source. Each medallion
occupies roughly a 26x30px region in that image — this is a photo of a
phone/monitor screen, further JPEG-compressed. That is the actual ceiling of
detail available; upscaling it does not recover more information, it only
blurs. Four fresh, larger crops of each medallion are committed at
`tools/assets/task_icon_{agitators,voxengine,obtruder,gatekeeper}_source.png`
(260x300, already upscaled from the same 26x30px source region — this is as
good as the source gets).

Given that ceiling: do not attempt a detailed/shaded portrait render. The
reference's medallions themselves show recognizable creature/machine
silhouettes (a skull for Agitators, a stepped mechanical emblem for Vox
Engines, twin wing/blade shapes for Obtruders, a helmeted head shape for
Gatekeepers) — the goal is a clean, readable icon that captures that
silhouette and color pattern (gold/amber marks against a dark ring), not a
literal small-scale reproduction of shading detail that was never really
there at this source size to begin with. Readable and recognizably matching
beats an attempt at photorealism that isn't achievable from this source.

## What to actually do differently

1. Open each of the four source crops and look at them directly (they're
   already committed, don't re-crop from the original screenshot — the
   existing crops are the right ones, confirmed against pixel-sampled row
   centers).
2. Manually trace the silhouette: identify by eye which pixels belong to
   the mark itself vs. background/ring/noise, at whatever intermediate
   resolution is comfortable to work at (e.g. block out a 32x32 or 24x24
   grid by hand/eye, then downsample to the final ~16x16 icon size used in
   `moCombIconW`/`moCombIconH` in config.h), rather than trusting an
   automated threshold to make that call. A script CAN still be the
   mechanism that turns a hand-specified pixel grid into a PROGMEM array
   (that part of the old pipeline was fine) — what has to change is who
   decides which pixels are "on": a human looking at the image, not a
   luminance cutoff.
3. After tracing, render the `combined` preview scene
   (`tools/preview.sh combined`) and compare the four icons against
   `mo_task_icons_reference.jpg` side by side yourself before calling this
   done. If a shape doesn't read as the right silhouette at a glance,
   redo it — don't ship a result you haven't visually checked against the
   source.
4. Keep everything else from PR #10 as-is (row layout, column math, gold
   tint, positional-mapping limitation/comments, countWords/percentage
   logic) — only the icon bitmaps themselves are wrong and need redoing.

## Scope

Branch: stay on `mo-task-icons` (already exists, has PR #10 open against
it) — commit the fix as a new commit on the same branch, same PR, do not
open a second PR. PR only, do not merge.

Rebuild `src/hud_icons.h`'s four new entries in place (or wherever v1 put
them). Update `tools/gen_icons.py` if it's kept at all — if the fix is done
by hand-authoring pixel grids rather than through the automated script, it's
fine to leave the script as dead tooling or delete it, whichever keeps the
repo cleaner; use judgement.

Verify: `check_layout.py` and `count_floor_test.sh` still green, build
clean, note the flash delta. Confirm in the PR update comment that the new
icons were visually compared against the reference image, not just that the
pipeline ran without error.
