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

  // Buffer the whole body before parsing. The API always answers with
  // Transfer-Encoding: chunked and no Content-Length, and the Arduino-ESP32
  // 2.0.x HTTPClient does not reliably strip the chunk framing when the body
  // is consumed through getStream() — ArduinoJson then chokes on the chunk
  // size lines (InvalidInput). getString() de-chunks internally, so parse
  // from that instead. Payloads here are a few KB at most.
  const String body = http.getString();
  http.end();

  DeserializationError err =
      filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
             : deserializeJson(doc, body);

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
  // Identity, so the poll loop can tell "the same order, five minutes later"
  // from "a brand new order" and throw away rate history across the boundary.
  out.id = a["id"] | 0;
  out.title = a["title"] | "MAJOR ORDER";
  out.briefing = a["briefing"] | "";
  out.briefing.trim();
  out.rewardAmount = a["reward"]["amount"] | 0;
  out.expiration = parseIso8601Utc(a["expiration"] | "");

  // ---- Task extraction ---------------------------------------------------
  // A task looks like:  {"type":13,"values":[1,1,270],"valueTypes":[3,11,12]}
  //
  // An order carries one entry in `tasks` per objective — a "defend three
  // planets" order is three tasks, each naming its own planet — and `progress`
  // is a parallel array with one entry per task. Reading only tasks[0] would
  // silently drop every target but the first, so walk the whole array.
  //
  // Within a task, `values` and `valueTypes` are parallel arrays and the *type
  // code* tells you what each slot means. 12 == planet index. Scanning
  // valueTypes for 12 is deliberately more robust than indexing `values` by a
  // fixed position, because slot ordering differs between task types (liberate
  // / defend / kill-N-of-faction) and Arrowhead has reshuffled them before.
  // If no slot is tagged 12, the task isn't planet-scoped (e.g. a pure
  // "kill 500 million Terminids" order) and we render without a target card.
  //
  // The task's own `type` is kept raw: the renderer, not this layer, decides
  // what an unrecognised objective kind should look like.
  constexpr int kValueTypePlanetIndex = 12;

  JsonArray tasks = a["tasks"].as<JsonArray>();
  JsonArray progress = a["progress"].as<JsonArray>();
  if (!tasks.isNull()) {
    const size_t n = min(tasks.size(), (size_t)kMaxOrderTasks);
    if (tasks.size() > n) {
      // Never drop targets quietly — if this ever fires, raise kMaxOrderTasks.
      Serial.printf("[api] order has %u tasks; showing the first %u\n",
                    (unsigned)tasks.size(), (unsigned)n);
    }
    for (size_t i = 0; i < n; i++) {
      JsonObject t = tasks[i];
      if (t.isNull()) continue;

      OrderTask &ot = out.tasks[out.taskCount];
      ot.taskType = t["type"] | -1;

      JsonArray values = t["values"].as<JsonArray>();
      JsonArray types = t["valueTypes"].as<JsonArray>();
      if (!values.isNull() && !types.isNull()) {
        const size_t m = min(values.size(), types.size());
        for (size_t j = 0; j < m; j++) {
          if ((int)types[j] == kValueTypePlanetIndex) {
            ot.planetIndex = (int32_t)values[j];
            break;
          }
        }
      }

      // For a planet-scoped task this is 0 or 1: held/taken, or not.
      if (!progress.isNull() && i < progress.size()) {
        ot.complete = ((int)progress[i]) > 0;
      }

      ot.valid = true;
      out.taskCount++;
    }
  }

  out.valid = true;
  return true;
}

bool HD2Api::fetchPlanet(int32_t index, PlanetInfo &out) {
  out = PlanetInfo();
  if (index < 0) return false;

  // The raw planet object carries long biome/hazard prose we don't show.
  // Filtering keeps the parsed document small — an array-element filter is
  // written as [0], which ArduinoJson applies to every element of that array.
  //
  // Same request as before: the card's event and hazard rows are new fields
  // pulled out of a response we were already fetching, not new traffic.
  JsonDocument filter;
  filter["name"] = true;
  filter["sector"] = true;
  filter["biome"]["name"] = true;
  filter["hazards"][0]["name"] = true;
  filter["currentOwner"] = true;
  filter["health"] = true;
  filter["maxHealth"] = true;
  filter["statistics"]["playerCount"] = true;
  filter["event"]["faction"] = true;
  filter["event"]["health"] = true;
  filter["event"]["maxHealth"] = true;
  filter["event"]["endTime"] = true;
  filter["event"]["eventType"] = true;

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

  // ---- Hazards -----------------------------------------------------------
  // `hazards` is present on every planet but reads [{"name":"None"}] when
  // there are none, so the sentinel has to be dropped rather than drawn.
  JsonArray hz = doc["hazards"].as<JsonArray>();
  if (!hz.isNull()) {
    for (JsonVariant h : hz) {
      if (out.hazardCount >= kMaxHazards) break;
      const HazardKind k = hazardFromName(String(h["name"] | ""));
      if (k == kHazardNone) continue;
      out.hazards[out.hazardCount++] = k;
    }
  }

  // ---- Active event ------------------------------------------------------
  // `event` is null on a planet nobody is attacking. When it is present it
  // carries its own health pair — the defence bar — and its own deadline,
  // which is the "VICTORY IN" clock and is unrelated to the Major Order's
  // expiration.
  JsonObject ev = doc["event"];
  if (!ev.isNull()) {
    out.event.active = true;
    out.event.faction = ev["faction"] | "";
    out.event.health = ev["health"] | 0U;
    out.event.maxHealth = ev["maxHealth"] | 0U;
    out.event.endTime = parseIso8601Utc(ev["endTime"] | "");
    out.event.eventType = ev["eventType"] | 0;
  }

  // Liberation is the inverse of remaining planet HP. `health` counts *down*
  // as the playerbase pushes: a freshly attacked planet sits at maxHealth
  // (0% liberated) and reaches 0 when fully liberated.
  //
  // Always divide by the per-planet maxHealth rather than a hardcoded
  // 1,000,000 — defence campaigns and planets with regions use other values.
  //
  // This number is only meaningful for a planet that is actually being pushed:
  // an unattacked planet sits at full health and reads 0%, which says nothing
  // about a defend objective. See taskIsLiberation() in hd2_model.h — the
  // renderer only shows this figure for liberate-style tasks.
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
