// ---------------------------------------------------------------------------
//  hud_renderer.h — all drawing for the Super Earth command HUD.
//
//  Owns the TFT_eSPI instance. Knows nothing about WiFi or HTTP; it is handed
//  a fully-populated HudModel and paints it.
//
//  Redraw strategy (deliberately simple for a prototype):
//    * The frame + static chrome are painted once.
//    * A content signature is derived from the model; when it changes, the
//      body is fully repainted. Real data only moves every 5 minutes.
//    * The Major Order card and the campaign card each carry their own
//      signature, because the carousel (MO task cards, then campaign cards)
//      repaints just the active card on each advance, not the whole screen.
//    * Everything that ticks on its own — the two clocks in the card's
//      objective bar, the WiFi dot, the "last synced" line — is repainted
//      through small off-screen sprites, which keeps it flicker-free without
//      needing a full framebuffer (480*320*2 = 300KB would not fit in this
//      ESP32's SRAM).
// ---------------------------------------------------------------------------
#pragma once

#include <TFT_eSPI.h>

#include "hd2_model.h"

class HUDRenderer {
 public:
  void begin();

  // Full-screen status pages used before the HUD has data.
  void showBoot(const char *status);
  void showPortal(const char *ssid, const char *pass);
  // The two touch calibration screens, drawn from touch_required_reference.jpg
  // and touch_success_reference.jpg. Same bridge, same wordmark, same card
  // geometry; they differ in accent, chip, icon and copy. See the cal*
  // constants in config.h.
  //
  // The prompt is shown once on a unit that has never been calibrated, and
  // left up while main.cpp waits for a contact -- touching it is what starts
  // touch::calibrate(), which paints its own four-corner chrome over the top.
  // The confirmation is shown for a few seconds after any calibration that
  // completes, from either route into one.
  void showTouchPrompt();
  void showTouchSuccess();

  // Paints `m`, repainting only what changed. `nowUtc` drives the clocks.
  void update(const HudModel &m, time_t nowUtc);

  // Steps the carousel by `delta` pages, wrapping, and restarts its dwell
  // timer. This is what a swipe calls -- see hud_touch.h. The 7s auto-advance
  // is deliberately left running underneath: touch is an accelerator, not a
  // replacement, so a board nobody is touching still cycles on its own.
  void advancePage(const HudModel &m, int8_t delta);

  // Puts the carousel back on its first page with a full dwell ahead of it.
  // Called when a new-order announcement is dismissed, so the screen it hands
  // back to is that order's first objective rather than wherever the carousel
  // happened to be when the announcement interrupted it.
  void resetCarousel();

  // Forces the next update() to repaint everything (e.g. after a status page).
  void invalidate();

  // The panel itself. The XPT2046 touch controller is on the same SPI bus,
  // behind the same transaction lock, so hud_touch has to drive it through
  // this instance rather than opening its own -- see hud_touch.h. Nothing else
  // should reach in here; drawing belongs in this class.
  TFT_eSPI &panel() { return _tft; }

#ifdef HUD_PREVIEW
  // Preview builds (tools/preview.sh) rasterise the panel to a PNG, so they
  // need at the pixels the renderer has drawn. Compiled out of the firmware.
  TFT_eSPI &canvas() { return _tft; }
#endif

