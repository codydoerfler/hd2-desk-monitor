// ---------------------------------------------------------------------------
//  tft_shim.h — a host-side stand-in for TFT_eSPI + the Arduino core.
//
//  This exists so tools/render_preview can compile src/hud_renderer.cpp *as
//  it ships* and rasterise it to a PNG. The previous tools/preview_hud.py
//  reimplemented the HUD in Python, which meant every firmware layout change
//  silently invalidated the preview -- it drifted two commits out of date and
//  was actively misleading. Nothing here models the HUD; it only provides the
//  drawing primitives the renderer calls, so there is no second layout to keep
//  in step.
//
//  Only the surface hud_renderer.cpp actually uses is implemented:
//    init/setRotation/fillScreen/fillRect/drawRect/drawFastHLine/fillCircle/
//    fillTriangle/drawBitmap/pushImage/setFreeFont/setTextColor/setTextDatum/
//    drawString/textWidth/get|setSwapBytes, plus TFT_eSprite.
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>

// --- Arduino core shims ----------------------------------------------------

#include "Arduino.h"  // String, F(), PROGMEM, pgm_read_*

#ifndef LOAD_GFXFF
#define LOAD_GFXFF
#endif

typedef struct {
  uint32_t bitmapOffset;
  uint8_t width, height;
  uint8_t xAdvance;
  int8_t xOffset, yOffset;
} GFXglyph;

typedef struct {
  uint8_t *bitmap;
  GFXglyph *glyph;
  uint16_t first, last;
  uint8_t yAdvance;
} GFXfont;

// Text datums, same values TFT_eSPI uses.
#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8

// --- the canvas ------------------------------------------------------------

class TFT_eSprite;

class TFT_eSPI {
 public:
  TFT_eSPI(int16_t w = 480, int16_t h = 320) : _w(w), _h(h), _px(size_t(w) * h, 0) {}

  void init() {}
  void setRotation(uint8_t) {}
  int16_t width() const { return _w; }
  int16_t height() const { return _h; }

  bool getSwapBytes() const { return _swap; }
  void setSwapBytes(bool s) { _swap = s; }

  void fillScreen(uint16_t c) { std::fill(_px.begin(), _px.end(), c); }

