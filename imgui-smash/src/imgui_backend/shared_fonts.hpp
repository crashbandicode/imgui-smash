#pragma once

#include "font_setup.hpp"

namespace ImguiSharedFonts {

// Fills `out` with the system shared fonts that are loaded, in merge order: Standard (Latin and Japanese), Chinese
// Simplified, Extended Chinese Simplified, Chinese Traditional, Korean, Nintendo Extended. Returns how many.
// A font that is not loaded yet is left out; nothing waits.
int query(ImguiFontSetup::SharedFont *out, int capacity);

}  // namespace ImguiSharedFonts
