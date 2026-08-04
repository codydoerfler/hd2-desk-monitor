// ---------------------------------------------------------------------------
//  Helldivers 2 — Major Order Desk Monitor
//
//  Standalone read-only status display for the current Major Order, running on
//  a Hosyond 4.0" 320x480 ESP32-32E board (ST7796S over SPI).
//
//  Flow:  boot -> WiFiManager (captive portal on first run) -> NTP -> poll the
//         Helldivers 2 Community API every 5 minutes -> paint the HUD.
//
//  Wiring/pin configuration lives in platformio.ini. Layout, palette and
//  timing knobs live in src/config.h.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "config.h"
#include "hd2_api.h"
#include "hud_renderer.h"

static HUDRenderer hud;
static HD2Api api;
static HudModel model;
static Preferences prefs;

// Poll scheduling. `pollDueMs` is the next attempt; `backoffS` grows only
// while polls are failing and resets on the first success.
static uint32_t pollDueMs = 0;
static uint32_t backoffS = kBackoffMinS;
static uint32_t wifiRetryDueMs = 0;

// ---------------------------------------------------------------------------
//  Time
// ---------------------------------------------------------------------------

// --- UTC offset -------------------------------------------------------------
//
// The device clock stays on UTC; the offset is applied only where a clock time
// is drawn. It is entered in the WiFi setup portal as an hours[:minutes]
// string, which is what people know their timezone as, and stored in NVS as a
// plain minute count.

// Parses "-6", "+5:30", "5.75", "-3:45". Returns false on anything else.
static bool parseUtcOffset(const char *s, int16_t &outMinutes) {
  if (!s) return false;
  while (*s == ' ') s++;

  int sign = 1;
  if (*s == '+' || *s == '-') {
    if (*s == '-') sign = -1;
    s++;
  }
  if (!isdigit((unsigned char)*s)) return false;

  long hours = 0;
  while (isdigit((unsigned char)*s)) hours = hours * 10 + (*s++ - '0');

  long minutes = 0;
  if (*s == ':') {
    s++;
    if (!isdigit((unsigned char)*s)) return false;
    while (isdigit((unsigned char)*s)) minutes = minutes * 10 + (*s++ - '0');
    if (minutes > 59) return false;
  } else if (*s == '.' || *s == ',') {
    // Fractional hours, the way UTC+5.5 is often written.
    s++;
    long frac = 0, scale = 1;
    if (!isdigit((unsigned char)*s)) return false;
    while (isdigit((unsigned char)*s)) {
      frac = frac * 10 + (*s++ - '0');
      scale *= 10;
    }
    minutes = (frac * 60 + scale / 2) / scale;
  }
  while (*s == ' ') s++;
  if (*s != '\0') return false;

  const long total = sign * (hours * 60 + minutes);
  if (total < kUtcOffsetMinutesMin || total > kUtcOffsetMinutesMax) return false;
  outMinutes = (int16_t)total;
  return true;
}

// Inverse of the above, for pre-filling the portal field: "0", "-6", "+5:30".
static void formatUtcOffset(int16_t minutes, char *out, size_t n) {
  const char *sign = minutes < 0 ? "-" : (minutes > 0 ? "+" : "");
  const int mag = abs((int)minutes);
  if (mag % 60 == 0) {
    snprintf(out, n, "%s%d", sign, mag / 60);
  } else {
    snprintf(out, n, "%s%d:%02d", sign, mag / 60, mag % 60);
  }
}

// The /api/v1/war payload carries a `now` field, but it is not a usable wall
// clock (it reports a 1972 date), so the expiration countdown is driven by
// NTP instead. Everything is kept in UTC; the API's timestamps are UTC too.
static void syncClock() {
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  const uint32_t deadline = millis() + 15000;
  while (time(nullptr) < 1600000000 && millis() < deadline) {
    delay(250);
  }
  if (time(nullptr) < 1600000000) {
    Serial.println(F("[time] NTP sync failed; countdown will show '--'"));
  } else {
    Serial.printf("[time] UTC epoch %ld\n", (long)time(nullptr));
  }
}

// ---------------------------------------------------------------------------
//  Polling
// ---------------------------------------------------------------------------

