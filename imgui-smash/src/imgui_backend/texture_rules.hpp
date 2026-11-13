#pragma once

#include <cstddef>
#include <cstdint>

// Host-testable rules for the backend's dynamic ImGui textures (ImGuiBackendFlags_RendererHasTextures).

// Texture descriptors in the backend's own texture pool. NVN reserves the entries below 256; the legacy font
// texture used 257, which stays the first.
struct TextureDescriptorIds {
  static constexpr int kFirst = 257;
  static constexpr int kCount = 99;  // 257..355, the pool holds 356 entries
  bool used[kCount]{};

  int acquire() {
    for (int i = 0; i < kCount; ++i) {
      if (!used[i]) {
        used[i] = true;
        return kFirst + i;
      }
    }
    return -1;
  }

  void release(int id) {
    if (id >= kFirst && id < kFirst + kCount) {
      used[id - kFirst] = false;
    }
  }
};

// Memory-pool bytes for a texture's storage: the storage size rounded up to the pool page, never below one page.
// `alignment` is the texture's storage alignment; the pool starts page-aligned, so it must divide the page.
inline size_t texturePoolBytes(size_t storageBytes, size_t alignment, size_t page) {
  if (page == 0 || (page & (page - 1)) != 0 || alignment == 0 || page % alignment != 0) {
    return 0;
  }
  const size_t bytes = storageBytes == 0 ? page : storageBytes;
  return (bytes + page - 1) & ~(page - 1);
}

// What the draw slots know about one slot when a retired texture is considered for destruction.
struct SlotRecord {
  uint64_t serial;  // 0: never recorded since it was last freed
  bool pending;     // recorded or submitted and its fence has not signalled, or quarantined
};

// A texture ImGui retired after the submission with serial `retireAfter` may be destroyed once no slot whose
// serial is at most that is still pending. Later submissions never reference it.
inline bool retireReady(uint64_t retireAfter, const SlotRecord *slots, int count) {
  for (int i = 0; i < count; ++i) {
    if (slots[i].serial != 0 && slots[i].serial <= retireAfter && slots[i].pending) {
      return false;
    }
  }
  return true;
}