 private:
  // --- composite sections ---
  // Outer frame and corner brackets; on their own on every screen.
  void drawFrame();
  // LIBCON status chip: a small colour-coded pill, right-aligned above the
  // footer's sync line. `tier` is 1-5 (1 = white .. 5 = blue, matching the
  // community app's own legend); 0 or out of range leaves it undrawn.
  // Returns the x just past the chip's right edge; unused by its one caller
  // (drawFooter, which positions it independently) but kept for symmetry
  // with the row-packing helpers elsewhere in this file.
  int16_t drawLibcon(int16_t x, int16_t y, int16_t h, int8_t tier);
  // The header row every screen shares: the objective type, the LIBCON chip
  // beside it, the carousel pips, and the link state. `title` names the
  // screen; empty keeps the default SUPER EARTH strip. `tier`/`pages`/`page`
  // are skipped when zero, which is what the boot and setup screens want.
  // `orderBtn` draws the reopen button at the row's right; false clears its
  // box. The boot and setup screens leave it off -- there is no order to
  // reopen before the first poll lands.
  void drawStatusHeader(const String &title = String(), int8_t tier = 0,
                        uint8_t pages = 0, uint8_t page = 0,
                        bool orderBtn = false);
  // The reopen button itself: the SEAF emblem sampled down to header height,
  // in a bordered chip. `on` false clears the box instead. The only tap
  // target on the panel outside the overlay -- main.cpp hit-tests against
  // layout::moBtnHit*, which is this box plus a fingertip's slack.
  void drawOrderButton(bool on);
  // Works out what that row should say for `m` at the current page and repaints
  // it when the answer changed. Called on every update(), not just on a full
  // body repaint: a carousel step within one Major Order repaints only the
  // card, and without this the pips and the objective-type word beside them
  // stay on whatever the last full repaint left there.
  void drawHeader(const HudModel &m);
  void drawBody(const HudModel &m, time_t nowUtc);
  void drawIdleBody(const HudModel &m);
  // One campaign card, `p`/`h` resolved by the caller from m.campaigns[]/
  // m.campaignHistory[] at whichever slot the carousel is on. Also what the
  // idle screen falls back to showing when there is no Major Order.
  void drawCampaignBody(const HudModel &m, const PlanetInfo &p, const RateSample &h);
  void drawWifi(bool up);
  void drawFooter(const HudModel &m, time_t nowUtc);
  // --- the two touch calibration screens ---
  // Everything below is shared by showTouchPrompt() and showTouchSuccess()
  // except the two body icons, which are what the screens are for.
  //
  // The destroyer bridge the card is set on: space, the planet in the
  // viewport, the ship's structure, the diver. Flat tones, no photograph.
  void drawBridge();
  // What drawBridge() left at (x,y). The wordmark is anti-aliased down onto
  // the bridge and so has to know what is behind each of its edge pixels;
  // this is the one function that answers that, and drawBridge() paints from
  // the same geometry so the two cannot disagree.
  uint16_t bridgeAt(int16_t x, int16_t y);
  // The HELLDIVERS II mark, box-filtered from the 440x172 cut in hud_icons.h
  // into an arbitrary box and blended onto the bridge.
  void drawWordmark(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t ink);
  // The card, everything but the body: fill, border, header band with its chip
  // and title, and the hatched band with `bandText` centred in it. `accent` is
  // the bright member of the colour family, `dim` the one the hatch is drawn
  // in. `check` swaps the chip's warning triangle for a check in a disc, and
  // puts the reference's wing marks either side of the band's text.
  void drawCalCard(uint16_t accent, uint16_t dim, const String &title,
                   const String &bandText, bool check);
  // The card's five rows of copy, to the right of its icon: a salutation in
  // `headColor` and two two-line paragraphs. Both screens set the same rows at
  // the same rhythm, so only the strings and that one colour are the caller's.
  void drawCalCopy(uint16_t headColor, const String &head, const String &l1,
                   const String &l2, const String &l3, const String &l4);
  // A reticle with a hand tapping its centre (the prompt), and a Super Earth
  // service medallion (the confirmation), each in an `s`-square box at (x,y).
  void drawCalReticle(int16_t x, int16_t y, int16_t s);
  void drawCalMedallion(int16_t x, int16_t y, int16_t s);
  // The diagonal hatch along the card's bottom edge.
  void drawHazardBar(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t ink);
  // A ring segment: every pixel between `rIn` and `rOut` of (cx,cy) whose angle
  // is in [a0,a1], degrees, measured from +x with y downward. There is no
  // drawArc() in this TFT_eSPI build's shim, and the medallion's laurel is the
  // only thing on the panel that needs one.
  void drawArcRing(int16_t cx, int16_t cy, int16_t rIn, int16_t rOut, float a0,
                   float a1, uint16_t c);
  // A filled five-pointed star of radius `r`, point up. The medallion's rank
  // row is five of them.
  void drawStar(int16_t cx, int16_t cy, int16_t r, uint16_t c);
  // A full-screen event announcement: the new-order dispatch, or a verdict on
  // one that has ended. Takes the whole panel — no frame chrome, no carousel,
  // no clocks — because it is meant to interrupt, and stays up until it is
  // dismissed. See HudModel::overlay.
  //
  // All three are one composition, drawn from mo_new_reference.jpg and
  // mo_verdict_reference.jpg: a dark text panel down the left with an angled
  // right edge, the flag photograph filling the right of the screen, an accent
  // hairline on the divider between them. They differ in colour family, badge,
  // copy, and how the one stored art plate is graded. See the ovl* constants
  // in config.h for the geometry and what does and does not match.
  void drawOverlay(const HudModel &m, time_t nowUtc);
  // x of the panel's angled right edge at row `y`, and the right edge of the
  // text column there — the latter is what every row of type has to wrap to,
  // since the column is a trapezoid and narrows on the way down.
  static int16_t overlayEdgeX(int16_t y);
  static int16_t overlayTextR(int16_t y);
  // The photographic plate down the right: hud_mo_art.h scaled 2x through the
  // same srcCoord()/lerp565() pair drawBiome() uses, and re-graded per screen
  // on the way past. There is one plate for three screens because three would
  // not fit the app slot — tools/gen_mo_art.py has that arithmetic. `kind`
  // picks the grade: the announcement is the plate as stored, a success is
  // exposed up, a failure is desaturated toward a warm grey with an ember
  // glow off the skyline.
  void drawOverlayArt(OverlayKind kind);
  // The one-day bulletin, which is not one of the three above and shares none
  // of their layout: hud_bulletin_art.h scaled 1.6x onto the whole panel
  // through the same srcCoord()/lerp565() pair, plus the dismiss hint. No
  // subject, no grade, no panel — the card is already composed.
  // tools/gen_bulletin_art.py has the sizing and the crop.
  void drawBulletinOverlay();
  // Shreds bitten out of the flag's fly edge, slits through its body, and
  // tatters hanging off its hem — the failure reference's flag is in rags and
  // a grade alone cannot tear cloth. Failure only, over the graded plate.
  void drawFlagTears();
  // The text panel, its angled right edge, the accent hairline riding that
  // edge, and the border around the whole screen.
  void drawOverlayPanel(uint16_t accent, uint16_t dim);
  // The header row every overlay shares: badge at the left margin, the title
  // beside it in Anton, and a rule under both ending in the reference's
  // angled tail.
  void drawOverlayHeader(OverlayKind kind, uint16_t accent, const String &title);
  // One of the announcement's section rules — a centred ALL-CAPS word with a
  // wing mark either side of it, e.g. "= OBJECTIVE =".
  void drawOverlayDivider(int16_t y, const String &label, uint16_t accent);
  // Three stacked bars tapering away from the text they flank. `len` is the
  // middle bar; the outer two step shorter. `dir` is +1 for a mark to the
  // right of `x`, -1 for one to its left. The same motif the calibration
  // card's band already carries, and the references repeat it on every rule.
  void drawWingBars(int16_t x, int16_t y, int16_t len, int8_t dir, uint16_t c);
  // Greedy word wrap in the current font. Fills up to `maxLines` entries of
  // `out` and returns how many were used; ellipses the last line if the text
  // did not fit. Only the overlay needs this — every other screen is built
  // from fields short enough to size by hand.
  int8_t wrapText(const String &s, int16_t maxW, int8_t maxLines, String *out);
  // The announcement's briefing block at page _briefPage, and the tick that
  // advances it. The block holds ovlBriefMax lines and a real briefing is
  // routinely longer, so the text pages through it rather than being cut at
  // the fourth line. `accent` tints the n/N marker. See the note on
  // drawOverlayBriefing() for why this pages rather than scrolls or grows.
  void drawOverlayBriefing(const MajorOrder &o, uint16_t accent);
  void updateOverlayBriefing(const HudModel &m);

