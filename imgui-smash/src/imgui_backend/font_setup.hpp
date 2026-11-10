#pragma once

#include "imgui.h"

#include <cstddef>
#include <cstdint>

// The overlay's fonts. Host-testable: no nn or nvn code here.
//
// JetBrains Mono NL (the ASCII subset) stays the first source at 18 px, so ASCII renders as before. Every valid
// system shared font is merged after it with its data left where the system mapped it.
//
// Two glyph sets share the one 18 px bake:
// - the static set, rasterized once in setup() and never dropped: ASCII, Latin-1 Supplement and Latin Extended-A
//   (European names), plus the fallback and ellipsis glyphs;
// - the dynamic set: any other character, rasterized by ImGui when a string first draws it (measuring text loads
//   advances only). frameBoundary() reads each frame's draw data, and once a dynamic glyph that was on screen has
//   not been drawn for graceFrames, it drops every dynamic glyph between two frames and repacks the atlas into a
//   new texture. The characters still on screen load again in the next frame, so the dynamic set holds the text on
//   screen, not what ever was. (ImGui rasterizes a whole drawn line, so characters cut off by a window edge stay
//   with their line.)
//
// A character no source has draws the fallback glyph (U+FFFD when a source has it, else '?'), that character only.
// The fill guard is the one other refusal: a frame whose own text would fill half of the largest atlas draws the
// fallback for the excess, because past that ImGui's "Out of texture memory" assert would abort the game.
namespace ImguiFontSetup {

struct SharedFont {
  const void *data;
  size_t size;
};

struct Limits {
  // Share of the largest atlas that packed glyphs may fill before a dynamic glyph is refused. Packing below it
  // always succeeds after growth, so ImGui never reaches its "Out of texture memory" assert. ImGui grows the height
  // first while the atlas is square, so the largest atlas must be square for growth to reach it.
  float fillLimit;
  int texMinWidth;
  int texMinHeight;
  int texMaxWidth;
  int texMaxHeight;
  // A dynamic glyph that was drawn has left the screen after this many frames without a draw.
  int graceFrames;
  // The draw data is read every scanInterval frames (1: every frame).
  int scanInterval;
  // A rebuild that only follows refusals (nothing left the screen) waits at least this long after the last one.
  int refusalCooldownFrames;
};

constexpr float kFontSize = 18.0f;
constexpr int kMaxSharedFonts = 8;
constexpr Limits kDefaultLimits = {0.5f, 512, 128, 1024, 1024, 60, 4, 240};
static_assert(kDefaultLimits.texMaxWidth == kDefaultLimits.texMaxHeight, "growth reaches only a square maximum");

// Static ranges: ASCII 0x20-0x7E (and the controls below it, e.g. the TAB glyph ImGui adds to every bake),
// Latin-1 Supplement 0xA0-0xFF, Latin Extended-A 0x100-0x17F.
constexpr bool isStaticCodepoint(unsigned int c) { return c <= 0x7E || (c >= 0xA0 && c <= 0x17F); }

struct Stats {
  int sharedSources;
  int rejectedSources;
  int fallbackChar;
  int staticGlyphs;        // glyphs of the static set (in the atlas since setup)
  int dynamicGlyphs;       // dynamic glyphs in the atlas now
  int dynamicVisible;      // of them, drawn within the last graceFrames (as of the last scan)
  long long dynamicLoads;  // dynamic glyphs rasterized since setup
  long long refusals;      // dynamic glyph loads the fill guard refused
  long long rebuilds;      // dynamic-set rebuilds
  long long misplaced;     // frameBoundary() calls made inside a frame (refused; always 0 in the host's frame loop)
  long long indexBytes;    // the bake's glyph table and per-codepoint index (ImGui sizes them by the largest codepoint)
};

// Adds the fonts to `atlas` (empty, not locked) and rasterizes the static set. Returns the main font, or nullptr
// when it could not be added.
ImFont *setup(ImFontAtlas *atlas, const SharedFont *shared, int count, const Limits &limits = kDefaultLimits);

// Call between two frames, before ImGui::NewFrame(), with the draw data of the frame just rendered (or null).
// Notes which dynamic glyphs that frame drew and, when one that had been on screen has left it, drops the dynamic
// set and repacks the atlas (a new texture: the old one is destroyed by the backend after the GPU is done with it).
// Returns true when it rebuilt. Inside a frame it does nothing and counts a misplaced call.
bool frameBoundary(ImFontAtlas *atlas, const ImDrawData *drawData);

// The dynamic codepoints in the atlas as of the last frameBoundary(), ascending. Writes at most `cap`, returns the
// total. Safe from any thread.
int dynamicSet(uint32_t *out, int cap);

// True when `data` parses as a font stb_truetype accepts: an sfnt (or the first font of a collection) whose table
// records lie inside `size`, with cmap, head, hhea and hmtx, glyf + loca or CFF, and a Unicode cmap subtable.
bool validFont(const void *data, size_t size);

// Whether one more dynamic glyph may be rasterized, given the atlas's live packed surface.
bool admitDynamic(long long livePackedSurface, float fontSize, const Limits &limits);

Stats stats();

}  // namespace ImguiFontSetup
