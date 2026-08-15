#!/usr/bin/env python3
"""Builds the SD card's contents into sdcard/ at the repo root.

The card holds the event clips the two overlay screens play (see
src/hud_storage.h for the on-card layout and why it is optional). They live
here rather than in PROGMEM because flash is the binding constraint on this
project and an SD card is not -- gen_audio_clip.py's header explains the
lengths the compiled-in clip goes to for the same 30KB these three spend
freely.

Two ways to produce a clip, checked in this order:

  1. tools/assets/<name>_source.<ext>, converted the same way
     gen_audio_clip.py converts the hellpods line -- afconvert to 8 kHz mono,
     trim, normalise. Drop a real recording in and it wins.
  2. a synthesised fallback, so a clean checkout produces a working card
     without needing audio nobody can redistribute. These are deliberately
     plain terminal tones: a rising triad for success, a descending tritone
     pulse for failure. They are meant to be unmistakable from across a desk
     and distinguishable from each other by ear, not to be good.

The output is 8-bit unsigned PCM WAV at 8 kHz mono, which is what the board's
DAC wants (src/hud_audio.h) and what hud_storage.cpp's reader plays back with
no conversion at all.

Usage:  python3 tools/gen_sd_assets.py [--preview]
"""
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ASSETS = os.path.join(HERE, "assets")
OUT_DIR = os.path.join(ROOT, "sdcard", "audio")

SAMPLE_RATE = 8000
# Same gate and headroom as gen_audio_clip.py, for the same reasons: keep the
# attack of the first word, and leave the amplifier a little room at the top.
TRIM_THRESHOLD = 0.02
PEAK_TARGET = 0.90

# Source audio wins over the synthesised fallback when it is present. The
# extensions are what afconvert will happily take.
SOURCE_EXTS = (".mp3", ".wav", ".m4a", ".aif", ".aiff")


# --- conversion (a real recording) -----------------------------------------

def find_source(stem):
    for ext in SOURCE_EXTS:
        p = os.path.join(ASSETS, stem + "_source" + ext)
        if os.path.exists(p):
            return p
    return None


def decode_to_wav(src, dst):
    """anything -> 8 kHz mono 16-bit WAV, via macOS's afconvert."""
    subprocess.run(
        ["afconvert", "-f", "WAVE", "-d", "LEI16@%d" % SAMPLE_RATE, "-c", "1", src, dst],
        check=True,
    )


def load_samples(path):
    with wave.open(path) as w:
        assert w.getnchannels() == 1, "expected mono"
        assert w.getframerate() == SAMPLE_RATE, "expected %d Hz" % SAMPLE_RATE
        n = w.getnframes()
        return list(struct.unpack("<%dh" % n, w.readframes(n)))


def trim(samples):
    gate = 32768 * TRIM_THRESHOLD
    first = next((i for i, v in enumerate(samples) if abs(v) > gate), 0)
    last = next((i for i in range(len(samples) - 1, -1, -1) if abs(samples[i]) > gate),
                len(samples) - 1)
    return samples[first:last + 1]


# --- synthesis (the fallback) ----------------------------------------------

def envelope(i, n, attack=0.01, release=0.25):
    """Linear attack/release in seconds, applied by sample index.

    Without this every note starts and ends on a step, which the amplifier
    reproduces as a click louder than the note itself -- the same reason
    hud_audio.cpp parks the DAC at mid-scale between clips.
    """
    a = max(1, int(attack * SAMPLE_RATE))
    r = max(1, int(release * SAMPLE_RATE))
    if i < a:
        return i / a
    if i > n - r:
        return max(0.0, (n - i) / r)
    return 1.0


def note(freq, seconds, harmonics=(1.0, 0.35, 0.15), release=0.25):
    """One tone, with a couple of harmonics so it reads as an instrument.

    A bare sine through a small cone is thin and easy to miss across a room;
    the third and fifth partials cost nothing here and carry much better.
    """
    n = int(seconds * SAMPLE_RATE)
    out = []
    for i in range(n):
        t = i / SAMPLE_RATE
        v = sum(amp * math.sin(2 * math.pi * freq * (k + 1) * t)
                for k, amp in enumerate(harmonics))
        out.append(v * envelope(i, n, release=release) / sum(harmonics))
    return out