  void drawPixel(int16_t x, int16_t y, uint16_t c) {
    if (x < 0 || y < 0 || x >= _w || y >= _h) return;
    _px[size_t(y) * _w + x] = c;
  }

  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    for (int16_t j = 0; j < h; ++j)
      for (int16_t i = 0; i < w; ++i) drawPixel(x + i, y + j, c);
  }

  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    for (int16_t i = 0; i < w; ++i) {
      drawPixel(x + i, y, c);
      drawPixel(x + i, y + h - 1, c);
    }
    for (int16_t j = 0; j < h; ++j) {
      drawPixel(x, y + j, c);
      drawPixel(x + w - 1, y + j, c);
    }
  }

  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) {
    for (int16_t j = 0; j < h; ++j) drawPixel(x, y + j, c);
  }

  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) {
    for (int16_t i = 0; i < w; ++i) drawPixel(x + i, y, c);
  }

  void fillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t c) {
    for (int16_t j = -r; j <= r; ++j)
      for (int16_t i = -r; i <= r; ++i)
        if (i * i + j * j <= r * r) drawPixel(cx + i, cy + j, c);
  }

  void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2,
                    int16_t y2, uint16_t c) {
    const int16_t minx = std::min({x0, x1, x2}), maxx = std::max({x0, x1, x2});
    const int16_t miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
    auto edge = [](int16_t ax, int16_t ay, int16_t bx, int16_t by, int16_t px,
                   int16_t py) {
      return (int32_t)(bx - ax) * (py - ay) - (int32_t)(by - ay) * (px - ax);
    };
    for (int16_t y = miny; y <= maxy; ++y) {
      for (int16_t x = minx; x <= maxx; ++x) {
        const int32_t w0 = edge(x1, y1, x2, y2, x, y);
        const int32_t w1 = edge(x2, y2, x0, y0, x, y);
        const int32_t w2 = edge(x0, y0, x1, y1, x, y);
        if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
          drawPixel(x, y, c);
      }
    }
  }

  // 1-bit mask, MSB first, rows padded to whole bytes. Set bits paint; clear
  // bits are left alone, matching TFT_eSPI's single-colour drawBitmap().
  void drawBitmap(int16_t x, int16_t y, const uint8_t *bits, int16_t w, int16_t h,
                  uint16_t c) {
    const int16_t stride = (w + 7) / 8;
    for (int16_t j = 0; j < h; ++j)
      for (int16_t i = 0; i < w; ++i)
        if (bits[j * stride + (i >> 3)] & (0x80 >> (i & 7))) drawPixel(x + i, y + j, c);
  }

  void pushImage(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *data) {
    for (int16_t j = 0; j < h; ++j)
      for (int16_t i = 0; i < w; ++i) drawPixel(x + i, y + j, data[j * w + i]);
  }

  // Key-colour overload: pixels equal to `transp` are skipped, leaving whatever
  // was already there. Matches TFT_eSPI's own transparent pushImage().
  void pushImage(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *data,
                 uint16_t transp) {
    for (int16_t j = 0; j < h; ++j)
      for (int16_t i = 0; i < w; ++i) {
        const uint16_t c = data[j * w + i];
        if (c != transp) drawPixel(x + i, y + j, c);
      }
  }

  void setFreeFont(const GFXfont *f) { _font = f; }
  void setTextColor(uint16_t fg) { _fg = fg; _hasBg = false; }
  void setTextColor(uint16_t fg, uint16_t bg) { _fg = fg; _bg = bg; _hasBg = true; }
  void setTextDatum(uint8_t d) { _datum = d; }

  int16_t textWidth(const char *s) const {
    if (!_font || !s) return 0;
    int32_t w = 0;
    for (const char *p = s; *p; ++p) {
      const uint8_t ch = (uint8_t)*p;
      if (ch < _font->first || ch > _font->last) continue;
      w += _font->glyph[ch - _font->first].xAdvance;
    }
    return (int16_t)w;
  }
  int16_t textWidth(const String &s) const { return textWidth(s.c_str()); }

  // fontHeight()/ascent are only needed to place the datum; TFT_eSPI derives
  // them from yAdvance the same way.
  int16_t fontHeight() const { return _font ? _font->yAdvance : 0; }

  void drawString(const char *s, int32_t x, int32_t y) {
    if (!_font || !s) return;
    const int16_t w = textWidth(s);
    const int16_t h = fontHeight();

    // TFT_eSPI anchors GFX fonts on the baseline; these offsets convert the
    // requested datum into one.
    int32_t cx = x, cy = y;
    switch (_datum) {
      case TC_DATUM: case MC_DATUM: case BC_DATUM: cx -= w / 2; break;
      case TR_DATUM: case MR_DATUM: case BR_DATUM: cx -= w; break;
      default: break;
    }
    int16_t asc = (int16_t)(h * 0.75f);
    if (_font->first <= '0' && '0' <= _font->last) {
      // TFT_eSPI takes the cap height from the '0' glyph rather than a
      // fraction of yAdvance. Guessing pushed the baseline down far enough
      // that a baseline-anchored glyph like ',' (yOffset 0) fell out of the
      // sprite and clipped to a dot.
      asc = (int16_t)(-_font->glyph['0' - _font->first].yOffset);
    }
    switch (_datum) {
      case TL_DATUM: case TC_DATUM: case TR_DATUM: cy += asc; break;
      case ML_DATUM: case MC_DATUM: case MR_DATUM: cy += asc / 2; break;
      default: break;
    }

    int32_t pen = cx;
    for (const char *p = s; *p; ++p) {
      const uint8_t ch = (uint8_t)*p;
      if (ch < _font->first || ch > _font->last) continue;
      const GFXglyph &g = _font->glyph[ch - _font->first];
      const uint8_t *bmp = _font->bitmap + g.bitmapOffset;
      uint16_t bit = 0;
      for (uint8_t j = 0; j < g.height; ++j) {
        for (uint8_t i = 0; i < g.width; ++i, ++bit) {
          if (bmp[bit >> 3] & (0x80 >> (bit & 7)))
            drawPixel(pen + g.xOffset + i, cy + g.yOffset + j, _fg);
        }
      }
      pen += g.xAdvance;
    }
  }
  void drawString(const String &s, int32_t x, int32_t y) { drawString(s.c_str(), x, y); }

  const std::vector<uint16_t> &pixels() const { return _px; }

 protected:
  int16_t _w, _h;
  std::vector<uint16_t> _px;
  const GFXfont *_font = nullptr;
  uint16_t _fg = 0xFFFF, _bg = 0;
  bool _hasBg = false;
  uint8_t _datum = TL_DATUM;
  bool _swap = false;
};

// Sprites are just offscreen canvases that blit back to the parent.
class TFT_eSprite : public TFT_eSPI {
 public:
  explicit TFT_eSprite(TFT_eSPI *parent) : TFT_eSPI(1, 1), _parent(parent) {}

  void setColorDepth(int) {}

  bool createSprite(int16_t w, int16_t h) {
    if (w <= 0 || h <= 0) return false;
    _w = w; _h = h;
    _px.assign(size_t(w) * h, 0);
    return true;
  }
  void deleteSprite() { _px.clear(); _w = _h = 0; }
  void fillSprite(uint16_t c) { fillScreen(c); }

  void pushSprite(int32_t x, int32_t y) {
    if (!_parent) return;
    for (int16_t j = 0; j < _h; ++j)
      for (int16_t i = 0; i < _w; ++i)
        _parent->drawPixel(x + i, y + j, _px[size_t(j) * _w + i]);
  }

 private:
  TFT_eSPI *_parent;
};
