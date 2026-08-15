#include "hud_audio.h"

#include <pgmspace.h>

namespace audio {
namespace {

// Settle time for the amplifier's enable line. The FM8002E needs a moment to
// bias up; driving samples at it before that lands as a click rather than the
// first few milliseconds of the clip.
const uint16_t kAmpSettleMs = 12;

// Sample clock for the streaming API. File-scope rather than passed around
// because there is one DAC and one amplifier: two concurrent streams are not a
// thing this board can do, so pretending otherwise would only invite it.
uint32_t streamStepUs = 0;
uint32_t streamNextUs = 0;

void ampOn() {
  dacWrite(kDacPin, kDacIdle);   // park at mid-scale *before* unmuting
  digitalWrite(kAmpEnablePin, LOW);
  delay(kAmpSettleMs);
}

void ampOff() {
  digitalWrite(kAmpEnablePin, HIGH);
  dacWrite(kDacPin, kDacIdle);
}

}  // namespace

void begin() {
  pinMode(kAmpEnablePin, OUTPUT);
  digitalWrite(kAmpEnablePin, HIGH);  // muted until something plays
  dacWrite(kDacPin, kDacIdle);
}

void tone(uint16_t freqHz, uint16_t durationMs) {
  if (freqHz == 0 || durationMs == 0) return;
  ampOn();

  // Half-period in microseconds; the level flips at each boundary.
  const uint32_t halfUs = 500000UL / freqHz;
  const uint32_t endUs = micros() + (uint32_t)durationMs * 1000UL;
  bool high = false;
  uint32_t nextUs = micros();

  while ((int32_t)(micros() - endUs) < 0) {
    // Softer than 0/255: a full-scale square into a small cone is harsher
    // than it needs to be for what is only a confidence check.
    dacWrite(kDacPin, high ? 200 : 56);
    high = !high;
    nextUs += halfUs;
    while ((int32_t)(micros() - nextUs) < 0) {
    }
  }

  ampOff();
}

void beginStream(uint32_t sampleRateHz) {
  // Fixed microsecond step per sample. Deriving the deadline from a running
  // counter rather than delayMicroseconds() per sample keeps the clip from
  // drifting slow: the dacWrite and loop overhead would otherwise be added
  // on top of every single interval.
  streamStepUs = sampleRateHz ? (1000000UL / sampleRateHz) : 0;
  ampOn();
  // Started only after the amp has settled, so the settle delay is not itself
  // counted as playback time and the first samples are not dropped catching up.
  streamNextUs = micros();
}

void writeSample(uint8_t sample) {
  dacWrite(kDacPin, sample);
  streamNextUs += streamStepUs;
  // Signed comparison, so a caller that fell behind (a slow SD block read)
  // does not wait out a whole micros() wrap before continuing. The clip plays
  // those samples late rather than stalling for 71 minutes.
  while ((int32_t)(micros() - streamNextUs) < 0) {
  }
}

void endStream() { ampOff(); }

void playPcm8(const uint8_t *pcm, size_t len, uint32_t sampleRateHz) {
  if (!pcm || len == 0 || sampleRateHz == 0) return;
  beginStream(sampleRateHz);
  for (size_t i = 0; i < len; i++) writeSample(pgm_read_byte(&pcm[i]));
  endStream();
}

}  // namespace audio
