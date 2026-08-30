#!/bin/sh
# ---------------------------------------------------------------------------
#  tools/count_floor_test.sh — run the count-task progress-floor checks.
#
#  Compiles tools/count_floor_test.cpp against the real src/hd2_model.h and the
#  host Arduino shim the preview harness already carries, so the logic under
#  test is the firmware's own. No hardware, no network.
#
#      ./tools/count_floor_test.sh
# ---------------------------------------------------------------------------
set -e
cd "$(dirname "$0")/.."

OUT=.pio/preview
mkdir -p "$OUT"

c++ -std=c++17 -O1 -Wall \
    -iquote tools/preview \
    -I tools/preview \
    -I src \
    tools/count_floor_test.cpp \
    -o "$OUT/count_floor_test"

"$OUT/count_floor_test"
