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

// Politeness gap between the calls that make up one poll. A poll is now
// 1 assignments + one planet per Major Order task (up to kMaxOrderTasks = 4)
// + 1 war = up to 6 requests, which at the old 600ms spacing would have put 6
// requests inside a 3.6s window.
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
// Campaign screen — what stands in for the idle screen when there is no Major
// Order but the galaxy is still fighting. Shares the tile row and footer with
// the idle screen; only the block above the tiles differs, so these all sit
// between the header rule and tileY below.
// Identity block, then the biome plate, then the readout. The plate is the
// only photographic element on any screen, so everything else is pushed clear
// of it rather than overlaid -- text on a photograph at this size is unreadable
// however it is treated.
// kCampNameH must clear FONT_HEADLINE's yAdvance (Anton24px = 37) or the
// sprite clips the tops of the glyphs.
constexpr int16_t kCampNameY = 44, kCampNameH = 38;
constexpr int16_t kCampSectorY = 83, kCampSectorH = 16;
constexpr int16_t kCampBadgeY = 50, kCampBadgeH = 22;
constexpr int16_t kCampBiomeY = 104, kCampBiomeH = 84;
constexpr int16_t kCampBarY = 194;
constexpr int16_t kCampPctY = 218, kCampPctH = 24;
// Four-value strip along the bottom, mirroring the companion app's: share of
// the galaxy's divers, divers here, the players' push, the enemy's regen.
//
// Two rows: a caption in the built-in 6x8 GLCD font over the value in
// FONT_LABEL. The captions are what make the strip readable -- four bare
// percentages in four colours is a puzzle, not a readout -- and 6x8 is the
// only face small enough to caption a row this tight, the smallest free font
// being 9pt.
//
// Note this block runs to 278, past rule3Y (260). The campaign screen
// therefore draws no rule3: it used to, and the line went straight through
// the digits. See drawCampaignBody().
constexpr int16_t kCampStripY = 246, kCampStripCapH = 10;
constexpr int16_t kCampStripValY = 256, kCampStripValH = 22;
constexpr int16_t kCampStripCols = 4;

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
//  Major Order card
//
//  The card is one target's worth of the order, and the order's targets take
//  turns in it. Seven bands, top to bottom, all sharing the content column:
//
//    objective bar   status word, the defence clock, the order's own clock
//    identity        globe, planet name, sector, who holds it
//    alert ribbon    the invasion callout, or the objective's plain status
//    scene           biome, the hazards the API actually reported, the crest
//    bars            defence progress and the invader's remaining share
//    verdict         the literal percentage, and winning/losing
//    footer          divers present, reward, last sync, link state
//
//  Row rectangles are fixed. A liberate objective has no event, so it draws a
//  single centred bar and a plain status ribbon, but it does not move anything
//  — a carousel that reflowed between targets would be unreadable.
// --------------------------------------------------------------------------
constexpr int16_t cardX = padX;       // 20
constexpr int16_t cardW = contentW;   // 440

constexpr int16_t objY = 14, objH = 30;
constexpr int16_t idyY = 48, idyH = 54;
constexpr int16_t ribY = 105, ribH = 22;
constexpr int16_t sceneY = 130, sceneH = 60;
constexpr int16_t barsY = 195, cardBarH = 20, barsGap = 3;
constexpr int16_t verdictY = 242, verdictH = 26;
constexpr int16_t cardRuleY = 274;
constexpr int16_t cardFootY = 283, cardFootH = 18;

// Objective bar: icon slot, then the status word, then the carousel pips.
constexpr int16_t objPad = 10;
constexpr int16_t objIconGap = 8;
constexpr int16_t objPipGap = 18;
// The gold flag carrying the order's own expiry, at the bar's right end. `cut`
// is the horizontal run of its diagonal leading edge.
constexpr int16_t flagPad = 11;
constexpr int16_t flagCut = 11;
// "VICTORY IN:" and its clock, right-aligned against the flag.
constexpr int16_t victoryGap = 14;

// Identity row: globe, name/sector block, owner badge.
constexpr int16_t idyPad = 12;
constexpr int16_t idyIconGap = 12;
constexpr int16_t idyNameY = 3, idyNameH = 30;   // row-relative
constexpr int16_t idySectorY = 33, idySectorH = 17;

// Alert ribbon: hazard-stripe blocks at each end, callout centred between.
constexpr int16_t ribStripeW = 46;
constexpr int16_t ribStripePitch = 7;
constexpr int16_t ribIconGap = 8;

// Scene panel: the order's own title, the biome and the reported hazards
// stacked on the left, the device crest on the right. There is no per-planet
// artwork in the public API, so the panel is a tinted weave rather than a
// photograph, and nothing is drawn here that the API did not supply.
constexpr int16_t scenePad = 12;
constexpr int16_t chipGap = 9;
constexpr int16_t sceneTitleDy = 4, sceneTitleH = 17;   // row-relative
constexpr int16_t sceneBiomeDy = 21, sceneBiomeH = 22;
constexpr int16_t sceneChipDy = 44;
constexpr int16_t sceneWeavePitch = 4;

// Progress bars: track on the left, %/h readout in a fixed column on the
// right, so the two bars' readouts line up whatever their fills are doing.
constexpr int16_t rateW = 116;
constexpr int16_t rateGap = 10;
constexpr int16_t trackW = cardW - rateW - rateGap;  // 314
constexpr int16_t rateIconGap = 5;

// Verdict row.
constexpr int16_t verdictIconGap = 7;

// Footer: divers on the left, reward centred, sync + link state on the right.
constexpr int16_t footIconGap = 6;
constexpr int16_t footDotR = 5;
}  // namespace layout
