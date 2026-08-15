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

// Politeness gap between the calls that make up one poll. A poll is
// 1 assignments + one planet per Major Order task (up to kMaxOrderTasks = 4)
// + 1 war = up to 6 requests, which at the old 600ms spacing would have put 6
// requests inside a 3.6s window. (The campaigns call only runs in the
// no-order state, which by definition has no per-task planet fetches, so it
// never adds to that worst case.)
//
// The documented limit is 5 requests / 10 s. At spacing d, a 10s window holds
// floor(10/d) + 1 requests; d = 2600ms gives 4, leaving a request of headroom.
// Worst case a poll then takes ~13s of spacing plus transfer time, against a
// 300s poll interval — the only visible cost is that the countdown tile stops
// ticking for those few seconds.
static const uint32_t kInterRequestDelayMs = 2600;

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

// The firmware version the last boot ran, so a boot that follows an OTA can
// tell it is new and announce itself. Same namespace as everything else.
#define HD2_PREFS_FW_KEY "fwVer"

// The touch panel's raw-to-screen mapping, stored as a struct blob (see the
// Calibration struct in hud_touch.cpp). NVS rather than a compiled-in constant
// so a panel is calibrated once, by the person holding it, and keeps that
// across reboots and OTAs -- which is the part the first attempt at touch was
// missing.
#define HD2_PREFS_TOUCH_KEY "touchCal"

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

// --- Major Order card ------------------------------------------------------
// Faction accents. Automaton reuses `red` and Humans reuse `blue`; these are
// the two the existing palette had no entry for.
constexpr uint16_t purple   = rgb565(0xA8, 0x5C, 0xD8);  // #A85CD8 Illuminate
constexpr uint16_t amber    = rgb565(0xE0, 0x8A, 0x2A);  // #E08A2A Terminids

// Card chrome.
constexpr uint16_t cardEdge = rgb565(0x2C, 0x32, 0x3C);  // #2C323C panel hairlines
constexpr uint16_t hatch    = rgb565(0x1A, 0x20, 0x28);  // #1A2028 scene-panel weave
constexpr uint16_t alarm    = rgb565(0x2E, 0x0E, 0x14);  // #2E0E14 alert ribbon fill
constexpr uint16_t flagInk  = rgb565(0x14, 0x10, 0x06);  // #141006 text on the gold flag
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

// LIBCON status chip -- a small colour-coded pill inline in the top row
// (idle header / objective bar), sized to sit comfortably in whatever gap
// that row already has rather than floating outside the frame.
constexpr int16_t libconW = 96;
constexpr int16_t libconH = 18;
// Horizontal clearance between the chip and the sync line, when they share
// a row (the campaign screen's footer).
constexpr int16_t libconGap = 14;

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
// The footer row's own geometry lives with the rest of the footer constants,
// down in "header + footer".

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

// --------------------------------------------------------------------------
//  Cards
//
//  Both card types -- a Major Order target and a liberation campaign -- stack
//  the same bands, so the carousel does not reflow the screen as it advances:
//
//    header    objective type, the LIBCON chip, link state
//    art       the biome plate, with the planet's identity set over it
//    bars      progress: one track for a liberation, two for a defence
//    strip     four values: diver share, divers, the push, the enemy's regen
//    footer    divers present, reward, last sync
//
//  The identity sits *on* the artwork rather than above it. That is what buys
//  the art enough height to read as a photograph instead of a strip, and a
//  left-side scrim (artScrimW/artScrimMax) darkens the plate under the text so
//  the two do not fight -- the alternative, a flat panel behind the words,
//  wastes the same space the art was given.
//
//  The two card types differ only in how much of the band budget the bars
//  need. A defence draws two tracks and a liberation one, so the art absorbs
//  the difference (artOrderH vs artCampH) and the strip still lands on the
//  same row either way.
// --------------------------------------------------------------------------
constexpr int16_t cardX = padX;       // 20
constexpr int16_t cardW = contentW;   // 440

// --- art band -------------------------------------------------------------
constexpr int16_t artY = 46;
constexpr int16_t artCampH = 150;     // one track below it
constexpr int16_t artOrderH = 130;    // two tracks below it
constexpr int16_t artPad = 14;        // text inset from the plate's left edge
// Rows inside the art, relative to artY. The name is set in Anton24px, whose
// yAdvance is 37, so artNameH has to clear that or the glyph tops are cut.
constexpr int16_t artNameDy = 6, artNameH = 38;
constexpr int16_t artSectorDy = 42, artSectorH = 18;
constexpr int16_t artStatDy = 62, artStatH = 28;
// The order's own name, under the headline. Card-side only: a campaign has no
// order to belong to, and its art is taller by exactly this row's absence.
constexpr int16_t artTitleDy = 92, artTitleH = 18;
// The scrim: how far the darkening reaches across the plate, and how dark it
// is at the left edge (0-255, blended toward black).
constexpr int16_t artScrimW = 300;
constexpr uint8_t artScrimMax = 225;

