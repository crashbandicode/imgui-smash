// Concurrent ownership checks against the frozen frame_guard.cpp helper.
// Acceptance is FrameEntryOwner itself. This file does not imitate submitCommands.

#include "imgui_backend/frame_guard.hpp"

#include <atomic>
#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {

int g_failures = 0;

void expect(bool condition, const char* name) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", name);
        g_failures += 1;
    }
}

void test_two_threads() {
    FrameEntryOwner owner;
    std::barrier<> holding(2);
    std::barrier<> missed(2);
    std::barrier<> released(2);
    std::barrier<> acquired(2);
    std::atomic<int> miss_holds{-1};
    std::atomic<int> later_holds{-1};

    std::thread contender([&] {
        holding.arrive_and_wait();
        FrameEntryOwner::Claim miss = owner.begin(true);
        miss_holds.store(miss.holds() ? 1 : 0, std::memory_order_release);
        missed.arrive_and_wait();
        released.arrive_and_wait();
        FrameEntryOwner::Claim later = owner.begin(true);
        later_holds.store(later.holds() ? 1 : 0, std::memory_order_release);
        acquired.arrive_and_wait();
    });

    {
        FrameEntryOwner::Claim first = owner.begin(true);
        expect(first.holds(), "first thread holds");
        holding.arrive_and_wait();
        missed.arrive_and_wait();
        expect(miss_holds.load(std::memory_order_acquire) == 0, "second claim fails while held");
        expect(owner.accepted() == 1, "one accept while held");
        expect(owner.busy_skips() == 1, "busy increments while held");
    }
    released.arrive_and_wait();
    acquired.arrive_and_wait();
    contender.join();
    expect(later_holds.load(std::memory_order_acquire) == 1, "second claim succeeds after release");
    expect(owner.accepted() == 2, "accept after release");
    expect(owner.busy_skips() == 1, "later success is not busy");
}

void leave_early(FrameEntryOwner& owner, int& stage) {
    FrameEntryOwner::Claim claim = owner.begin(true);
    if (!claim.holds()) {
        stage = -1;
        return;
    }
    stage = 1;
}

void test_early_return_releases() {
    FrameEntryOwner owner;
    int stage = 0;
    leave_early(owner, stage);
    expect(stage == 1, "early return left a held claim");
    FrameEntryOwner::Claim next = owner.begin(true);
    expect(next.holds(), "early return released the claim");
    expect(owner.accepted() == 2, "accept after early return");
    expect(owner.busy_skips() == 0, "early return is not a busy skip");
}

void test_production_getter_abi() {
    expect(imgui_smash_frame_guard_stats(nullptr) == 0, "null getter returns 0");
    ImguiSmashFrameGuardStats stats{};
    expect(imgui_smash_frame_guard_stats(&stats) == 1, "getter returns 1");
    expect(stats.version == 1, "getter version is 1");
    expect(stats.size == 24, "getter size is 24");
    expect(sizeof(ImguiSmashFrameGuardStats) == 24, "local layout is 24");
}

}  // namespace

int main() {
    test_two_threads();
    test_early_return_releases();
    test_production_getter_abi();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("frame-guard concurrent tests passed\n");
    return 0;
}
