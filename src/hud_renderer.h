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
//    * Everything that ticks on its own — the countdown, the WiFi dot, the
//      "last updated" line — is repainted through small off-screen sprites,
//      which keeps it flicker-free without needing a full framebuffer
//      (480*320*2 = 300KB would not fit in this ESP32's SRAM).
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

  // Paints `m`, repainting only what changed. `nowUtc` drives the countdown.
  void update(const HudModel &m, time_t nowUtc);

  // Forces the next update() to repaint everything (e.g. after a status page).
  void invalidate();

 private:
  // --- composite sections ---
  void drawChrome();
  void drawBody(const HudModel &m, time_t nowUtc);
  void drawOrderBody(const HudModel &m);
  void drawIdleBody();
  void drawWifi(bool up);
  void drawCountdown(const HudModel &m, time_t nowUtc);
  void drawFooter(const HudModel &m, time_t nowUtc);

  // --- primitives ---
  // Paints `s` into an off-screen sprite covering the box, then blits it.
  // `datum` is a TFT_eSPI datum constant (TL/ML/MC/MR/TR...).
  void textBox(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t bg,
               const GFXfont *font, uint16_t fg, uint8_t datum, const String &s,
               int16_t inset = 0);
  void drawRule(int16_t y);
  void drawStatBox(int16_t x, int16_t y, int16_t w, int16_t h, const String &label,
                   const String &value, uint16_t valueColor);
  void drawProgressBar(float pct, bool known);
  void drawBadge(const String &faction);
  // Width the faction badge will occupy, including its icon. drawBadge() and
  // the target-row layout both need it, so it lives in one place.
  int16_t badgeWidth(const String &faction);
  // Greedy word wrap. Returns the number of lines written to `out`.
  int wrapText(const String &in, const GFXfont *font, int16_t maxW, String *out,
               int maxLines);

  TFT_eSPI _tft;

  bool _chromeDrawn = false;
  String _contentSig;    // last-painted body signature
  String _countdownSig;  // last-painted countdown string
  String _footerSig;
  int8_t _wifiSig = -1;  // -1 = never drawn, 0 = down, 1 = up
};
