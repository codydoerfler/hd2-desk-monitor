# Project Context — Read This First

This file exists so any assistant (Claude Code, Claude desktop, a fresh
session, whatever) can get fully oriented on this project without needing
prior chat history. Read this, then README.md for full technical detail.

## What this is

A standalone desk display that shows the live Helldivers 2 Major Order
(target planet, liberation %, time remaining, player counts) on a small
ESP32-driven color LCD. No buttons; touch is swipe-to-page and tap-to-dismiss
only. WiFi + USB-C powered. Polls the community API at api.helldivers2.dev
every 5 minutes and renders a Super Earth command-terminal style HUD.

Cards are art-led: the biome plate fills the upper half with the planet's
identity set over it, progress tracks beneath, a stat strip (four values on a
planet objective, three on a count one), then the footer. An active Major Order owns the screen (its targets are the only
carousel pages); with no order, the five busiest liberation campaigns take
over. Pages advance on a 7s timer, or on a swipe (which restarts the timer).

Two events take the whole panel: a new Major Order arriving, and the verdict
when one ends. They interrupt the carousel and stay up until tapped, with a
one-poll-interval timeout so a dead panel cannot strand the HUD on them. A
boot — power-on, power restore, or the reboot after an OTA — also raises the
new-order screen once, for whichever order is already live. It drives the
board's speaker on all of those events and after an OTA update, from SD-card
clips where a card is present.

## Where it lives

- Repo: `~/dev/hd2-desk-monitor` on the Mac Mini (Tailscale 100.64.112.105,
  SSH user `codydoerfler`), pushed to github.com/codydoerfler/hd2-desk-monitor.
  Tagging `v*` there is what ships an OTA to every unit — see the release
  notes in README.
- Board: PlatformIO project, env `hosyond-esp32-32e`.
- Hardware: Hosyond 4.0" ESP32-32E display module (LCDwiki E32R40T),
  ESP32-D0WD-V3, ST7796S panel, 480x320 landscape, no PSRAM, 4MB flash.
  XPT2046 touch on the panel's HSPI bus (CS 33, PEN/IRQ on IO36, unused).
  Audio: GPIO26 internal DAC -> FM8002E amp, GPIO4 enable (active low).
  There is an onboard MicroSD slot on the *other* SPI peripheral (VSPI: SCK
  18, MISO 19, MOSI 23, CS 5) -- no wiring needed, and no contention with the
  display, which owns HSPI. Optional at runtime; see README's SD card section.

## Source layout

- `src/main.cpp` — app entry, WiFi setup (WiFiManager), poll loop.
- `src/hd2_api.cpp` / `.h` — HTTP calls to api.helldivers2.dev, JSON parsing.
- `src/hd2_model.h` — data model for assignments/war/planets.
- `src/hud_renderer.cpp` / `.h` — all drawing code, the actual HUD layout
  (largest file). Screen states: order card, campaign card, idle, stale/
  offline, boot, plus three full-screen event overlays (new order, order
  complete, order failed). Both card types share drawArtBand()/drawStrip()/
  drawBarRow(); see the `Cards` block in config.h for the band geometry and
  why the art height differs between them. The overlays share none of it —
  they take the raw panel, by design.
- `src/hud_audio.cpp` / `.h` — the speaker. Amp enable + blocking 8-bit DAC
  playback; knows nothing about the model, same isolation the renderer keeps.
- `src/hud_audio_clip.h` — generated 8kHz PCM clip (~30KB PROGMEM).
- `src/hud_storage.cpp` / `.h` — the MicroSD slot. Mount, RIFF/WAVE parse,
  block-streamed playback through hud_audio's stream API. Everything here is
  best-effort: no card, or a missing file, is a logged note and nothing more.
  New assets belong here rather than in PROGMEM — flash is the binding
  constraint on this board and the card is not.
- `src/hud_touch.cpp` / `.h` — the XPT2046. Borrows TFT_eSPI's instance for
  bus arbitration and raw conversions, and replaces its validation layer:
  TFT_eSPI's validTouch() demands two samples agree within 20 ADC counts,
  which is a stationarity test a moving finger cannot pass, and is the actual
  reason the earlier attempt read as "calibration will not hold". Produces
  tap/swipe-left/swipe-right; calibration (four corner targets) is stored in
  NVS and entered by holding the panel while powering on — or, on a unit that
  has never been set up, by the first-boot prompt (see below).
