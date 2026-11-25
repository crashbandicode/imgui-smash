#include "font_feed.hpp"

namespace ImguiFontFeed {

void reset() {}

bool startWorker(const Platform *) { return false; }

bool ready(ImFontAtlas *, const ImguiFontSetup::Limits &) { return false; }

unsigned loadedMask() { return 0; }

bool workerDone() { return false; }

}  // namespace ImguiFontFeed
