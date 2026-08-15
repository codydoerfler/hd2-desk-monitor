// ---------------------------------------------------------------------------
//  hud_touch.h — the XPT2046 resistive panel, read for gestures.
//
//  Touch was built once before and reverted; CONTEXT.md recorded that "the
//  calibration would not hold on this panel". That is worth being precise
//  about, because the second attempt only makes sense if the first one's
//  failure is understood.
//
//  The previous attempt is NOT in git history despite what CONTEXT.md and the
//  README claimed (checked: no blob under src/ in any reachable commit
//  mentions getTouch, setTouch, calData or XPT2046). It was reverted before it
//  was ever committed, so the only surviving record is the old README's
//  procedure -- run TFT_eSPI's Touch_calibrate example, feed the five-element
//  array to tft.setTouch(), read positions with tft.getTouch(). That is the
//  library's intended path, and it is almost certainly what was tried.
//
//  Reading TFT_eSPI's Extensions/Touch.cpp, that path has a specific and fatal
//  problem for this use case. getTouch() calls validTouch(), which takes two
//  raw samples a few milliseconds apart and rejects the reading unless they
//  agree to within _RAWERR, a hardcoded 20 ADC counts:
//
//      if (abs(x_tmp - x_tmp2) > _RAWERR) return false;
//
//  That is a deadband designed for buttons -- it asks "is the finger holding
//  still?" A swipe is by definition a finger that is not holding still, so
//  getTouch() rejects most of a drag outright. It also opens with a
//  wait-for-pressure-to-stop-rising loop (`while (z1 > z2) delay(1)`) and
//  repeats the whole validation up to five times per call, which on a moving
//  contact can run tens of milliseconds and still return nothing.
//
//  The symptom of all that, from the outside, is a panel that responds
//  erratically and lands taps in the wrong place -- indistinguishable from
//  calibration that "would not hold", and not fixed by recalibrating.
//
//  So this module keeps TFT_eSPI for the two things it does well -- bus
//  arbitration (touch shares HSPI with the display and the transaction lock
//  has to be shared with it) and the raw conversions themselves, which already
//  follow the datasheet's settle-then-keep-the-last-sample pattern -- and
//  replaces the validation layer above them:
//
//    * Median of several samples, not a stationarity test. A median throws out
//      the occasional wild ADC outlier, which is the actual failure mode on a
//      resistive panel, without assuming the finger is parked.
//    * Pressure gated before and after the burst, which rejects the noisy
//      make/break transients at touch-down and lift-off -- the samples that
//      would otherwise put a tap somewhere near the screen edge.
//    * Calibration stored in NVS in a versioned struct, so it survives a
//      reboot and an OTA and never has to be recompiled in.
//
//  PENIRQ (IO36) is deliberately not used. It would save an SPI read per poll,
//  but GPIO34-39 on the ESP32 are input-only and have no internal pull-ups, so
//  the line's idle state depends on a pull-up this board may or may not
//  populate -- and a pressure read costs ~20us, which is not worth a gamble on
//  a resistive divider we cannot see. It is logged by dumpDiagnostics() so it
//  can be checked on real hardware if it ever becomes interesting.
//
//  >>> UNVERIFIED ON HARDWARE <<< This was written without a board to hand.
//  The reasoning above is from the datasheet, the LCDwiki E32R40T pin table
//  and TFT_eSPI's source; none of it has been confirmed against the panel.
//  dumpDiagnostics() exists to make that confirmation a single serial session
//  rather than a bisect -- see README's "Touch" section.
// ---------------------------------------------------------------------------
#pragma once

#include <TFT_eSPI.h>

namespace touch {

// What a completed contact turned out to be. Reported once, on release --
// there is no "still holding" state, because nothing in this UI needs one.
enum Gesture {
  kNone = 0,
  kTap,         // pressed and released without travelling far
  kSwipeLeft,   // travelled right-to-left: "show me the next card"
  kSwipeRight,  // travelled left-to-right: "go back"
};

// The panel's touch controller is on the same SPI bus, behind the same
// transaction lock, as the display -- so it can only be driven through the
// TFT_eSPI instance that owns them. `tft` must already have begin() and
// setRotation() called on it: calibration is stored per-rotation and is
// meaningless against a different one.
void begin(TFT_eSPI &tft);

// True once a calibration has been loaded from NVS or produced by calibrate().
// Without one, poll() still detects contact and still classifies gestures --
// swipes only need relative motion -- but the coordinates a tap reports are
// raw ADC counts mapped through a rough default, so they may be some way off.
bool calibrated();

// Call from loop(). Cheap when nothing is touching: one pressure conversion,
// about 20us. Returns kNone until a contact ends.
Gesture poll();

// Where the last kTap landed, in screen pixels at the current rotation. Only
// meaningful immediately after poll() returned kTap.
void lastTap(int16_t &x, int16_t &y);

// True while a finger is down. For code that wants "dismiss on any contact"
// without waiting for the gesture classifier to decide what the contact was.
bool pressed();

// Four-corner calibration, drawn on the panel and stored in NVS. Blocks until
// all four targets have been hit or `timeoutMs` elapses; returns false on
// timeout, leaving any existing calibration alone. Safe to call at any time.
bool calibrate(uint32_t timeoutMs = 60000);

// Drops the stored calibration so the next boot runs calibrate() again.
void forget();

// Dumps raw ADC readings, the pressure value, the PENIRQ line and the current
// calibration to serial for a few seconds. This is the tool for answering "is
// the panel wired and reading sane numbers?" before blaming the mapping --
// which is the question the previous attempt never got a straight answer to.
void dumpDiagnostics(uint32_t durationMs = 10000);

}  // namespace touch