  // --- the Major Order card ---
  // One target's card, top to bottom. Repainted as a unit — both when the data
  // moves and when the carousel advances — because almost every row of it is
  // target-specific.
  void drawCard(const HudModel &m);
  // The combined card: every target of a count-only order on one page, one row
  // each, instead of a carousel slide per target. Taken when
  // orderIsCombinedCount() holds -- see the note there for what qualifies.
  // Laid out from mo_overhaul_reference.jpg: a flat band naming the subject
  // and the order over the order's clock plate, then the target rows.
  void drawCombinedCard(const HudModel &m);
  // One target's row on that card: what it is counting and the figures, the
  // target's own percentage right-aligned beside them, and a track under both.
  // `capY` is the top of the caption row; the track hangs below it.
  void drawCombinedRow(const HudModel &m, uint8_t taskIdx, int16_t capY);
  // A biome photograph scaled into an arbitrary rect. `scrimW` darkens the
  // plate from its left edge inward so overlaid text stays legible; 0 leaves
  // the artwork untouched. See tools/gen_biomes.py for why the plates are
  // stored at half size.
  void drawBiome(const uint16_t *src, int16_t sw, int16_t sh, int16_t dx,
                 int16_t dy, int16_t dw, int16_t dh, int16_t scrimW = 0,
                 uint8_t scrimMax = 0);
  // The art band: biome plate, the planet's name/sector/headline stat set over
  // it, and the faction mark. Shared by both card types.
  // `fallbackName` is what stands in for the identity line when the planet
  // record is missing — the community API 404s planets absent from its static
  // table, so this is a routine state, not a fault. Empty falls back to
  // "UNKNOWN", which is all a campaign card can honestly say.
  void drawArtBand(const PlanetInfo &p, int16_t h, const String &headline,
                   uint16_t headlineColor, const String &orderTitle = String(),
                   const String &fallbackName = String());
  // Text straight onto whatever is already drawn, with no background fill —
  // for the identity set over the biome plate.
  void drawOverText(int16_t x, int16_t y, int16_t h, const GFXfont *font, uint16_t fg,
                    const String &s);
  // The faction insignia alone, on a dark disc, top-right of the art band.
  void drawFactionMark(const String &faction);
  // A progress track with its caption row: what it measures on the left, the
  // value and rate on the right.
  void drawBarRow(int16_t capY, int16_t barY, int16_t barH, const String &label,
                  const String &readout, uint16_t readoutColor, float pct,
                  uint16_t fill);
  // An empty track carrying a centred caption, for an objective with no
  // percentage to show at all.
  void drawIdleTrack(int16_t capY, int16_t barY, int16_t barH, const String &label);
  // The stat strip. `accent` tints the enemy-regen column. `countStyle` swaps
  // the PUSH/REGEN pair -- which a count objective's bar is not moving on, and
  // which read "--" on every count card by construction -- for a single ETA
  // column carrying `eta`; empty `eta` shows the usual dash.
  void drawStrip(const HudModel &m, const PlanetInfo &p, bool haveRate, float rate,
                 uint16_t accent, const String &eta = String(),
                 bool countStyle = false);
  // The order's countdown and title, on their own plate bottom-right of the
  // art. The clock ticks every second, so it repaints independently.
  void drawCardClocks(const HudModel &m, time_t nowUtc);
  // Advances the unified carousel index (MO task cards, then campaign cards)
  // when its dwell time is up. Cheap; called every update().
  void updateCarousel(const HudModel &m);

