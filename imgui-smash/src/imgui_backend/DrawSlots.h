#pragma once

#include "imgui_smash_draw_stats.h"
#include "nvn_Cpp.h"

class MemoryBuffer;

namespace ImguiDrawSlots {

void ensure(nvn::Device *device);
int begin(nvn::Device *device, size_t vertexBytes, size_t indexBytes);
bool overflowed(int slot);
nvn::CommandBuffer *commandBuffer(int slot);
MemoryBuffer *vertex(int slot);
MemoryBuffer *index(int slot);
MemoryBuffer *uniform(int slot);
void complete(int slot, nvn::Device *device, nvn::CommandHandle handle);
void discardUnsubmitted(int slot, nvn::Device *device);
void fenceAfterSubmit(int slot, nvn::Queue *queue);
int active();
// Serial of the last recording begun (0 before the first).
uint64_t lastSerial();
// True once every recording with a serial up to `serial` has completed on the GPU or was never submitted.
bool completedThrough(uint64_t serial);
void shutdown(nvn::Device *device);
void copyStats(ImguiSmashDrawStats *out);

}
