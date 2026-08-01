// Redirects the renderer's #include <TFT_eSPI.h> at the host shim, so
// src/hud_renderer.cpp compiles unmodified.
//
// The real TFT_eSPI pulls every GFXFF font table in when LOAD_GFXFF is
// defined, and src/hud_fonts.h relies on that; the same tables are vendored
// under tools/preview/Fonts/GFXFF so host text metrics match the device
// glyph for glyph.
#pragma once

#include "tft_shim.h"

#include "Fonts/GFXFF/FreeSans9pt7b.h"
#include "Fonts/GFXFF/FreeSansBold9pt7b.h"
#include "Fonts/GFXFF/FreeSansBold12pt7b.h"
#include "Fonts/GFXFF/FreeSansBold18pt7b.h"
