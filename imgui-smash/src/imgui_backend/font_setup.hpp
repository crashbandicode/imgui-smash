#pragma once

#include "imgui.h"

#include <cstddef>
#include <cstdint>

// The overlay's fonts. Host-testable: no nn or nvn code here.
//
// JetBrains Mono NL (the ASCII subset) stays the first source at 18 px, so ASCII renders as before. Every valid
// system shared font is merged after it with its data left where the system mapped it. ImGui then rasterizes a
// glyph the first time a string draws it (ImGuiBackendFlags_RendererHasTextures), so only the characters actually
// shown cost atlas space. A character no source has, or one the admission limits refuse, draws the font's fallback
// glyph (U+FFFD when a source has it, else '?') for that character only.
namespace ImguiFontSetup {

struct SharedFont {
  const void *data;
  size_t size;
};

struct Limits {
  // Glyphs taken from the shared fonts at most.
  int glyphBudget;
  // Share of the largest atlas that packed glyphs may fill before a shared-font glyph is refused. Packing below it
  // always succeeds after growth, so ImGui never reaches its "Out of texture memory" assert.
  float fillLimit;
  int texMinWidth;
  int texMinHeight;
  int texMaxWidth;
  int texMaxHeight;
};

constexpr float kFontSize = 18.0f;
constexpr int kMaxSharedFonts = 8;
// 512 glyphs at 18 px (oversample 1, padding 1) fill well under half of 1024x1024. Alpha8: at most 1 MiB of ImGui
// pixels plus the same in the NVN pool.
constexpr Limits kDefaultLimits = {512, 0.5f, 512, 128, 1024, 1024};

struct Stats {
  int sharedSources;
  int rejectedSources;
  int admitted;
  int refusals;
  int fallbackChar;
};

// Adds the fonts to `atlas` (empty, not locked). Returns the main font, or nullptr when it could not be added.
ImFont *setup(ImFontAtlas *atlas, const SharedFont *shared, int count, const Limits &limits = kDefaultLimits);

// True when `data` parses as a font stb_truetype accepts: an sfnt (or the first font of a collection) whose table
// records lie inside `size`, with cmap, head, hhea and hmtx, glyf + loca or CFF, and a Unicode cmap subtable.
bool validFont(const void *data, size_t size);

// Whether one more shared-font glyph may be rasterized, given how many were and the atlas's live packed surface.
bool admitGlyph(int admitted, long long livePackedSurface, float fontSize, const Limits &limits);

Stats stats();

}  // namespace ImguiFontSetup
