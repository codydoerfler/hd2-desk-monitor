#include "hd2_api.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "config.h"

// ===========================================================================
//  Time / formatting helpers
// ===========================================================================

// Howard Hinnant's days-from-civil. Avoids depending on timegm() being
// present and correct in the toolchain's newlib, and avoids mktime()'s
// dependence on the local TZ.
static int64_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= (m <= 2);
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);                    // [0, 399]
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;        // [0, 146096]
  return era * 146097 + (int64_t)doe - 719468;
}

time_t parseIso8601Utc(const char *s) {
  if (!s || !*s) return 0;
  int Y = 0, M = 0, D = 0, h = 0, m = 0, sec = 0;
  // Trailing fractional seconds / "Z" / offset are simply ignored: the API
  // always reports UTC and we don't need sub-second precision for a countdown.
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) != 6) return 0;
  if (M < 1 || M > 12 || D < 1 || D > 31) return 0;
  return (time_t)(daysFromCivil(Y, (uint32_t)M, (uint32_t)D) * 86400LL + h * 3600LL +
                  m * 60LL + sec);
}

String formatDuration(int64_t seconds) {
  if (seconds <= 0) return "EXPIRED";
  char buf[16];
  const int64_t d = seconds / 86400;
  const int64_t h = (seconds % 86400) / 3600;
  const int64_t m = (seconds % 3600) / 60;
  const int64_t s = seconds % 60;
  if (d > 0) {
    snprintf(buf, sizeof(buf), "%lldd %02lldh", d, h);
  } else if (h > 0) {
    snprintf(buf, sizeof(buf), "%lldh %02lldm", h, m);
  } else {
    snprintf(buf, sizeof(buf), "%lldm %02llds", m, s);
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
    snprintf(buf, sizeof(buf), "%llu", v);
  }
  return String(buf);
}

// ===========================================================================
//  Transport
// ===========================================================================

bool HD2Api::getJson(const String &path, JsonDocument &doc, JsonDocument *filter) {
  // Both objects are stack-local so the ~40KB of TLS working memory is
  // released the moment the request finishes.
  WiFiClientSecure client;
  // No certificate pinning: this is a read-only public data feed and the
  // firmware has no clock-independent trust store. See README "Security".
  client.setInsecure();
  client.setTimeout(kHttpTimeoutMs / 1000);

  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  http.setConnectTimeout(kHttpTimeoutMs);
  http.setReuse(false);

  const String url = String(HD2_API_BASE) + path;
  if (!http.begin(client, url)) {
    _lastError = "begin() failed: " + url;
    return false;
  }

  // Both of these are mandatory — the API answers 400 without them.
  http.addHeader("X-Super-Client", HD2_CLIENT_HEADER);
  http.addHeader("X-Super-Contact", HD2_CONTACT_HEADER);
  http.addHeader("Accept", "application/json");

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    _lastError = "HTTP " + String(code) + " on " + path;
    http.end();
    return false;
  }

  DeserializationError err =
      filter ? deserializeJson(doc, http.getStream(),
                               DeserializationOption::Filter(*filter))
             : deserializeJson(doc, http.getStream());
  http.end();

  if (err) {
    _lastError = String("JSON: ") + err.c_str() + " on " + path;
    return false;
  }
  return true;
}

// ===========================================================================
//  Endpoints
// ===========================================================================

