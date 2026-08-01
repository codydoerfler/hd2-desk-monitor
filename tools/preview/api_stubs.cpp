// Host-side definitions for the two hd2_api.cpp helpers the renderer calls.
//
// The real src/hd2_api.cpp cannot be compiled here -- it pulls in HTTPClient
// and WiFiClientSecure -- but these two functions are pure string formatting.
// Kept byte-identical to the firmware's own bodies so every duration and count
// in a preview matches the device exactly.
#include "hd2_api.h"

String formatDuration(int64_t seconds) {
  if (seconds <= 0) return "EXPIRED";
  char buf[16];
  const int64_t d = seconds / 86400;
  const int64_t h = (seconds % 86400) / 3600;
  const int64_t m = (seconds % 3600) / 60;
  const int64_t s = seconds % 60;
  if (d > 0) {
    snprintf(buf, sizeof(buf), "%lldd %02lldh", (long long)d, (long long)h);
  } else if (h > 0) {
    snprintf(buf, sizeof(buf), "%lldh %02lldm", (long long)h, (long long)m);
  } else {
    snprintf(buf, sizeof(buf), "%lldm %02llds", (long long)m, (long long)s);
  }
  return String(buf);
}

String formatCompact(uint64_t v) {
  char buf[16];
  if (v >= 1000000000ULL) {
    snprintf(buf, sizeof(buf), "%.1fB", (double)v / 1e9);
  } else if (v >= 1000000ULL) {
    snprintf(buf, sizeof(buf), "%.1fM", (double)v / 1e6);
  } else if (v >= 1000ULL) {
    snprintf(buf, sizeof(buf), "%.1fK", (double)v / 1e3);
  } else {
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
  }
  return String(buf);
}
