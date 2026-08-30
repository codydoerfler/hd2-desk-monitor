#!/bin/sh
# ---------------------------------------------------------------------------
#  tools/preview.sh — render the real HUD to PNGs, on the host.
#
#  Compiles src/hud_renderer.cpp (the firmware's own renderer, unmodified)
#  against the host shim in tools/preview/, so the previews cannot drift from
#  what the device draws. Supersedes the old tools/preview_hud.py, which was a
#  parallel Python reimplementation of the layout.
#
#      ./tools/preview.sh            # every scene
#      ./tools/preview.sh defense    # just one
#
#  Scenes: boot, touchprompt, touchsuccess, defense, invasion, liberation,
#          campaign, count, countreset, extraction, idle, stale, uncalibrated,
#          neworder, success, failure, carousel.
#
#  `countreset` is the count card after the community API resets a completed
#  task's progress to 0 — it drives applyCountProgressFloor() over two polls,
#  so it should read 100%/complete, never 0.0%.
#
#  `success` is the Major Order verdict overlay; `touchsuccess` is the touch
#  calibration confirmation. Unrelated screens, easily confused by name.
#
#  `carousel` is the odd one out: it is shot after a page advance rather than
#  onto a cleared screen, so it covers the incremental repaint path the device
#  actually spends its time in. See shootAdvance() in render_preview.cpp.
# ---------------------------------------------------------------------------
set -e
cd "$(dirname "$0")/.."

OUT=.pio/preview
mkdir -p "$OUT"

# tools/preview comes first on the include path so its TFT_eSPI.h and
# hd2_api.h shadow the real ones; src/ still supplies everything else.
# -iquote puts tools/preview ahead of src for quoted includes too, so the
# shim TFT_eSPI.h / hd2_api.h / Arduino.h win over the firmware's real ones.
c++ -std=c++17 -O1 -w -DHUD_PREVIEW \
    -iquote tools/preview \
    -I tools/preview \
    -I src \
    tools/preview/render_preview.cpp \
    tools/preview/api_stubs.cpp \
    src/hud_renderer.cpp \
    -o "$OUT/render_preview"

"$OUT/render_preview" "$@"

# The C writer emits stored (uncompressed) deflate to stay dependency-free,
# which costs ~1.8MB a frame. Squeeze them if Pillow is around; harmless if not.
python3 - "$@" <<'PY' 2>/dev/null || true
import glob, sys
try:
    from PIL import Image
except ImportError:
    sys.exit(0)
names = sys.argv[1:] or ["*"]
seen = set()
for n in names:
    for f in glob.glob(f"preview_{n}.png"):
        if f not in seen:
            seen.add(f)
            Image.open(f).save(f, optimize=True)
PY
