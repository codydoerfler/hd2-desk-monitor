# Task: show the new-Major-Order overlay on power-on and after OTA update

## Context — read first

Read the Obsidian vault note for this project (hd2-desk-monitor) before starting,
per the repo's standing convention, for any background not repeated here.

The new-Major-Order overlay (`kOverlayNewOrder`) already exists end to end:
`classifyOrderOutcome()` / `queueOverlay()` / `drawOverlay()` in `src/main.cpp` and
`src/hud_renderer.cpp` already render a full-screen gold-framed card with the real
MO title, wrapped briefing text, and (for success) reward, driven off
`MajorOrder` fields already parsed from the live API in `src/hd2_api.cpp`
(`/api/v1/assignments` — title, briefing, reward, expiration, per-task
progress/goal/planet). It already plays a clip via `playOverlayAlert()` /
`storage::kClipNewOrder` (SD card `/audio/mo_new.wav`, falling back to the
compiled alert if no card), and it is already touch-to-dismiss with a
`kOverlayFallbackMs` (one poll interval) safety-net timeout so a broken touch
panel can't strand the unit on the screen forever.

**Do not redesign or re-theme this screen.** Cody was shown a mockup and said
"nevermind, that took too long" — he does NOT want new artwork or a new layout,
he wants the existing overlay's trigger conditions widened. The screen itself
(gold frame, band, title, wrapped briefing, "TOUCH TO DISMISS") already looks
right and is already populated from the real API. Confirm this understanding
against `drawOverlay()` (`src/hud_renderer.cpp` ~line 1558) before touching
anything — if you find the existing screen materially doesn't match what's
described above, stop and note the discrepancy in the PR rather than guessing.

## What's actually missing

`orderIsNew` in `src/main.cpp` (~line 645) is deliberately gated on
`model.haveData`, specifically to suppress the overlay on the first poll after
any reboot:

```cpp
const bool orderIsNew = order.valid && model.haveData && order.id != model.order.id;
```

The comment above it explains this was intentional — "the order that was
already running when the board booted is not news." Cody is now explicitly
overriding that: he wants the new-MO overlay (same screen, same data, same
audio) to also show:

1. On genuinely new MO detection — unchanged, already works.
2. On power-on / power restore (board boots with an already-active MO still
   showing on the API — currently suppressed by the `haveData` gate above).
3. Right after an OTA update completes (the reboot that follows a firmware
   flash via `src/hd2_ota.cpp`).

Case 2 and 3 both need the overlay to show even though `model.order.id` hasn't
changed from anything (there is no "before" in RAM after a reboot) — so this
can't reuse `orderIsNew`'s change-detection logic as-is. Needs a boot-time flag
that says "show the current MO as an announcement on the next successful poll,
regardless of whether its id is new," separate from the ongoing
new-id-detection path, but reusing the exact same `queueOverlay(kOverlayNewOrder, order)`
call and rendering.

## Design

- Add a static bool (e.g. `announceOnNextPoll`, scope in `main.cpp`) set `true`
  during `setup()` under both trigger conditions below, consumed (set back to
  `false`) the first time `poll()` sees a valid order and queues it. If the
  first poll(s) after boot return no active MO (`order.valid == false`), leave
  the flag armed and keep trying on subsequent polls rather than firing on
  garbage — the goal is "announce whatever MO is live," not "announce nothing."
- **Power-on**: simplest correct trigger is "always arm on boot." Every
  `setup()` call already only happens on power-on/reset, this is not a hot
  path. Confirm this reading is correct (i.e. `setup()` never runs on anything
  other than a real boot in this codebase) before relying on it — if there's a
  soft-reset path that also hits `setup()` and would over-fire, note it and
  handle appropriately, but don't over-engineer if there isn't one.
- **OTA update completion**: needs a real signal, not an assumption that every
  boot is post-OTA. Add an NVS-persisted "last seen firmware version" string
  (`Preferences`, same pattern already used in `hud_touch.cpp` for calibration
  state — pick a sensible namespace/key, e.g. alongside or adjacent to the
  existing touch NVS usage, whichever fits the codebase's existing
  conventions better). On boot, compare stored value against `HD2_FW_VERSION`
  (already stamped from `git describe`, see `setup()` in `main.cpp`). Mismatch
  (including empty/first-ever-boot) means this boot follows a flash — write
  the new version to NVS and treat it as an OTA-triggering boot. This same
  flag naturally also covers the very first boot of a brand new unit, which is
  fine — showing "here's the current Major Order" on first power-up is
  reasonable, not a bug.
- Both power-on and OTA-update boots are covered by the same "arm on setup()"
  behavior if power-on always arms — in which case the NVS version-compare
  may turn out to be redundant with "always arm on boot" and unnecessary. Use
  judgment: if unconditionally arming on every `setup()` already satisfies
  both requirements (2) and (3) correctly and simply, prefer that over adding
  NVS plumbing that isn't earning its keep. Only add the version-compare NVS
  flag if there's a real reason power-on-always-arm isn't suffient (e.g. if
  Cody or a future maintainer would want power-on and OTA-update to ever be
  distinguishable in logging/behavior — a `Serial.printf` noting *why* the
  overlay is being forced, "power-on" vs "post-update", is a reasonable thing
  to add cheaply either way since it costs nothing and helps future debugging).
- Whichever design you land on, do not touch `orderIsNew`'s existing gate or
  behavior for genuinely-new-order-while-running detection — that logic stays
  exactly as is. This is purely an additional, separate trigger path that
  also ends at the same `queueOverlay(kOverlayNewOrder, order)` call.
- Verdict overlays (success/failure) are NOT in scope for this change — only
  the new-order announcement gets the new boot triggers. Don't add power-on
  firing for `kOverlaySuccess`/`kOverlayFailure`.

## Verification

- Trace/simulate (can't flash physical hardware from here) both new paths:
  power-on with an already-active MO on the feed, and a version-mismatch NVS
  boot, and confirm each reaches `queueOverlay(kOverlayNewOrder, order)`
  exactly once, not on every subsequent poll.
- Confirm the existing genuinely-new-MO path (`orderIsNew`) is untouched and
  still fires correctly mid-runtime when the API rotates to a new assignment
  id.
- Confirm dismiss (touch or fallback timeout) behaves identically regardless
  of which trigger raised the overlay — it already should, since all paths
  converge on the same `overlayQueue`/`model.overlay` mechanism, but check.
- Run `tools/preview.sh` (or whatever the repo's existing preview-PNG tooling
  is called — check `tools/`) to regenerate a preview of the overlay screen if
  the tooling supports simulating it, so there's visual confirmation nothing
  about the rendering itself changed.
- `pio run` must pass.
- Update README/CHANGELOG or equivalent docs only if the repo's existing
  convention does so for behavior changes like this (check how the touch
  onboarding / hud-cleanup PRs documented themselves and match that).

## Process

- Branch off latest `main`.
- Open a PR, do not merge yourself — this changes when a full-screen
  interrupt appears on real hardware in the field, same category as the touch
  calibration and touch/swipe work that were held for review, not a pure
  CI/infra change.
- Regenerate any preview PNGs the repo's tooling produces for visual review.
- Write a session summary to the Obsidian vault per the repo's established
  convention.
