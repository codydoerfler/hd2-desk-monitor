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

// Set the first time the forced first-boot calibration prompt is put on the
// panel, and never cleared. Separate from the calibration itself because
// "there is no calibration" and "this unit has never been set up" are
// different questions: a panel someone deliberately cleared with forget(), or
// one whose stored blob a firmware update made unreadable, has been through
// setup already and must not have the boot stopped for it again. Only a unit
// with neither a blob nor this flag is genuinely fresh out of the box.
#define HD2_PREFS_TOUCH_SETUP_KEY "touchSetup"

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
// The calibration-success accent. Not `green`: that one is the link-state dot,
// a muted sea green picked to sit quietly in a footer, and the reference art's
// success card is a bright yellow-green that carries a whole screen the way
// `gold` does. Same three-step family as the gold set — accent, border, hatch.
constexpr uint16_t lime     = rgb565(0x8C, 0xC6, 0x3F);  // #8CC63F success accent
constexpr uint16_t limeDim  = rgb565(0x59, 0x7E, 0x28);  // #597E28 borders
constexpr uint16_t limeMute = rgb565(0x35, 0x4C, 0x18);  // #354C18 hazard hatch

// The two Major Order verdict accents, sampled off mo_verdict_reference.jpg.
//
// Neither is `green`/`red`: those are the link-state dot and the enemy-regen
// tint, both picked to sit quietly inside a card, and a verdict accent has to
// carry a whole screen the way `gold` does. Nor is the success one `lime` --
// that came off the calibration reference and is an electric yellow-green,
// where this reference's pair are deliberately muted, a sage and a brick.
// Same three-step shape as the gold and lime families: accent, border, hatch.
constexpr uint16_t sage      = rgb565(0x8F, 0xB5, 0x6A);  // #8FB56A success accent
constexpr uint16_t sageDim   = rgb565(0x5A, 0x73, 0x42);  // #5A7342 borders
constexpr uint16_t sageMute  = rgb565(0x33, 0x44, 0x28);  // #334428 hazard hatch
constexpr uint16_t brick     = rgb565(0xA8, 0x45, 0x3A);  // #A8453A failure accent
constexpr uint16_t brickDim  = rgb565(0x6B, 0x2A, 0x23);  // #6B2A23 borders
constexpr uint16_t brickMute = rgb565(0x3E, 0x18, 0x15);  // #3E1815 hazard hatch
// What a tear in the failure screen's flag shows through. Not the sky behind
// it -- the plate is one photograph and there is nothing behind it to sample
// -- but the smoke the reference's shredded flag is hanging in.
constexpr uint16_t smoke     = rgb565(0x26, 0x14, 0x0F);  // #26140F

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

// --- Destroyer bridge ------------------------------------------------------
// The backdrop the two calibration screens are set against: in the reference
// art it is a photograph of a ship's bridge with a planet filling the viewport
// behind the title. Reproduced as five flat tones rather than a photograph --
// see the note over the cal* constants below for what that does and does not
// get you. Every one of them is a near-black, deliberately: this is scenery
// behind a card, and anything with real contrast in it would compete with the
// card for a panel that only has 480x320 to give.
constexpr uint16_t voidBlue = rgb565(0x08, 0x0C, 0x14);  // #080C14 space
constexpr uint16_t discFill = rgb565(0x10, 0x18, 0x26);  // #101826 the planet
constexpr uint16_t discRim  = rgb565(0x2A, 0x42, 0x5E);  // #2A425E its lit limb
constexpr uint16_t hull     = rgb565(0x0D, 0x11, 0x18);  // #0D1118 ship structure
constexpr uint16_t hullEdge = rgb565(0x1C, 0x24, 0x30);  // #1C2430 its lit edges
constexpr uint16_t diverInk = rgb565(0x1C, 0x23, 0x2E);  // #1C232E the silhouette
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
// The uncalibrated-touch hint sits between the reward and the clock, in the
// gap the footer rework deliberately left empty. Its box is derived from the
// reward block's own width in hud_renderer.cpp rather than named here, because
// both of that block's terms (the medal icon and the reward box) live there.

