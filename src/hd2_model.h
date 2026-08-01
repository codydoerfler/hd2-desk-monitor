// ---------------------------------------------------------------------------
//  hd2_model.h — the plain-data structures shared between the API layer and
//  the renderer. Neither module knows about the other; both know about this.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

// --- Hazards ----------------------------------------------------------------
//
// A planet's `hazards` array is free text — "Fire Tornadoes", "Ion Storms",
// "Meteor Showers" — and a planet with none reports the single entry "None"
// rather than an empty array. There is no documented enumeration, so the names
// are matched to the icon set by keyword and anything unmatched falls through
// to kHazardOther, which draws a generic warning chip. Nothing is invented: a
// chip is only ever drawn for a hazard the API actually listed.
enum HazardKind : uint8_t {
  kHazardNone = 0,
  kHazardFire,
  kHazardIon,
  kHazardCold,
  kHazardMeteor,
  kHazardStorm,
  kHazardTremor,
  kHazardOther,
};

// Keyword match, most specific first — "Meteor Storms" is a meteor hazard and
// "Ion Storms" an ion one, so the generic "storm" test has to come last.
inline HazardKind hazardFromName(const String &raw) {
  String s = raw;
  s.toLowerCase();
  if (s.length() == 0 || s == "none") return kHazardNone;
  if (s.indexOf("meteor") >= 0) return kHazardMeteor;
  if (s.indexOf("ion") >= 0 || s.indexOf("electr") >= 0) return kHazardIon;
  if (s.indexOf("fire") >= 0 || s.indexOf("volcan") >= 0 || s.indexOf("heat") >= 0)
    return kHazardFire;
  if (s.indexOf("cold") >= 0 || s.indexOf("blizzard") >= 0 || s.indexOf("snow") >= 0 ||
      s.indexOf("freez") >= 0)
    return kHazardCold;
  if (s.indexOf("tremor") >= 0 || s.indexOf("quake") >= 0 || s.indexOf("seism") >= 0)
    return kHazardTremor;
  if (s.indexOf("storm") >= 0 || s.indexOf("sand") >= 0 || s.indexOf("rain") >= 0 ||
      s.indexOf("wind") >= 0 || s.indexOf("fog") >= 0 || s.indexOf("spore") >= 0)
    return kHazardStorm;
  return kHazardOther;
}

// More than this on one planet has not been seen, and the card's chip row only
// has room for so many.
static const int kMaxHazards = 4;

// The hostile event sitting on a planet, when there is one — the `event` object
// on GET /api/v1/planets/{index}. This is what makes a defend objective a
// defend objective: `health` counts *down* from `maxHealth` as the playerbase
// holds the line, so defence progress is the inverse of the remaining bar, and
// `endTime` is the deadline for the defence itself (distinct from the Major
// Order's own expiration).
struct PlanetEvent {
  bool active = false;
  String faction;            // the invader: Terminids | Automaton | Illuminate
  uint32_t health = 0;
  uint32_t maxHealth = 0;
  time_t endTime = 0;        // UTC epoch, 0 if absent/unparseable
  int32_t eventType = 0;

  // Percentage of the defence completed, 0..100. Meaningless without an event.
  float defended() const {
    if (!active || maxHealth == 0) return 0.0f;
    const float v = 100.0f * (1.0f - (float)health / (float)maxHealth);
    return v < 0.0f ? 0.0f : (v > 100.0f ? 100.0f : v);
  }
};

// One planet (GET /api/v1/planets/{index}).
struct PlanetInfo {
  bool valid = false;
  int32_t index = -1;
  String name;
  String sector;
  String biome;
  String owner;              // Humans | Terminids | Automaton | Illuminate
  uint32_t health = 0;
  uint32_t maxHealth = 0;
  uint32_t playerCount = 0;
  float liberation = 0.0f;   // 0..100
  PlanetEvent event;
  HazardKind hazards[kMaxHazards] = {};
  uint8_t hazardCount = 0;

  // UTC epoch of the poll this snapshot was taken at. Stamped by the poll loop
  // rather than the API layer, which has no clock of its own, and used as the
  // time base for the %-per-hour readouts. A snapshot carried forward from a
  // failed fetch keeps its original stamp, so a stale sample cannot masquerade
  // as a fresh one.
  time_t observedAt = 0;
};

