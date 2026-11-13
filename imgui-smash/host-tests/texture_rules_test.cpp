#include "imgui_backend/texture_rules.hpp"

#include <cstdio>

namespace {

int g_failures = 0;

void expect(bool condition, const char *name) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s\n", name);
    g_failures += 1;
  }
}

void test_descriptor_ids_start_at_the_legacy_font_slot_and_are_reused() {
  TextureDescriptorIds ids;
  expect(ids.acquire() == 257, "first id is the legacy font texture's 257");
  expect(ids.acquire() == 258, "second id follows");
  ids.release(257);
  expect(ids.acquire() == 257, "a released id is handed out again");
  ids.release(10);
  ids.release(400);
  int last = 0;
  int handed = 2;
  for (int id = ids.acquire(); id >= 0; id = ids.acquire()) {
    last = id;
    handed += 1;
  }
  expect(handed == TextureDescriptorIds::kCount, "every id up to the pool's end is handed out once");
  expect(last == 355, "the last id is the pool's last entry");
  expect(ids.acquire() == -1, "an exhausted pool refuses");
}

void test_pool_bytes_round_to_the_page() {
  expect(texturePoolBytes(512 * 128, 512, 4096) == 65536, "Alpha8 512x128 is 64 KiB");
  expect(texturePoolBytes(512 * 128 * 4, 512, 4096) == 262144, "RGBA8 512x128 is the legacy 256 KiB");
  expect(texturePoolBytes(5000, 512, 4096) == 8192, "a partial page rounds up");
  expect(texturePoolBytes(0, 512, 4096) == 4096, "no storage still takes one page");
  expect(texturePoolBytes(4096, 8192, 4096) == 0, "an alignment above the page is refused");
  expect(texturePoolBytes(4096, 512, 3000) == 0, "a page that is not a power of two is refused");
}

void test_a_retired_texture_waits_for_earlier_submissions_only() {
  SlotRecord slots[3] = {{5, true}, {6, false}, {0, false}};
  expect(!retireReady(5, slots, 3), "submission 5 still pending blocks a texture retired after it");
  expect(retireReady(4, slots, 3), "submissions recorded after the retirement do not block");
  slots[0].pending = false;
  expect(retireReady(6, slots, 3), "all earlier submissions signalled");
  SlotRecord quarantined[1] = {{3, true}};
  expect(!retireReady(9, quarantined, 1), "a quarantined slot that may hold the texture keeps it alive");
  SlotRecord free[1] = {{0, true}};
  expect(retireReady(9, free, 1), "a slot without a recording does not block");
}

}  // namespace

int main() {
  test_descriptor_ids_start_at_the_legacy_font_slot_and_are_reused();
  test_pool_bytes_round_to_the_page();
  test_a_retired_texture_waits_for_earlier_submissions_only();
  if (g_failures == 0) {
    std::printf("texture_rules_test: ok\n");
  }
  return g_failures == 0 ? 0 : 1;
}