- `src/hud_icons.h` — generated 1-bit icon bitmap tables (glyphs for stat
  tiles, crest/skull mark, shield icon, hazard chips, etc).
- `src/hud_faction_icons.h` — generated full-colour (RGB565) faction badges
  for Automaton/Terminid/Illuminate. Separate from hud_icons.h because these
  three carry their own colour rather than being 1-bit masks tinted by the
  caller — see the file's header comment for why.
- `src/hud_biomes.h` — generated biome backdrop strips for the campaign
  screen, one per planet terrain type.
- `src/hud_mo_art.h` — the Major Order overlay art plate: the flag and the
  orbital city, RGB565, 120x160 stored and drawn at 2x. 38.4KB, the single
  largest asset in the image. One plate serves all three overlays; the
  renderer grades it per screen (`ArtGrade` in `hud_renderer.cpp`).
- `src/hud_header_art.h` — the Earth/gold-sweep wash that backed the old
  objective bar. **No longer referenced**: the card redesign dropped the bar,
  and with it ~12.7KB of PROGMEM. The file and tools/gen_header_art.py are
  still present but unused; see git history if that look is ever wanted.
- `src/hud_fonts.h`, `src/hud_font_anton.h` — font tables.
- `src/config.h` — tunables, most also exposed as `platformio.ini` build
  flags (poll interval, API contact header, etc).

## Off-target preview tooling (important — use this, don't flash blind)

`tools/preview.sh` compiles the firmware's own renderer on the host (not the
ESP32) and rasterizes each HUD screen state to PNG. This is the primary way
to check a layout/art change before flashing real hardware. Scenes: `boot`,
`touchprompt`, `touchsuccess`, `idle`, `liberation`, `defense`, `invasion`,
`stale`, `uncalibrated`, `campaign`, `count`, `extraction`, `neworder`,
`success`, `failure`, `carousel` (`success` is the Major Order verdict
overlay; `touchsuccess` is the calibration confirmation). All but the last
are shot onto a cleared screen; `carousel` advances a page and repaints
incrementally, which is the path the device actually lives on. Outputs land at
repo root as `preview_<scene>.png` and are **gitignored there**; the copies a PR
is read against live in `docs/`, so copy them across after regenerating.
Regenerate after any renderer/icon change and look at them before calling a
visual change done.

There is also `docs/icon-audit/` — a standalone HTML report
(`icon_audit.html`) that lays out every icon/mark in the project at a
readable size with notes, generated as a one-off review tool while auditing
the icon set. It is not wired into `tools/preview.sh` and has no regenerate
script; treat it as a snapshot from when it was made, not a live view.

Icon/art generator scripts (Python, in `tools/`):
- `tools/gen_icons.py` — generates `src/hud_icons.h`. Contains the `crest()`
  function (scene-band corner icon) among others.
- `tools/gen_header_art.py` — generates `src/hud_header_art.h` SEPARATELY
  from gen_icons.py. Now only emits the header-bar background wash
  (`headerBg`) — see the source layout note above on why the old title-bar
  badge/skull is gone.
- `tools/gen_faction_icons.py` — generates `src/hud_faction_icons.h`, the
  full-colour Automaton/Terminid/Illuminate badges. Source art is
  `tools/assets/faction_*_source.png` (official insignia, reduced rather
  than redrawn).
- `tools/gen_biomes.py` — generates `src/hud_biomes.h`, the campaign-screen
  terrain backdrops, from `tools/assets/biomes/*.webp`.
- `tools/gen_mo_art.py` — generates `src/hud_mo_art.h` from
  `mo_new_reference.jpg` at the repo root. The crop and the half-size storage
  are both deliberate; the docstring carries the arithmetic for why three
  plates (or one full-size one) do not fit the app slot.
- `tools/gen_anton_font.py` — generates the Anton display font table.
- `tools/gen_audio_clip.py` — generates `src/hud_audio_clip.h` from
  tools/assets/hellpods_source.mp3 via macOS `afconvert`. `--preview` dumps an
  ASCII waveform.
- `tools/check_layout.py` — layout/bounds sanity checks.
- `tools/assets/` — source art (e.g. `seaf_emblem.jpg`, skull source photos).

