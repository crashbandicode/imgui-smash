#pragma once

namespace ImguiSharedFonts {

// Starts the worker thread that makes every nn::pl font call (each one is an IPC to pl:u) and publishes the loaded
// system shared fonts to ImguiFontFeed, in query order: Standard (Latin and Japanese), Chinese Simplified, Extended
// Chinese Simplified, Chinese Traditional, Korean, Nintendo Extended. The thread has the lowest priority and no
// core of its own choosing (the system's default), a 16 KiB stack from the host allocator, and ends once all six
// are loaded. False when the thread could not be created: the overlay then stays off.
bool start();

}  // namespace ImguiSharedFonts
