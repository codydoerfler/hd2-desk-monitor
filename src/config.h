// ---------------------------------------------------------------------------
//  config.h — tunables, palette and screen geometry for the HD2 desk monitor.
//
//  Anything here that is also settable from platformio.ini is guarded so the
//  build flag wins.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

// --------------------------------------------------------------------------
//  API
// --------------------------------------------------------------------------
#define HD2_API_HOST "api.helldivers2.dev"
#define HD2_API_BASE "https://" HD2_API_HOST

#ifndef HD2_CLIENT_HEADER
#define HD2_CLIENT_HEADER "hd2-desk-monitor"
#endif

// The API returns HTTP 400 without both X-Super-Client and X-Super-Contact.
#ifndef HD2_CONTACT_HEADER
#define HD2_CONTACT_HEADER "https://github.com/your-user/hd2-desk-monitor"
#endif

// --------------------------------------------------------------------------
//  Timing (seconds unless noted)
// --------------------------------------------------------------------------
#ifndef HD2_POLL_INTERVAL_S
#define HD2_POLL_INTERVAL_S 300  // Major Orders change on the order of days.
#endif

// Politeness gap between the 2-3 calls that make up one poll. The documented
// limit is 5 requests / 10 s; this keeps us comfortably inside it.
static const uint32_t kInterRequestDelayMs = 600;

// Exponential backoff bounds used after a failed poll.
static const uint32_t kBackoffMinS = 15;
static const uint32_t kBackoffMaxS = 600;

// Data older than this is drawn with the "stale" treatment.
static const uint32_t kStaleAfterS = HD2_POLL_INTERVAL_S * 2;

static const uint32_t kHttpTimeoutMs = 12000;

// WiFiManager captive portal: give up and reboot after this long so an
// unattended desk unit recovers on its own after a router outage.
static const uint32_t kPortalTimeoutS = 300;

// Kept short so it fits the setup screen's value box at full size.
static const char *kPortalSsid = "HD2-Monitor";
static const char *kPortalPass = "helldive";

// --------------------------------------------------------------------------
//  Palette — Super Earth command terminal
//  RGB565. Source hex values are in the comments.
// --------------------------------------------------------------------------
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace theme {
constexpr uint16_t bg       = rgb565(0x0A, 0x0C, 0x10);  // #0A0C10 near-black
constexpr uint16_t panel    = rgb565(0x12, 0x16, 0x1C);  // #12161C card fill
constexpr uint16_t track    = rgb565(0x1E, 0x23, 0x2B);  // #1E232B bar track
constexpr uint16_t gold     = rgb565(0xDF, 0xB2, 0x4F);  // #DFB24F accent
constexpr uint16_t goldDim  = rgb565(0x8C, 0x6F, 0x32);  // #8C6F32 borders
constexpr uint16_t goldMute = rgb565(0x5A, 0x48, 0x22);  // stale / disabled
constexpr uint16_t text     = rgb565(0xE6, 0xE8, 0xEB);  // #E6E8EB body
constexpr uint16_t grey     = rgb565(0x6E, 0x76, 0x80);  // #6E7680 labels
constexpr uint16_t blue     = rgb565(0x4A, 0x8C, 0xC7);  // #4A8CC7 progress
constexpr uint16_t red      = rgb565(0xC6, 0x3A, 0x2F);  // #C63A2F enemy
constexpr uint16_t green    = rgb565(0x5C, 0xA8, 0x60);  // #5CA860 online
}  // namespace theme

// --------------------------------------------------------------------------
//  Layout — landscape 480x320 (rotation 1).
//
//  Derived from mockup_35inch.png, which is a 1440x960 render of this same
//  HUD, i.e. exactly 3x scale. Mockup pixel / 3 == device pixel.
// --------------------------------------------------------------------------
namespace layout {
constexpr int16_t screenW = 480;
constexpr int16_t screenH = 320;

// Outer HUD frame + corner brackets.
constexpr int16_t frameX = 8;
constexpr int16_t frameY = 8;
constexpr int16_t frameW = screenW - 2 * frameX;  // 464
constexpr int16_t frameH = screenH - 2 * frameY;  // 304
constexpr int16_t bracketLen = 28;
constexpr int16_t bracketThick = 3;

// Content column.
constexpr int16_t padX = 20;
constexpr int16_t contentR = screenW - padX;            // 460 (exclusive)
constexpr int16_t contentW = contentR - padX;           // 440

// Rows (y = top edge of the row, h = its height).
constexpr int16_t headerY = 18, headerH = 15;
constexpr int16_t rule1Y = 39;
constexpr int16_t titleY = 46, titleH = 30;
constexpr int16_t briefY = 80, briefLineH = 16, briefMaxLines = 2;
constexpr int16_t rule2Y = 114;
constexpr int16_t targetY = 119, targetH = 30;
// "TARGET" is 62px wide at 9pt bold, so the label needs a 68px box and the
// planet name has to start clear of it.
constexpr int16_t targetLabelW = 68;
constexpr int16_t targetNameX = padX + 80;
constexpr int16_t subY = 152, subH = 15;
constexpr int16_t barY = 172, barH = 26;
constexpr int16_t statY = 206, statH = 46;
constexpr int16_t statGap = 8;
constexpr int16_t rule3Y = 258;
constexpr int16_t warY = 265, warH = 15;
constexpr int16_t footerY = 289, footerH = 15;

// Faction badge sits at the right of the target row.
constexpr int16_t badgeH = 22;
constexpr int16_t badgePadX = 9;

// Icon slots. Bitmaps live in hud_icons.h (generated by tools/gen_icons.py);
// only the surrounding whitespace is described here.
constexpr int16_t headerIconGap = 6;  // emblem -> "SUPER EARTH"
constexpr int16_t badgeIconGap = 5;   // faction icon -> faction name
// Was 150 before the badge carried an icon; the widest label (AUTOMATON)
// needs 158px now.
constexpr int16_t badgeMaxW = 170;
}  // namespace layout