static void onPollFailed(const char *what) {
  model.failCount++;
  model.stale = true;
  Serial.printf("[poll] FAILED (%s): %s — retry in %us\n", what,
                api.lastError().c_str(), backoffS);
  pollDueMs = millis() + backoffS * 1000UL;
  backoffS = min(backoffS * 2, kBackoffMaxS);
}

// The planet record the last good poll left for `index`, or nullptr. Used to
// ride out a single flaky request without blanking a target card.
static const PlanetInfo *cachedPlanet(int32_t index) {
  for (int i = 0; i < model.order.taskCount; i++) {
    const PlanetInfo &p = model.order.tasks[i].planet;
    if (p.valid && p.index == index) return &p;
  }
  return nullptr;
}

// --- Rate history -----------------------------------------------------------
//
// The card quotes a %-per-hour figure that the API does not publish, so it is
// measured here by keeping one earlier snapshot per target and letting the
// renderer diff it against the live one. Called with `next` fully populated but
// *before* it replaces model.order, which is what makes the old snapshots still
// reachable.
static void rollRateHistory(const MajorOrder &next) {
  // A different order means different targets; anything held over would diff
  // two unrelated planets and invent a swing that never happened.
  if (next.id != model.historyOrderId) {
    for (int i = 0; i < kMaxOrderTasks; i++) model.history[i] = RateSample();
    model.historyOrderId = next.id;
    return;
  }

  for (int i = 0; i < kMaxOrderTasks; i++) {
    RateSample &h = model.history[i];
    if (i >= next.taskCount || !next.tasks[i].planet.valid) {
      h = RateSample();
      continue;
    }
    const PlanetInfo &np = next.tasks[i].planet;
    if (h.planetIndex != np.index) h = RateSample();

    // Advance only when the incoming observation is genuinely newer than the
    // one already on screen. A poll whose planet fetch failed carries the
    // cached snapshot — and its original timestamp — forward unchanged; if that
    // were allowed to shift the history the next good poll would diff a sample
    // against itself and the readout would blink out for no reason.
    const PlanetInfo *prev = cachedPlanet(np.index);
    if (!prev || !prev->valid) continue;
    if (prev->observedAt <= h.at || prev->observedAt >= np.observedAt) continue;

    h.valid = true;
    h.planetIndex = prev->index;
    h.at = prev->observedAt;
    h.libPct = prev->liberation;
    h.haveEvent = prev->event.active;
    h.eventPct = prev->event.defended();
    h.eventEnd = prev->event.endTime;
  }
}

