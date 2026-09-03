# Add per-enemy icons to the combined MO card's task rows

Branch off main (mo-overhaul-combined-card / PR #9 is already merged as
v1.5.0). PR only, do not merge.

## What's wrong

The combined-card feature shipped in PR #9 (`drawCombinedRow()`,
hud_renderer.cpp) renders every row as plain text: a caption
("ELIMINATED  17,842,731 / 25,000,000"), a percentage, and a progress bar.
No icon. The reference screenshot Cody built this feature from
(`mo_task_icons_reference.jpg`, in repo root — same image as
`mo_overhaul_reference.jpg`) shows each row with its own small circular
medallion icon on the left identifying which enemy that row's kill count is
for. Right now every row on a same-type order (all four tasks in the current
MO are `type: 3`, eradicate) reads as generic "ELIMINATED" with nothing to
tell the rows apart visually — which is what Cody is flagging. This is a
follow-up to PR #9, not a revert of it.

## Source art

Four new source crops, extracted directly from `mo_task_icons_reference.jpg`
at as-clean-as-possible resolution, already committed to
`tools/assets/`:

- `task_icon_agitators_source.png` — Agitators row icon (skull/crossbones
  mark)
- `task_icon_voxengine_source.png` — Vox Engines row icon (central emblem
  with a vertical spike above, horizontal bar below)
- `task_icon_obtruder_source.png` — Obtruders row icon (two upward
  triangular/wing shapes flanking a vertical bar)
- `task_icon_gatekeeper_source.png` — Gatekeepers row icon (mech-head shape
  with a horizontal band above)

These are small (260x300) upscaled crops from a compressed screenshot, same
starting quality as every other traced icon already in this project
(`tools/assets/faction_*_source.png`, `seaf_emblem_source.jpg`,
`skull_wings_source.jpg` were all sourced the same way — a reference image
traced down to a clean 1-bit or RGB565 bitmap). Trace these the same way:
clean up jpeg noise/color banding by eye against the source, don't try to
upscale-and-guess detail that isn't really there in a compressed screenshot.
The circular medallion frame itself doesn't need to be traced — the existing
row layout doesn't have room for a full badge-in-a-ring per row; extract just
the inner symbol so it reads at small size (~16-18px square, to fit inside
`moCombCapH`, the same row height text already fits in).

## What identifies which icon a task needs

None of it comes from the API today. `hd2_api.cpp` only extracts
`planetIndex` (valueType 12) and `goal` (valueType 3) out of a task's
`values`/`valueTypes` arrays — nothing about which enemy/species a kill task
targets. Checked live against `/api/v1/assignments`: all four tasks on the
current MO carry identical `type: 3`, and the only field that differs between
them besides progress/goal is one raw value (index 3 in `values`, no
valueType tag identifying it as anything) — e.g. 1371180916, 4066406510,
3621116014, 1870840792 for Agitators/Vox Engine/Obtruder/Gatekeeper
respectively, in that task order. These look like faction/species hash IDs
but nothing in this codebase or the community API docs currently maps them to
names — that mapping does not exist yet anywhere in this project.

Given that, do not try to build a live species-ID lookup this pass — there is
nothing to key it off reliably, and guessing at an ID scheme risks being
wrong when the next MO's task order or values shift. Instead:

- Keep `countWords()`'s existing text working exactly as it does now
  (ELIMINATED/EXTRACTED/etc, keyed off `taskType`) — that part is correct
  and unrelated to this fix.
- Add the four new icons as an ordered set matched to task *position* within
  the combined card for now (row 0 gets icon 0, row 1 gets icon 1, etc.),
  which is what the reference screenshot and the live order both show today
  (Agitators, Vox Engine, Obtruder, Gatekeeper in that fixed task order) —
  flag this positional assumption clearly in a comment and in the PR body as
  a known limitation, since a future MO with different tasks in a different
  order would show the wrong icon next to the wrong row. This is an
  intentional scope cut, not an oversight — get real icons on screen now
  rather than blocking on an ID scheme that doesn't exist in this codebase
  yet.

## Where to wire it in

`drawCombinedRow()` in hud_renderer.cpp (the function this task is fixing) —
draw the row's icon to the left of the caption text, inside `capW`'s
reserved space (shrink the caption's usable width to make room, the same way
the percentage column already carves out `moCombPctW`). Follow the existing
icon convention: add the four new bitmaps to `hud_icons.h` (or a new
sibling header if that file is getting large — check its current size and
use judgement) with the same `constexpr int16_t <name>W/H` + `PROGMEM` array
pattern every other icon there uses, generate them through whatever the
existing tracing pipeline is (check `tools/` for how `faction_*_source.png`
files got turned into `hud_faction_icons.h` / `hud_icons.h` entries — likely
a Python script; use it rather than hand-writing bitmap arrays).

## Verify

- Render the `combined` and `combineddone` preview scenes
  (`tools/preview.sh combined` / `combineddone`) and confirm all four rows
  show a distinct icon, not the same one repeated.
- Compare the rendered previews against `mo_task_icons_reference.jpg`
  side by side — icons should be recognizably the same shapes, not a
  loose reinterpretation.
- Build clean on the existing toolchain, note flash size delta (four new
  small bitmaps).
- Don't touch `countWords()`, the percentage math, the count-progress floor,
  or anything else PR #9 already shipped correctly — this is additive icon
  art only.
