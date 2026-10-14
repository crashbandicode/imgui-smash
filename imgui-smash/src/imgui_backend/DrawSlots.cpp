#include "DrawSlots.h"

#include "MemoryBuffer.h"
#include "command_memory_reserve.hpp"
#include "draw_slot_rules.hpp"
#include "helpers/assert.hpp"
#include "helpers/memoryHelper.h"
#include "imgui.h"

#include <atomic>
#include <cstring>
#include <cstddef>
#include <cstdint>

namespace {

#ifndef IMGUI_SMASH_INITIAL_COMMAND_BUDGET
#define IMGUI_SMASH_INITIAL_COMMAND_BUDGET (256u * 1024u)
#endif
#ifndef IMGUI_SMASH_INITIAL_CONTROL_BUDGET
#define IMGUI_SMASH_INITIAL_CONTROL_BUDGET (64u * 1024u)
#endif

constexpr size_t kInitialCommand = IMGUI_SMASH_INITIAL_COMMAND_BUDGET;
constexpr size_t kInitialControl = IMGUI_SMASH_INITIAL_CONTROL_BUDGET;
constexpr size_t kReserveCommand = 256u * 1024u;
constexpr size_t kReserveControl = 64u * 1024u;
constexpr size_t kGeometryInitial = 64u * 1024u;
constexpr size_t kGeometryCap = 1024u * 1024u;
constexpr int kPageFallback = 0x1000;
constexpr int kUniformBytes = 0x1000;

struct alignas(8) Slot {
  nvn::CommandBuffer command;
  nvn::MemoryPool pool;
  nvn::Sync sync;
  void *commandStorage = nullptr;
  void *controlStorage = nullptr;
  MemoryRegion commandRegion;
  MemoryRegion controlRegion;
  MemoryBuffer *vertex = nullptr;
  MemoryBuffer *indexBuffer = nullptr;
  MemoryBuffer *uniform = nullptr;
  nvn::CommandHandle handle = 0;
  bool commandLive = false;
  bool poolLive = false;
  bool syncLive = false;
  bool overflow = false;
};

static_assert(alignof(Slot) >= 8, "Slot must be at least 8-byte aligned");

Slot g_slots[DrawSlotRules::kCount];
DrawSlotRules g_rules;
bool g_attempted = false;
bool g_closed = false;
int g_active = -1;

std::atomic<uint64_t> g_submitted{0};
std::atomic<uint64_t> g_reused{0};
std::atomic<uint64_t> g_pendingSkips{0};
std::atomic<uint64_t> g_geometrySkips{0};
std::atomic<uint64_t> g_commandOverflowSkips{0};
std::atomic<uint64_t> g_maxCommandBytes{0};
std::atomic<uint64_t> g_maxControlBytes{0};

void raiseMax(std::atomic<uint64_t> &slot, size_t used) {
  uint64_t seen = slot.load(std::memory_order_relaxed);
  const uint64_t value = static_cast<uint64_t>(used);
  while (value > seen && !slot.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {
    continue;
  }
}

void memoryCallback(nvn::CommandBuffer *buffer, nvn::CommandBufferMemoryEvent::Enum event, size_t minSize,
                    void *data) {
  auto *slot = static_cast<Slot *>(data);
  if (slot == nullptr || buffer == nullptr || slot->controlStorage == nullptr) {
    SMASH_ABORT("command memory callback has no slot");
    __builtin_trap();
  }
  const ReserveKind kind = reserve_kind_from_event(static_cast<int>(event));
  const ReserveSupply supply = select_reserve(slot->commandRegion, slot->controlRegion, kind, minSize);
  if (supply.choice != ReserveChoice::Supply) {
    SMASH_ABORT("command memory callback cannot satisfy request event=%d min=%zu", static_cast<int>(event),
                minSize);
    __builtin_trap();
  }
  slot->overflow = true;
  if (kind == ReserveKind::Command) {
    buffer->AddCommandMemory(&slot->pool, static_cast<ptrdiff_t>(supply.offset), supply.bytes);
    return;
  }
  auto *base = static_cast<unsigned char *>(slot->controlStorage);
  buffer->AddControlMemory(base + supply.offset, supply.bytes);
}

void attachMemory(Slot &slot) {
  slot.command.SetMemoryCallback(&memoryCallback);
  slot.command.SetMemoryCallbackData(&slot);
  slot.command.AddCommandMemory(&slot.pool, 0, slot.commandRegion.base);
  slot.command.AddControlMemory(slot.controlStorage, slot.controlRegion.base);
  slot.overflow = false;
  slot.commandRegion.used = false;
  slot.controlRegion.used = false;
}

bool reinitCommand(Slot &slot, nvn::Device *device) {
  if (slot.commandLive) {
    slot.command.Finalize();
    slot.commandLive = false;
  }
  if (device == nullptr || !slot.command.Initialize(device)) {
    return false;
  }
  slot.commandLive = true;
  attachMemory(slot);
  return true;
}

void retireUnsubmitted(Slot &owned, int index, nvn::Device *device, nvn::CommandHandle extra) {
  if (extra != 0 && device != nullptr) {
    device->FinalizeCommandHandle(extra);
  }
  owned.handle = 0;
  if (!reinitCommand(owned, device)) {
    g_rules.fail(index);
  } else {
    g_rules.cancelUnsubmitted(index);
  }
}

bool reinitSync(Slot &slot, nvn::Device *device) {
  if (slot.syncLive) {
    slot.sync.Finalize();
    slot.syncLive = false;
  }
  if (device == nullptr || !slot.sync.Initialize(device)) {
    return false;
  }
  slot.syncLive = true;
  return true;
}

MemoryBuffer *createMemoryBuffer(size_t bytes, nvn::MemoryPoolFlags flags) {
  void *storage = ImGui::MemAlloc(sizeof(MemoryBuffer));
  if (storage == nullptr) {
    return nullptr;
  }
  return new (ImNewWrapper(), storage) MemoryBuffer(bytes, flags);
}

void releaseBuffer(MemoryBuffer *&buffer) {
  if (buffer == nullptr) {
    return;
  }
  buffer->Finalize();
  IM_FREE(buffer);
  buffer = nullptr;
}

bool ensureGeometry(MemoryBuffer *&buffer, size_t bytes) {
  if (bytes > kGeometryCap) {
    return false;
  }
  if (buffer != nullptr && buffer->IsBufferReady() && buffer->GetPoolSize() >= bytes) {
    return true;
  }
  releaseBuffer(buffer);
  size_t alloc = bytes < kGeometryInitial ? kGeometryInitial : bytes;
  if (alloc > kGeometryCap) {
    return false;
  }
  buffer = createMemoryBuffer(alloc, nvn::MemoryPoolFlags::CPU_UNCACHED | nvn::MemoryPoolFlags::GPU_CACHED);
  return buffer != nullptr && buffer->IsBufferReady();
}

bool initSlot(Slot &slot, nvn::Device *device, int commandAlign, int controlAlign, int minCommand, int minControl,
              int poolPage) {
  const SlotReserveLayout layout =
      plan_slot_memory(kInitialCommand, kInitialControl, kReserveCommand, kReserveControl, minCommand, minControl,
                       commandAlign, controlAlign, poolPage);
  if (!layout.ok) {
    return false;
  }
  slot.commandRegion = layout.command;
  slot.controlRegion = layout.control;
  const size_t commandPoolBytes = layout.command_pool;
  const size_t controlBytes = layout.control.base + layout.control.reserve;
  const int poolAlign = device_alignment(poolPage, kPageFallback);
  if (poolAlign <= 0) {
    return false;
  }
  slot.commandStorage = Mem::AllocateAlign(static_cast<size_t>(poolAlign), commandPoolBytes);
  slot.controlStorage = Mem::AllocateAlign(static_cast<size_t>(controlAlign), controlBytes);
  if (slot.commandStorage == nullptr || slot.controlStorage == nullptr) {
    return false;
  }
  std::memset(slot.commandStorage, 0, commandPoolBytes);
  std::memset(slot.controlStorage, 0, controlBytes);
  nvn::MemoryPoolBuilder builder;
  builder.SetDefaults()
      .SetDevice(device)
      .SetFlags(nvn::MemoryPoolFlags::CPU_UNCACHED | nvn::MemoryPoolFlags::GPU_UNCACHED)
      .SetStorage(slot.commandStorage, commandPoolBytes);
  if (!slot.pool.Initialize(&builder)) {
    return false;
  }
  slot.poolLive = true;
  if (!reinitCommand(slot, device) || !reinitSync(slot, device)) {
    return false;
  }
  slot.uniform = createMemoryBuffer(kUniformBytes, nvn::MemoryPoolFlags::CPU_UNCACHED | nvn::MemoryPoolFlags::GPU_CACHED);
  return slot.uniform != nullptr && slot.uniform->IsBufferReady();
}

void releaseIdleSlot(Slot &slot, nvn::Device *device) {
  if (slot.handle != 0 && device != nullptr) {
    device->FinalizeCommandHandle(slot.handle);
    slot.handle = 0;
  }
  releaseBuffer(slot.vertex);
  releaseBuffer(slot.indexBuffer);
  releaseBuffer(slot.uniform);
  if (slot.commandLive) {
    slot.command.Finalize();
    slot.commandLive = false;
  }
  if (slot.poolLive) {
    slot.pool.Finalize();
    slot.poolLive = false;
  }
  if (slot.syncLive) {
    slot.sync.Finalize();
    slot.syncLive = false;
  }
  if (slot.commandStorage != nullptr) {
    Mem::Deallocate(slot.commandStorage);
    slot.commandStorage = nullptr;
  }
  if (slot.controlStorage != nullptr) {
    Mem::Deallocate(slot.controlStorage);
    slot.controlStorage = nullptr;
  }
}

}  // namespace

namespace ImguiDrawSlots {

void ensure(nvn::Device *device) {
  if (g_attempted || g_closed) {
    return;
  }
  g_attempted = true;
  if (device == nullptr) {
    for (int i = 0; i < DrawSlotRules::kCount; ++i) {
      g_rules.fail(i);
    }
    return;
  }
  int commandQuery = 0;
  int controlQuery = 0;
  int minCommand = 0;
  int minControl = 0;
  device->GetInteger(nvn::DeviceInfo::COMMAND_BUFFER_COMMAND_ALIGNMENT, &commandQuery);
  device->GetInteger(nvn::DeviceInfo::COMMAND_BUFFER_CONTROL_ALIGNMENT, &controlQuery);
  device->GetInteger(nvn::DeviceInfo::COMMAND_BUFFER_MIN_COMMAND_SIZE, &minCommand);
  device->GetInteger(nvn::DeviceInfo::COMMAND_BUFFER_MIN_CONTROL_SIZE, &minControl);
  int pageQuery = 0;
  device->GetInteger(nvn::DeviceInfo::MEMORY_POOL_PAGE_SIZE, &pageQuery);
  const int poolPage = device_alignment(pageQuery, kPageFallback);
  const int commandAlign = device_alignment(commandQuery, poolPage);
  const int controlAlign = device_alignment(controlQuery, poolPage);
  if (poolPage <= 0 || commandAlign <= 0 || controlAlign <= 0) {
    for (int i = 0; i < DrawSlotRules::kCount; ++i) {
      g_rules.fail(i);
    }
    return;
  }
  for (int i = 0; i < DrawSlotRules::kCount; ++i) {
    if (!initSlot(g_slots[i], device, commandAlign, controlAlign, minCommand, minControl, poolPage)) {
      releaseIdleSlot(g_slots[i], device);
      g_rules.fail(i);
    }
  }
}

int begin(nvn::Device *device, size_t vertexBytes, size_t indexBytes) {
  ensure(device);
  if (g_closed) {
    g_pendingSkips.fetch_add(1, std::memory_order_relaxed);
    return -1;
  }
  int waits[DrawSlotRules::kCount];
  for (int i = 0; i < DrawSlotRules::kCount; ++i) {
    waits[i] = -1;
    if (g_rules.phase[i] == DrawSlotPhase::Submitted && g_slots[i].syncLive) {
      const nvn::SyncWaitResult result = g_slots[i].sync.Wait(0);
      waits[i] = static_cast<int>(static_cast<nvn::SyncWaitResult::Enum>(result));
    }
  }
  const int index = g_rules.acquire(waits);
  if (index < 0) {
    g_pendingSkips.fetch_add(1, std::memory_order_relaxed);
    return -1;
  }
  Slot &slot = g_slots[index];
  if (g_rules.phase[index] == DrawSlotPhase::Submitted) {
    if (slot.handle != 0 && device != nullptr) {
      device->FinalizeCommandHandle(slot.handle);
      slot.handle = 0;
    }
    if (!reinitCommand(slot, device) || !reinitSync(slot, device)) {
      g_rules.fail(index);
      return -1;
    }
    g_reused.fetch_add(1, std::memory_order_relaxed);
  }
  if (g_rules.phase[index] == DrawSlotPhase::Failed || !slot.commandLive || slot.uniform == nullptr) {
    g_rules.fail(index);
    return -1;
  }
  g_rules.markRecorded(index);
  if (!ensureGeometry(slot.vertex, vertexBytes) || !ensureGeometry(slot.indexBuffer, indexBytes)) {
    g_rules.cancelUnsubmitted(index);
    g_geometrySkips.fetch_add(1, std::memory_order_relaxed);
    return -1;
  }
  slot.overflow = false;
  return index;
}

bool overflowed(int slot) { return g_slots[slot].overflow; }

nvn::CommandBuffer *commandBuffer(int slot) { return &g_slots[slot].command; }

MemoryBuffer *vertex(int slot) { return g_slots[slot].vertex; }

MemoryBuffer *index(int slot) { return g_slots[slot].indexBuffer; }

MemoryBuffer *uniform(int slot) { return g_slots[slot].uniform; }

void complete(int slot, nvn::Device *device, nvn::CommandHandle handle) {
  g_active = -1;
  Slot &owned = g_slots[slot];
  if (owned.commandLive) {
    raiseMax(g_maxCommandBytes, owned.command.GetCommandMemoryUsed());
    raiseMax(g_maxControlBytes, owned.command.GetControlMemoryUsed());
  }
  const bool overflow = owned.overflow;
  if (overflow || handle == 0) {
    retireUnsubmitted(owned, slot, device, handle);
    if (overflow) {
      g_commandOverflowSkips.fetch_add(1, std::memory_order_relaxed);
    }
    return;
  }
  owned.handle = handle;
  g_rules.markRecorded(slot);
  g_active = slot;
}

void discardUnsubmitted(int slot, nvn::Device *device) {
  if (g_active == slot) {
    g_active = -1;
  }
  if (g_rules.phase[slot] != DrawSlotPhase::Recorded) {
    return;
  }
  retireUnsubmitted(g_slots[slot], slot, device, g_slots[slot].handle);
}

void fenceAfterSubmit(int slot, nvn::Queue *queue) {
  if (g_active == slot) {
    g_active = -1;
  }
  Slot &owned = g_slots[slot];
  if (g_rules.phase[slot] != DrawSlotPhase::Recorded || owned.handle == 0) {
    return;
  }
  if (queue == nullptr || !owned.syncLive) {
    g_rules.fail(slot);
    return;
  }
  queue->FenceSync(&owned.sync, nvn::SyncCondition::ALL_GPU_COMMANDS_COMPLETE, nvn::SyncFlagBits(0));
  g_rules.markSubmitted(slot);
  g_submitted.fetch_add(1, std::memory_order_relaxed);
}

int active() { return g_active; }

void shutdown(nvn::Device *device) {
  if (g_closed) {
    return;
  }
  g_closed = true;
  g_active = -1;
  for (int i = 0; i < DrawSlotRules::kCount; ++i) {
    if (g_rules.phase[i] == DrawSlotPhase::Submitted || g_rules.phase[i] == DrawSlotPhase::Failed) {
      continue;
    }
    releaseIdleSlot(g_slots[i], device);
    if (g_rules.phase[i] == DrawSlotPhase::Recorded) {
      g_rules.cancelUnsubmitted(i);
    }
  }
}

void copyStats(ImguiSmashDrawStats *out) {
  if (out == nullptr) {
    return;
  }
  out->version = IMGUI_SMASH_DRAW_STATS_VERSION;
  out->size = static_cast<uint32_t>(sizeof(ImguiSmashDrawStats));
  out->submitted = g_submitted.load(std::memory_order_relaxed);
  out->reused = g_reused.load(std::memory_order_relaxed);
  out->pendingSkips = g_pendingSkips.load(std::memory_order_relaxed);
  out->failed = g_rules.quarantined.load(std::memory_order_relaxed);
  out->maxCommandBytes = g_maxCommandBytes.load(std::memory_order_relaxed);
  out->maxControlBytes = g_maxControlBytes.load(std::memory_order_relaxed);
  out->geometrySkips = g_geometrySkips.load(std::memory_order_relaxed);
  out->commandOverflowSkips = g_commandOverflowSkips.load(std::memory_order_relaxed);
}

}  // namespace ImguiDrawSlots

extern "C" int imgui_smash_draw_stats(ImguiSmashDrawStats *out) {
  if (out == nullptr) {
    return 0;
  }
  ImguiDrawSlots::copyStats(out);
  return 1;
}
