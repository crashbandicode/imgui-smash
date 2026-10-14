#include "imgui_backend/frame_guard.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

int g_failures = 0;

void expect(bool condition, const char* name) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", name);
        g_failures++;
    }
}

struct SubmitCounts {
    int original = 0;
    int overlay = 0;
};

void submit_once(FrameEntryOwner& owner, bool overlay_ready, SubmitCounts& counts, bool reenter) {
    FrameEntryOwner::Claim claim = owner.begin(overlay_ready);
    if (!claim.holds()) {
        counts.original += 1;
        return;
    }
    counts.overlay += 1;
    if (reenter) {
        submit_once(owner, true, counts, false);
    }
}

void test_unready_forwards_without_counters() {
    FrameEntryOwner owner;
    SubmitCounts counts;
    submit_once(owner, false, counts, false);
    expect(counts.original == 1, "unready forwards once");
    expect(counts.overlay == 0, "unready omits overlay");
    expect(owner.accepted() == 0, "unready does not accept");
    expect(owner.busy_skips() == 0, "unready is not busy");
}

void test_enter_holds_until_release() {
    FrameEntryOwner owner;
    SubmitCounts counts;
    submit_once(owner, true, counts, false);
    expect(counts.original == 0, "enter does not forward");
    expect(counts.overlay == 1, "enter draws once");
    expect(owner.accepted() == 1, "enter accepts one");
    expect(owner.busy_skips() == 0, "enter is not busy");
    submit_once(owner, true, counts, false);
    expect(owner.accepted() == 2, "release allows the next frame");
    expect(counts.overlay == 2, "second frame enters");
}

void test_reentry_forwards_once() {
    FrameEntryOwner owner;
    SubmitCounts counts;
    submit_once(owner, true, counts, true);
    expect(counts.overlay == 1, "outer entry owns the frame");
    expect(counts.original == 1, "reentry forwards original once");
    expect(owner.accepted() == 1, "reentry does not accept another frame");
    expect(owner.busy_skips() == 1, "reentry counts one busy skip");
    submit_once(owner, true, counts, false);
    expect(owner.accepted() == 2, "outer release clears the flag");
    expect(owner.busy_skips() == 1, "later entry is not a busy skip");
}

void test_exclusive_second_claim() {
    FrameEntryOwner owner;
    FrameEntryOwner::Claim first = owner.begin(true);
    FrameEntryOwner::Claim second = owner.begin(true);
    expect(first.holds(), "first claim owns");
    expect(!second.holds(), "second claim does not own");
    expect(owner.accepted() == 1, "one accepted owner");
    expect(owner.busy_skips() == 1, "second claim is busy");
}

void test_getter_copies_atomics() {
    ImguiSmashFrameGuardStats before{};
    expect(imgui_smash_frame_guard_stats(nullptr) == 0, "null getter returns 0");
    expect(imgui_smash_frame_guard_stats(&before) == 1, "getter returns 1");
    expect(before.version == 1, "version is 1");
    expect(before.size == 24, "size is 24");
    const uint64_t accepted = before.accepted;
    const uint64_t busy = before.busy_skips;
    {
        FrameEntryOwner::Claim claim = claim_frame_entry(true);
        expect(claim.holds(), "production claim owns");
        FrameEntryOwner::Claim nested = claim_frame_entry(true);
        expect(!nested.holds(), "production reentry does not own");
        ImguiSmashFrameGuardStats during{};
        expect(imgui_smash_frame_guard_stats(&during) == 1, "getter during claim");
        expect(during.version == 1 && during.size == 24, "abi during claim");
        expect(during.accepted == accepted + 1, "getter sees accepted");
        expect(during.busy_skips == busy + 1, "getter sees busy skip");
    }
    FrameEntryOwner::Claim after = claim_frame_entry(false);
    expect(!after.holds(), "unready production claim does not own");
    ImguiSmashFrameGuardStats stats{};
    expect(imgui_smash_frame_guard_stats(&stats) == 1, "getter after release");
    expect(stats.accepted == accepted + 1, "unready does not accept");
    expect(stats.busy_skips == busy + 1, "unready does not count busy");
    FrameEntryOwner::Claim again = claim_frame_entry(true);
    expect(again.holds(), "production release allows another claim");
}

}  // namespace

int main() {
    test_unready_forwards_without_counters();
    test_enter_holds_until_release();
    test_reentry_forwards_once();
    test_exclusive_second_claim();
    test_getter_copies_atomics();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("frame-guard host tests passed\n");
    return 0;
}
