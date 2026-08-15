#include "hud_touch.h"

#include <Preferences.h>

#include "config.h"
#include "hud_fonts.h"

namespace touch {
namespace {

// The display's instance. Touch shares its SPI bus and its transaction lock,
// so every conversion has to go through it -- see the header for why this
// module borrows the panel rather than opening its own bus.
TFT_eSPI *panel = nullptr;

// --- pressure --------------------------------------------------------------
//
// TFT_eSPI's getTouchRawZ() returns 4095 + Z1 - Z2, which is 0 with nothing on
// the panel (Z1 pulled to 0, Z2 to full scale) and climbs with contact force.
// The library's own default gate is 350; this sits above it because a swipe
// only has to clear the bar while moving, and a *low* gate is what lets the
// noisy fringe of a lift-off register as a contact somewhere near the edge of
// the screen.
constexpr uint16_t kPressureMin = 400;

// --- sampling --------------------------------------------------------------
//
// Odd, so the median is a real sample rather than an average of two. Five raw
// conversions cost about 310us at SPI_TOUCH_FREQUENCY, which is nothing
// against the 15ms poll interval below.
constexpr uint8_t kSamples = 5;

// If the samples in one burst disagree by more than this many ADC counts, the
// burst straddled something -- a lift-off, a bounce, a hand brushing past --
// and averaging across it would land the point somewhere the finger never was.
// A genuinely fast drag moves maybe 40 counts across a burst, so this is loose
// enough not to reject real motion.
constexpr uint16_t kSpreadMax = 300;

// How often poll() actually touches the SPI bus. The loop calls it far more
// often than this, and a resistive panel has nothing to say at 1kHz.
constexpr uint32_t kPollIntervalMs = 15;

// --- gestures --------------------------------------------------------------
//
// A resistive panel drops readings mid-drag whenever the finger's contact
// patch changes shape, which is constantly. Ending the contact on the first
// missed sample would chop one swipe into three, so a release has to be quiet
// for this long before it counts as a release.
constexpr uint32_t kReleaseMs = 70;

// Travel, in screen pixels, that separates a swipe from a tap that wandered.
// The screen is 480 wide, so this is an eighth of it -- comfortably more than
// a finger rolls during a press, comfortably less than a deliberate flick.
constexpr int16_t kSwipeMinPx = 60;
constexpr int16_t kTapMaxPx = 24;

// A swipe that took longer than this was a drag, and a drag with nothing to
// drag is more likely a finger resting on the panel than an instruction.
constexpr uint32_t kSwipeMaxMs = 900;

// A contact this long is something leaning on the screen. Abandoned rather
// than classified, so a stack of papers cannot page the carousel.
constexpr uint32_t kMaxContactMs = 4000;

// After reporting a gesture, ignore the panel briefly: resistive contacts
// bounce on release, and the bounce is easily long enough to look like a
// second tap.
constexpr uint32_t kLockoutMs = 250;

// --- persistence -----------------------------------------------------------

// Bumped whenever the struct below changes shape, so a stored blob from an
// older firmware is discarded rather than reinterpreted. An OTA can move a
// device between firmware versions without anyone present to notice a panel
// that has started reporting nonsense.
constexpr uint16_t kCalVersion = 1;

// The mapping from raw ADC counts to screen pixels.
//
// Stored as "the raw reading at each edge" rather than as min/max plus invert
// flags, which is how TFT_eSPI stores it. The spans are signed, so an axis
// that runs backwards is simply a negative span and the same arithmetic
// handles it -- there is no invert flag to get out of step with the values it
// is supposed to describe.
struct Calibration {
  uint16_t version;
  uint8_t swapAxes;  // the panel's raw X runs along the screen's vertical
  uint8_t rotation;  // the setRotation() this was taken at; see below
  int16_t xAtLeft, xAtRight;
  int16_t yAtTop, yAtBottom;
};

// A starting guess, used until someone runs calibrate(). It is a guess, and
// begin() says so on the serial log.
//
// The panel's raw axes are fixed to the glass, which is native portrait
// (320x480). At rotation 1 the screen's X therefore runs along the panel's
// raw Y, hence swapAxes. Both spans are written high-to-low because that is
// the more common handedness on these modules -- but "more common" is exactly
// the kind of claim calibration exists to settle, and a wrong guess here is
// visible immediately (taps land mirrored) rather than subtly.
constexpr Calibration kDefaultCal = {
    kCalVersion, /*swapAxes=*/1, /*rotation=*/1,
    /*xAtLeft=*/3800, /*xAtRight=*/300,
    /*yAtTop=*/3800,  /*yAtBottom=*/300,
};

Calibration cal = kDefaultCal;
bool haveCal = false;

// --- gesture state ---------------------------------------------------------

bool down = false;
int16_t startX = 0, startY = 0;   // screen px, where the contact began
int16_t curX = 0, curY = 0;       // screen px, most recent valid reading
int16_t tapX = 0, tapY = 0;       // screen px of the last reported tap
uint32_t downMs = 0;              // millis() at contact start
uint32_t lastSeenMs = 0;          // millis() of the last valid reading
uint32_t nextPollMs = 0;
uint32_t lockedUntilMs = 0;

// Deadlines are compared as signed differences throughout this file, never as
// `millis() < deadline`. The plain comparison wedges across the 49.7-day
// millis() rollover -- a deadline stamped just before the wrap stays "in the
// future" for another full wrap, which would leave the panel unresponsive for
// seven weeks on a device that is meant to sit on a desk and never be touched
// by a programmer again. Same idiom main.cpp uses for its poll scheduling.
inline bool reached(uint32_t deadline) { return (int32_t)(millis() - deadline) >= 0; }

// --- raw reading -----------------------------------------------------------

uint16_t medianOf(uint16_t *v, uint8_t n) {
  // Insertion sort. n is 5; anything cleverer would be longer than this.
  for (uint8_t i = 1; i < n; i++) {
    const uint16_t k = v[i];
    int8_t j = i - 1;
    while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; }
    v[j + 1] = k;
  }
  return v[n / 2];
}

uint16_t spreadOf(const uint16_t *sorted, uint8_t n) { return sorted[n - 1] - sorted[0]; }

// One filtered reading in raw ADC counts. False means "nothing usable", which
// covers no contact, too light a contact, and a burst that spanned a
// transition -- the caller does not need to tell those apart.
bool readRaw(uint16_t &rx, uint16_t &ry, uint16_t &rz) {
  rz = panel->getTouchRawZ();
  if (rz < kPressureMin) return false;

  uint16_t xs[kSamples], ys[kSamples];
  for (uint8_t i = 0; i < kSamples; i++) panel->getTouchRaw(&xs[i], &ys[i]);

  // Pressure is checked again on the way out. A burst that began under a
  // finger and ended in mid-air reads perfectly plausible numbers for its
  // first samples and garbage for the rest, and the median cannot tell.
  if (panel->getTouchRawZ() < kPressureMin) return false;

  rx = medianOf(xs, kSamples);
  ry = medianOf(ys, kSamples);
  if (spreadOf(xs, kSamples) > kSpreadMax) return false;
  if (spreadOf(ys, kSamples) > kSpreadMax) return false;
  return true;
}

int16_t clamp16(int32_t v, int16_t lo, int16_t hi) {
  return (int16_t)(v < lo ? lo : (v > hi ? hi : v));
}

// Raw counts to screen pixels, through the stored calibration.
void toScreen(uint16_t rx, uint16_t ry, int16_t &sx, int16_t &sy) {
  int32_t ax = rx, ay = ry;
  if (cal.swapAxes) { const int32_t t = ax; ax = ay; ay = t; }

  const int32_t xSpan = (int32_t)cal.xAtRight - cal.xAtLeft;
  const int32_t ySpan = (int32_t)cal.yAtBottom - cal.yAtTop;
  // A zero span would mean a calibration taken without moving between two
  // corners. Refusing to divide is better than a trap, and the clamp below
  // turns the result into a harmless screen-centre reading.
  sx = xSpan ? clamp16((ax - cal.xAtLeft) * (layout::screenW - 1) / xSpan, 0,
                       layout::screenW - 1)
             : layout::screenW / 2;
  sy = ySpan ? clamp16((ay - cal.yAtTop) * (layout::screenH - 1) / ySpan, 0,
                       layout::screenH - 1)
             : layout::screenH / 2;
}

// --- NVS -------------------------------------------------------------------

bool loadCal() {
  Preferences p;
  if (!p.begin(HD2_PREFS_NS, /*readOnly=*/true)) return false;
  Calibration stored{};
  const size_t n = p.getBytes(HD2_PREFS_TOUCH_KEY, &stored, sizeof(stored));
  p.end();
  if (n != sizeof(stored) || stored.version != kCalVersion) return false;

  // Calibration is a mapping onto a particular screen orientation; applying
  // one taken at a different rotation is worse than having none, because it
  // looks like it is working.
  if (stored.rotation != panel->getRotation()) {
    Serial.printf("[touch] stored calibration is for rotation %u, running %u — ignored\n",
                  (unsigned)stored.rotation, (unsigned)panel->getRotation());
    return false;
  }
  cal = stored;
  return true;
}

void saveCal() {
  Preferences p;
  if (!p.begin(HD2_PREFS_NS, /*readOnly=*/false)) return;
  p.putBytes(HD2_PREFS_TOUCH_KEY, &cal, sizeof(cal));
  p.end();
}

// --- calibration UI --------------------------------------------------------

// Where the four targets sit. Inset from the corners because a resistive
// panel's response is least linear at the very edge, and because a target in
// the literal corner is half off the glass.
constexpr int16_t kTargetInset = 34;

void drawTarget(int16_t x, int16_t y, uint16_t colour) {
  panel->drawCircle(x, y, 12, colour);
  panel->drawCircle(x, y, 4, colour);
  panel->drawFastHLine(x - 18, y, 37, colour);
  panel->drawFastVLine(x, y - 18, 37, colour);
}

void calibrationChrome(const char *line) {
  panel->fillScreen(theme::bg);
  panel->setFreeFont(FONT_LABEL);
  panel->setTextColor(theme::gold, theme::bg);
  panel->setTextDatum(MC_DATUM);
  panel->drawString("TOUCH CALIBRATION", layout::screenW / 2, layout::screenH / 2 - 16);
  panel->setFreeFont(FONT_BODY);
  panel->setTextColor(theme::grey, theme::bg);
  panel->drawString(line, layout::screenW / 2, layout::screenH / 2 + 10);
  panel->setTextDatum(TL_DATUM);
}

// Waits for one deliberate, settled contact and returns its raw coordinates.
//
// Deliberate means: the panel has to be quiet first (so the release from the
// *previous* target is not read as this one), then held long enough to produce
// several agreeing readings. Averaging those is safe here in a way it is not
// during a swipe, because the finger really is meant to be still.
bool captureTarget(uint16_t &rx, uint16_t &ry, uint32_t deadlineMs) {
  uint16_t x = 0, y = 0, z = 0;

  while (!reached(deadlineMs)) {
    if (!readRaw(x, y, z)) break;   // panel is quiet, go on to the capture
    delay(10);
  }

  uint32_t sumX = 0, sumY = 0;
  uint8_t got = 0;
  while (!reached(deadlineMs)) {
    if (readRaw(x, y, z)) {
      sumX += x;
      sumY += y;
      if (++got >= 12) {
        rx = sumX / got;
        ry = sumY / got;
        // Wait out the release before returning, so the caller's next target
        // does not immediately capture the tail of this contact.
        while (!reached(deadlineMs) && readRaw(x, y, z)) delay(10);
        delay(120);
        return true;
      }
    } else if (got > 0) {
      // Lifted early. Start over rather than average a partial press.
      got = 0;
      sumX = sumY = 0;
    }
    delay(8);
  }
  return false;
}

}  // namespace

