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
// --- Biome backdrop ---------------------------------------------------------
//
// Index into biomes::table in hud_biomes.h, matched from the API's biome name.
//
// This mapping is a judgement call, not an authority. The API names biomes in
// the game's own vocabulary ("Ionic Jungle", "Desert Cliffs") while the art set
// is labelled in another ("Charged Primordial", "Craggy Sands"), and nothing
// joins the two: the art's own biome ids identify a family, not a variant. So
// this matches on keywords the way hazardFromName() does, and picks the closest
// plate. A miss shows a plausible landscape of the wrong flavour, never a
// broken screen -- kBiomeUnknown is the floor.
//
// Positionally coupled to biomes::table -- the value IS the table index, so
// this list and the table's order must be edited together. The table is
// generated from the slugs in tools/assets/biomes sorted by name, minus the
// UNREACHABLE set in tools/gen_biomes.py, which is why nine plates the art
// set ships (the extra black holes and glaciers, the two tutorial biomes) have
// no enumerator here: nothing below ever returned them, so they were costing a
// flash slot 19,360 B apiece to never be drawn. kBiomeCyberstan is the one
// exception kept on purpose -- nothing returns it either, but it is a real
// planet the live war could start naming, so the plate is held as a hedge.
enum : int8_t {
  kBiomeBlackholeBase = 0, kBiomeBugHive, kBiomeConiferous, kBiomeCyberstan,
  kBiomeDeciduousAutumn, kBiomeDeciduous, kBiomeGlacier,
  kBiomeMagma, kBiomeMoorArid, kBiomeMoor, kBiomeMoorRed, kBiomeMoorTundra,
  kBiomePrimordial, kBiomePrimordialBlue, kBiomePrimordialBug,
  kBiomePrimordialDead, kBiomePrimordialPurple, kBiomeSandyAcid, kBiomeSandy,
  kBiomeSandyMineral, kBiomeSandyMoon, kBiomeSandySpiky, kBiomeSuperEarth,
  kBiomeSwamp, kBiomeSwampHaunted, kBiomeUnknown,
};

