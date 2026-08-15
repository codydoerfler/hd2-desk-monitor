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
  void drawStatusHeader(const String &title = String(), int8_t tier = 0,
                        uint8_t pages = 0, uint8_t page = 0);
  void drawBody(const HudModel &m, time_t nowUtc);
  void drawIdleBody(const HudModel &m);
  // One campaign card, `p`/`h` resolved by the caller from m.campaigns[]/
  // m.campaignHistory[] at whichever slot the carousel is on. Also what the
  // idle screen falls back to showing when there is no Major Order.
  void drawCampaignBody(const HudModel &m, const PlanetInfo &p, const RateSample &h);
  void drawWifi(bool up);
  void drawFooter(const HudModel &m, time_t nowUtc);
  // A full-screen event announcement: the new-order dispatch, or a verdict on
  // one that has ended. Takes the whole panel — no frame chrome, no carousel,
  // no clocks — because it is meant to interrupt, and stays up until it is
  // dismissed. See HudModel::overlay.
  void drawOverlay(const HudModel &m, time_t nowUtc);
  // Greedy word wrap in the current font. Fills up to `maxLines` entries of
  // `out` and returns how many were used; ellipses the last line if the text
  // did not fit. Only the overlay needs this — every other screen is built
  // from fields short enough to size by hand.
  int8_t wrapText(const String &s, int16_t maxW, int8_t maxLines, String *out);

  // --- the Major Order card ---
  // One target's card, top to bottom. Repainted as a unit — both when the data
  // moves and when the carousel advances — because almost every row of it is
  // target-specific.
  void drawCard(const HudModel &m);
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
  // The four-value strip. `accent` tints the enemy-regen column.
  void drawStrip(const HudModel &m, const PlanetInfo &p, bool haveRate, float rate,
                 uint16_t accent);
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
  String _contentSig;  // last-painted body signature
  String _targetSig;   // last-painted card signature
  String _campaignSig; // last-painted campaign-card signature
  String _clockSig;    // last-painted pair of card clocks
  String _footerSig;
  String _overlaySig;    // last-painted overlay; empty when none is up
  int8_t _wifiSig = -1;  // -1 = never drawn, 0 = down, 1 = up

  // Unified carousel: MO task cards first (if an order is active), then
  // campaign cards. Index into that combined sequence — see
  // updateCarousel()/pageCount() in hud_renderer.cpp for how it's split back
  // out — plus the millis() stamp of the last advance.
  uint8_t _pageIdx = 0;
  uint32_t _pageSwitchMs = 0;
};
