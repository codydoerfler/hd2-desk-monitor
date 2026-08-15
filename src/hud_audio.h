// ---------------------------------------------------------------------------
//  hud_audio.h — the board's speaker output.
//
//  The Hosyond/LCDwiki E32R40T drives a 2-pin JST speaker header from an
//  FM8002E amplifier (1.5W into 8R): GPIO26 carries the ESP32's internal DAC
//  output, GPIO4 is the amplifier's enable line and is ACTIVE LOW. This is a
//  DAC part, not I2S -- there is no external codec on the board.
//
//  The amp is left disabled whenever nothing is playing. It idles audibly
//  otherwise, and switching it around each clip is also what keeps the DAC's
//  rest voltage from thumping the cone on the way in and out.
//
//  Playback is blocking, deliberately: a clip is a few seconds, the HUD is a
//  near-static screen with nothing to animate, and the poll loop already
//  blocks far longer than this on its HTTP burst. Nothing here is on a
//  deadline that a DMA-fed I2S path would rescue.
//
//  Not compiled by tools/preview.sh -- it is referenced only from main.cpp,
//  the same way the renderer stays free of hardware concerns.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

namespace audio {

// Pin assignment is fixed by the board; see the header comment above.
static const uint8_t kDacPin = 26;
static const uint8_t kAmpEnablePin = 4;   // active LOW

// Idle level the DAC rests at. Mid-scale, because an 8-bit unsigned PCM
// stream is centred on 128 -- parking anywhere else means every clip starts
// and ends with a step the amp reproduces as a click.
static const uint8_t kDacIdle = 128;

// Configures the pins and leaves the amp disabled. Call once from setup().
void begin();

// A plain square-ish test tone, for confirming the speaker is wired and the
// amp is switching. Blocking.
void tone(uint16_t freqHz, uint16_t durationMs);

// Plays 8-bit unsigned PCM held in PROGMEM at `sampleRateHz`. Blocking, and
// a no-op if the data is null/empty. `len` is in samples (== bytes).
void playPcm8(const uint8_t *pcm, size_t len, uint32_t sampleRateHz);

// --- streaming ------------------------------------------------------------
//
// The same playback path for a clip that is not all in memory at once, which
// is what a WAV read off the SD card is: the file is walked a block at a time
// and handed over sample by sample. playPcm8() above is itself written in
// terms of these, so there is one sample clock and one amp-gating rule rather
// than two that can drift apart.
//
// Call in order — beginStream, writeSample per sample, endStream — and do not
// interleave two streams; there is a single clock and a single amp. The amp is
// unmuted for the whole run, so a caller that stalls between samples leaves it
// on: keep the fetch between writeSample() calls short. See the block-size
// note in hud_storage.cpp for how the SD reader stays inside that budget.
void beginStream(uint32_t sampleRateHz);
void writeSample(uint8_t sample);
void endStream();

}  // namespace audio