// --- Task types -------------------------------------------------------------
//
// The `type` field on an assignment task says what *kind* of objective it is,
// which decides how it can honestly be displayed. Only the codes we actually
// key off are named; anything else is carried through as a raw number and gets
// the neutral complete/in-progress treatment in the renderer.
//
// The distinction that matters: a liberation percentage (derived from a
// planet's health/maxHealth) is only meaningful while a planet is being pushed
// down from full health. For a defend task the planet sits at full health when
// nothing is attacking it, so the same formula reads "0% liberated" for a
// planet that is in fact fully secured. Defend success is binary, per the
// task's own progress entry.
static const int32_t kTaskTypeLiberate = 11;  // take/hold a planet
static const int32_t kTaskTypeDefend = 13;    // defend a planet against attacks

// Only a liberate-style task may be drawn as a liberation bar.
inline bool taskIsLiberation(int32_t taskType) {
  return taskType == kTaskTypeLiberate;
}

// One objective inside a Major Order. Orders routinely carry several — a
// "defend three planets" order is three tasks with one progress entry each.
struct OrderTask {
  bool valid = false;
  int32_t taskType = -1;    // raw `type` field from the API
  int32_t planetIndex = -1; // -1 if the task has no planet-index slot
  // progress[i] > 0. Binary for planet-scoped tasks; for a count-style task
  // (e.g. "kill 500M Terminids") the API's progress is a running total, so
  // this only means "has started" — such tasks are not planet-scoped and the
  // renderer does not claim a status for them.
  bool complete = false;
  // Filled in after the fact by the poll loop, from GET /planets/{index}.
  PlanetInfo planet;
};

// Headroom over the largest order seen in the wild (three targets).
static const int kMaxOrderTasks = 4;

// One Major Order (GET /api/v1/assignments, element 0).
struct MajorOrder {
  bool valid = false;
  int32_t id = 0;           // assignment id; identity for the rate history
  String title;
  String briefing;
  int32_t rewardAmount = 0;
  time_t expiration = 0;    // UTC epoch, 0 if unparseable
  OrderTask tasks[kMaxOrderTasks];
  int taskCount = 0;
};

// Galactic totals (GET /api/v1/war). Optional garnish; a failure here never
// fails a poll.
struct WarStats {
  bool valid = false;
  uint64_t totalKills = 0;
  uint32_t missionSuccessRate = 0;  // percent
  uint32_t playerCount = 0;         // divers online, galaxy-wide
};

// --- Rate of change ---------------------------------------------------------
//
// The API publishes levels, never rates: there is no "%/h" field anywhere in
// the payload. The card quotes one, so it has to be measured on-device by
// diffing consecutive polls. One previous sample per target is enough — the
// poll interval is 5 minutes and the figure is a trend indicator, not
// telemetry.
struct RateSample {
  bool valid = false;
  int32_t planetIndex = -1;
  time_t at = 0;             // UTC epoch the sample was taken at
  float libPct = 0.0f;       // planet liberation, 0..100
  float eventPct = 0.0f;     // event defence, 0..100
  bool haveEvent = false;    // false => eventPct carries nothing
  // Which defence this eventPct belongs to. When one event ends and another
  // starts on the same planet the health pair restarts from scratch, and
  // diffing across that reset would report a huge fictitious swing.
  time_t eventEnd = 0;
};

// Percentage points per hour between two samples, or false when the pair can't
// support a number: no previous sample (first poll after boot, or a target that
// has only just appeared), a sample from a different planet, or two samples
// with no measurable time between them. Callers print "--" on false rather
// than a rate conjured out of a divide-by-zero.
inline bool ratePerHour(float nowPct, time_t nowAt, float prevPct, time_t prevAt,
                        float &out) {
  if (nowAt <= 0 || prevAt <= 0) return false;
  const double hours = (double)(nowAt - prevAt) / 3600.0;
  if (hours <= 0.0004) return false;  // < ~1.5s apart: noise, not a rate
  out = (float)((nowPct - prevPct) / hours);
  return true;
}

// Everything the renderer needs for one frame.
struct HudModel {
  MajorOrder order;
  WarStats war;

  // Previous poll's progress, one slot per order task. Written by the poll
  // loop before it overwrites `order`, and wiped whenever the Major Order id
  // changes — a new order's targets share nothing with the old one's, so
  // diffing across the boundary would quote a rate for a jump that never
  // happened.
  RateSample history[kMaxOrderTasks];
  int32_t historyOrderId = 0;

  bool haveData = false;      // at least one successful poll since boot
  bool stale = false;         // last poll failed / data older than kStaleAfterS
  bool wifiUp = false;
  time_t lastSuccess = 0;     // UTC epoch of the last good poll
  uint32_t failCount = 0;

  // Minutes to add to UTC when a clock time is *displayed*. Set from NVS at
  // boot (see main.cpp); durations such as the countdown ignore it.
  int16_t utcOffsetMin = 0;
};
