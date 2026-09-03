# Major Order screen overhaul

Four related fixes/changes to how the active Major Order is shown. Branch off
main. PR only, do not merge. Standard Obsidian summary convention (read the
relevant project note before starting, write a session summary back after).

## 1. Briefing text is cut off

The "NEW MAJOR ORDER" overlay (drawOverlayAnnouncement / the `!verdict`
branch around hud_renderer.cpp:1932) wraps `o.briefing` into a fixed
`ovlBriefMax` lines and silently drops anything past that. `o.briefing` is
already parsed in full in hd2_api.cpp (`out.briefing = a["briefing"]`) — this
is a rendering cap, not a data gap. Fix so the full briefing text is reachable
on screen: either grow the wrap area/line count so realistic briefings fit
without clipping, and/or make the briefing scrollable within its region if a
long one still cannot fit in the space available. Whatever approach, verify
against a genuinely long real briefing (not just the current short one) so
"fits today" isn't mistaken for "fits".

## 2. No way to reopen the MO screen — add a small Super Earth logo button

Right now `kOverlayNewOrder` fires once when a new order arrives (or once on
boot, see mo-boot-overlay branch), gets dismissed by any touch
(main.cpp ~1044), and there is no way to bring it back — it's gone until the
next order change or reboot.

Add a small persistent button in the header area, visible only when
`m.haveData && m.order.valid` (an MO is currently active). Icon: reuse the
existing SEAF emblem bitmap (`icons::emblemLarge`, hud_icons.h) — do not
generate new art; scale/crop or draw a cropped region if the header's 15px
height doesn't fit the full 39px glyph, whichever reads cleanest at that
size. Tapping it reopens the same `kOverlayNewOrder` briefing/objective/reward
overlay that fires on a new order, current order's data, no special-cased
copy. Wire it into hud_touch's tap handling — this is the first on-screen
tap target in this codebase (touch has so far only been swipe-left/right +
"any touch dismisses overlay"), so add real hit-testing against the button's
screen rect, not just gesture type. Keep it out of the way of the existing
swipe gesture area/carousel pips.

## 3. Reward/impact number placement is off

On the current per-task card layout (drawCard/drawStrip in
hud_renderer.cpp), whatever renders each task's reward or percentage figure
is positioned awkwardly. This is superseded by item 4 below — the four
separate per-task cards are being replaced with one combined card. Carry the
per-task percentage into that new combined card's layout (see item 4) with
deliberate, readable placement next to each task's name — this is where the
"weird spot" complaint gets resolved, not as a patch on the old per-task
card.

## 4. Combine count-style MO tasks into ONE card, not one per task

Reference: attached screenshot of the in-game MO screen. Current MO example
(verified live against api.helldivers2.dev, id 3038612729): 4 kill tasks —
Agitators 25,000,000 / Vox Engines 1,000,000 / Obtruders 50,000,000 /
Gatekeepers 250,000. Confirmed this MO has no per-task planet — it's
galaxy-wide, matching `taskIsCount()` + `fallbackName = "GALAXY-WIDE"` in the
existing card code.

Right now the carousel (drawCard, `activeTask(m, _pageIdx)`) draws one task
per page — a full page per kill-count target. Cody's ask: when an order's
tasks are all count-style with no distinct planet each (i.e. the
"galaxy-wide, multiple kill targets" shape this MO is), collapse them into a
single card/page that lists all tasks together — one row per task (icon or
name, progress bar, "current / goal" figures, percent complete), stacked in
one screen, instead of a full-page carousel slide per task. This does not
apply to liberation/defense tasks tied to a specific planet — those keep
their existing one-task-per-page card. Scope the change specifically to
count-style tasks sharing one order (`taskIsCount(*t)` true for the set),
which is the existing test already used to branch this exact case elsewhere
in the file.

Per-task percentage on each row: there is no per-task reward-split field in
the assignment API (checked live — `reward`/`rewards` on the assignment is a
single order-level amount, no per-task breakdown). The in-game screenshot's
"REWARD IMPACT %" per row is actually just each task's own progress/goal
percentage (verified against live numbers: 17,842,731/25,000,000 ≈ 71%,
348,672/1,000,000 ≈ 35%, 29,763,431/50,000,000 ≈ 60%, 143,289/250,000 ≈
57% — matches the screenshot exactly). Use `taskPercent(*t)` (already exists)
for that figure, labelled honestly for what it is — do not invent a reward
split that doesn't exist in the data.

Carousel/paging: this combined card becomes one page in the existing
carousel (pageCount/_pageIdx machinery), replacing what would otherwise be N
separate count-task pages for the same order. Existing count-progress-floor
logic (per-task high-water-mark, main.cpp:730) still applies per task inside
the combined card — don't bypass it.

## General

- Small header button in item 2 is new interaction surface — keep it minimal
  and consistent with the existing visual language (theme:: colors, existing
  font constants), not a new UI style.
- Don't touch fetchPlanet()'s health math, the liberation-complete floor, or
  the count-progress floor logic already shipped (PRs #7/#8) — this task is
  additive/layout, not another progress-correctness fix.
- Build clean on the existing toolchain, note flash size delta if
  significant (new button hit-testing + combined card layout, no new bitmap
  art expected beyond reusing emblemLarge).
