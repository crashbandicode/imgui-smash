#include "imgui_backend/command_memory_reserve.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

void expect(bool condition, const char *name) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s\n", name);
    g_failures += 1;
  }
}

void test_alignment_and_minimum() {
  const MemoryBudget small = align_budget(1, 512, 256);
  expect(small.ok && small.bytes == 512, "request below minimum becomes the device minimum");
  const MemoryBudget full = align_budget(256u * 1024u, 512, 256);
  expect(full.ok && full.bytes == 256u * 1024u, "256KiB reserve stays 256KiB");
  const MemoryBudget bad = align_budget(64, 8, 3);
  expect(!bad.ok, "non power-of-two alignment is rejected");
  const SlotReserveLayout layout =
      plan_slot_memory(1, 1, 256u * 1024u, 64u * 1024u, 512, 64, 256, 64, 4096);
  expect(layout.ok, "small initial budget plans");
  expect(layout.command.base == 512, "command base is the device minimum");
  expect(layout.command.reserve == 256u * 1024u, "command reserve stays full");
  expect(layout.control.base == 64, "control base is the device minimum");
  expect(layout.control.reserve == 64u * 1024u, "control reserve stays full");
  expect(layout.command_pool >= layout.command.base + layout.command.reserve, "pool covers base plus reserve");
  expect(layout.command_pool % 4096u == 0, "pool size is page rounded");
  expect(layout.command.base + layout.command.reserve <= layout.command_pool, "reserve fits in the pool");
}

void test_default_budgets() {
  // Largest per-frame usage seen in the Eden evidence (Deluxe + Elite menus,
  // CSS, match, results): 1,256 B command, 120 B control.
  constexpr size_t observed_command = 1256;
  constexpr size_t observed_control = 120;
  expect(kDefaultInitialCommand >= 25 * observed_command, "initial command budget keeps 25x the observed maximum");
  expect(kDefaultReserveCommand >= observed_command, "command reserve covers one observed frame");
  expect(kDefaultInitialControl >= 25 * observed_control, "initial control budget keeps 25x the observed maximum");
  expect(kDefaultReserveControl >= observed_control, "control reserve covers one observed frame");
  // NX: command alignment 4, control alignment 8, memory-pool page 64 KiB.
  const SlotReserveLayout nx = plan_slot_memory(kDefaultInitialCommand, kDefaultInitialControl, kDefaultReserveCommand,
                                                kDefaultReserveControl, 0, 0, 4, 8, 65536);
  expect(nx.ok, "default budgets plan on NX constants");
  expect(nx.command_pool == 65536, "default command pool is exactly one 64 KiB page");
  expect(nx.command.base + nx.command.reserve == nx.command_pool, "default command budgets fill the page");
  expect(nx.control.base + nx.control.reserve == 16u * 1024u, "default control block is 16 KiB");
  // Device minimums below the budgets do not change the layout.
  const SlotReserveLayout mins = plan_slot_memory(kDefaultInitialCommand, kDefaultInitialControl, kDefaultReserveCommand,
                                                  kDefaultReserveControl, 4096, 1024, 4, 8, 65536);
  expect(mins.ok && mins.command_pool == 65536 && mins.control.base + mins.control.reserve == 16u * 1024u,
         "device minimums below the budgets keep the default layout");
  // A 4 KiB page gives the same pool because the budgets are page multiples.
  const SlotReserveLayout small_page = plan_slot_memory(kDefaultInitialCommand, kDefaultInitialControl,
                                                        kDefaultReserveCommand, kDefaultReserveControl, 0, 0, 4, 8, 4096);
  expect(small_page.ok && small_page.command_pool == 65536, "default command pool on a 4 KiB page is still 64 KiB");
}

