#pragma once

#include "font_setup.hpp"

namespace ImguiSharedFonts {

// Fills `out` with the system shared fonts that are loaded, in merge order: Standard (Latin and Japanese), Chinese
// Simplified, Extended Chinese Simplified, Chinese Traditional, Korean, Nintendo Extended.
// A font that is not loaded yet is left out; nothing waits.
struct Query {
  int count;
  unsigned loaded;
  bool complete;
};
Query query(ImguiFontSetup::SharedFont *out, int capacity, bool request, bool log);

}  // namespace ImguiSharedFonts
