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
//    * The Major Order card carries a second signature, because an order with
//      several targets cycles through them every few seconds and that must not
//      drag the whole screen through a repaint each time.
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

  // Forces the next update() to repaint everything (e.g. after a status page).
  void invalidate();

#ifdef HUD_PREVIEW
  // Preview builds (tools/preview.sh) rasterise the panel to a PNG, so they
  // need at the pixels the renderer has drawn. Compiled out of the firmware.
  TFT_eSPI &canvas() { return _tft; }
#endif

 private:
  // --- composite sections ---
  // Outer frame and corner brackets; on their own on every screen.
  void drawFrame();
  // The plain "SUPER EARTH" strip, used by every screen that is not showing a
  // Major Order.
  // `title` names the screen; empty keeps the default SUPER EARTH strip.
  void drawStatusHeader(const String &title = String());
  void drawBody(const HudModel &m, time_t nowUtc);
  void drawIdleBody(const HudModel &m);
  // Stands in for the idle screen whenever a live campaign was found: the
  // busiest planet in the galaxy, drawn as a liberation readout.
  void drawCampaignBody(const HudModel &m);
  void drawWifi(bool up);
  void drawFooter(const HudModel &m, time_t nowUtc);

  // --- the Major Order card ---
  // One target's card, top to bottom. Repainted as a unit — both when the data
  // moves and when the carousel advances — because almost every row of it is
  // target-specific.
  void drawCard(const HudModel &m);
  void drawObjectiveBar(const HudModel &m, const OrderTask &t, uint16_t accent);
  // A biome photograph scaled into an arbitrary rect. Shares drawWash()'s
  // scaler; see tools/gen_biomes.py for why the plates are stored at half size.
  void drawBiome(const uint16_t *src, int16_t sw, int16_t sh, int16_t dx,
                 int16_t dy, int16_t dw, int16_t dh);
  // The Earth/gold-sweep wash behind the objective bar. drawWash() scales the
  // generated art onto any destination rect; drawObjWash() is the objective
  // bar's own, and takes the sub-rect to repaint.
  void drawWash(int16_t sx, int16_t sy, int16_t sw, int16_t sh,
                int16_t dx, int16_t dy, int16_t dw, int16_t dh);
  void drawObjWash(int16_t sx, int16_t sy, int16_t sw, int16_t sh);
  void washText(int16_t x, int16_t y, int16_t w, int16_t h, const GFXfont *font,
                uint16_t fg, uint8_t datum, const String &s);
  void drawIdentity(const OrderTask &t, uint16_t accent);
  void drawAlertRibbon(const OrderTask &t, uint16_t accent);
  // The biome band. Also carries the order's own title, which the objective bar
  // above it no longer has room for.
  void drawScene(const HudModel &m, const OrderTask &t, uint16_t accent);
  void drawProgress(const HudModel &m, const OrderTask &t, uint16_t accent);
  void drawVerdict(const HudModel &m, const OrderTask &t);
  // The two clocks set into the objective bar: the defence's own deadline and
  // the order's expiry, on the gold flag. Both tick every second, so they own
  // their slots and repaint independently of the rest of the card.
  void drawCardClocks(const HudModel &m, time_t nowUtc);
  // Advances the carousel index when its dwell time is up. Cheap; called every
  // update().
  void updateTargetCycle(const HudModel &m);

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
  void drawRateBar(int16_t y, float pct, uint16_t fill, bool haveRate, float rate);
  // An empty track carrying a centred caption, for an objective that has no
  // percentage to show at all.
  void drawIdleBar(int16_t y, const String &label, uint16_t color);
  void drawBadge(const String &faction, int16_t x, int16_t y, int16_t h);
  // Width the faction badge will occupy, including its icon. drawBadge() and
  // the identity row's layout both need it, so it lives in one place.
  int16_t badgeWidth(const String &faction);
  // Diagonal hazard hatching, clipped to the given box. Used by the alert
  // ribbon's end blocks and, at a coarser pitch, by the scene panel.
  void drawHatch(int16_t x, int16_t y, int16_t w, int16_t h, int16_t pitch,
                 uint16_t c);

  TFT_eSPI _tft;

  bool _chromeDrawn = false;
  // Whether the last body paint left the Major Order card on screen. The WiFi
  // indicator lives in the card's footer when it did, and on the "SUPER EARTH"
  // strip when it did not, so it has to know.
  bool _cardMode = false;
  String _contentSig;  // last-painted body signature
  String _targetSig;   // last-painted card signature
  String _clockSig;    // last-painted pair of card clocks
  String _footerSig;
  int8_t _wifiSig = -1;  // -1 = never drawn, 0 = down, 1 = up

  // Where drawCardClocks() has to paint. Set by drawObjectiveBar(), which is
  // what measures the gold flag against its own text.
  int16_t _flagX = 0, _flagW = 0;
  int16_t _victoryX = 0, _victoryW = 0;

  // Carousel over the order's targets. An order can name three planets and
  // there is no room to show three of these cards at a legible size, so they
  // take turns. Index into MajorOrder::tasks, plus the millis() stamp of the
  // last switch.
  uint8_t _targetIdx = 0;
  uint32_t _targetSwitchMs = 0;
};
