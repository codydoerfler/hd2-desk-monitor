// ---------------------------------------------------------------------------
//  hd2_api.h — Helldivers 2 Community API client.
//
//  Owns HTTPS transport + JSON decoding only. It never touches the display;
//  it hands back the structs in hd2_model.h.
//
//    https://api.helldivers2.dev  (docs: https://helldivers-2.github.io/api/)
// ---------------------------------------------------------------------------
#pragma once

#include <ArduinoJson.h>

#include "hd2_model.h"

class HD2Api {
 public:
  // Each of these performs exactly one HTTP request and returns false on
  // transport error, non-200 status, or malformed JSON. On false the output
  // struct is left with valid == false.
  bool fetchMajorOrder(MajorOrder &out);
  bool fetchPlanet(int32_t index, PlanetInfo &out);
  bool fetchWar(WarStats &out);

  // Human-readable reason for the most recent failure (for the serial log).
  const String &lastError() const { return _lastError; }

 private:
  // Streams `path` into `doc`, applying `filter` if non-null so we only
  // allocate the fields we actually use.
  bool getJson(const String &path, JsonDocument &doc, JsonDocument *filter);

  String _lastError;
};

// --- helpers, exposed for reuse by main/renderer -------------------------

// Parses "2026-07-27T09:00:21.8603907Z" (fractional seconds and a trailing Z
// are both optional) into a UTC epoch. Returns 0 if it can't be parsed.
time_t parseIso8601Utc(const char *s);

// "18h 22m", "2d 04h", "45m 12s", or "EXPIRED".
String formatDuration(int64_t seconds);

// 34912 -> "34.9K", 393647382910 -> "393.6B".
String formatCompact(uint64_t v);
