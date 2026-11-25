#pragma once

#include "font_setup.hpp"

#include <cstddef>

// Where the overlay's shared fonts come from. Host-testable: the platform's calls are behind `Platform`.
//
// Every nn::pl font call (request, load state, address, size) is an IPC to the pl:u service. One worker thread makes
// all of them and publishes what it found through atomics; it ends when every font type is loaded. The render path
// makes no platform call: between two frames it reads the published set and hands new faces to ImguiFontSetup.
namespace ImguiFontFeed {

// Font types in query order: Standard, Chinese Simplified, Extended Chinese Simplified, Chinese Traditional, Korean,
// Nintendo Extended.
constexpr int kTypes = 6;

struct Platform {
  void (*request)(int type);            // ask the system to load a font type
  bool (*loaded)(int type);             // whether it is loaded now
  const void *(*address)(int type);     // of a loaded type
  size_t (*size)(int type);             // of a loaded type
  bool (*sleep)();                      // between two passes of the worker; false stops the worker
  bool (*start)(void (*entry)(void *), void *arg);  // start a thread running entry(arg); false if it cannot
};

// Forgets everything published. Before startWorker(), and between host tests.
void reset();

// Starts the worker on `platform`, which must outlive it. False when no thread could be started: nothing is ever
// published then, and ready() stays false.
bool startWorker(const Platform *platform);

// Render path, between two frames with the atlas unlocked. Hands faces published since the last call to
// ImguiFontSetup::setup(). True when a frame may be drawn: a face that can show ordinary text is in the font.
bool ready(ImFontAtlas *atlas, const ImguiFontSetup::Limits &limits = ImguiFontSetup::kDefaultLimits);

// Bit `type` is set once that type's address and size are published.
unsigned loadedMask();

// The worker saw every type loaded and returned.
bool workerDone();

}  // namespace ImguiFontFeed