## Current state (as of last commit, see `git log`)

Run `git log --oneline -10` for the authoritative recent history. The most
recent work (branch `mo-boot-overlay`) is two passes: when the new-order
screen appears, and then what all three overlays look like.

The restyle, second and larger:

- **All three overlays are drawn from `mo_new_reference.jpg` and
  `mo_verdict_reference.jpg`** — left text panel, angled divider, photographic
  flag-and-skyline on the right, per-screen accent (gold / sage / brick). The
  data plumbing is unchanged: same title, same wrapped briefing, same
  objectives-met count and reward, same touch-to-dismiss and same fallback
  timeout. Presentation only.
- **The art is a real photograph, and that was the whole question.** Cody's
  instruction was "as close as possible to these images", explicitly a higher
  bar than the touch-screen pass that flattened its reference to vectors. It
  fits at exactly one plate: 240x320x2 = 153,600 B against 66,939 B free,
  two half-res plates 76,800 B, one half-res plate 38,400 B. So one plate,
  graded three ways by `ArtGrade`/`gradePixel()` rather than three assets.
- **The grades were sampled, not eyeballed.** Reference means against the
  same regions of this plate: success sky (27,40,78)->(67,85,123), failure
  flag (38,48,79)->(60,56,58). Two things fell out of that. The failure
  reference is *not* a red wash — it is a heavy desaturate with a warm bias
  and the fires doing the colour. And an early "protect the flag's blue from
  the smoke" idea is unworkable and was dropped: in a blue-night photograph
  the sky is bluer than the cloth (b-r 60 against 54), so no colour test can
  separate them — and the reference lets the flag desaturate anyway.
- **The torn flag is geometry over the top**, measured off the plate rather
  than guessed. The fly edge runs off the right of the frame, so damage there
  can only read as notches in the screen border; what reads is the hem, a
  clean curve from (102,185) to (234,217) silhouetted against the horizon,
  with tatters hanging off it in the graded cloth colour.
- **The cost is 42KB and the slot is now at 98.8%** (2,006,697 of 2,031,616,
  ~24KB spare). Inside the budget, but that is the end of compiled-in art.
- **`tools/check_layout.py` grew an overlay section**, which it had none of
  before — it was passing these screens by not looking at them. It now mirrors
  the trapezoid (`edge_x()`/`text_r()`), so every row is measured against the
  width available *at its own height*, and it caught three real overruns:
  "TO BE DETERMINED", "12 OF 12 OBJECTIVES MET" and "1000000 MEDALS AWARDED",
  now "NOT YET POSTED", "12/12 OBJECTIVES MET" and "+1000000 MEDALS".

The trigger change, first and smaller:

- **A boot announces the live Major Order.** Cody's call, overriding the
  earlier design: power-on, power restore and the post-OTA reboot all put the
  current order on the panel using the existing `kOverlayNewOrder` screen,
  data and clip. Nothing about the trigger logic was touched by the restyle
  that followed it.
- **It is a separate trigger, not a loosened gate.** `orderIsNew` still reads
  `order.valid && model.haveData && order.id != model.order.id`, byte for
  byte. A reboot has no previous order in RAM for an id to differ from, which
  is the exact case that gate exists to suppress, so change detection cannot
  be made to cover this. `announceOnNextPoll` (a `static bool` in `main.cpp`,
  armed in `setup()`) is the second path; both end at the same
  `queueOverlay(kOverlayNewOrder, order)`, and the flag is cleared there, so
  the two firing on one poll is one announcement.
- **Armed unconditionally on `setup()`, which is enough.** `setup()` runs on a
  real reset and nothing else here — the firmware's single `ESP.restart()` is
  the WiFi portal timing out — so power-on and post-OTA are the same event
  from this code's point of view and need no separate detection. The NVS
  version compare the update alert already keeps (`hd2`/`fwVer`) was
  *not* extended into a second gate; it only picks the word the serial log
  prints (`power-on` / `post-update boot` / `first boot`).
- **A boot into a quiet galaxy keeps the flag armed** rather than spending it
  on `order.valid == false`. The job is "announce whatever is live", so the
  announcement waits for the poll where an order appears.
- **The two-deep overlay queue still cannot overflow.** A verdict needs a
  valid `model.order` from an earlier poll, which is precisely the state that
  has already spent the boot flag — so the worst case is still
  `[verdict, new order]`. Checked by simulation, not by inspection.
