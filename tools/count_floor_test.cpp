// ---------------------------------------------------------------------------
//  count_floor_test.cpp — drive HudModel across synthetic polls, on the host.
//
//  Covers the two upstream-reset defences the card carries: the count-task
//  progress floor (applyCountProgressFloor) and the completed-liberation
//  figure (taskLiberation).
//
//  There is no hardware and no network here: applyCountProgressFloor() lives in
//  hd2_model.h with the rest of the plain data, so a poll can be simulated by
//  handing it a MajorOrder and looking at what comes back. That is the whole
//  reason the floor is a free function on the model rather than a private step
//  inside poll() in main.cpp, which cannot be compiled off-device.
//
//  Built + run via tools/count_floor_test.sh. Prints one line per case and
//  exits non-zero on the first failure.
// ---------------------------------------------------------------------------
#include "preview/Arduino.h"

#include <cstdio>
#include <cmath>

#include "../src/hd2_model.h"

static int failures = 0;

static void check(bool ok, const char *what) {
  printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

static bool near(float a, float b) { return fabsf(a - b) < 0.05f; }

// One count task, shaped like the live eradicate objective the 2026-08-15
// notes were taken against (assignment 3979642198).
static MajorOrder countOrder(int32_t id, uint64_t progress, uint64_t goal = 1250000000ull,
                             int32_t taskType = kTaskTypeEradicate) {
  MajorOrder o;
  o.valid = true;
  o.id = id;
  o.title = "ERADICATE";
  o.taskCount = 1;
  OrderTask &t = o.tasks[0];
  t.valid = true;
  t.taskType = taskType;
  t.planetIndex = 280;
  t.progress = progress;
  t.goal = goal;
  // Exactly how hd2_api.cpp derives it from the payload.
  t.complete = t.goal > 0 ? (t.progress >= t.goal) : (t.progress > 0);
  return o;
}

// One liberate task, shaped the way the assignments endpoint delivers them:
// progress/goal are the binary 0/1 or 1/1 pair, and the planet record is
// filled in afterwards by the /planets/{index} fetch.
static OrderTask liberateTask(uint64_t progress, uint64_t goal) {
  OrderTask t;
  t.valid = true;
  t.taskType = kTaskTypeLiberate;
  t.planetIndex = 172;
  t.progress = progress;
  t.goal = goal;
  t.complete = t.goal > 0 ? (t.progress >= t.goal) : (t.progress > 0);
  t.planet.valid = true;
  t.planet.index = 172;
  t.planet.name = "GAR HAREN";
  t.planet.maxHealth = 1000000;
  t.planet.health = 1000000;
  return t;
}

// Push one order through the same call the poll loop makes, then commit it to
// the model the way poll() does.
static MajorOrder poll(HudModel &m, MajorOrder next) {
  applyCountProgressFloor(m, next);
  m.order = next;
  m.haveData = true;
  return next;
}

int main() {
  const uint64_t kGoal = 1250000000ull;

  // --- The reported bug -----------------------------------------------------
  // A count task reaches its goal, then the next poll reports progress reset
  // to 0 with the goal still intact. The card must not flip to 0.0%.
  {
    printf("completed task whose progress resets to 0:\n");
    HudModel m;
    poll(m, countOrder(4001, kGoal));
    const MajorOrder after = poll(m, countOrder(4001, 0));
    check(after.tasks[0].progress == kGoal, "progress held at the goal");
    check(near(taskPercent(after.tasks[0]), 100.0f), "still reads 100.0%");
    check(after.tasks[0].complete, "still reads complete");
  }

  // --- A partial task that regresses ---------------------------------------
  // The same reshuffle can land on a task that had not finished. It floors to
  // the high water mark, which is not 100% and must not be reported as one.
  {
    printf("in-progress task that regresses:\n");
    HudModel m;
    poll(m, countOrder(4002, 625000000ull));  // 50%
    const MajorOrder after = poll(m, countOrder(4002, 12ull));
    check(after.tasks[0].progress == 625000000ull, "progress held at the mark");
    check(near(taskPercent(after.tasks[0]), 50.0f), "still reads 50.0%");
    check(!after.tasks[0].complete, "not falsely marked complete");
  }

  // --- Ordinary forward progress is untouched -------------------------------
  {
    printf("normal climb across four polls:\n");
    HudModel m;
    const uint64_t steps[] = {0ull, 125000000ull, 900000000ull, kGoal};
    const float want[] = {0.0f, 10.0f, 72.0f, 100.0f};
    bool ok = true;
    for (int i = 0; i < 4; i++) {
      const MajorOrder after = poll(m, countOrder(4003, steps[i]));
      if (after.tasks[0].progress != steps[i] || !near(taskPercent(after.tasks[0]), want[i]))
        ok = false;
    }
    check(ok, "every reading passed through as parsed");
    check(m.order.tasks[0].complete, "ends complete");
  }

  // --- A fresh order is not frozen at the old one's mark --------------------
  {
    printf("new order id after a completed one:\n");
    HudModel m;
    poll(m, countOrder(4004, kGoal));
    const MajorOrder after = poll(m, countOrder(4005, 0));
    check(after.tasks[0].progress == 0, "starts from zero again");
    check(near(taskPercent(after.tasks[0]), 0.0f), "reads 0.0%, not 100%");
    check(!after.tasks[0].complete, "not complete");
  }

  // --- A goal revised mid-order does not inherit the mark -------------------
  {
    printf("goal revised mid-order:\n");
    HudModel m;
    poll(m, countOrder(4006, kGoal));
    const MajorOrder after = poll(m, countOrder(4006, 500ull, 2000000000ull));
    check(after.tasks[0].progress == 500ull, "reading taken at face value");
    check(!after.tasks[0].complete, "not carried over as complete");
  }

  // --- A task edited in place does not inherit the mark ---------------------
  {
    printf("task type changed in the same slot:\n");
    HudModel m;
    poll(m, countOrder(4007, kGoal));
    const MajorOrder after =
        poll(m, countOrder(4007, 4ull, kGoal, kTaskTypeOperations));
    check(after.tasks[0].progress == 4ull, "reading taken at face value");
    check(!after.tasks[0].complete, "not carried over as complete");
  }

  // --- The %/h readout sees the floored figure too ---------------------------
  // rollRateHistory() diffs model.order against the incoming one, both floored
  // by the time it runs, so a reset cannot produce a large negative rate.
  {
    printf("rate readout across the reset:\n");
    HudModel m;
    poll(m, countOrder(4008, 1237500000ull));  // 99%
    const float prevPct = taskPercent(m.order.tasks[0]);
    const MajorOrder after = poll(m, countOrder(4008, 0));
    // ratePerHour() wants two real UTC stamps an hour apart, the way two
    // consecutive polls' model.lastSuccess would be.
    const time_t prevAt = 1756000000;
    float rate = 0.0f;
    const bool have =
        ratePerHour(taskPercent(after.tasks[0]), prevAt + 3600, prevPct, prevAt, rate);
    check(have, "a rate is available at all");
    check(rate >= 0.0f, "no negative %/h spike");
  }

  // --- No order at all clears the marks -------------------------------------
  {
    printf("order leaves the feed, then a new one arrives:\n");
    HudModel m;
    poll(m, countOrder(4009, kGoal));
    poll(m, MajorOrder());  // empty feed: id 0, no tasks
    const MajorOrder after = poll(m, countOrder(4010, 0));
    check(after.tasks[0].progress == 0, "fresh order starts at zero");
    check(!m.countFloor[0].valid || m.countFloor[0].progress == 0,
          "no stale mark left behind");
  }

  // --- A completed liberation whose planet reads as healed -----------------
  // The mirror image of the reset above, and the reason taskLiberation()
  // exists: the API stops reporting a resolved planet as a combat target and
  // reports it at full health instead, which the health-derived figure reads
  // as 0% taken. The task's own progress/goal still say it is done.
  {
    printf("liberate task complete, planet reported healed:\n");
    OrderTask t = liberateTask(1, 1);
    t.planet.liberation = 0.0f;   // what fetchPlanet() derives from a healed planet
    t.planet.owner = "Humans";
    check(t.complete, "the task itself still reads complete");
    check(near(taskLiberation(t), 100.0f), "card figure is 100%, not 0%");
  }

  // --- A live push is left exactly as fetched -------------------------------
  // The whole risk in this fix is changing how an in-progress liberation
  // looks. It must pass through untouched at every reading, including a
  // planet sitting at genuine 0% before anyone has pushed it.
  {
    printf("liberation in progress:\n");
    const float steps[] = {0.0f, 12.5f, 79.4f, 99.9f};
    bool ok = true;
    for (int i = 0; i < 4; i++) {
      OrderTask t = liberateTask(0, 1);
      t.planet.liberation = steps[i];
      if (!near(taskLiberation(t), steps[i])) ok = false;
    }
    check(ok, "every reading passed through as fetched");
  }

  // --- Nothing else is touched ----------------------------------------------
  // Only a liberate-style task gets the 100% floor. A defend task at full
  // health is not "fully liberated", and a count task is not measured by
  // planet health at all — both already read t.complete their own way.
  {
    printf("other task types at full health:\n");
    OrderTask d = liberateTask(1, 1);
    d.taskType = kTaskTypeDefend;
    d.planet.liberation = 0.0f;
    check(near(taskLiberation(d), 0.0f), "a complete defend task is not floored");

    MajorOrder c = countOrder(4011, kGoal);
    c.tasks[0].planet.valid = true;
    c.tasks[0].planet.liberation = 0.0f;
    check(near(taskLiberation(c.tasks[0]), 0.0f), "a complete count task is not floored");
  }

  printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", failures);
  return failures ? 1 : 0;
}
