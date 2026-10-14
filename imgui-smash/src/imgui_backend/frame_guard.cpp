#include "frame_guard.hpp"

namespace {
FrameEntryOwner g_frame_entry;
}

FrameEntryOwner::Claim FrameEntryOwner::begin(bool overlay_ready) {
    if (!overlay_ready) {
        return Claim(nullptr);
    }
    if (flag_.test_and_set(std::memory_order_acquire)) {
        busy_skips_.fetch_add(1, std::memory_order_relaxed);
        return Claim(nullptr);
    }
    accepted_.fetch_add(1, std::memory_order_relaxed);
    return Claim(this);
}

void FrameEntryOwner::release() noexcept {
    flag_.clear(std::memory_order_release);
}

FrameEntryOwner::Claim claim_frame_entry(bool overlay_ready) {
    return g_frame_entry.begin(overlay_ready);
}

extern "C" int imgui_smash_frame_guard_stats(ImguiSmashFrameGuardStats* out) {
    if (out == nullptr) {
        return 0;
    }
    out->version = 1;
    out->size = static_cast<uint32_t>(sizeof(ImguiSmashFrameGuardStats));
    out->accepted = g_frame_entry.accepted();
    out->busy_skips = g_frame_entry.busy_skips();
    return 1;
}
