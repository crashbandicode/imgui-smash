#include "overlay_texture_policy.hpp"

#include <cstdio>

namespace {

int g_failures = 0;

void expect(bool condition, const char* text, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s:%d %s\n", "overlay_texture_policy_test.cpp", line, text);
        ++g_failures;
    }
}

#define CHECK(condition) expect(static_cast<bool>(condition), #condition, __LINE__)

overlay_texture::OverlayTextureBinding supported(int count) {
    return overlay_texture::OverlayTextureBinding{true, count};
}

}  // namespace

int main() {
    using overlay_texture::kOverlayTextureDisabled;
    using overlay_texture::kWindowAcquireNativeError;
    using overlay_texture::kWindowAcquireSuccess;
    using overlay_texture::binding_from_plan;
    using overlay_texture::install_overlay_texture_table;
    using overlay_texture::overlay_index_after_native_acquire;
    using overlay_texture::overlay_texture_index_accepted;
    using overlay_texture::plan_overlay_textures;
    using overlay_texture::publish_overlay_texture_table;

    int reads = 0;
    auto reader = [&]() {
        ++reads;
        return 4;
    };
    const auto failed = overlay_index_after_native_acquire(kWindowAcquireNativeError, true, reader, supported(3));
    CHECK(failed.replace_current);
    CHECK(failed.slot == kOverlayTextureDisabled);
    CHECK(reads == 0);
    const auto large = overlay_index_after_native_acquire(0x7fffffff, true, reader, supported(3));
    CHECK(large.replace_current);
    CHECK(large.slot == kOverlayTextureDisabled);
    CHECK(reads == 0);
    const auto missing = overlay_index_after_native_acquire(kWindowAcquireSuccess, false, reader, supported(3));
    CHECK(missing.replace_current);
    CHECK(missing.slot == kOverlayTextureDisabled);
    CHECK(reads == 0);

    const auto unsupported = overlay_index_after_native_acquire(kWindowAcquireSuccess, true, reader,
                                                                overlay_texture::OverlayTextureBinding{false, 0});
    CHECK(unsupported.replace_current);
    CHECK(unsupported.slot == kOverlayTextureDisabled);
    CHECK(reads == 0);

    auto in_range = [&]() {
        ++reads;
        return 2;
    };
    const auto published = overlay_index_after_native_acquire(kWindowAcquireSuccess, true, in_range, supported(3));
    CHECK(published.replace_current);
    CHECK(published.slot == 2);
    CHECK(reads == 1);

    int sequence_reads = 0;
    auto sequence_reader = [&]() {
        ++sequence_reads;
        return 1;
    };
    const auto acquired = overlay_index_after_native_acquire(kWindowAcquireSuccess, true, sequence_reader, supported(3));
    CHECK(acquired.replace_current);
    CHECK(acquired.slot == 1);
    CHECK(sequence_reads == 1);
    const auto lost = overlay_index_after_native_acquire(kWindowAcquireNativeError, true, sequence_reader, supported(3));
    CHECK(lost.replace_current);
    CHECK(lost.slot == kOverlayTextureDisabled);
    CHECK(sequence_reads == 1);
    const auto null_output = overlay_index_after_native_acquire(kWindowAcquireSuccess, false, sequence_reader, supported(3));
    CHECK(null_output.replace_current);
    CHECK(null_output.slot == kOverlayTextureDisabled);
    CHECK(sequence_reads == 1);

    auto out_of_range = [&]() {
        ++reads;
        return 3;
    };
    const auto rejected = overlay_index_after_native_acquire(kWindowAcquireSuccess, true, out_of_range, supported(3));
    CHECK(rejected.replace_current);
    CHECK(rejected.slot == kOverlayTextureDisabled);
    CHECK(reads == 2);
    CHECK(!overlay_texture_index_accepted(3, supported(3)));
    CHECK(!overlay_texture_index_accepted(-1, supported(3)));
    CHECK(!overlay_texture_index_accepted(0, overlay_texture::OverlayTextureBinding{false, 0}));
    CHECK(overlay_texture_index_accepted(0, supported(1)));

    const auto oversized = plan_overlay_textures(4, true);
    CHECK(!oversized.copy_into_overlay);
    CHECK(!oversized.overlay_supported);
    CHECK(oversized.native_count == 4);
    CHECK(!binding_from_plan(oversized).supported);

    const auto huge = plan_overlay_textures(2147483647, true);
    CHECK(!huge.copy_into_overlay);
    CHECK(huge.native_count == 2147483647);

    const auto negative = plan_overlay_textures(-5, true);
    CHECK(!negative.copy_into_overlay);
    CHECK(negative.native_count == -5);

    const auto absent = plan_overlay_textures(3, false);
    CHECK(!absent.copy_into_overlay);
    CHECK(absent.native_count == 3);

    const auto fitted = plan_overlay_textures(2, true);
    CHECK(fitted.copy_into_overlay);
    CHECK(fitted.overlay_count == 2);
    CHECK(fitted.native_count == 2);
    CHECK(binding_from_plan(fitted).supported);
    CHECK(binding_from_plan(fitted).count == 2);
    const auto replaced = install_overlay_texture_table(fitted);
    CHECK(replaced.binding.supported);
    CHECK(replaced.binding.count == 2);
    CHECK(replaced.current_index == kOverlayTextureDisabled);
    const auto replaced_huge = install_overlay_texture_table(huge);
    CHECK(!replaced_huge.binding.supported);
    CHECK(replaced_huge.current_index == kOverlayTextureDisabled);

    int marker_a = 1;
    int marker_b = 2;
    int marker_c = 3;
    int* source[3] = {&marker_a, &marker_b, &marker_c};
    int* destination[3] = {source[0], source[1], source[2]};
    publish_overlay_texture_table(oversized, destination, source);
    CHECK(destination[0] == nullptr);
    CHECK(destination[1] == nullptr);
    CHECK(destination[2] == nullptr);

    publish_overlay_texture_table(huge, destination, static_cast<int* const*>(nullptr));
    CHECK(destination[0] == nullptr);

    publish_overlay_texture_table(fitted, destination, source);
    CHECK(destination[0] == &marker_a);
    CHECK(destination[1] == &marker_b);
    CHECK(destination[2] == nullptr);

    if (g_failures != 0) {
        std::fprintf(stderr, "%d overlay texture policy checks failed\n", g_failures);
        return 1;
    }
    std::printf("overlay texture policy checks passed\n");
    return 0;
}
