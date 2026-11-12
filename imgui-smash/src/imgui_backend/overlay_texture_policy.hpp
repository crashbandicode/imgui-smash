#pragma once

// Decisions for the pinned ImGui NVN backend's private texture table.
// Native nvnWindowBuilderSetTextures and nvnWindowAcquireTexture keep their
// own arguments and result. This header only decides whether the backend may
// copy pointers into its three-slot table or use one as an overlay target.

namespace overlay_texture {

// NVNwindowAcquireTextureResult::SUCCESS. Failure values, including
// NATIVE_ERROR (1) and the LARGE sentinel, are not success.
constexpr int kWindowAcquireSuccess = 0;
constexpr int kWindowAcquireNativeError = 1;
constexpr int kOverlayTextureCapacity = 3;
constexpr int kOverlayTextureDisabled = -1;

struct OverlayTextureBinding {
    bool supported;
    int count;
};

struct OverlayTexturePlan {
    bool copy_into_overlay;
    bool overlay_supported;
    int overlay_count;
    int native_count;
};

struct OverlayIndexUpdate {
    bool replace_current;
    int slot;
};

struct TableInstall {
    OverlayTextureBinding binding;
    int current_index;
};

inline OverlayTexturePlan plan_overlay_textures(int num_textures, bool textures_nonnull) {
    OverlayTexturePlan plan{};
    plan.native_count = num_textures;
    if (textures_nonnull && num_textures >= 1 && num_textures <= kOverlayTextureCapacity) {
        plan.copy_into_overlay = true;
        plan.overlay_supported = true;
        plan.overlay_count = num_textures;
    }
    return plan;
}

inline OverlayTextureBinding binding_from_plan(OverlayTexturePlan plan) {
    if (!plan.overlay_supported) {
        return OverlayTextureBinding{false, 0};
    }
    return OverlayTextureBinding{true, plan.overlay_count};
}

// Any replacement of the private table drops the previous slot until a later
// successful acquire publishes a new one. Supported counts do this too.
inline TableInstall install_overlay_texture_table(OverlayTexturePlan plan) {
    return TableInstall{binding_from_plan(plan), kOverlayTextureDisabled};
}

// Copies only a supported plan into the fixed table. An unsupported count
// clears the table and does not read `source`.
template <typename Texture>
inline void publish_overlay_texture_table(OverlayTexturePlan plan, Texture** destination,
                                          Texture* const* source) {
    for (int slot = 0; slot < kOverlayTextureCapacity; ++slot) {
        destination[slot] = nullptr;
    }
    if (!plan.copy_into_overlay || source == nullptr || plan.overlay_count < 1) {
        return;
    }
    const int count = plan.overlay_count < kOverlayTextureCapacity ? plan.overlay_count
                                                                    : kOverlayTextureCapacity;
    for (int slot = 0; slot < count; ++slot) {
        destination[slot] = source[slot];
    }
}

inline bool overlay_texture_index_accepted(int slot, OverlayTextureBinding binding) {
    return binding.supported && slot >= 0 && slot < binding.count && slot < kOverlayTextureCapacity;
}

// `read_index` runs only after a successful acquire whose table can hold a slot.
// A failed acquire leaves the native output unwritten, so it must not be loaded.
template <typename ReadIndex>
inline OverlayIndexUpdate overlay_index_after_native_acquire(int native_result, bool index_available,
                                                             ReadIndex read_index,
                                                             OverlayTextureBinding binding) {
    if (native_result != kWindowAcquireSuccess || !index_available) {
        return OverlayIndexUpdate{true, kOverlayTextureDisabled};
    }
    if (!binding.supported) {
        return OverlayIndexUpdate{true, kOverlayTextureDisabled};
    }
    const int slot = static_cast<int>(read_index());
    if (!overlay_texture_index_accepted(slot, binding)) {
        return OverlayIndexUpdate{true, kOverlayTextureDisabled};
    }
    return OverlayIndexUpdate{true, slot};
}

extern OverlayTextureBinding g_overlay_texture_binding;

}  // namespace overlay_texture
