#pragma once

#include <stdint.h>

// Version 1 layout. `size` is sizeof(ImguiSmashFontStats) as written by the getter. The render thread writes these
// values; another thread may read them at any time, each field on its own.

#define IMGUI_SMASH_FONT_STATS_VERSION 1

typedef struct ImguiSmashFontStats {
  uint32_t version;
  uint32_t size;
  uint32_t sharedSources;      // system shared fonts merged
  uint32_t rejectedSources;    // shared fonts that failed validation or AddFont
  uint32_t glyphsAdmitted;     // shared-font glyphs rasterized into the atlas
  uint32_t glyphRefusals;      // shared-font glyph loads the limits refused (drawn as the fallback glyph)
  uint32_t fallbackChar;       // codepoint drawn for a missing glyph
  uint32_t atlasWidth;         // newest live texture
  uint32_t atlasHeight;
  uint32_t atlasBytesPerPixel;
  uint32_t texturesLive;
  uint32_t reserved;
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

#ifdef __cplusplus
}
#endif
