#pragma once

#include <atomic>
#include <cstdint>

// Host-testable acquisition rules for the eight draw slots.
// Wait codes match nvn::SyncWaitResult: 0 already signaled, 1 condition
// satisfied, 2 timeout, 3 failed. Any other code leaves a submitted slot pending.

enum class DrawSlotPhase : std::uint8_t { Free, Recorded, Submitted, Failed };

struct DrawSlotRules {
  static constexpr int kCount = 8;

  DrawSlotPhase phase[kCount]{};
  int cursor = 0;
  std::atomic<std::uint64_t> quarantined{0};

  int acquire(const int waits[kCount]) {
    for (int step = 0; step < kCount; ++step) {
      const int index = (cursor + step) % kCount;
      if (phase[index] == DrawSlotPhase::Failed || phase[index] == DrawSlotPhase::Recorded) {
        continue;
      }
      if (phase[index] == DrawSlotPhase::Submitted) {
        const int wait = waits[index];
        if (wait == 0 || wait == 1) {
          cursor = (index + 1) % kCount;
          return index;
        }
        if (wait == 3) {
          fail(index);
        }
        continue;
      }
      cursor = (index + 1) % kCount;
      return index;
    }
    return -1;
  }

  void markRecorded(int index) {
    if (index >= 0 && index < kCount && phase[index] != DrawSlotPhase::Failed) {
      phase[index] = DrawSlotPhase::Recorded;
    }
  }

  void markSubmitted(int index) {
    if (index >= 0 && index < kCount && phase[index] == DrawSlotPhase::Recorded) {
      phase[index] = DrawSlotPhase::Submitted;
    }
  }

  bool cancelUnsubmitted(int index) {
    if (index < 0 || index >= kCount || phase[index] != DrawSlotPhase::Recorded) {
      return false;
    }
    phase[index] = DrawSlotPhase::Free;
    return true;
  }

  void fail(int index) {
    if (index < 0 || index >= kCount || phase[index] == DrawSlotPhase::Failed) {
      return;
    }
    phase[index] = DrawSlotPhase::Failed;
    quarantined.fetch_add(1, std::memory_order_relaxed);
  }
};