// --------------------------------------------------------------------------
//  The two touch calibration screens
//
//  Traced from touch_required_reference.jpg and touch_success_reference.jpg at
//  this panel's own 480x320 rather than scaled from them. Both are the same
//  screen -- destroyer bridge behind a centred HELLDIVERS II wordmark, one
//  card below it, a hatched band along the card's bottom edge -- and differ
//  only in the card's accent colour, its chip, its icon and its copy. So
//  everything here is shared by both, and hud_renderer.cpp branches on an
//  accent colour rather than carrying two layouts.
//
//  Neither screen draws the HUD frame or the SUPER EARTH strip. The reference
//  has neither: the card floats on the bridge. That is also what drawOverlay()
//  already does for the two event screens, so a full-bleed status screen is not
//  a new idea here.
//
//  Where the two do not match the reference, and why:
//
//   * The card is 92% of the screen's width where the reference's is 60%. The
//     body copy is set at 9pt, which is the floor on this panel, and the
//     widest line of it is 303px. Add the reticle, the gaps and the padding
//     and the card cannot be narrower than about 430 without the copy
//     rewrapping to six lines, which the body band has no room for. Everything
//     that costs is horizontal: the reference's diver stands in the margin
//     this does not have.
//   * The wordmark is 35% of the width where the reference's is 24%, i.e. it
//     is scaled up by half again. Strictly proportional would be 113px of
//     "HELLDIVERS", which is ten letters in ten pixels of cap height.
//
//  Vertically the proportions do survive: the free space above and below the
//  card splits 64:36, against the reference's 63:37.
// --------------------------------------------------------------------------
// The wordmark, centred over the card. Not a new bitmap -- hud_icons.h already
// carries this mark at 440x172 for the boot screen, and drawWordmark() box-
// filters that cut down to this box. See the note there.
constexpr int16_t calLogoW = 168, calLogoH = 66;
constexpr int16_t calLogoX = (screenW - calLogoW) / 2;  // 156
constexpr int16_t calLogoY = 9;
// The planet in the viewport, behind the wordmark. Sized and placed off the
// reference: its lit limb breaks the top of the screen at y=20 and its
// diameter is 42% of the screen's width. Most of it is behind the card, which
// is also true of the reference.
constexpr int16_t calDiscX = screenW / 2, calDiscY = 121, calDiscR = 101;
// The diver. In the reference this is a full figure filling the left fifth of
// the frame; there is no left fifth here (see above), so what is left is a
// helmet and a shoulder in the band beside the wordmark.
constexpr int16_t calDiverX = 14, calDiverY = 26;
constexpr int16_t calDiverW = 48, calDiverH = 58;

