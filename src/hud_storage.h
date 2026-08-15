// ---------------------------------------------------------------------------
//  hud_storage.h — the SD card, and the assets that live on it.
//
//  The Hosyond/LCDwiki E32R40T has a MicroSD slot on board; nothing needed to
//  be wired for this. It sits on the ESP32's *other* SPI peripheral (VSPI:
//  SCK 18, MISO 19, MOSI 23, CS 5), not the HSPI group the display and touch
//  controller share (12/13/14/15 + CS 33), so the card and the panel never
//  contend for a bus and no CS-juggling or clock-rate compromise is needed
//  between them. See README "Pin mapping" for the source of those numbers.
//
//  Everything here is optional by construction. A unit with no card in the
//  slot — which is every unit already in the field — must keep working
//  exactly as it did, so begin() failing is a normal state and every other
//  call degrades to "no asset" rather than to an error. The device's core job
//  is the HUD, and the HUD is compiled in.
//
//  On-card layout (FAT32, see README "SD card"):
//
//      /audio/mo_new.wav       a new Major Order was issued
//      /audio/mo_success.wav   the order completed
//      /audio/mo_failure.wav   the order expired unfinished
//
//  WAV is uncompressed PCM, mono, 8- or 16-bit — whatever the file says, since
//  the reader honours its header. The board's output is an 8-bit DAC (see
//  hud_audio.h) so 16-bit is truncated on the way out; 8 kHz / 8-bit mono is
//  what tools/gen_sd_assets.py writes and is plenty for a desk alert.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

namespace storage {

// Chip select for the on-board slot. The other three SPI lines are VSPI's
// hardware defaults and are set up by begin().
static const uint8_t kSdCsPin = 5;

// The card is clocked well below the display. SD cards negotiate up from a
// slow initial clock and the cheap ones on long slot traces are unreliable at
// the 40MHz the panel runs at; a sound clip has no deadline worth risking a
// mount failure for.
static const uint32_t kSdFrequencyHz = 16000000;

// Mounts the card. Call once from setup(), after audio::begin(). Returns false
// when there is no card, the card is not FAT-formatted, or the slot is empty —
// all of which are logged and none of which are fatal.
bool begin();

// Whether begin() found a usable card. Everything below is a no-op returning
// false when this is false.
bool available();

// The event clips, addressed by what they mean rather than by path, so the
// on-card layout is this file's business and not its callers'.
enum Clip {
  kClipNewOrder,  // /audio/mo_new.wav
  kClipSuccess,   // /audio/mo_success.wav
  kClipFailure,   // /audio/mo_failure.wav
};

// Plays a clip from the card through audio::. Blocking for the length of the
// clip, same as everything else in hud_audio.h. Returns false if there is no
// card, no such file, or the file is not PCM WAV this board can play — the
// caller's fallback is its own business, and main.cpp's is to fall back to the
// compiled-in alert so an event is never silent.
bool playClip(Clip c);

// Whether a clip's file is actually present, without playing it. Lets the
// caller pick between an SD clip and the compiled-in fallback before it is
// committed to either.
bool hasClip(Clip c);

}  // namespace storage
