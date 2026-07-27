// ---------------------------------------------------------------------------
//  hd2_model.h — the plain-data structures shared between the API layer and
//  the renderer. Neither module knows about the other; both know about this.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

// One Major Order (GET /api/v1/assignments, element 0).
struct MajorOrder {
  bool valid = false;
  String title;
  String briefing;
  int32_t rewardAmount = 0;
  time_t expiration = 0;    // UTC epoch, 0 if unparseable
  int32_t planetIndex = -1; // target planet, -1 if the task has no planet
  bool taskComplete = false;
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
};

// Galactic totals (GET /api/v1/war). Optional garnish; a failure here never
// fails a poll.
struct WarStats {
  bool valid = false;
  uint64_t totalKills = 0;
  uint32_t missionSuccessRate = 0;  // percent
  uint32_t playerCount = 0;         // divers online, galaxy-wide
};

// Everything the renderer needs for one frame.
struct HudModel {
  MajorOrder order;
  PlanetInfo planet;
  WarStats war;

  bool haveData = false;      // at least one successful poll since boot
  bool stale = false;         // last poll failed / data older than kStaleAfterS
  bool wifiUp = false;
  time_t lastSuccess = 0;     // UTC epoch of the last good poll
  uint32_t failCount = 0;
};