def silence(seconds):
    return [0.0] * int(seconds * SAMPLE_RATE)


def synth_success():
    """A rising major triad landing on the octave: unambiguously 'good'."""
    seq = []
    for f in (523.25, 659.25, 783.99):          # C5 E5 G5
        seq += note(f, 0.16, release=0.06)
    seq += note(1046.50, 0.62, release=0.45)    # C6, held
    return seq


def synth_failure():
    """A descending tritone, pulsed twice. Dissonant on purpose."""
    seq = []
    for _ in range(2):
        seq += note(311.13, 0.20, harmonics=(1.0, 0.5, 0.3), release=0.05)  # Eb4
        seq += note(220.00, 0.26, harmonics=(1.0, 0.5, 0.3), release=0.08)  # A3
        seq += silence(0.07)
    seq += note(155.56, 0.55, harmonics=(1.0, 0.6, 0.4), release=0.4)       # Eb3
    return seq


def synth_new_order():
    """Two rising calls, the shape of an announcement rather than a verdict."""
    seq = []
    for _ in range(2):
        seq += note(587.33, 0.13, release=0.04)   # D5
        seq += note(880.00, 0.22, release=0.10)   # A5
        seq += silence(0.06)
    return seq


# --- output ----------------------------------------------------------------

def to_u8_from_float(samples):
    peak = max((abs(v) for v in samples), default=1.0) or 1.0
    gain = PEAK_TARGET / peak
    # Centred on 128 == audio::kDacIdle, so the clip starts and ends at the
    # level the DAC already rests at.
    return bytearray(max(0, min(255, int(round(v * gain * 127)) + 128)) for v in samples)


def to_u8_from_pcm16(samples):
    peak = max((abs(v) for v in samples), default=1) or 1
    gain = (32767 * PEAK_TARGET) / peak
    out = bytearray()
    for v in samples:
        s = int(v * gain)
        s = -32768 if s < -32768 else (32767 if s > 32767 else s)
        out.append((s + 32768) >> 8)
    return out


def write_wav(path, pcm8):
    """8-bit unsigned mono WAV. wave's own writer, so the header is canonical.

    hud_storage.cpp walks the chunk list rather than assuming fixed offsets, so
    it does not care what else an encoder puts in here -- but there is no
    reason to make it work for its living.
    """
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(bytes(pcm8))


def build(stem, synth):
    src = find_source(stem)
    if src:
        with tempfile.TemporaryDirectory() as tmp:
            wav = os.path.join(tmp, "clip.wav")
            decode_to_wav(src, wav)
            pcm8 = to_u8_from_pcm16(trim(load_samples(wav)))
        origin = os.path.relpath(src, ROOT)
    else:
        pcm8 = to_u8_from_float(synth())
        origin = "synthesised"
    return pcm8, origin


def preview(pcm8):
    """Coarse waveform, so a bad envelope is visible without a card."""
    cols = 72
    step = max(1, len(pcm8) // cols)
    bars = " .:-=+*#%@"
    row = ""
    for i in range(0, len(pcm8), step):
        window = pcm8[i:i + step]
        amp = max(abs(int(b) - 128) for b in window) / 128.0
        row += bars[min(len(bars) - 1, int(amp * len(bars)))]
    return row


CLIPS = (
    ("mo_new", synth_new_order),
    ("mo_success", synth_success),
    ("mo_failure", synth_failure),
)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for stem, synth in CLIPS:
        pcm8, origin = build(stem, synth)
        path = os.path.join(OUT_DIR, stem + ".wav")
        write_wav(path, pcm8)
        print("%-12s %5.2fs  %6d bytes  <- %s" %
              (stem + ".wav", len(pcm8) / SAMPLE_RATE, len(pcm8), origin))
        if "--preview" in sys.argv:
            print("             " + preview(pcm8))

    print("\nwrote %s" % os.path.relpath(OUT_DIR, ROOT))
    print("Copy the *contents* of sdcard/ to the root of a FAT32 card:")
    print("    cp -R sdcard/ /Volumes/<CARD>/")


if __name__ == "__main__":
    main()