void begin(TFT_eSPI &tft) {
  panel = &tft;
  haveCal = loadCal();
  if (haveCal) {
    Serial.printf("[touch] calibration from NVS: swap=%u x %d..%d y %d..%d (rot %u)\n",
                  (unsigned)cal.swapAxes, cal.xAtLeft, cal.xAtRight, cal.yAtTop,
                  cal.yAtBottom, (unsigned)cal.rotation);
  } else {
    cal = kDefaultCal;
    Serial.println(F("[touch] no stored calibration — using built-in defaults, "
                     "which are a guess. Run calibration to fix tap positions."));
  }
}

bool calibrated() { return haveCal; }

bool pressed() {
  if (!panel) return false;
  return panel->getTouchRawZ() >= kPressureMin;
}

void lastTap(int16_t &x, int16_t &y) {
  x = tapX;
  y = tapY;
}

Gesture poll() {
  if (!panel) return kNone;

  const uint32_t now = millis();
  if (!reached(nextPollMs)) return kNone;
  nextPollMs = now + kPollIntervalMs;
  if (!reached(lockedUntilMs)) return kNone;

  uint16_t rx = 0, ry = 0, rz = 0;
  const bool valid = readRaw(rx, ry, rz);

  if (valid) {
    int16_t sx, sy;
    toScreen(rx, ry, sx, sy);
    if (!down) {
      down = true;
      startX = curX = sx;
      startY = curY = sy;
      downMs = now;
    } else {
      curX = sx;
      curY = sy;
    }
    lastSeenMs = now;

    // A finger that has been down this long is resting, not gesturing. Drop
    // the contact on the floor and require a lift before listening again.
    if (now - downMs > kMaxContactMs) {
      down = false;
      lockedUntilMs = now + kLockoutMs;
    }
    return kNone;
  }

  if (!down) return kNone;
  // Missing readings are normal mid-drag; only a sustained silence is a lift.
  if (now - lastSeenMs < kReleaseMs) return kNone;

  down = false;
  lockedUntilMs = now + kLockoutMs;

  const int16_t dx = curX - startX;
  const int16_t dy = curY - startY;
  const int16_t adx = dx < 0 ? -dx : dx;
  const int16_t ady = dy < 0 ? -dy : dy;
  const uint32_t heldMs = lastSeenMs - downMs;

  // Horizontal travel, mostly horizontal, and quick enough to be a flick.
  // The 2:1 test is what keeps a diagonal drag off the carousel: without it,
  // any downward swipe with a little sideways lean pages the screen.
  if (adx >= kSwipeMinPx && adx > 2 * ady && heldMs <= kSwipeMaxMs) {
    Serial.printf("[touch] swipe %s (%d px in %u ms)\n", dx < 0 ? "left" : "right",
                  (int)dx, (unsigned)heldMs);
    return dx < 0 ? kSwipeLeft : kSwipeRight;
  }

  if (adx <= kTapMaxPx && ady <= kTapMaxPx) {
    tapX = curX;
    tapY = curY;
    Serial.printf("[touch] tap at %d,%d\n", (int)tapX, (int)tapY);
    return kTap;
  }

  // Travelled too far to be a tap, not cleanly enough to be a swipe. Saying
  // nothing is the honest answer -- guessing here is how a UI acquires a
  // reputation for doing things you did not ask for.
  return kNone;
}