constexpr int16_t calCardX = padX;      // 20
constexpr int16_t calCardW = contentW;  // 440
constexpr int16_t calCardY = 84;
constexpr int16_t calCardH = 190;
constexpr int16_t calHeadH = 40;        // header band, to the rule under it
constexpr int16_t calStripeH = 30;      // hatched band, below its own rule
// Header chip: an accent tab with the state's mark knocked out of it, at the
// left of the header. The trailing edge slants outward on the way down -- the
// tab is wider at the bottom than the top -- which is the reference's, and is
// what stops it reading as a plain rectangle at this size. Both screens use
// it: a warning triangle on one, a check in a disc on the other.
constexpr int16_t calBadgeInset = 10, calBadgeW = 40, calBadgeH = 28;
constexpr int16_t calBadgeSlant = 8;
constexpr int16_t calTitleGap = 16;     // chip -> "TOUCH CALIBRATION ..."
// Body: the icon on the left, the copy on the right.
constexpr int16_t calIconInset = 14, calIconS = 92;
constexpr int16_t calTextGap = 16;      // icon box -> text column
constexpr int16_t calTextPadR = 10;     // text column -> the card's border
// Text rows, relative to the top of the body band.
//
// These are the only rows on the panel set in mixed case, so they are also the
// only ones that have to reserve descender space -- 17px of glyph against the
// 13px every ALL-CAPS row elsewhere gets away with. They are drawn TL_DATUM
// rather than the ML_DATUM the rest of the HUD uses, which is what keeps them
// this tight: TFT_eSPI centres a free font on its ascent alone (drawString()
// adds glyph_ab, then ML_DATUM subtracts only half of it), so a middle-datum
// row carrying a 'y' needs 25px to avoid clipping the tail. Top-datum puts the
// baseline a fixed 13px down and needs 19. Four of these fit the band; four of
// those would not.
constexpr int16_t calAddrDy = 8, calAddrH = 20;   // "Helldiver," / the headline
constexpr int16_t calCopyDy = 30;                 // first body line
constexpr int16_t calLineH = 20;                  // 19px of need + 1 of slack
constexpr int16_t calParaGap = 6;
// Hazard hatch: pitch along the x axis, drawn at 45 degrees.
//
// Thin, and in the dim member of the accent's family rather than the bright
// one. The earlier card had this at 7px of ink on a 14px pitch, which is a
// road-works barrier; the reference's band is a fine hatch you read as texture
// and not as a second headline.
constexpr int16_t calStripePitch = 10, calStripeInk = 2;
// The line the band carries, centred, on a plate cut out of the hatch. The
// reference puts FOR SUPER EARTH! here on the success screen and leaves the
// band empty on the other -- but the prompt has to say what to do with itself
// somewhere, and this band is the one place on the screen that is neither the
// card's message nor the bridge behind it.
constexpr int16_t calBandTextW = 300, calBandTextH = 20;
constexpr int16_t calBandTextX = calCardX + (calCardW - calBandTextW) / 2;  // 90
// The wing marks flanking that line on the success screen: three stacked bars,
// tapering away from the text, mirrored either side of it.
constexpr int16_t calMarkW = 22, calMarkGap = 14;

// --------------------------------------------------------------------------
//  The three Major Order event overlays
//
//  Traced from mo_new_reference.jpg (the announcement) and the two stacked
//  mockups in mo_verdict_reference.jpg (success, failure). All three are the
//  same composition -- a dark text panel down the left with an angled right
//  edge, the flag photograph filling the right, an accent hairline along the
//  divider -- and differ in accent colour, badge, copy and how the art is
//  graded. So the geometry below is shared by all three and hud_renderer.cpp
//  branches on a colour family, the way the two calibration screens already
//  do.
//
//  Proportions against the references:
//
//   * The art takes 50% of the width. The announcement reference gives it 60%
//     and the verdict pair 49%, but the announcement reference is 1179px wide
//     and this panel is 480: a proportional 40% text column is 192px, and the
//     briefing does not wrap into that at 9pt, which is the floor here. 50%
//     splits the difference and is inside the range the references span.
//   * The divider leans 30px left over the full height. The announcement
//     reference leans 35px over 620 and the verdict pair 65px over 330; this
//     is between them, nearer the announcement, because on a 320px-tall panel
//     the verdict slant would eat the bottom of the text column.
//   * The art plate is a photograph, at half resolution and scaled up -- see
//     tools/gen_mo_art.py for the byte arithmetic that forces that, and for
//     why one plate is graded three ways instead of three plates.
//
//  What does not match: the reference's announcement art is a blue night, its
//  success a brighter cloudy sky and its failure a burning red one, three
//  separate photographs. There is room for one. The success and failure are
//  that one re-graded on the fly, so their skylines are the announcement's
//  buildings under a different sky rather than genuinely different cities.
// --------------------------------------------------------------------------
// The art plate: drawn at 2x moart::kW/kH, flush to the right edge.
constexpr int16_t ovlArtW = 240;
constexpr int16_t ovlArtX = screenW - ovlArtW;  // 240
// The panel's angled right edge, top and bottom. The art starts left of
// ovlEdgeBot so the divider never uncovers a gap at the foot of the screen.
constexpr int16_t ovlEdgeTop = 272, ovlEdgeBot = 242;
constexpr int16_t ovlEdgeInk = 2;   // the accent hairline riding that edge
// Text column: left margin, and the clearance it keeps off the angled edge.
constexpr int16_t ovlPadX = 22, ovlTextPadR = 14;
// The vertical hazard stripe down the far left. The announcement reference has
// one; neither verdict does, so neither of those draws it.
constexpr int16_t ovlStripeW = 10;
// The hatched band along the panel's foot. The verdict references have one;
// the announcement uses the space for its closing line instead.
constexpr int16_t ovlBandH = 24;

