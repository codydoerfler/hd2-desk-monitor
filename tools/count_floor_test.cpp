// ---------------------------------------------------------------------------
//  count_floor_test.cpp — drive HudModel across synthetic polls, on the host.
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

  printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", failures);
  return failures ? 1 : 0;
}