static void poll() {
  if (WiFi.status() != WL_CONNECTED) {
    onPollFailed("wifi down");
    return;
  }

  Serial.println(F("[poll] fetching assignments"));
  MajorOrder order;
  if (!api.fetchMajorOrder(order)) {
    onPollFailed("assignments");
    return;
  }

  // An empty assignments array is a valid state, not a failure: it just means
  // there is no Major Order right now. order.valid stays false and the HUD
  // shows the idle screen.
  //
  // One lookup per task: a "defend three planets" order names three different
  // planets and dropping all but the first is exactly the bug this loop
  // exists to avoid. kInterRequestDelayMs is sized for the resulting burst.
  if (order.valid) {
    for (int i = 0; i < order.taskCount; i++) {
      OrderTask &t = order.tasks[i];
      if (t.planetIndex < 0) continue;  // galaxy-wide objective, nothing to fetch

      delay(kInterRequestDelayMs);
      Serial.printf("[poll] fetching planet %d (task %d/%d, type %d)\n",
                    (int)t.planetIndex, i + 1, order.taskCount, (int)t.taskType);
      if (api.fetchPlanet(t.planetIndex, t.planet)) {
        // Time-base for the %-per-hour readouts. Left at 0 until NTP has landed
        // so a pre-sync boot cannot stamp a sample with a 1970 date.
        const time_t now = time(nullptr);
        if (now > 1600000000) t.planet.observedAt = now;
      } else {
        Serial.printf("[poll] planet lookup failed: %s\n", api.lastError().c_str());
        // The cached record is copied whole, timestamp included, so it stays
        // honestly dated as the older observation it is.
        const PlanetInfo *prev = cachedPlanet(t.planetIndex);
        if (prev) t.planet = *prev;
      }
    }
  }

  // With no Major Order there is no task list and therefore no planet, so the
  // screen would have nothing on it but a placeholder. Fall back to the
  // busiest live campaign. Only costs a request in the state that would
  // otherwise be idle, so the steady-state request budget is unchanged.
  PlanetInfo campaign;
  if (!order.valid) {
    delay(kInterRequestDelayMs);
    Serial.println(F("[poll] no order — fetching campaigns"));
    if (api.fetchTopCampaign(campaign)) {
      const time_t now = time(nullptr);
      if (now > 1600000000) campaign.observedAt = now;
      Serial.printf("[poll] campaign: %s, %.2f%% lib, %u divers\n",
                    campaign.name.c_str(), campaign.liberation,
                    (unsigned)campaign.playerCount);
    } else {
      // Not an error: a quiet galaxy, or the fetch failed. Either way the
      // previous campaign is better than blanking the screen.
      Serial.printf("[poll] campaign lookup: %s\n", api.lastError().c_str());
      campaign = model.campaign;
    }
  }

  // War stats are garnish — a failure here never fails the poll, and we keep
  // whatever we had before.
  delay(kInterRequestDelayMs);
  WarStats war;
  if (!api.fetchWar(war)) {
    Serial.printf("[poll] war stats failed: %s\n", api.lastError().c_str());
    war = model.war;
  }

  // Reads the *outgoing* model.order, so it has to run before the assignment.
  rollRateHistory(order);

  // Same rule for the campaign: only advance the sample when the incoming
  // observation is actually newer, so a carried-forward snapshot can't be
  // diffed against itself. A different planet resets rather than diffing two
  // unrelated liberations.
  {
    RateSample &h = model.campaignHistory;
    const PlanetInfo &prev = model.campaign;
    if (!campaign.valid || h.planetIndex != campaign.index) h = RateSample();
    if (campaign.valid && prev.valid && prev.index == campaign.index &&
        prev.observedAt > h.at && prev.observedAt < campaign.observedAt) {
      h.valid = true;
      h.planetIndex = prev.index;
      h.at = prev.observedAt;
      h.libPct = prev.liberation;
    }
  }

  model.campaign = campaign;
  model.order = order;
  model.war = war;
  model.haveData = true;
  model.stale = false;
  model.failCount = 0;
  model.lastSuccess = time(nullptr);

  backoffS = kBackoffMinS;
  pollDueMs = millis() + (uint32_t)HD2_POLL_INTERVAL_S * 1000UL;

  if (order.valid) {
    Serial.printf("[poll] OK — \"%s\", %d target(s), heap %u\n", order.title.c_str(),
                  order.taskCount, (unsigned)ESP.getFreeHeap());
    for (int i = 0; i < order.taskCount; i++) {
      const OrderTask &t = order.tasks[i];
      // Liberation is only quoted for the task type it means something for.
      if (t.planet.valid && t.planet.event.active) {
        Serial.printf("  [%d] type %d  %s  %.4f%% defended vs %s (%u hazard(s))\n",
                      i + 1, (int)t.taskType, t.planet.name.c_str(),
                      t.planet.event.defended(), t.planet.event.faction.c_str(),
                      (unsigned)t.planet.hazardCount);
      } else if (taskIsLiberation(t.taskType) && t.planet.valid) {
        Serial.printf("  [%d] type %d  %s  %.1f%% liberated\n", i + 1, (int)t.taskType,
                      t.planet.name.c_str(), t.planet.liberation);
      } else {
        Serial.printf("  [%d] type %d  %s  %s (owner %s)\n", i + 1, (int)t.taskType,
                      t.planet.valid ? t.planet.name.c_str() : "n/a",
                      t.complete ? "complete" : "incomplete",
                      t.planet.valid ? t.planet.owner.c_str() : "?");
      }
    }
  } else {
    Serial.printf("[poll] OK — no active Major Order, heap %u\n",
                  (unsigned)ESP.getFreeHeap());
  }
}