- **Known consequence, flagged for Cody in the PR and not decided here:**
  `playBootChime()` already plays the new-order clip (his earlier call), so a
  power-on with WiFi up now plays that clip twice — once at boot, once behind
  the announcement a poll later. Left alone because the chime is the only
  thing a unit can say before it has a network, and dropping it would go
  silent on exactly the boots that fail to connect.

Before that (branches `touch-onboarding` and `restyle-touch-screens`) was
first-run touch setup:

- **A genuinely fresh unit is prompted to calibrate, once.** `setup()` calls
  `runFirstBootSetupIfDue()` after `maybeRecalibrateTouch()`, so the
  hold-at-power-on path is unchanged and still takes precedence. The prompt is
  a full-screen card (`HUDRenderer::showTouchPrompt()`) with a 30 s bounded
  wait for a contact, then boot carries on regardless — a dead panel cannot
  hold the device here.
- **A calibration that succeeds is confirmed for 3 s**
  (`HUDRenderer::showTouchSuccess()`), from both routes into one — the
  first-boot prompt and the hold-at-power-on gesture. `confirmCalibration()`
  in `main.cpp` is the single place either lands. Success only: a timed-out
  calibration has nothing to confirm. Fixed wait rather than tap-to-dismiss,
  because the finger from the fourth corner is usually still down.
- **Both screens are drawn from `touch_required_reference.jpg` and
  `touch_success_reference.jpg`** — destroyer bridge, HELLDIVERS II wordmark,
  one card, hatched footer band; they share `drawCalCard()`/`drawCalCopy()`
  and differ in accent, chip, icon and copy. The earlier, simpler
  `touch_calibration_reference.jpg` is superseded. The wordmark is
  box-filtered down from the 440x172 cut `hud_icons.h` already carries for the
  boot screen rather than stored a second time.
- **"Fresh" is narrower than "uncalibrated".** A second NVS key
  (`HD2_PREFS_TOUCH_SETUP_KEY`, `hd2`/`touchSetup`) records that the prompt has
  been shown, and `touch::firstRunSetupDue()` requires no calibration blob, no
  stored blob at all, *and* no setup flag. A panel someone cleared with
  `forget()`, or whose blob a firmware update rejected, has been through setup
  already and is not stopped again. The flag is written *before* the prompt, so
  a brown-out mid-calibration cannot make every later boot stop here.
- **A footer hint while uncalibrated**, `HOLD SCREEN AT POWER-ON TO
  CALIBRATE`, 6x8 grey, in the gap the footer rework left between the reward
  and the sync clock. It is in `_footerSig`, so it clears on the first frame
  after a calibration succeeds. Grey, not amber: amber in that row already
  means the data is stale.
- **New preview scenes `touchprompt`, `touchsuccess` and `uncalibrated`**,
  plus card, band, chip, medallion and hint assertions in
  `tools/check_layout.py`.
- **The card's body copy is the HUD's only mixed-case text**, and so the only
  place drawn `TL_DATUM`. TFT_eSPI centres a free font on its ascent alone, so
  `ML_DATUM` pushes descenders out of the sprite unless the row is made a third
  taller than the type needs. Top datum puts the baseline a fixed `glyph_ab`
  (13px at 9pt) down instead. Worth knowing before adding any other mixed-case
  row.

Before that (branch `hud-cleanup`) was a header/strip/footer pass:

- **The header row repaints on its own signature now** (`drawHeader()`, called
  from every `update()`). It didn't before: only a full body repaint drew it,
  and a carousel step within one Major Order repaints just the card — so the
  pips and the objective-type word beside them sat on whatever the last full
  repaint left there. On a live three-task order the pips never appeared to
  move at all. The pips are also bigger and now filled-vs-hollow-and-smaller,
  because same-size fill alone was not a difference you could see from a desk.
- **The stat strip drops to three columns on a count objective.** PUSH /H and
  REGEN /H are planet readings; on a kill/extract/operations task there is no
  planet rate to put in them and they read `--` on every such card by
  construction. Those cards get `SHARE | DIVERS | ETA` instead, the ETA
  projected from the count rate already being measured. Planet objectives keep
  all four, unchanged.
