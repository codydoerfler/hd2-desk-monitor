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
//  Local time
//
//  Clock times on screen (the "SYNCED hh:mm" footer) are UTC plus this offset.
//  Durations — the Major Order countdown — are timezone-independent and are
//  never shifted.
//
//  The offset is normally set at runtime, in the WiFi setup portal, and kept
//  in NVS; the constant below is only the factory default used until someone
//  sets one. 0 means the display shows UTC, which is what a freshly flashed
//  unit does. There is no DST handling: a fixed offset is the whole feature.
// --------------------------------------------------------------------------
static const int16_t kUtcOffsetMinutesDefault = 0;

// Range of real-world civil offsets, UTC-12:00 to UTC+14:00.
static const int16_t kUtcOffsetMinutesMin = -12 * 60;
static const int16_t kUtcOffsetMinutesMax = 14 * 60;

// NVS namespace + key the offset is persisted under.
#define HD2_PREFS_NS "hd2"
#define HD2_PREFS_TZ_KEY "utcOffMin"

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

// Major Order header bar. The bar's own fill is the artwork in
// hud_header_art.h; these are the two things drawn over it.
constexpr uint16_t hdrEdge  = rgb565(0x32, 0x36, 0x2C);  // #32362C hairline border
constexpr uint16_t hdrSkull = rgb565(0x14, 0x16, 0x1C);  // #14161C skull inside the disc
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
//
// The HUD is deliberately icon-led: there is no briefing paragraph and no
// sector/biome subtitle line, and the three stat tiles carry an icon instead
// of an ALL-CAPS label. That buys the taller target row, progress bar and
// tiles below.
//
// headerY/headerH and rule1Y are the plain "SUPER EARTH" strip, which is what
// the boot, WiFi-setup and no-order screens still use. The Major Order screen
// replaces it with the header bar below, which occupies the same top-of-card
// space and absorbs the order title that used to sit on its own row.
constexpr int16_t headerY = 18, headerH = 15;
constexpr int16_t rule1Y = 39;
constexpr int16_t rule2Y = 84;
constexpr int16_t targetY = 90, targetH = 34;
constexpr int16_t barY = 132, barH = 36;
constexpr int16_t tileY = 180, tileH = 68, tileGap = 7;
constexpr int16_t rule3Y = 260;
constexpr int16_t footerY = 285, footerH = 15;

// Major Order header bar — the skull badge, the Anton-set order title and the
// Earth/gold-sweep background wash, in place of the "SUPER EARTH" strip.
//
// Traced from tools/assets/header_mockup_reference.py, whose draw_header_bar()
// works in fractions of the bar height: the badge is 0.82*h square, inset
// 0.12*h from the left, with a 0.18*h gap before the title. Those are resolved
// here for h = 58 so the layout stays greppable. The artwork itself is
// generated by tools/gen_header_art.py, which mirrors the same constants.
constexpr int16_t hdrX = padX;        // 20
constexpr int16_t hdrY = 18;
constexpr int16_t hdrW = contentW;    // 440
constexpr int16_t hdrH = 58;          // bottom edge at 75, clear of rule2Y
constexpr int16_t hdrBadgeX = 6;      // bar-relative, = int(0.12 * hdrH)
constexpr int16_t hdrTitleGap = 10;   // badge -> title, = int(0.18 * hdrH)
// Right-hand slot the WiFi state takes over, measured in from the bar's right
// edge, and the padding that keeps it off the bar's border.
constexpr int16_t hdrWifiW = 100;
constexpr int16_t hdrWifiPad = 10;

// Inside a stat tile: icon top-left, value on the row beneath it.
constexpr int16_t tileInsetX = 12;
constexpr int16_t tileIconDy = 9;
constexpr int16_t tileValueDy = 38, tileValueH = 24;

// Faction badge sits at the right of the target row.
constexpr int16_t badgeH = 30;
constexpr int16_t badgePadX = 9;

// Icon slots. Bitmaps live in hud_icons.h (generated by tools/gen_icons.py);
// only the surrounding whitespace is described here. The x of the planet name
// is derived from the crosshair's width in hud_renderer.cpp.
constexpr int16_t headerIconGap = 6;  // emblem -> "SUPER EARTH"
constexpr int16_t badgeIconGap = 6;   // faction icon -> faction name
constexpr int16_t targetIconGap = 8;  // crosshair -> planet name
constexpr int16_t tagGap = 16;        // planet name -> sector tag
constexpr int16_t badgeGap = 12;      // name/tag block -> faction badge
constexpr int16_t footerIconGap = 6;  // medal -> reward amount
// The widest label (AUTOMATON) needs 165px alongside a 20px faction icon.
constexpr int16_t badgeMaxW = 175;

// Two-up boxes on the WiFi setup screen — the only place drawStatBox() is
// still used now that the body runs on icon tiles.
constexpr int16_t statY = 160, statH = 46, statGap = 8;
}  // namespace layout