// Faction mark, top-right on the art: the insignia alone on a dark disc. No
// label -- the badge's text lived in a row this design no longer has, and the
// three icons are distinct enough to carry it.
constexpr int16_t markInset = 10;
constexpr int16_t markPad = 7;         // disc radius beyond the icon

// The order's countdown, bottom-right on the art, on its own filled plate:
// the clock ticks, and repainting text straight onto the photograph would
// mean regenerating the artwork under it every second.
//
// Two stacked rows — what the clock is counting down to, then the clock —
// rather than one line reading "VICTORY IN 5h 41m". The one-line form is
// nearly twice as wide, and a plate that wide reaches back across the art
// into the headline beside it.
constexpr int16_t plateInset = 8;
constexpr int16_t platePadX = 8, platePadY = 4;
constexpr int16_t plateLabelH = 11, plateClockH = 18;
constexpr int16_t plateW = 100;

// --- progress bars --------------------------------------------------------
// Each track carries a caption row above it: what the track measures on the
// left, its value and rate on the right.
constexpr int16_t barCapH = 12;
constexpr int16_t campCapY = 202, campBarY = 214, campBarH = 18;
constexpr int16_t orderCap1Y = 182, orderBar1Y = 194;
constexpr int16_t orderCap2Y = 210, orderBar2Y = 222;
constexpr int16_t orderBarH = 12;
// A liberation objective has one track, not two. Rather than leave the
// second's band empty it takes the campaign card's taller bar and sits
// centred in the space both would have used.
constexpr int16_t orderSoloCapY = 194, orderSoloBarY = 206;
constexpr int16_t orderBandEnd = orderBar2Y + orderBarH;

// --- four-value strip -----------------------------------------------------
// Share of the galaxy's divers, the head-count here, the players' push, the
// enemy's regen. Two rows: a caption in the built-in 6x8 GLCD face over the
// value in FONT_LABEL. The captions are what make it readable -- four bare
// percentages in four colours is a puzzle -- and 6x8 is the only face small
// enough for a row this tight, the smallest free font being 9pt.
constexpr int16_t stripCapY = 242, stripCapH = 10;
constexpr int16_t stripValY = 252, stripValH = 22;
constexpr int16_t stripCols = 4;

// --- header + footer ------------------------------------------------------
// Gap between the objective-type word and the LIBCON chip beside it.
constexpr int16_t libconGapX = 14;
// Carousel position pips, in the header row opposite the type word. pipS is
// the active pip; the inactive ones are hollow and pipDimInset smaller on
// every side, because fill alone at 6px was a difference you had to hunt for
// from a desk away. Three pips cost 3*pipS + 2*pipGap = 34px, which still
// clears the WiFi slot with the longest type word in front of them.
constexpr int16_t pipS = 8, pipGap = 5, pipRowGap = 12;
constexpr int16_t pipDimInset = 2;
// The right-hand slot the WiFi label and dot own on the header row. Also what
// the rest of that row is cleared *up to* -- see drawStatusHeader().
constexpr int16_t wifiSlotW = 110;
// Footer: the order's reward at the left, the sync clock at the right.
//
// Deeper than the 18px it was: the reward is the row's headline now that the
// diver count has gone (it was a second copy of the strip's DIVERS column) and
// it is set in FONT_VALUE, which needs the height. The row still ends clear of
// the frame's bottom bracket at y=309.
constexpr int16_t footerY = 280, footerH = 24;
constexpr int16_t footDotR = 5;  // the WiFi dot, on the header row
// Sync clock, right-aligned: a small ring-and-hands glyph and HH:MM in the
// built-in 6x8 face. A staleness indicator reads as one at caption size --
// spelling out "SYNCED" in body text spent 150px of the row on a clock.
constexpr int16_t syncGlyphR = 5;
constexpr int16_t syncGap = 6;      // glyph -> time
constexpr int16_t syncBoxW = 96;    // fixed, so a shrinking string leaves no tail
}  // namespace layout
