#pragma once

#include <atomic>
#include <cstdint>

// Nonblocking exclusive ownership for one ImGui frame entry.
// Contention does not wait, and this helper never calls ImGui or NVN.

struct ImguiSmashFrameGuardStats {
    uint32_t version;
    uint32_t size;
    uint64_t accepted;
    uint64_t busy_skips;
};

static_assert(sizeof(ImguiSmashFrameGuardStats) == 24, "frame guard stats ABI is 24 bytes");

class FrameEntryOwner {
public:
    class Claim {
    public:
        Claim(const Claim&) = delete;
        Claim& operator=(const Claim&) = delete;
        Claim(Claim&&) = delete;
        Claim& operator=(Claim&&) = delete;

        ~Claim() {
            if (owner_ != nullptr) {
                owner_->release();
            }
        }

        bool holds() const { return owner_ != nullptr; }

    private:
        friend class FrameEntryOwner;
        explicit Claim(FrameEntryOwner* owner) : owner_(owner) {}

        FrameEntryOwner* owner_ = nullptr;
    };

    FrameEntryOwner() = default;
    FrameEntryOwner(const FrameEntryOwner&) = delete;
    FrameEntryOwner& operator=(const FrameEntryOwner&) = delete;

    // overlay_ready false does not touch the flag. A taken flag increments
    // busy_skips and does not own. A successful test_and_set increments
    // accepted and owns until the claim is destroyed.
    Claim begin(bool overlay_ready);

    uint64_t accepted() const { return accepted_.load(std::memory_order_relaxed); }
    uint64_t busy_skips() const { return busy_skips_.load(std::memory_order_relaxed); }

private:
    friend class Claim;
    void release() noexcept;

    std::atomic_flag flag_{};
    std::atomic<uint64_t> accepted_{0};
    std::atomic<uint64_t> busy_skips_{0};
};

// Production owner. Host tests use their own FrameEntryOwner for the
// ownership cases and this function for the linked getter.
FrameEntryOwner::Claim claim_frame_entry(bool overlay_ready);

extern "C" int imgui_smash_frame_guard_stats(ImguiSmashFrameGuardStats* out);