// ---------------------------------------------------------------------------
//  Setup / loop
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== HD2 Major Order Monitor ==="));
  // Stamped from `git describe` by tools/fw_version.py. Deliberately not
  // guarded by an #ifndef: a build that somehow loses the flag should fail to
  // compile rather than quietly ship an image that lies about its version.
  Serial.printf("[boot] firmware %s\n", HD2_FW_VERSION);

  hud.begin();
  hud.showBoot("Connecting to WiFi...");

  // Displayed clock times are UTC + this. Persisted in NVS alongside (but
  // separately from) the WiFi credentials, so it survives reflashing and is
  // set without recompiling.
  prefs.begin(HD2_PREFS_NS, false);
  model.utcOffsetMin =
      (int16_t)prefs.getShort(HD2_PREFS_TZ_KEY, kUtcOffsetMinutesDefault);
  char offsetField[10];
  formatUtcOffset(model.utcOffsetMin, offsetField, sizeof(offsetField));
  Serial.printf("[time] display offset %s (%d min from UTC)\n", offsetField,
                (int)model.utcOffsetMin);

  WiFiManager wm;
  wm.setDebugOutput(false);
  // Bounded so an unattended desk unit reboots and retries rather than sitting
  // in the config portal forever after a router outage.
  wm.setConfigPortalTimeout(kPortalTimeoutS);
  wm.setAPCallback([](WiFiManager *) { hud.showPortal(kPortalSsid, kPortalPass); });

  // Extra field on the portal page, under the network picker. It must outlive
  // autoConnect(), which is why it is declared here and not in a helper.
  WiFiManagerParameter offsetParam(
      "utcoff", "UTC offset in hours (e.g. -6, +1, +5:30)", offsetField,
      sizeof(offsetField) - 1);
  wm.addParameter(&offsetParam);

  if (!wm.autoConnect(kPortalSsid, kPortalPass)) {
    Serial.println(F("[wifi] portal timed out — rebooting"));
    hud.showBoot("Setup timed out. Restarting...");
    delay(2000);
    ESP.restart();
  }

  Serial.printf("[wifi] connected to %s as %s\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str());
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  // If the portal ran, the field holds whatever was typed; if it didn't, it
  // still holds the value we pre-filled, so this is a no-op on a normal boot.
  // A value that doesn't parse is ignored rather than reset to UTC, so a typo
  // can't silently wipe a good setting.
  int16_t typed = 0;
  if (parseUtcOffset(offsetParam.getValue(), typed)) {
    if (typed != model.utcOffsetMin) {
      model.utcOffsetMin = typed;
      prefs.putShort(HD2_PREFS_TZ_KEY, typed);
      Serial.printf("[time] display offset saved: %d min from UTC\n", (int)typed);
    }
  } else {
    Serial.printf("[time] ignoring unparseable UTC offset \"%s\"; keeping %d min\n",
                  offsetParam.getValue(), (int)model.utcOffsetMin);
  }

  hud.showBoot("Synchronising clock...");
  syncClock();

  hud.showBoot("Contacting High Command...");
  hud.invalidate();

  pollDueMs = millis();  // poll immediately
}

void loop() {
  const uint32_t nowMs = millis();
  model.wifiUp = (WiFi.status() == WL_CONNECTED);

  // Nudge the WiFi stack if it has dropped. autoReconnect usually handles it;
  // this covers the cases where it gives up.
  if (!model.wifiUp && (int32_t)(nowMs - wifiRetryDueMs) >= 0) {
    Serial.println(F("[wifi] disconnected — reconnecting"));
    WiFi.reconnect();
    wifiRetryDueMs = nowMs + 15000;
  }

  if ((int32_t)(nowMs - pollDueMs) >= 0) poll();

  const time_t nowUtc = time(nullptr);

  // Stale if the last poll failed, or if we simply haven't heard anything in
  // a couple of poll intervals (e.g. the ESP is up but the network isn't).
  if (model.haveData) {
    model.stale = (model.failCount > 0) ||
                  (nowUtc > 1600000000 && model.lastSuccess > 0 &&
                   (uint32_t)(nowUtc - model.lastSuccess) > kStaleAfterS);
  }

  hud.update(model, nowUtc);

  delay(200);
}
