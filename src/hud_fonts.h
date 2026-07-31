// ---------------------------------------------------------------------------
//  hud_fonts.h — semantic names for the Adafruit GFX free fonts the HUD uses.
//
//  TFT_eSPI pulls every GFXFF font table in via Fonts/GFXFF/gfxfont.h when
//  LOAD_GFXFF is defined (see platformio.ini), so there is nothing to include
//  here beyond TFT_eSPI itself — including the font headers again would be a
//  redefinition error, as they carry no include guards.
//
//  The tables are const/PROGMEM and the build uses -ffunction-sections with
//  --gc-sections, so the fonts we never reference cost nothing in the image.
//
//  To restyle the HUD, change these aliases. Available sizes are 9, 12, 18 and
//  24pt in FreeSans / FreeSansBold / FreeMono / FreeSerif.
//
//  The exception is the Major Order header bar, which is set in Anton to match
//  the game's own title treatment. That face is not part of TFT_eSPI, so it is
//  generated into hud_font_anton.h — same GFXfont format, same setFreeFont()
//  path, so nothing downstream can tell the difference.
// ---------------------------------------------------------------------------
#pragma once

#include <TFT_eSPI.h>

#include "hud_font_anton.h"

#define FONT_BODY    (&FreeSans9pt7b)       // sector tag, footer, status lines
#define FONT_LABEL   (&FreeSansBold9pt7b)   // small caps labels, faction badge
#define FONT_VALUE   (&FreeSansBold12pt7b)  // stat values, progress bar label
#define FONT_DISPLAY (&FreeSansBold18pt7b)  // planet name, status screens

// Header bar title, and the drop-down for an order title too long to set at
// full size.
#define FONT_HEADLINE    (&Anton24px)
#define FONT_HEADLINE_SM (&Anton16px)
