#include "draw_slot_rules.hpp"

#include <cstdio>

static int g_failures = 0;

static void expect(bool condition, const char *name) {
  if (!condition) {
    std::fprintf(stderr, "fail %s\n", name);
    g_failures += 1;
  }
}

static void fill(int waits[DrawSlotRules::kCount], int value) {
  for (int i = 0; i < DrawSlotRules::kCount; ++i) {
    waits[i] = value;
  }
}

static void testPendingIsNotReusable() {
  DrawSlotRules rules;
  int waits[DrawSlotRules::kCount];
  fill(waits, -1);
  rules.phase[0] = DrawSlotPhase::Submitted;
  waits[0] = 2;
  expect(rules.acquire(waits) == 1, "timeout selects the next free slot");
  expect(rules.phase[0] == DrawSlotPhase::Submitted, "timeout stays submitted");
  rules.phase[1] = DrawSlotPhase::Recorded;
  for (int i = 0; i < DrawSlotRules::kCount; ++i) {
    rules.phase[i] = DrawSlotPhase::Submitted;
    waits[i] = 2;
  }
  expect(rules.acquire(waits) == -1, "all timeouts skip the draw");
}

static void testSignaledIsReusable() {
  DrawSlotRules rules;
  int waits[DrawSlotRules::kCount];
  fill(waits, 2);
  rules.phase[0] = DrawSlotPhase::Submitted;
  waits[0] = 0;
  expect(rules.acquire(waits) == 0, "already signaled is reusable");
  rules.phase[0] = DrawSlotPhase::Submitted;
  rules.cursor = 0;
  waits[0] = 1;
  expect(rules.acquire(waits) == 0, "condition satisfied is reusable");
}

static void testFailureQuarantine() {
  DrawSlotRules rules;
  int waits[DrawSlotRules::kCount];
  fill(waits, -1);
  rules.phase[0] = DrawSlotPhase::Submitted;
  waits[0] = 3;
  expect(rules.acquire(waits) == 1, "failed slot is skipped");
  expect(rules.phase[0] == DrawSlotPhase::Failed, "failed slot is quarantined");
  expect(rules.quarantined == 1, "quarantine counted once");
  rules.phase[1] = DrawSlotPhase::Free;
  rules.cursor = 0;
  expect(rules.acquire(waits) == 1, "quarantine persists");
  expect(rules.quarantined == 1, "second scan does not recount");
}

static void testRoundRobin() {
  DrawSlotRules rules;
  int waits[DrawSlotRules::kCount];
  fill(waits, -1);
  for (int i = 0; i < DrawSlotRules::kCount; ++i) {
    const int got = rules.acquire(waits);
    expect(got == i, "round robin order");
    rules.markRecorded(got);
  }
  expect(rules.acquire(waits) == -1, "recorded slots are not acquired twice");
}

static void testUnsubmittedCancel() {
  DrawSlotRules rules;
  int waits[DrawSlotRules::kCount];
  fill(waits, -1);
  const int first = rules.acquire(waits);
  rules.markRecorded(first);
  expect(rules.cancelUnsubmitted(first), "cancel recorded slot");
  expect(rules.phase[first] == DrawSlotPhase::Free, "cancel returns the slot");
  expect(!rules.cancelUnsubmitted(first), "cancel is not a second free");
  rules.cursor = 0;
  expect(rules.acquire(waits) == first, "cancelled slot can be acquired");
  rules.markRecorded(0);
  rules.markSubmitted(0);
  expect(rules.phase[0] == DrawSlotPhase::Submitted, "recorded slot can be submitted");
  expect(!rules.cancelUnsubmitted(0), "submitted slot is not cancelled");
}

int main() {
  testPendingIsNotReusable();
  testSignaledIsReusable();
  testFailureQuarantine();
  testRoundRobin();
  testUnsubmittedCancel();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failures\n", g_failures);
    return 1;
  }
  std::printf("draw_slot_rules_test ok\n");
  return 0;
}