// Header row, shared: a badge at the left margin, the title beside it, a rule
// under both with the reference's angled tail on its right end.
constexpr int16_t ovlHdrY = 12, ovlBadgeS = 34, ovlTitleGap = 12;
constexpr int16_t ovlRuleY = 52, ovlRuleTail = 10;
// The announcement's badge carries wing bars either side of its disc; they sit
// outside ovlBadgeS and so push the title along.
constexpr int16_t ovlWingW = 14, ovlWingGap = 3;

// --- announcement rows ----------------------------------------------------
// The briefing, in the reference's prose-block position and, like it, four
// lines. The lead is 19 and not the 17 the glyphs need, for the reason spelled
// out over calCopyDy: these are mixed-case rows drawn TL_DATUM, the baseline
// lands a fixed 13px down, and an opaque-background row 17px below this one
// would repaint over this one's descenders.
//
// Four lines of this column is ~160 characters and High Command writes longer,
// so wrapText() ellipses. The order's title is not in here -- it is in the
// objective card below, in full, which is where the reference puts the thing
// the screen is actually announcing.
constexpr int16_t ovlBriefY = 62, ovlBriefLead = 19, ovlBriefMax = 4;
// "=== OBJECTIVE ===" and "=== REWARD ===", the reference's section rules.
constexpr int16_t ovlDivH = 18;
constexpr int16_t ovlObjDivY = 150;
constexpr int16_t ovlObjCardY = 168, ovlObjCardH = 66;
constexpr int16_t ovlObjLead = 22;   // title rows inside that card
constexpr int16_t ovlRwdDivY = 242;
constexpr int16_t ovlRwdRowY = 262, ovlRwdRowH = 28;
// The closing line: a wing mark and the dismiss hint, where the reference has
// its eagle and PREPARE. DEPLOY. COMPLETE.
constexpr int16_t ovlFootY = 296, ovlFootH = 18;

// --- verdict rows ---------------------------------------------------------
// Three lines of the reference's own closing prose, then its accent line, then
// the two rows this device has that a marketing mockup does not: how many
// objectives were met, and what the order paid.
constexpr int16_t ovlVerdProseY = 66, ovlVerdLead = 20;
constexpr int16_t ovlVerdAccentY = 134, ovlVerdAccentH = 20;
constexpr int16_t ovlVerdStatY = 162, ovlVerdStatH = 22;
// The reference's star row: five stars between two wing marks.
constexpr int16_t ovlVerdStarY = 228, ovlVerdStarR = 7, ovlVerdStarGap = 20;
// The dismiss hint, above the hatched band.
constexpr int16_t ovlVerdDismissY = 262, ovlVerdDismissH = 18;

// --- the one-day bulletin -------------------------------------------------
// Nothing above applies to it: hud_bulletin_art.h is a finished card, drawn
// edge to edge with no panel, no divider and no header, so the only geometry
// it needs is where the dismiss hint goes.
//
// The card leaves a black strip under its footer rule once cropped to 3:2 --
// tools/gen_bulletin_art.py slides the crop down to keep exactly that -- and
// the hint sits in it at the left, clear of the "THIS IS NOT A DRILL" line
// above and of the chevrons that flank it. Measured off the scaled plate
// rather than guessed: in the x span below, rows 297-303 carry that line's
// rule and rows 304 down are black. So the box is one row into the rule and
// sixteen into the strip, which keeps it within a row of the ovlFootH=18 the
// other three overlays set this same string in. Any less and FONT_LABEL's
// caps start getting clipped to protect a hairline nobody can see.
constexpr int16_t ovlBulHintX = 8, ovlBulHintW = 150;
constexpr int16_t ovlBulHintY = screenH - 17, ovlBulHintH = 17;
}  // namespace layout