bool calibrate(uint32_t timeoutMs) {
  if (!panel) return false;

  const uint32_t deadline = millis() + timeoutMs;
  const int16_t w = layout::screenW, h = layout::screenH;
  struct Target { int16_t x, y; const char *name; };
  const Target targets[4] = {
      {kTargetInset, kTargetInset, "top left"},
      {(int16_t)(w - 1 - kTargetInset), kTargetInset, "top right"},
      {(int16_t)(w - 1 - kTargetInset), (int16_t)(h - 1 - kTargetInset), "bottom right"},
      {kTargetInset, (int16_t)(h - 1 - kTargetInset), "bottom left"},
  };

  uint16_t rawX[4], rawY[4];
  for (uint8_t i = 0; i < 4; i++) {
    char line[48];
    snprintf(line, sizeof(line), "Touch the %s marker  (%u of 4)", targets[i].name,
             (unsigned)(i + 1));
    calibrationChrome(line);
    drawTarget(targets[i].x, targets[i].y, theme::gold);

    if (!captureTarget(rawX[i], rawY[i], deadline)) {
      Serial.println(F("[touch] calibration timed out — keeping previous mapping"));
      return false;
    }
    drawTarget(targets[i].x, targets[i].y, theme::green);
    Serial.printf("[touch] %-12s screen %3d,%3d  raw %4u,%4u\n", targets[i].name,
                  targets[i].x, targets[i].y, rawX[i], rawY[i]);
  }

  Calibration next{};
  next.version = kCalVersion;
  next.rotation = panel->getRotation();

  // Which raw axis tracks the screen's horizontal? Compare how much each moved
  // between the two top targets, which differ only in screen X. Whichever
  // changed more is the one carrying it.
  const int32_t dxAlongX = (int32_t)rawX[1] - rawX[0];
  const int32_t dyAlongX = (int32_t)rawY[1] - rawY[0];
  next.swapAxes = (labs(dyAlongX) > labs(dxAlongX)) ? 1 : 0;

  // Per screen edge, average the two targets that share it -- this is what
  // takes the tilt out of a panel whose axes are a degree or two off the
  // glass, and it is the whole reason for using four points instead of two.
  const uint16_t *hAxis = next.swapAxes ? rawY : rawX;
  const uint16_t *vAxis = next.swapAxes ? rawX : rawY;
  const int32_t atLeft = ((int32_t)hAxis[0] + hAxis[3]) / 2;   // TL, BL
  const int32_t atRight = ((int32_t)hAxis[1] + hAxis[2]) / 2;  // TR, BR
  const int32_t atTop = ((int32_t)vAxis[0] + vAxis[1]) / 2;    // TL, TR
  const int32_t atBottom = ((int32_t)vAxis[2] + vAxis[3]) / 2; // BR, BL

  // The targets are inset, so those readings describe the box between them,
  // not the screen. Extrapolating out to the real edges is what stops every
  // tap being pulled toward the centre by kTargetInset pixels -- a 34px error
  // at the edges, which is most of the way to being someone else's button.
  const int32_t hInner = w - 1 - 2 * kTargetInset;
  const int32_t vInner = h - 1 - 2 * kTargetInset;
  const int32_t hPerPx = hInner ? ((atRight - atLeft) * 256) / hInner : 0;
  const int32_t vPerPx = vInner ? ((atBottom - atTop) * 256) / vInner : 0;
  next.xAtLeft = (int16_t)(atLeft - (hPerPx * kTargetInset) / 256);
  next.xAtRight = (int16_t)(atRight + (hPerPx * kTargetInset) / 256);
  next.yAtTop = (int16_t)(atTop - (vPerPx * kTargetInset) / 256);
  next.yAtBottom = (int16_t)(atBottom + (vPerPx * kTargetInset) / 256);

  // A panel that reported four nearly identical points was not touched four
  // times in four places -- it is unwired, or stuck. Storing that mapping
  // would make every tap land dead centre forever, including the tap that
  // would have started a recalibration.
  const int32_t hRange = labs(next.xAtRight - next.xAtLeft);
  const int32_t vRange = labs(next.yAtBottom - next.yAtTop);
  if (hRange < 400 || vRange < 400) {
    Serial.printf("[touch] calibration rejected: raw range %ld x %ld is too small "
                  "to be four distinct corners\n",
                  (long)hRange, (long)vRange);
    return false;
  }

  cal = next;
  haveCal = true;
  saveCal();
  Serial.printf("[touch] calibrated: swap=%u x %d..%d y %d..%d (rot %u), saved to NVS\n",
                (unsigned)cal.swapAxes, cal.xAtLeft, cal.xAtRight, cal.yAtTop,
                cal.yAtBottom, (unsigned)cal.rotation);

  calibrationChrome("Calibration stored.");
  delay(900);
  return true;
}