- **The footer is the reward and the sync clock, nothing else.** The diver
  count that used to sit at its left was the same number as the strip's DIVERS
  column two rows up. The reward moved left and up to 12pt bold; the sync line
  became a ring glyph and `hh:mm` in the built-in 6x8 face, right-aligned,
  amber with a `STALE` prefix when the data has aged out. Staleness needs a
  word at that size — a muted colour alone made the exception *less* visible
  than the normal state, which was the first thing the preview caught.
- **New preview scene `carousel`**, shot after a page advance instead of onto
  a cleared screen. Every other scene calls `invalidate()` first, which is why
  none of them could have caught the header bug. Add to it rather than adding
  another from-scratch scene when the thing under test is a repaint.

Before that (branch `touch-and-events`) came touch, SD audio and the event
screens:

- **Count-style order tasks show real progress.** Eradicate/extract-style
  tasks measure a raw count, not a planet's health, so the card's bar was
  reading empty on them. Progress is now taken from the task's own
  progress/goal pair, and it survives the planet lookup 404ing — which is the
  normal state for a planet the community API's static table has not caught up
  with, and exactly the case the bug was photographed in. Preview scenes
  `count` and `extraction` cover both.
- **SD card audio.** Optional; no wiring change was needed, the slot is on
  VSPI and free. Three clips (`mo_new`, `mo_success`, `mo_failure`) built by
  `tools/gen_sd_assets.py` into `sdcard/audio/`. No card falls back to the
  compiled-in clip for all three.
- **Touch works, and the earlier diagnosis was wrong.** It was not the
  panel's calibration: TFT_eSPI's validTouch() requires two successive samples
  to agree within 20 ADC counts, a stationarity test a moving finger cannot
  pass, so most of a swipe is discarded and taps land wherever the finger
  happened to pause. `hud_touch.cpp` replaces that layer. **NOT YET VERIFIED
  ON HARDWARE** — written against the datasheet and the verified E32R40T
  pinout, with the reasoning documented in `hud_touch.h`. First flash should
  hold the panel at boot to run the diagnostics dump and calibrate.
- **Event screens** for a new order and for the verdict when one ends. The
  API has no outcome field and no history endpoint, so the verdict is inferred
  from the last state seen before the order left the feed — see
  `classifyOrderOutcome()` in main.cpp, and note that completion is tested
  *before* the deadline on purpose.

Before that (v1.1.0) was a card redesign plus audio:

- **Cards rebuilt around the artwork.** The biome plate now fills the upper
  half with the planet's identity set *over* it (left-side scrim for
  legibility), progress tracks below, then the four-value strip and footer.
  This deleted the whole previous card stack — objective bar with its gold
  clock flag, identity row, alert ribbon, woven scene panel, verdict row —
  along with drawWash/washText/drawHatch and the header wash art. Net effect
  was flash *down* despite adding a 30KB audio clip.
- **LIBCON chip and carousel pips moved into the header row**, which every
  screen now shares. The footer became divers / reward / sync (the divers
  reading has since gone — see the header/strip/footer pass above).
- **An active Major Order owns the screen.** pageCount() returns the order's
  tasks when one is live and campaigns only when none is; the campaigns feed
  is not even fetched while an order runs. Don't "fix" this into a combined
  strip — it was tried and deliberately reverted.
- **Touch was built and reverted here**, on the conclusion that the XPT2046
  calibration would not hold, and the carousel was timed (7s) instead. That
  diagnosis was wrong and the work is back — see the branch notes above. Note
  that attempt was never committed: this file used to say the implementation
  was in git history, and it is not. Don't go looking for it.
- **Audio** on a new Major Order and after an OTA — see `hud_audio.*` above
  and the README's Audio section for the trigger rules. The OTA alert is
  gated on the NVS version compare so a plain reboot doesn't replay it; the
  new-order alert is no longer suppressed on a boot, see the
  `mo-boot-overlay` notes at the top of this section.

