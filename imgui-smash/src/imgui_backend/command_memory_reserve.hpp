#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

// Shared by DrawSlots and the host tests. Event codes 0 and 1 are
// NVNcommandBufferMemoryEvent OUT_OF_COMMAND_MEMORY and OUT_OF_CONTROL_MEMORY.

// Per-slot budgets. A frame records one setup group plus a few commands per
// ImDrawCmd; the largest recording seen across the Eden evidence (Deluxe and
// Elite menus open together through menus, CSS, a match and results) used
// 1,256 bytes of command memory and 120 bytes of control memory. The initial
// budgets below keep more than 25x that, and the reserve of the same size
// finishes and discards one oversized frame instead of aborting. The command
// pool is rounded up to the device's memory-pool page (64 KiB on NX), so
// 32 KiB + 32 KiB fills exactly one page; the former 256 KiB + 256 KiB pool
// and 64 KiB + 64 KiB control block cost 5 MiB of game heap over eight slots.
#ifndef IMGUI_SMASH_INITIAL_COMMAND_BUDGET
#define IMGUI_SMASH_INITIAL_COMMAND_BUDGET (32u * 1024u)
#endif
#ifndef IMGUI_SMASH_RESERVE_COMMAND_BUDGET
#define IMGUI_SMASH_RESERVE_COMMAND_BUDGET (32u * 1024u)
#endif
#ifndef IMGUI_SMASH_INITIAL_CONTROL_BUDGET
#define IMGUI_SMASH_INITIAL_CONTROL_BUDGET (8u * 1024u)
#endif
#ifndef IMGUI_SMASH_RESERVE_CONTROL_BUDGET
#define IMGUI_SMASH_RESERVE_CONTROL_BUDGET (8u * 1024u)
#endif

constexpr size_t kDefaultInitialCommand = IMGUI_SMASH_INITIAL_COMMAND_BUDGET;
constexpr size_t kDefaultReserveCommand = IMGUI_SMASH_RESERVE_COMMAND_BUDGET;
constexpr size_t kDefaultInitialControl = IMGUI_SMASH_INITIAL_CONTROL_BUDGET;
constexpr size_t kDefaultReserveControl = IMGUI_SMASH_RESERVE_CONTROL_BUDGET;

struct MemoryBudget {
  size_t bytes = 0;
  bool ok = false;
};

inline int device_alignment(int queried, int fallback) {
  if (queried > 0 && (queried & (queried - 1)) == 0) {
    return queried;
  }
  if (fallback > 0 && (fallback & (fallback - 1)) == 0) {
    return fallback;
  }
  return 0;
}

inline MemoryBudget align_budget(size_t requested, int minimum, int alignment) {
  MemoryBudget out;
  if (alignment <= 0 || (alignment & (alignment - 1)) != 0) {
    return out;
  }
  size_t need = requested;
  if (minimum > 0) {
    const size_t min_bytes = static_cast<size_t>(minimum);
    if (min_bytes > need) {
      need = min_bytes;
    }
  }
  const size_t align = static_cast<size_t>(alignment);
  if (need > std::numeric_limits<size_t>::max() - (align - 1u)) {
    return out;
  }
  out.bytes = (need + align - 1u) & ~(align - 1u);
  out.ok = true;
  return out;
}

struct MemoryRegion {
  size_t base = 0;
  size_t reserve = 0;
  bool used = false;
};

struct SlotReserveLayout {
  MemoryRegion command;
  MemoryRegion control;
  size_t command_pool = 0;
  bool ok = false;
};

inline SlotReserveLayout plan_slot_memory(size_t initial_command, size_t initial_control,
                                         size_t reserve_command, size_t reserve_control, int min_command,
                                         int min_control, int command_align, int control_align, int pool_page) {
  SlotReserveLayout layout;
  const MemoryBudget command_base = align_budget(initial_command, min_command, command_align);
  const MemoryBudget command_reserve = align_budget(reserve_command, min_command, command_align);
  const MemoryBudget control_base = align_budget(initial_control, min_control, control_align);
  const MemoryBudget control_reserve = align_budget(reserve_control, min_control, control_align);
  if (!command_base.ok || !command_reserve.ok || !control_base.ok || !control_reserve.ok) {
    return layout;
  }
  if (command_base.bytes > std::numeric_limits<size_t>::max() - command_reserve.bytes ||
      control_base.bytes > std::numeric_limits<size_t>::max() - control_reserve.bytes) {
    return layout;
  }
  const size_t command_bytes = command_base.bytes + command_reserve.bytes;
  const MemoryBudget pool = align_budget(command_bytes, 0, pool_page);
  if (!pool.ok || pool.bytes < command_bytes) {
    return layout;
  }
  layout.command = {command_base.bytes, command_reserve.bytes, false};
  layout.control = {control_base.bytes, control_reserve.bytes, false};
  layout.command_pool = pool.bytes;
  layout.ok = true;
  return layout;
}

enum class ReserveKind { Command, Control, Unrecognized };

inline ReserveKind reserve_kind_from_event(int event) {
  if (event == 0) {
    return ReserveKind::Command;
  }
  if (event == 1) {
    return ReserveKind::Control;
  }
  return ReserveKind::Unrecognized;
}

enum class ReserveChoice { Supply, Abort };

struct ReserveSupply {
  ReserveChoice choice = ReserveChoice::Abort;
  size_t offset = 0;
  size_t bytes = 0;
};

inline ReserveSupply select_reserve(MemoryRegion &command, MemoryRegion &control, ReserveKind kind, size_t min_size) {
  ReserveSupply supply;
  if (kind == ReserveKind::Unrecognized) {
    return supply;
  }
  MemoryRegion &region = kind == ReserveKind::Command ? command : control;
  if (region.used || min_size > region.reserve) {
    return supply;
  }
  region.used = true;
  supply.choice = ReserveChoice::Supply;
  supply.offset = region.base;
  supply.bytes = region.reserve;
  return supply;
}