bool HD2Api::fetchMajorOrder(MajorOrder &out) {
  out = MajorOrder();

  // The whole assignments payload is only a few hundred bytes, so no filter.
  JsonDocument doc;
  if (!getJson("/api/v1/assignments", doc, nullptr)) return false;

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) {
    // Not an error: between Major Orders the API legitimately returns [].
    _lastError = "no active Major Order";
    return true;  // out.valid stays false; caller renders the idle screen.
  }

  // v1 shows the first order if several are somehow active at once.
  JsonObject a = arr[0];
  out.title = a["title"] | "MAJOR ORDER";
  out.briefing = a["briefing"] | "";
  out.briefing.trim();
  out.rewardAmount = a["reward"]["amount"] | 0;
  out.expiration = parseIso8601Utc(a["expiration"] | "");

  // ---- Target planet extraction -----------------------------------------
  // A task looks like:  {"type":11,"values":[1,1,249],"valueTypes":[3,11,12]}
  // `values` and `valueTypes` are parallel arrays; the *type code* tells you
  // what each slot means. 12 == planet index. Scanning valueTypes for 12 is
  // deliberately more robust than indexing `values` by a fixed position,
  // because slot ordering differs between task types (liberate / defend /
  // kill-N-of-faction) and Arrowhead has reshuffled them before.
  // If no slot is tagged 12, the task isn't planet-scoped (e.g. a pure
  // "kill 500 million Terminids" order) and we render without a target card.
  constexpr int kValueTypePlanetIndex = 12;

  JsonArray tasks = a["tasks"].as<JsonArray>();
  if (!tasks.isNull() && tasks.size() > 0) {
    JsonObject t = tasks[0];
    JsonArray values = t["values"].as<JsonArray>();
    JsonArray types = t["valueTypes"].as<JsonArray>();
    if (!values.isNull() && !types.isNull()) {
      const size_t n = min(values.size(), types.size());
      for (size_t i = 0; i < n; i++) {
        if ((int)types[i] == kValueTypePlanetIndex) {
          out.planetIndex = (int32_t)values[i];
          break;
        }
      }
    }
    // `progress` parallels `tasks`; for a liberate task it is 0 or 1.
    JsonArray progress = a["progress"].as<JsonArray>();
    if (!progress.isNull() && progress.size() > 0) {
      out.taskComplete = ((int)progress[0]) > 0;
    }
  }

  out.valid = true;
  return true;
}

bool HD2Api::fetchPlanet(int32_t index, PlanetInfo &out) {
  out = PlanetInfo();
  if (index < 0) return false;

  // The raw planet object carries long biome/hazard prose we don't show.
  // Filtering keeps the parsed document around 200 bytes instead of ~1KB.
  JsonDocument filter;
  filter["name"] = true;
  filter["sector"] = true;
  filter["biome"]["name"] = true;
  filter["currentOwner"] = true;
  filter["health"] = true;
  filter["maxHealth"] = true;
  filter["statistics"]["playerCount"] = true;

  JsonDocument doc;
  if (!getJson("/api/v1/planets/" + String(index), doc, &filter)) return false;

  out.index = index;
  out.name = doc["name"] | "UNKNOWN";
  out.sector = doc["sector"] | "";
  out.biome = doc["biome"]["name"] | "";
  out.owner = doc["currentOwner"] | "";
  out.health = doc["health"] | 0U;
  out.maxHealth = doc["maxHealth"] | 0U;
  out.playerCount = doc["statistics"]["playerCount"] | 0U;

  // Liberation is the inverse of remaining planet HP. `health` counts *down*
  // as the playerbase pushes: a freshly attacked planet sits at maxHealth
  // (0% liberated) and reaches 0 when fully liberated.
  //
  // Always divide by the per-planet maxHealth rather than a hardcoded
  // 1,000,000 — defence campaigns and planets with regions use other values.
  if (out.maxHealth > 0) {
    out.liberation = 100.0f * (1.0f - (float)out.health / (float)out.maxHealth);
    out.liberation = constrain(out.liberation, 0.0f, 100.0f);
  }

  out.valid = true;
  return true;
}

bool HD2Api::fetchWar(WarStats &out) {
  out = WarStats();

  JsonDocument filter;
  filter["statistics"]["terminidKills"] = true;
  filter["statistics"]["automatonKills"] = true;
  filter["statistics"]["illuminateKills"] = true;
  filter["statistics"]["missionSuccessRate"] = true;
  filter["statistics"]["playerCount"] = true;

  JsonDocument doc;
  if (!getJson("/api/v1/war", doc, &filter)) return false;

  JsonObject st = doc["statistics"];
  if (st.isNull()) {
    _lastError = "war: no statistics object";
    return false;
  }

  out.totalKills = (uint64_t)(st["terminidKills"] | 0ULL) +
                   (uint64_t)(st["automatonKills"] | 0ULL) +
                   (uint64_t)(st["illuminateKills"] | 0ULL);
  out.missionSuccessRate = st["missionSuccessRate"] | 0U;
  out.playerCount = st["playerCount"] | 0U;
  out.valid = true;
  return true;
}