Before that, a SEAF/skull rebrand:
- Faction label "Humans" → "SEAF" in all **UI text only**. The underlying
  API string matching against the community API's actual `"Humans"` string
  was deliberately left untouched (factionColor, objectiveStatus,
  factionIcon, factionAccent all still match `human*` — do not "fix" this,
  it's correct, the API still says Humans even though we display SEAF).
- SEAF emblem (Super Earth globe + laurel, user-supplied source photo) is
  the boot screen and idle-screen centerpiece art (`emblemLarge`, 72x39
  only — it was tested and confirmed illegible at any smaller icon/badge
  size, so smaller emblem variants were removed entirely rather than kept
  as broken assets). The faction badge for SEAF is text-only, no icon, by
  design (matches the "unknown faction" fallback style).
- Winged skull (user-supplied source photo,
  `tools/assets/skull_wings_source.jpg` → `crest_mask_v2.png`) replaced the
  project's previous crest mark:
  1. Scene-band corner icon (`crest()` in `tools/gen_icons.py`) — the scene
     band itself is gone as of the card redesign, so this one is no longer
     drawn anywhere.
  2. Boot screen — actually swapped OUT here in favor of the SEAF emblem
     per explicit request ("use the super earth logo on the startup
     screen"), so boot screen shows SEAF emblem, not the skull.
  There used to be a third place — a traced version of this same skull inside
  the Major Order title-bar badge (`headerBadge`/`headerSkull`) — but that
  badge design was already superseded by a background-wash-only title bar
  before this rebrand, the badge/skull generation was just never pruned. It
  has since been removed (see the `hud_header_art.h` note above); don't go
  looking for a third skull location, there are only two live ones now.
  `icons::shield` (the old Defense objective bar, now unreferenced) and
  `icons::skull` (kill-count
  stat tile, a plain no-wings skull) are visually similar but were
  confirmed unrelated and intentionally left alone — don't touch them
  when asked to change "the skull icon" without double-checking which one
  is meant.
- Full-colour faction badges (Automaton/Terminid/Illuminate) and biome
  terrain backdrops for the campaign screen were added on top of the SEAF
  rebrand — see the `hud_faction_icons.h`/`hud_biomes.h` and
  `gen_faction_icons.py`/`gen_biomes.py` notes above. The Automaton badge
  went through one revision: an earlier draft used a drawn robot-head shape,
  caught by the icon audit as wrong (the actual insignia is a four-pointed
  star) and corrected by replacing `tools/assets/faction_automaton_source.png`
  and regenerating — `docs/icon-audit/icon_audit.html` predates that fix, so
  its "automaton is not the faction insignia" finding is stale/resolved, not
  an open issue.

## Working conventions for this project

- Board target is fixed: `hosyond-esp32-32e` in `platformio.ini`. Don't
  change the board/pin mapping without being asked — it's already verified
  against physical hardware.
- Verify builds with `python3 -m platformio run` before considering a
  change done. Watch RAM/flash usage in the build output (device has no
  PSRAM, headroom is limited).
- Flash headroom is nearly gone: OTA needs two app slots, `partitions_hd2.csv`
  caps the image at 1.9375 MiB, and the build now sits at **98.8% (2,006,697
  of 2,031,616 bytes, ~24KB spare)** — the Major Order overlay art took 42KB
  of it. Compiled-in art is what fills it, and there is no longer room for
  another plate. If a change overflows the slot, the fix is the SD card, or a
  custom partition CSV growing **both** app slots equally — not reverting to
  `huge_app.csv`, which would remove OTA.
- Regenerate and visually check `preview_*.png` via `tools/preview.sh` for
  any change touching `hud_renderer.cpp` or the generated art headers, and run
  `python3 tools/check_layout.py`, before saying a visual change is complete.
  The preview caught two real collisions in the card redesign (a clipped clock
  plate and a plate overlapping the headline) that the constants alone did not
  make obvious.
- API usage: community API at api.helldivers2.dev requires `X-Super-Client`
  and `X-Super-Contact` headers (400 without them). Rate limit is 5
  requests / 10s; poll interval is 300s (5 min) and spends up to 6 requests
  per poll, don't lower it much. `HD2_CONTACT_HEADER` now points at this
  repo (it was a placeholder until v1.1.0).
- This device IS flashed and connected to real hardware (confirmed 2026-08-02:
  a CH340 USB-serial adapter, VID:PID 1A86:7523, was present at
  `/dev/cu.usbserial-210` on the Mac Mini). Don't assume based on git history
  alone whether a unit is live — flashing doesn't require a commit. If in
  doubt, check `pio device list` for a connected USB-serial adapter and ask
  the user directly.