  // --- primitives ---
  // Paints `s` into an off-screen sprite covering the box, then blits it.
  // `datum` is a TFT_eSPI datum constant (TL/ML/MC/MR/TR...).
  void textBox(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t bg,
               const GFXfont *font, uint16_t fg, uint8_t datum, const String &s,
               int16_t inset = 0);
  void drawRule(int16_t y);
  // One of the three stat tiles, by index 0..2: a bordered panel with an icon
  // where a text label used to be, and a value beneath it. Only the no-order
  // screen still uses these.
  void drawTile(int i, const uint8_t *icon, int16_t iw, int16_t ih,
                const String &value, uint16_t valueColor);
  static int16_t tileX(int i);
  // Two-up label/value box; only the WiFi setup screen still uses it.
  void drawStatBox(int16_t x, int16_t y, int16_t w, int16_t h, const String &label,
                   const String &value, uint16_t valueColor);
  // A progress track with a chevron cap at the leading edge of its fill, and
  // the measured %-per-hour beside it. `rate` is only drawn when `haveRate`;
  // without it the column reads "--", because the API publishes no rate and a
  // single poll cannot produce one.
  // The bare track: border, fill, and the chevron cap riding its leading edge.
  void drawTrack(int16_t y, int16_t h, float pct, uint16_t fill);
  void drawBadge(const String &faction, int16_t x, int16_t y, int16_t h);
  // Width the faction badge will occupy, including its icon. Only the WiFi
  // setup path still needs it, but it stays alongside drawBadge().
  int16_t badgeWidth(const String &faction);

  TFT_eSPI _tft;

  bool _chromeDrawn = false;
  // Whether the last body paint left the Major Order card on screen. The WiFi
  // indicator lives in the card's footer when it did, and on the "SUPER EARTH"
  // strip when it did not, so it has to know.
  bool _cardMode = false;
  // _cardMode as of the previous update() call, so a carousel advance that
  // crosses the MO-card/campaign-card boundary (different header/footer
  // geometry) is detected even when the underlying data (contentSignature)
  // didn't change — only which page is being looked at did.
  bool _lastCardMode = false;
  // Whether the card on screen is the combined one. Set by drawCard() and read
  // by drawCardClocks(), which puts the clock plate at a different corner and
  // must not read a defence clock off a single target when several are shown.
  bool _combined = false;
  String _headerSig;   // last-painted header row (type word, tier, pip row)
  String _contentSig;  // last-painted body signature
  String _targetSig;   // last-painted card signature
  String _campaignSig; // last-painted campaign-card signature
  String _clockSig;    // last-painted pair of card clocks
  String _footerSig;
  String _overlaySig;    // last-painted overlay; empty when none is up
  int8_t _wifiSig = -1;  // -1 = never drawn, 0 = down, 1 = up
  // Which page of the announcement's briefing is showing, how many there are,
  // and when the current one went up. Meaningless unless an announcement is on
  // the panel; reset by drawOverlay() every time one is raised.
  int8_t _briefPage = 0;
  int8_t _briefPages = 1;
  uint32_t _briefPageMs = 0;

  // Unified carousel: MO task cards first (if an order is active), then
  // campaign cards. Index into that combined sequence — see
  // updateCarousel()/pageCount() in hud_renderer.cpp for how it's split back
  // out — plus the millis() stamp of the last advance.
  uint8_t _pageIdx = 0;
  uint32_t _pageSwitchMs = 0;
};