void forget() {
  Preferences p;
  if (p.begin(HD2_PREFS_NS, /*readOnly=*/false)) {
    p.remove(HD2_PREFS_TOUCH_KEY);
    p.end();
  }
  cal = kDefaultCal;
  haveCal = false;
  Serial.println(F("[touch] stored calibration cleared"));
}

void dumpDiagnostics(uint32_t durationMs) {
  if (!panel) return;
  Serial.println(F("[touch] --- diagnostics ---"));
  Serial.printf("[touch] TOUCH_CS=%d, SPI_TOUCH_FREQUENCY=%d, rotation=%u\n", TOUCH_CS,
                SPI_TOUCH_FREQUENCY, (unsigned)panel->getRotation());
  Serial.printf("[touch] calibration %s: swap=%u x %d..%d y %d..%d\n",
                haveCal ? "from NVS" : "DEFAULT (a guess)", (unsigned)cal.swapAxes,
                cal.xAtLeft, cal.xAtRight, cal.yAtTop, cal.yAtBottom);
  Serial.println(F("[touch] PEN/IRQ is IO36. It is input-only with no internal "
                   "pull-up, so an idle HIGH here means an external pull-up is "
                   "fitted; a stuck LOW with no contact means there is not one."));
  Serial.println(F("[touch] expect z≈0 untouched; a firm press should read well "
                   "above the gate. Raw x/y should sweep most of 200..3900 as a "
                   "finger crosses the glass."));

  pinMode(36, INPUT);
  const uint32_t until = millis() + durationMs;
  uint32_t reported = 0;
  while (!reached(until)) {
    const uint16_t z = panel->getTouchRawZ();
    uint16_t x = 0, y = 0;
    panel->getTouchRaw(&x, &y);
    // Untouched readings are printed once a second so the log shows the floor
    // without burying the interesting lines; contact is printed every sample.
    const bool contact = z >= kPressureMin;
    if (contact || millis() - reported >= 1000) {
      reported = millis();
      int16_t sx, sy;
      toScreen(x, y, sx, sy);
      Serial.printf("[touch] z=%4u raw=%4u,%4u  pen=%d  %s -> %3d,%3d\n", z, x, y,
                    digitalRead(36), contact ? "CONTACT" : "  idle ", sx, sy);
    }
    delay(contact ? 40 : 60);
  }
  Serial.println(F("[touch] --- end diagnostics ---"));
}

}  // namespace touch
