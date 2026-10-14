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
void shutdown(nvn::Device *device);
void copyStats(ImguiSmashDrawStats *out);

}
