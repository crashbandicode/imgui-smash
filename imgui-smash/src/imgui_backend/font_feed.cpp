#include "font_feed.hpp"

#include <atomic>

namespace ImguiFontFeed {
namespace {

constexpr unsigned kAll = (1u << kTypes) - 1u;

// Written by the worker, read by the render path. A type's address and size are stored before its mask bit.
std::atomic<unsigned> g_mask{0};
std::atomic<const void *> g_address[kTypes];
std::atomic<size_t> g_size[kTypes];
std::atomic<bool> g_done{false};
std::atomic<const Platform *> g_platform{nullptr};

// Render path only.
unsigned g_seen = 0;
bool g_drawing = false;

void worker(void *) {
  const Platform *platform = g_platform.load(std::memory_order_acquire);
  if (platform == nullptr) {
    return;
  }
  for (int type = 0; type < kTypes; ++type) {
    platform->request(type);
  }
  for (;;) {
    unsigned mask = g_mask.load(std::memory_order_relaxed);
    for (int type = 0; type < kTypes; ++type) {
      if ((mask & (1u << type)) != 0 || !platform->loaded(type)) {
        continue;
      }
      g_address[type].store(platform->address(type), std::memory_order_relaxed);
      g_size[type].store(platform->size(type), std::memory_order_relaxed);
      mask |= 1u << type;
      g_mask.store(mask, std::memory_order_release);
    }
    if (mask == kAll) {
      g_done.store(true, std::memory_order_release);
      return;
    }
    if (!platform->sleep()) {
      return;
    }
  }
}

}  // namespace

void reset() {
  g_platform.store(nullptr, std::memory_order_release);
  g_mask.store(0, std::memory_order_release);
  g_done.store(false, std::memory_order_release);
  for (int type = 0; type < kTypes; ++type) {
    g_address[type].store(nullptr, std::memory_order_relaxed);
    g_size[type].store(0, std::memory_order_relaxed);
  }
  g_seen = 0;
  g_drawing = false;
}

bool startWorker(const Platform *platform) {
  if (platform == nullptr) {
    return false;
  }
  g_platform.store(platform, std::memory_order_release);
  if (platform->start(worker, nullptr)) {
    return true;
  }
  // No worker: nothing is ever published, so ready() stays false. The render path never makes the calls itself.
  g_platform.store(nullptr, std::memory_order_release);
  return false;
}

bool ready(ImFontAtlas *atlas, const ImguiFontSetup::Limits &limits) {
  const unsigned mask = g_mask.load(std::memory_order_acquire);
  if (mask == g_seen) {
    return g_drawing;
  }
  g_seen = mask;
  ImguiFontSetup::SharedFont shared[kTypes];
  int count = 0;
  for (int type = 0; type < kTypes; ++type) {
    if ((mask & (1u << type)) == 0) {
      continue;
    }
    const void *address = g_address[type].load(std::memory_order_relaxed);
    const size_t size = g_size[type].load(std::memory_order_relaxed);
    if (address != nullptr && size != 0) {
      shared[count++] = {address, size};
    }
  }
  g_drawing = ImguiFontSetup::setup(atlas, shared, count, limits) != nullptr;
  return g_drawing;
}

unsigned loadedMask() { return g_mask.load(std::memory_order_acquire); }

bool workerDone() { return g_done.load(std::memory_order_acquire); }

}  // namespace ImguiFontFeed
