#pragma once

#include <stdint.h>

// Version 3 layout. `size` is sizeof(ImguiSmashFontStats) as written by the getter. The render thread writes these
// values; another thread may read them at any time, each field on its own.

#define IMGUI_SMASH_FONT_STATS_VERSION 3

typedef struct ImguiSmashFontStats {
  uint32_t version;
  uint32_t size;
  uint32_t sharedSources;      // system shared fonts merged
  uint32_t rejectedSources;    // shared fonts that failed validation or AddFont
  uint32_t fallbackChar;       // codepoint drawn for a missing glyph
  uint32_t staticGlyphs;       // ASCII, Latin-1 Supplement, Latin Extended-A (+ fallback, ellipsis), built once
  uint32_t dynamicGlyphs;      // other glyphs in the atlas now: the characters of the text on screen
  uint32_t dynamicVisible;     // of them, drawn within the last second
  uint32_t atlasWidth;         // newest live texture
  uint32_t atlasHeight;
  uint32_t atlasBytesPerPixel;
  uint32_t texturesLive;
  uint32_t indexPaged;         // 1 when the bake's per-codepoint index is paged (imgui_smash_font_use_paged_index)
  uint32_t indexPages;         // its 256-codepoint pages (0 when dense)
  uint64_t dynamicLoads;       // dynamic glyphs rasterized since boot
  uint64_t refusals;           // dynamic glyph loads the fill guard refused (drawn as the fallback glyph)
  uint64_t rebuilds;           // dynamic-set rebuilds (text left the screen)
  uint64_t misplaced;          // rebuild requests made inside a frame, refused (expected 0)
  uint64_t indexBytes;         // ImGui's glyph table and per-codepoint index for the bake
  uint64_t pixelBytes;         // ImGui's CPU pixel copies of the live textures
  uint64_t poolBytes;          // NVN memory pools of the live textures
  uint64_t creates;
  uint64_t updates;            // update rectangles uploaded
  uint64_t destroys;
} ImguiSmashFontStats;

#ifdef __cplusplus
extern "C" {
#endif

int imgui_smash_font_stats(ImguiSmashFontStats *out);

// The dynamic codepoints in the atlas, ascending. Writes at most `cap`, returns the total (-1 while it changes).
int imgui_smash_font_dynamic_set(uint32_t *out, uint32_t cap);

// Opt-in, before imgui_smash_init: the overlay's font bakes keep ImGui's per-codepoint index in 256-codepoint pages
// where glyphs exist, instead of one array up to the highest codepoint loaded. Off by default: modules built with an
// unpatched Dear ImGui read the index directly, so a context they may draw on (a standalone host's) stays dense.
// Returns 1.
int imgui_smash_font_use_paged_index(int enabled);

#ifdef __cplusplus
}
#endif