inline int8_t biomeFromName(const String &raw) {
  String s = raw;
  s.toLowerCase();
  if (s.length() == 0) return kBiomeUnknown;

  // Most specific first: several of these also contain a broader keyword.
  if (s.indexOf("supercolony") >= 0) return kBiomePrimordialBug;
  if (s.indexOf("hive") >= 0) return kBiomeBugHive;
  if (s.indexOf("ethereal") >= 0) return kBiomePrimordialPurple;
  if (s.indexOf("crimson") >= 0) return kBiomeMoorRed;
  if (s.indexOf("ionic") >= 0 || s.indexOf("charged") >= 0) return kBiomePrimordialBlue;
  if (s.indexOf("boneyard") >= 0 || s.indexOf("parasit") >= 0) return kBiomePrimordialDead;
  if (s.indexOf("volcan") >= 0 || s.indexOf("magma") >= 0 || s.indexOf("molten") >= 0 ||
      s.indexOf("scorch") >= 0)
    return kBiomeMagma;
  if (s.indexOf("haunt") >= 0) return kBiomeSwampHaunted;
  if (s.indexOf("swamp") >= 0 || s.indexOf("bog") >= 0) return kBiomeSwamp;
  if (s.indexOf("jungle") >= 0 || s.indexOf("primordial") >= 0 || s.indexOf("lush") >= 0)
    return kBiomePrimordial;
  if (s.indexOf("tundra") >= 0) return kBiomeMoorTundra;
  if (s.indexOf("glacier") >= 0 || s.indexOf("arctic") >= 0 || s.indexOf("icy") >= 0 ||
      s.indexOf("ice") >= 0 || s.indexOf("snow") >= 0)
    return kBiomeGlacier;
  if (s.indexOf("acid") >= 0) return kBiomeSandyAcid;
  if (s.indexOf("moon") >= 0 || s.indexOf("lunar") >= 0) return kBiomeSandyMoon;
  if (s.indexOf("cliff") >= 0 || s.indexOf("craggy") >= 0 || s.indexOf("canyon") >= 0 ||
      s.indexOf("mesa") >= 0 || s.indexOf("spiky") >= 0)
    return kBiomeSandySpiky;
  if (s.indexOf("rocky") >= 0 || s.indexOf("mineral") >= 0) return kBiomeSandyMineral;
  if (s.indexOf("desert") >= 0 || s.indexOf("sand") >= 0 || s.indexOf("dune") >= 0 ||
      s.indexOf("badland") >= 0 || s.indexOf("arid") >= 0)
    return kBiomeSandy;
  if (s.indexOf("moor") >= 0 || s.indexOf("deadland") >= 0) return kBiomeMoorArid;
  if (s.indexOf("plain") >= 0 || s.indexOf("highland") >= 0 || s.indexOf("meadow") >= 0)
    return kBiomeMoor;
  if (s.indexOf("conifer") >= 0 || s.indexOf("pine") >= 0) return kBiomeConiferous;
  if (s.indexOf("autumn") >= 0) return kBiomeDeciduousAutumn;
  if (s.indexOf("forest") >= 0 || s.indexOf("deciduous") >= 0 || s.indexOf("wood") >= 0)
    return kBiomeDeciduous;
  if (s.indexOf("terraform") >= 0 || s.indexOf("super earth") >= 0) return kBiomeSuperEarth;
  if (s.indexOf("singularity") >= 0 || s.indexOf("black hole") >= 0)
    return kBiomeBlackholeBase;
  return kBiomeUnknown;
}

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
  // Health the planet claws back on its own, per second. The enemy's half of
  // the tug-of-war: the net rate the HUD shows is the players' push minus
  // this. Only the campaigns feed reports it; a plain planet fetch leaves it 0.
  float regenPerSecond = 0.0f;
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
//
// The count-style types below are the other family: their progress is a
// running total against a goal the task carries itself, and it has nothing to
// do with the planet's health. Confirmed against a live order on 2026-08-15
// (assignment 3979642198, "complete operations / eliminate enemies / harvest
// samples"), which carried one of each. They *are* planet-scoped — every one
// of those three tasks named a planet — so do not assume a count implies a
// galaxy-wide objective; see taskIsCount() for why the goal, not the type
// code, is what the renderer actually branches on.
static const int32_t kTaskTypeExtract = 2;     // extract N samples
static const int32_t kTaskTypeEradicate = 3;   // kill N enemies
static const int32_t kTaskTypeOperations = 9;  // complete N operations
static const int32_t kTaskTypeLiberate = 11;   // take/hold a planet
static const int32_t kTaskTypeDefend = 13;     // defend a planet against attacks

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
  // The task's entry in the order's parallel `progress` array, raw. For a
  // planet-scoped liberate/defend this is 0 or 1; for a count-style task it is
  // a running total ("241192325 of 1250000000 enemies killed").
  uint64_t progress = 0;
  // The task's own goal, from its valueType-3 slot. 1 for the binary types,
  // the target count for a count-style task, 0 if the task carried no such
  // slot at all. 64-bit because kill goals have reached 1.25 *billion*, which
  // is already over half of what an int32 can hold.
  uint64_t goal = 0;
  // Derived, not read: `progress >= goal` where there is a goal to compare
  // against. Reading this straight off `progress > 0` — as this did before —
  // is what made a kill task 19% of the way in announce itself as
  // "OBJECTIVE COMPLETE".
  bool complete = false;
  // Filled in after the fact by the poll loop, from GET /planets/{index}.
  // May stay invalid on a perfectly healthy task: the community API 404s
  // /planets/{index} for indices missing from its own static table, which is
  // exactly what happens for the first days of a war's newly-added planets.
  // Nothing that can be drawn from the assignment payload alone may be gated
  // on this.
  PlanetInfo planet;
};

// A count-style objective, decided by data shape rather than by type code: a
// goal above 1 can only be a running total, whatever the type says. The
// community API's own repo documents valueTypes as "a list of numbers, purpose
// unknown" and Arrowhead has reshuffled task encodings before, so keying the
// *rendering* decision off the goal keeps a brand-new count type rendering
// correctly on day one. The named type codes above are used only to caption
// what is being counted, where being wrong costs a word, not a progress bar.
inline bool taskIsCount(const OrderTask &t) { return t.goal > 1; }

// A count task's completion, 0..100. Clamped at the top because the API keeps
// counting past the goal on eradicate objectives (the war does not stop the
// instant the target is met) and a 104%-full bar reads as a rendering fault.
inline float taskPercent(const OrderTask &t) {
  if (t.goal == 0) return 0.0f;
  const double pct = (double)t.progress * 100.0 / (double)t.goal;
  return pct > 100.0 ? 100.0f : (float)pct;
}

// Headroom over the largest order seen in the wild (three targets).
static const int kMaxOrderTasks = 4;