void test_second_use_and_reset() {
  MemoryRegion command{512, 4096, false};
  MemoryRegion control{64, 1024, false};
  const ReserveSupply first = select_reserve(command, control, ReserveKind::Command, 32);
  expect(first.choice == ReserveChoice::Supply && first.offset == 512 && first.bytes == 4096, "first command supply");
  expect(command.used && !control.used, "only command reserve is marked");
  const ReserveSupply second = select_reserve(command, control, ReserveKind::Command, 32);
  expect(second.choice == ReserveChoice::Abort, "second command request is rejected");
  expect(command.used, "rejected request leaves the reserve used");
  const ReserveSupply control_supply = select_reserve(command, control, ReserveKind::Control, 1024);
  expect(control_supply.choice == ReserveChoice::Supply && control_supply.offset == 64 && control_supply.bytes == 1024,
         "unused control reserve can still be supplied");
  MemoryRegion oversized{512, 4096, false};
  MemoryRegion unused_control{64, 1024, false};
  const ReserveSupply huge = select_reserve(oversized, unused_control, ReserveKind::Command, 8192);
  expect(huge.choice == ReserveChoice::Abort && !oversized.used, "minSize above the reserve is rejected");
  const ReserveSupply unknown = select_reserve(command, control, ReserveKind::Unrecognized, 1);
  expect(unknown.choice == ReserveChoice::Abort && command.used && control.used, "unrecognized event is rejected");
  command = {};
  control = {};
  command.base = 512;
  command.reserve = 4096;
  control.base = 64;
  control.reserve = 1024;
  expect(!command.used && !control.used, "retirement clears both reserves");
  const ReserveSupply again = select_reserve(command, control, ReserveKind::Command, 32);
  expect(again.choice == ReserveChoice::Supply, "reset reserve can be supplied again");
}

struct MockAdd {
  bool command = false;
  ptrdiff_t command_offset = -1;
  size_t command_bytes = 0;
  uintptr_t control_address = 0;
  size_t control_bytes = 0;
};

void apply_mock(const ReserveSupply &supply, ReserveKind kind, unsigned char *control, MockAdd &add) {
  if (supply.choice != ReserveChoice::Supply) {
    return;
  }
  if (kind == ReserveKind::Command) {
    add.command = true;
    add.command_offset = static_cast<ptrdiff_t>(supply.offset);
    add.command_bytes = supply.bytes;
    return;
  }
  add.control_address = reinterpret_cast<uintptr_t>(control + supply.offset);
  add.control_bytes = supply.bytes;
}

void test_mock_callback_regions() {
  alignas(64) unsigned char control[64 + 1024];
  std::memset(control, 0, sizeof(control));
  MemoryRegion command_region{512, 4096, false};
  MemoryRegion control_region{64, 1024, false};
  const size_t min_command = 100;
  const ReserveSupply command =
      select_reserve(command_region, control_region, reserve_kind_from_event(0), min_command);
  MockAdd add;
  apply_mock(command, ReserveKind::Command, control, add);
  expect(add.command, "mock records a command add");
  expect(add.command_offset == 512, "command reserve starts after the base");
  expect(add.command_bytes >= min_command, "command reserve is at least minSize");
  expect(static_cast<size_t>(add.command_offset) + add.command_bytes > 512, "command reserve is past the base");
  const size_t min_control = 40;
  const ReserveSupply control_supply =
      select_reserve(command_region, control_region, reserve_kind_from_event(1), min_control);
  apply_mock(control_supply, ReserveKind::Control, control, add);
  const uintptr_t base = reinterpret_cast<uintptr_t>(control);
  expect(add.control_address == base + 64, "control reserve does not overlap the base");
  expect(add.control_bytes >= min_control, "control reserve is at least minSize");
  expect(reserve_kind_from_event(2) == ReserveKind::Unrecognized, "event 2 is unrecognized");
  expect(reserve_kind_from_event(0) == ReserveKind::Command, "event 0 is command memory");
  expect(reserve_kind_from_event(1) == ReserveKind::Control, "event 1 is control memory");
}

}  // namespace

int main() {
  test_alignment_and_minimum();
  test_default_budgets();
  test_second_use_and_reset();
  test_mock_callback_regions();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("command memory reserve tests passed\n");
  return 0;
}
