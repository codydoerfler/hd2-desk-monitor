# HUD cleanup: pip advance, redundant DIVERS, dead rate columns, footer layout

Three related visual/UX fixes to src/hud_renderer.cpp (and hd2_model.h/main.cpp
if state plumbing is needed). Verify each with tools/preview.sh before/after,
regenerate PNGs for every screen you touch. This branch should be safe to
merge automatically once green per standing instruction (screenshot-verified,
not a hardware-risk change like the touch work) — go ahead and merge to main
yourself once you've confirmed the renders look right, no need to stop at PR
open for this one.

## Background

Real hardware reports on a live LIBCON 3 Major Order with 3 tasks:

1. The carousel page-indicator pips (drawStatusHeader, the small squares next
   to the LIBCON chip) never appear to move — always looks like the same
   state regardless of which page/task is showing. Expected behavior per the
   code's own comment is the active page's pip fills gold while the rest stay
   hollow outline, and it should visibly shift as _pageIdx advances via the
   auto-advance timer or swipe. Find out why it looks static on hardware:
   check that _pageIdx is actually changing and that drawStatusHeader is
   called with the fresh index every card draw, not a stale/cached page
   value. Fix whatever is actually wrong. If on inspection you find the pips
   ARE correct and the appearance of staying still is just because dot size/
   gap is too subtle at typical viewing distance, say so and make them more
   visually distinct (bigger delta between filled/hollow, e.g. size or a ring)
   rather than leaving it ambiguous.

2. The bottom stat strip (drawStrip: SHARE / DIVERS / PUSH /H / REGEN /H) and
   the footer (drawFooter) both show a diver headcount for the same planet —
   literally the same number in two different places on screen at once. Drop
   the DIVERS reading from the footer entirely (the strip's DIVERS column is
   the one to keep — it lives next to SHARE which needs the same planet
   record anyway). This frees up the whole left side of the footer row.

3. PUSH /H and REGEN /H in the stat strip are frequently blank ("--") or read
   as near-zero, because (confirmed in existing code comments) they only have
   data when the active task carries a planet record with a real rate/regen
   value — count-style tasks (kill counts, sample extraction, anything that
   isn't liberate/defend) have no such planet attached, so these columns are
   structurally always empty for that Major Order type. Don't show them for
   count-style tasks. Either: (a) collapse the strip to 2 columns (SHARE,
   DIVERS) when there's no planet/rate data so the row doesn't waste space on
   two guaranteed dashes, or (b) if 4 columns is load-bearing for layout
   consistency across screen types, replace PUSH/REGEN with something that
   actually has data for count-style tasks (e.g. progress rate/ETA if
   derivable from the assignment payload, or just drop to the 2-column
   layout — your call on which reads cleaner, screenshot both if unsure and
   pick the better one). Liberation/defense tasks that DO have a planet
   record keep PUSH/REGEN exactly as today, unchanged.

4. Footer layout rework. Current footer (drawFooter) is one row: diver count
   on the left (being removed per #2), reward medal count centered, and
   "SYNCED HH:MM" / "STALE HH:MM" right-aligned in a fixed 150px block. Cody's
   read: the synced-time block spends too much horizontal space for what it
   conveys (a clock reading), and now that DIVERS is dropped from this row,
   there's freed space to use better. Rework so:
   - Medal reward icon + count keeps a clear, prominent placement (arguably
     the most useful piece of info in the footer — don't shrink or bury it).
   - Synced/stale time becomes visually lighter/smaller — it's a staleness
     indicator, not a headline number. A compact "HH:MM" with a small
     icon (clock or dot, reuse an existing icon if one fits, otherwise a
     plain small glyph) is enough; it doesn't need "SYNCED"/"STALE" spelled
     out in full-size body font. Keeping the color distinction (stale =
     muted/warning color vs synced = normal) is still useful, keep that.
   - With both DIVERS-redundancy and the time block shrunk, use the reclaimed
     space usefully rather than leaving it as dead padding — e.g. give the
     medal reward more visual weight (bigger icon/number), or if there's
     another genuinely non-redundant, always-relevant reading worth surfacing
     (check hd2_model.h / HudModel for anything else already computed but not
     shown anywhere on the card — don't invent new API calls, only use data
     already fetched), use it there. If nothing else useful exists, it is
     fine to simply let the footer row be visually calmer/less cluttered
     rather than force-filling it — don't add clutter for its own sake.
   - Apply consistently across every screen that uses drawFooter (card mode
     and idle/campaign mode both call it — check both).

## Constraints

- Don't touch the touch/swipe/overlay code from the just-merged v1.2.0 work,
  this is a separate concern (header pips + footer/strip layout only).
- Regenerate tools/preview.sh PNGs for every screen type affected (both card
  types: liberation/defense with planet rate data, and count-style with none)
  so the diff is visually reviewable, not just described.
- Branch: hud-cleanup, off main (already includes v1.2.0 touch/events work).
- Keep changes scoped to rendering/layout — no new data sources, no new API
  fields beyond what HudModel already carries.