// Cards shown in the campaign-rotation carousel, top N by player count.
static const int kMaxCampaigns = 5;

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

  // A count-style task's completion at `countAt`, kept apart from the
  // planet-derived fields above and carrying its own timestamp on purpose.
  // Count progress rides in on the assignments payload itself, so it is
  // available on every successful poll — including the ones where the task's
  // planet 404s and every field above is reset. Sharing `at`/`planetIndex`
  // with them would throw this away on exactly the orders that most need a
  // rate, since a planet the API cannot resolve fails on every poll, not one.
  bool haveCount = false;
  time_t countAt = 0;        // UTC epoch of the poll countPct was read from
  float countPct = 0.0f;     // task completion, 0..100
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
// --- Event overlays ---------------------------------------------------------
//
// Full-screen announcements that take the panel away from the carousel until
// they are acknowledged. There are two kinds and they read differently on
// purpose: an announcement is "here is what you have been asked to do", a
// verdict is "here is how it went".
//
// The API has no event stream and no outcome field -- a Major Order that ends
// simply stops appearing in GET /api/v1/assignments -- so all three of these
// are inferred by the poll loop from what changed between two polls. See
// classifyOrderOutcome() in main.cpp for the rules and their one genuine
// ambiguity.
enum OverlayKind : uint8_t {
  kOverlayNone = 0,
  kOverlayNewOrder,  // an assignment id this device has not seen before
  kOverlaySuccess,   // left the feed with every task complete
  kOverlayFailure,   // left the feed incomplete, or ran out of time
};

struct HudModel {
  MajorOrder order;
  WarStats war;

  // The announcement currently owning the screen, and what it is about.
  //
  // `overlaySubject` is a copy rather than a reference into `order` because a
  // verdict outlives its order by definition: by the time SUCCESS is drawn the
  // assignment has already left the feed and `order` holds the next one, or
  // nothing at all.
  OverlayKind overlay = kOverlayNone;
  MajorOrder overlaySubject;

  // Previous poll's progress, one slot per order task. Written by the poll
  // loop before it overwrites `order`, and wiped whenever the Major Order id
  // changes — a new order's targets share nothing with the old one's, so
  // diffing across the boundary would quote a rate for a jump that never
  // happened.
  RateSample history[kMaxOrderTasks];
  int32_t historyOrderId = 0;

  // The active liberation campaigns, top kMaxCampaigns by player count,
  // fetched every poll regardless of whether a Major Order is active — they
  // form the back half of the carousel (MO task cards first, then
  // these). Also what the idle state falls back to showing when there is no
  // Major Order at all. Own rate history per slot, matched by planet index,
  // since these aren't any order's tasks.
  PlanetInfo campaigns[kMaxCampaigns];
  RateSample campaignHistory[kMaxCampaigns];
  uint8_t campaignCount = 0;

  bool haveData = false;      // at least one successful poll since boot
  bool stale = false;         // last poll failed / data older than kStaleAfterS
  bool wifiUp = false;
  // touch::calibrated() is false: taps land approximately, and the footer says
  // how to fix it. Carried on the model rather than read from hud_touch by the
  // renderer, which knows about nothing but this struct -- and it means the
  // preview harness can shoot the state without a touch panel to hand.
  bool touchUncalibrated = false;
  time_t lastSuccess = 0;     // UTC epoch of the last good poll
  uint32_t failCount = 0;

  // Minutes to add to UTC when a clock time is *displayed*. Set from NVS at
  // boot (see main.cpp); durations such as the countdown ignore it.
  int16_t utcOffsetMin = 0;
};

// --- LIBCON ------------------------------------------------------------
//
// The community app's own "Liberty Readiness Condition" -- a DEFCON parody,
// not a field the API publishes. Its site derives tier 1 from two inputs
// this device has no access to (a keyword scan over Super Earth's news
// dispatches, and a flag for Super Earth itself being under direct attack),
// so this mirrors only the part of its logic this device's own poll data can
// honestly support, and never claims tier 1 on its own -- the reference
// app's own FAQ says that tier is reserved for an explicit declaration, not
// something inferred from the war state.
//
//   2: a Major Order is active and at least one of its targets has a live
//      defence running (an invasion in progress).
//   3: a Major Order is active, no defence running.
//   4: no Major Order, but the campaigns feed found a live fight (what the
//      idle screen's campaign fallback is already showing).
//   5: no Major Order, no live campaign -- the quietest state this device
//      can observe.
inline int8_t libconTier(const HudModel &m) {
  if (!m.haveData) return 0;  // nothing polled yet: chip stays off
  if (m.order.valid) {
    for (int i = 0; i < m.order.taskCount; i++) {
      if (m.order.tasks[i].valid && m.order.tasks[i].planet.event.active) return 2;
    }
    return 3;
  }
  if (m.campaignCount > 0) return 4;
  return 5;
}
