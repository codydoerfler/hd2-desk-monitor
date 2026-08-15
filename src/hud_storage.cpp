#include "hud_storage.h"

#include <SD.h>
#include <SPI.h>

#include "hud_audio.h"

namespace storage {
namespace {

// The card's own SPI peripheral, kept separate from the display's. TFT_eSPI
// owns HSPI (USE_HSPI_PORT in platformio.ini) and drives it with its own
// transaction settings; handing SD a different peripheral entirely means
// neither library has to know the other exists.
SPIClass sdSpi(VSPI);

bool mounted = false;

const char *clipPath(Clip c) {
  switch (c) {
    case kClipNewOrder: return "/audio/mo_new.wav";
    case kClipSuccess:  return "/audio/mo_success.wav";
    case kClipFailure:  return "/audio/mo_failure.wav";
  }
  return nullptr;
}

// One SD block per read. At 8 kHz this is a quarter-second of audio per
// access, so the file system is touched four times a second rather than eight
// thousand — a per-sample File::read() would spend longer in the FAT layer
// than the 125us the sample clock allows, and the clip would play slow.
//
// The flip side is that the read itself has to fit inside one sample interval
// or the stream stutters at the block boundary. A 2KB read off a class-4 card
// lands in ~1-3ms, which is audibly a click at 125us per sample, so the buffer
// is filled one block *ahead* of where playback is: see playWav().
const size_t kBlockSize = 2048;

// A parsed RIFF/WAVE header: where the samples start, how many there are, and
// how to read them.
struct WavInfo {
  uint32_t sampleRate = 0;
  uint16_t channels = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataOffset = 0;
  uint32_t dataBytes = 0;
};

uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

// Walks the chunk list rather than assuming `fmt ` then `data` at fixed
// offsets. Encoders routinely insert LIST/INFO or a fact chunk between them,
// and a fixed-offset reader plays those bytes as a burst of noise.
bool parseWav(File &f, WavInfo &out) {
  uint8_t hdr[12];
  if (f.read(hdr, sizeof(hdr)) != (int)sizeof(hdr)) return false;
  if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return false;

  bool haveFmt = false;
  while (f.available() >= 8) {
    uint8_t ch[8];
    if (f.read(ch, sizeof(ch)) != (int)sizeof(ch)) return false;
    const uint32_t size = rd32(ch + 4);

    if (memcmp(ch, "fmt ", 4) == 0) {
      uint8_t fmt[16];
      if (size < sizeof(fmt)) return false;
      if (f.read(fmt, sizeof(fmt)) != (int)sizeof(fmt)) return false;
      // 1 == PCM. Anything else (IEEE float, ADPCM, the WAVE_FORMAT_EXTENSIBLE
      // wrapper) would need decoding this board has no business doing.
      if (rd16(fmt) != 1) return false;
      out.channels = rd16(fmt + 2);
      out.sampleRate = rd32(fmt + 4);
      out.bitsPerSample = rd16(fmt + 14);
      haveFmt = true;
      // Chunks are word-aligned and may carry extra bytes beyond the 16 read.
      f.seek(f.position() + (size - sizeof(fmt)) + (size & 1));
    } else if (memcmp(ch, "data", 4) == 0) {
      out.dataOffset = f.position();
      out.dataBytes = size;
      return haveFmt && out.sampleRate > 0 && out.channels > 0 &&
             (out.bitsPerSample == 8 || out.bitsPerSample == 16);
    } else {
      f.seek(f.position() + size + (size & 1));
    }
  }
  return false;
}

// Streams `f` to the DAC. Reads one block ahead of playback so the file system
// access happens while the previous block is still being clocked out, which is
// what keeps a 2ms read from landing as a gap between samples.
void playWav(File &f, const WavInfo &w) {
  static uint8_t bufA[kBlockSize], bufB[kBlockSize];
  uint8_t *play = bufA, *fill = bufB;

  const uint8_t bytesPerSample = w.bitsPerSample / 8;
  const uint8_t stride = bytesPerSample * w.channels;
  uint32_t left = w.dataBytes;

  size_t have = f.read(play, min((size_t)left, kBlockSize));
  if (have == 0) return;
  left -= have;

  audio::beginStream(w.sampleRate);
  while (have > 0) {
    // Queue the next block before spending time on this one. There is no DMA
    // here -- the read is synchronous -- but doing it first means the gap it
    // costs falls before beginStream's clock has to be honoured for these
    // samples rather than in the middle of them.
    size_t next = 0;
    if (left > 0) {
      next = f.read(fill, min((size_t)left, kBlockSize));
      left -= next;
    }

    for (size_t i = 0; i + stride <= have; i += stride) {
      // Only the first channel is played: the DAC is mono, and averaging a
      // stereo pair costs a multiply per sample for a difference this cone
      // cannot reproduce. 16-bit is signed and centred on 0, 8-bit WAV is
      // unsigned and centred on 128 -- which is already audio::kDacIdle.
      audio::writeSample(bytesPerSample == 2
                             ? (uint8_t)((int16_t)rd16(&play[i]) / 256 + 128)
                             : play[i]);
    }

    uint8_t *tmp = play;
    play = fill;
    fill = tmp;
    have = next;
  }
  audio::endStream();
}

}  // namespace

bool begin() {
  sdSpi.begin(18 /*SCK*/, 19 /*MISO*/, 23 /*MOSI*/, kSdCsPin);
  // SD.begin() pulls CS itself, but the pin is left in a defined state first
  // so nothing floats between power-on and here.
  pinMode(kSdCsPin, OUTPUT);
  digitalWrite(kSdCsPin, HIGH);

  if (!SD.begin(kSdCsPin, sdSpi, kSdFrequencyHz)) {
    // Expected on every unit without a card, so this is a plain note rather
    // than an error: the HUD is compiled in and loses nothing here.
    Serial.println(F("[sd] no card (assets fall back to compiled-in)"));
    mounted = false;
    return false;
  }

  const uint64_t mb = SD.cardSize() / (1024ULL * 1024ULL);
  Serial.printf("[sd] mounted, %llu MB, type %d\n", mb, (int)SD.cardType());
  mounted = true;
  return true;
}

bool available() { return mounted; }

bool hasClip(Clip c) {
  const char *path = clipPath(c);
  return mounted && path && SD.exists(path);
}

bool playClip(Clip c) {
  if (!mounted) return false;
  const char *path = clipPath(c);
  if (!path) return false;

  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[sd] %s missing\n", path);
    return false;
  }

  WavInfo w;
  if (!parseWav(f, w)) {
    // Named explicitly because the fix is on the card, not in the firmware:
    // whoever prepared it wrote something this reader cannot play.
    Serial.printf("[sd] %s is not mono/stereo PCM WAV\n", path);
    f.close();
    return false;
  }

  Serial.printf("[sd] playing %s (%u Hz, %u-bit, %uch, %.2fs)\n", path,
                (unsigned)w.sampleRate, (unsigned)w.bitsPerSample,
                (unsigned)w.channels,
                (double)w.dataBytes /
                    (double)(w.sampleRate * w.channels * (w.bitsPerSample / 8)));

  f.seek(w.dataOffset);
  playWav(f, w);
  f.close();
  return true;
}

}  // namespace storage
