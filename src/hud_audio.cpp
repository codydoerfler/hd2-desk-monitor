#include "hud_audio.h"

#include <pgmspace.h>

namespace audio {
namespace {

// Settle time for the amplifier's enable line. The FM8002E needs a moment to
// bias up; driving samples at it before that lands as a click rather than the
// first few milliseconds of the clip.
const uint16_t kAmpSettleMs = 12;

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

void playPcm8(const uint8_t *pcm, size_t len, uint32_t sampleRateHz) {
  if (!pcm || len == 0 || sampleRateHz == 0) return;
  ampOn();

  // Fixed microsecond step per sample. Deriving the deadline from a running
  // counter rather than delayMicroseconds() per sample keeps the clip from
  // drifting slow: the dacWrite and loop overhead would otherwise be added
  // on top of every single interval.
  const uint32_t stepUs = 1000000UL / sampleRateHz;
  uint32_t nextUs = micros();

  for (size_t i = 0; i < len; i++) {
    dacWrite(kDacPin, pgm_read_byte(&pcm[i]));
    nextUs += stepUs;
    while ((int32_t)(micros() - nextUs) < 0) {
    }
  }

  ampOff();
}

}  // namespace audio
