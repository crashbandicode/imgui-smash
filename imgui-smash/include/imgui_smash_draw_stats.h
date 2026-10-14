#pragma once

#include <stdint.h>

// Version 1 layout. `size` is sizeof(ImguiSmashDrawStats) as written by the getter.
// commandOverflowSkips is the fixed-capacity callback counter. The local NVN
// header does not define a safe way to continue a recording after the callback
// returns without adding memory, so that slot is quarantined rather than reused.

#define IMGUI_SMASH_DRAW_STATS_VERSION 1

typedef struct ImguiSmashDrawStats {
  uint32_t version;
  uint32_t size;
  uint64_t submitted;
  uint64_t reused;
  uint64_t pendingSkips;
  uint64_t failed;
  uint64_t maxCommandBytes;
  uint64_t maxControlBytes;
  uint64_t geometrySkips;
  uint64_t commandOverflowSkips;
} ImguiSmashDrawStats;

#ifdef __cplusplus
extern "C" {
#endif

int imgui_smash_draw_stats(ImguiSmashDrawStats *out);

#ifdef __cplusplus
}
#endif
