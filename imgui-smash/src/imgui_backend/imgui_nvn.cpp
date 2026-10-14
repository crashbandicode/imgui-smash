#include "imgui_nvn.h"
#include "DrawSlots.h"
#include "frame_guard.hpp"
#include "helpers/InputHelper.h"
#include "helpers/memoryHelper.h"
#include "imgui_backend/imgui_impl_nvn.hpp"
#include "imgui_backend_config.h"
#include "logger/Logger.hpp"
#include "nvn_CppFuncPtrImpl.h"
#include "nx/abort.h"

#include <climits>

nvn::Device *nvnDevice;
nvn::Queue *nvnQueue;
nvn::CommandBuffer *nvnCmdBuf;
nvn::Texture* nvnTextures[3];

static nvn::CommandBuffer *__cmdBuf = nullptr;
static const nvn::TexturePool *__texturePool = nullptr;
static const nvn::SamplerPool *__samplerPool = nullptr;

nvn::DeviceGetProcAddressFunc tempGetProcAddressFuncPtr;

nvn::CommandBufferInitializeFunc tempBufferInitFuncPtr;
nvn::DeviceInitializeFunc tempDeviceInitFuncPtr;
nvn::QueueInitializeFunc tempQueueInitFuncPtr;
nvn::QueuePresentTextureFunc tempPresentTexFunc;
nvn::QueueSubmitCommandsFunc tempQueueSubmitFunc;
nvn::WindowAcquireTextureFunc tempWindowAcquireTextureFunc;
nvn::WindowSetCropFunc tempSetCropFunc;
nvn::WindowBuilderSetTexturesFunc tempWindowBuilderSetTexturesFunc;

nvn::CommandBufferSetTexturePoolFunc tempCommandSetTexturePoolFunc;
nvn::CommandBufferSetSamplerPoolFunc tempCommandSetSamplerPoolFunc;

bool hasInitImGui = false;
int commandBufferCount = 0;
int currentTextureIndex = -1;
int numberOfTextures = -1;

namespace nvnImGui {
  ImVector<ProcDrawFunc> drawQueue;
  ImVector<InitFunc> contextSetupQueue;
  ImVector<InitFunc> preInitQueue;
  ImVector<InitFunc> postInitQueue;
  ImVector<NewFrameFunc> newFrameQueue;
}

extern "C" void imgui_smash_set_nvn_device(nvn::Device * device) {
    nvnDevice = device;    
}

extern "C" void imgui_smash_set_nvn_queue(nvn::Queue * queue) {
    nvnQueue = queue;
}

extern "C" void imgui_smash_set_proc_address(void* proc_address) {
    tempGetProcAddressFuncPtr = (nvn::DeviceGetProcAddressFunc)proc_address;
}

void setTexturePool(nvn::CommandBuffer *cmdBuf, const nvn::TexturePool *pool) {
  __cmdBuf = cmdBuf;
  __texturePool = pool;

  tempCommandSetTexturePoolFunc(cmdBuf, pool);
}

void setSamplerPool(nvn::CommandBuffer *cmdBuf, const nvn::SamplerPool *pool) {
  __samplerPool = pool;

  tempCommandSetSamplerPoolFunc(cmdBuf, pool);
}

void setCrop(nvn::Window *window, int x, int y, int w, int h) {

  Logger::log("Window Crop: x: %d y: %d w: %d h: %d\n", x, y, w, h);

  tempSetCropFunc(window, x, y, w, h);

  if (hasInitImGui)
    ImGui::GetIO().DisplaySize = ImVec2(w - x, h - y);
}

void presentTexture(nvn::Queue *queue, nvn::Window *window, int texIndex) {

  // save state; modified by procDraw
//   auto *buf = __cmdBuf;
//   auto *pool = __texturePool;
//   auto *samplerPool = __samplerPool;

//   if(currentTextureIndex != -1) {
//     // end previous recording (we dont need to submit)
//     buf->EndRecording();

//     if (hasInitImGui)
//         nvnImGui::procDraw();

//     // restore state
//     buf->BeginRecording();
//     setTexturePool(buf, pool);
//     setSamplerPool(buf, samplerPool);
//     auto handle = buf->EndRecording();
//     queue->SubmitCommands(1, &handle);

//     // start new recording back up (for next frame)
//     buf->BeginRecording();
//   }

  tempPresentTexFunc(queue, window, texIndex);
}

void submitCommands(nvn::Queue *queue, int num_commands, nvn::CommandHandle *handles) {
  const bool overlay_ready = currentTextureIndex != -1 && hasInitImGui;
  FrameEntryOwner::Claim claim = claim_frame_entry(overlay_ready);
  if (!claim.holds()) {
    tempQueueSubmitFunc(queue, num_commands, handles);
    return;
  }
  if (num_commands == INT_MAX || (num_commands > 0 && handles == nullptr)) {
    tempQueueSubmitFunc(queue, num_commands, handles);
    return;
  }

  nvn::CommandHandle handle = nvnImGui::procDraw();
  const int slot = ImguiDrawSlots::active();
  if (handle == 0 || slot < 0) {
    tempQueueSubmitFunc(queue, num_commands, handles);
    return;
  }
  if (num_commands < 0) {
    ImguiDrawSlots::discardUnsubmitted(slot, nvnDevice);
    tempQueueSubmitFunc(queue, num_commands, handles);
    return;
  }

  const size_t bytes = (static_cast<size_t>(num_commands) + 1u) * sizeof(nvn::CommandHandle);
  void *new_handles = malloc(bytes);
  if (new_handles == nullptr) {
    ImguiDrawSlots::discardUnsubmitted(slot, nvnDevice);
    tempQueueSubmitFunc(queue, num_commands, handles);
    return;
  }
  if (num_commands > 0) {
    memcpy(new_handles, handles, static_cast<size_t>(num_commands) * sizeof(nvn::CommandHandle));
  }
  static_cast<nvn::CommandHandle *>(new_handles)[num_commands] = handle;
  tempQueueSubmitFunc(queue, num_commands + 1, static_cast<nvn::CommandHandle *>(new_handles));
  free(new_handles);
  ImguiDrawSlots::fenceAfterSubmit(slot, queue);
}

void acquireTexture(nvn::Window * window, nvn::Sync * texture_avaliable_sync, int * texture_index){
    nvn::WindowAcquireTextureResult res = tempWindowAcquireTextureFunc(window, texture_avaliable_sync, texture_index);
    currentTextureIndex = *texture_index;
    return (void)res;
}

extern "C" void imgui_smash_lookup_original_functions() {
    if(nvnDevice != nullptr && tempGetProcAddressFuncPtr != nullptr){
        nvn::nvnLoadCPPProcs(nvnDevice, tempGetProcAddressFuncPtr);
    }
}

NVNboolean deviceInit(nvn::Device *device, const nvn::DeviceBuilder *builder) {
  NVNboolean result = tempDeviceInitFuncPtr(device, builder);
  nvnDevice = device;
  nvn::nvnLoadCPPProcs(nvnDevice, tempGetProcAddressFuncPtr);
  return result;
}

NVNboolean queueInit(nvn::Queue *queue, const nvn::QueueBuilder *builder) {
  NVNboolean result = tempQueueInitFuncPtr(queue, builder);
  nvnQueue = queue;
  return result;
}


NVNboolean cmdBufInit(nvn::CommandBuffer *buffer, nvn::Device *device) {
  NVNboolean result = tempBufferInitFuncPtr(buffer, device);
//   Logger::log("cmdBufCount: %d ", commandBufferCount);
  if (!hasInitImGui) {
    // XENO: wait for the right command buffer
    if (commandBufferCount++ != IMGUI_SMASH_COMMAND_BUFFER_ID) {
      return result;
    }
    nvnCmdBuf = buffer;
    hasInitImGui = nvnImGui::InitImGui();
  }

  return result;
}

void builderSetTextures(nvn::WindowBuilder* builder, int num_textures, nvn::Texture* textures) {
    if(textures != nullptr && num_textures > 0) {
        memcpy((void *)nvnTextures, (void *)textures, num_textures << 3);
    }
    numberOfTextures = num_textures;
    tempWindowBuilderSetTexturesFunc(builder, num_textures, (nvn::Texture* const*)textures);
}

nvn::GenericFuncPtrFunc getProc(nvn::Device *device, const char *procName) {
  nvn::GenericFuncPtrFunc ptr = tempGetProcAddressFuncPtr(nvnDevice, procName);
  Logger::log("Getting %s ", procName);
  if (strcmp(procName, "nvnQueueInitialize") == 0) {
    tempQueueInitFuncPtr = (nvn::QueueInitializeFunc) ptr;
    return (nvn::GenericFuncPtrFunc) &queueInit;
  } 
  else if (strcmp(procName, "nvnCommandBufferInitialize") == 0) {
    tempBufferInitFuncPtr = (nvn::CommandBufferInitializeFunc) ptr;
    return (nvn::GenericFuncPtrFunc) &cmdBufInit;
  } 
  else if (strcmp(procName, "nvnQueueSubmitCommands") == 0) {
    tempQueueSubmitFunc = (nvn::QueueSubmitCommandsFunc) ptr;
    return (nvn::GenericFuncPtrFunc) &submitCommands;
  } 
  else if (strcmp(procName, "nvnWindowAcquireTexture") == 0) {
    tempWindowAcquireTextureFunc = (nvn::WindowAcquireTextureFunc) ptr;
    return (nvn::GenericFuncPtrFunc) &acquireTexture;
  } 
//   else if (strcmp(procName, "nvnQueuePresentTexture") == 0) {
//     tempPresentTexFunc = (nvn::QueuePresentTextureFunc) ptr;
//     return (nvn::GenericFuncPtrFunc) &presentTexture;
//   } 
  else if (strcmp(procName, "nvnDeviceInitialize") == 0) {
    tempDeviceInitFuncPtr = (nvn::DeviceInitializeFunc) ptr;
    return (nvn::GenericFuncPtrFunc) &deviceInit;
  }
  else if (strcmp(procName, "nvnCommandBufferSetSamplerPool") == 0) {
    tempCommandSetSamplerPoolFunc = (nvn::CommandBufferSetSamplerPoolFunc) ptr;
    return (nvn::GenericFuncPtrFunc) &setSamplerPool;
  }
  else if (strcmp(procName, "nvnCommandBufferSetTexturePool") == 0) {
    tempCommandSetTexturePoolFunc = (nvn::CommandBufferSetTexturePoolFunc) ptr;
    return (nvn::GenericFuncPtrFunc) &setTexturePool;
  }
  else if (strcmp(procName, "nvnWindowSetCrop") == 0) {
    tempSetCropFunc = (nvn::WindowSetCropFunc) ptr;
    return (nvn::GenericFuncPtrFunc) &setCrop;
  }
  else if (strcmp(procName, "nvnDeviceGetProcAddress") == 0) {
    tempGetProcAddressFuncPtr = (nvn::DeviceGetProcAddressFunc) ptr;
    return (nvn::GenericFuncPtrFunc) &getProc;
  }
  else if (strcmp(procName, "nvnWindowBuilderSetTextures") == 0) {
    tempWindowBuilderSetTexturesFunc = (nvn::WindowBuilderSetTexturesFunc) ptr;
    return (nvn::GenericFuncPtrFunc) &builderSetTextures;
  }

  return ptr;
}

extern "C" void * imgui_smash_get_proc_address() {
    return (void *)&getProc;
}


void disableButtons(nn::hid::NpadBaseState *state) {
  if (!InputHelper::isReadInputs() && InputHelper::isInputToggled()) {
    // clear out the data within the state (except for the sampling number and attributes)
    state->mButtons = nn::hid::NpadButtonSet();
    state->mAnalogStickL = nn::hid::AnalogStickState();
    state->mAnalogStickR = nn::hid::AnalogStickState();
  }
}

void* nvnImGui::NvnBootstrapHook(const char *funcName, OrigNvnBootstrap origFn) {
  void *result = origFn(funcName);

  Logger::log("Getting Proc from Bootstrap: %s\n", funcName);

  if (strcmp(funcName, "nvnDeviceInitialize") == 0) {
    tempDeviceInitFuncPtr = (nvn::DeviceInitializeFunc) result;
    return (void *) &deviceInit;
  }
  if (strcmp(funcName, "nvnDeviceGetProcAddress") == 0) {
    tempGetProcAddressFuncPtr = (nvn::DeviceGetProcAddressFunc) result;
    return (void *) &getProc;
  }

  return result;
}

void nvnImGui::addDrawFunc(ProcDrawFunc func) {
  SMASH_ASSERT(func != nullptr, "Function cannot be nullptr!");
  SMASH_ASSERT(!drawQueue.contains(func), "Function has already been added to queue!");

  drawQueue.push_back(func);
}

void nvnImGui::addPreInitFunc(InitFunc func) {
  SMASH_ASSERT(func != nullptr, "Function cannot be nullptr!");
  SMASH_ASSERT(!preInitQueue.contains(func), "Function has already been added to queue!");

  preInitQueue.push_back(func);
}

void nvnImGui::addContextSetupFunc(InitFunc func) {
  SMASH_ASSERT(func != nullptr, "Function cannot be nullptr!");
  SMASH_ASSERT(!preInitQueue.contains(func), "Function has already been added to queue!");

  contextSetupQueue.push_back(func);
}

void nvnImGui::addPostInitFunc(InitFunc func) {
  SMASH_ASSERT(func != nullptr, "Function cannot be nullptr!");
  SMASH_ASSERT(!postInitQueue.contains(func), "Function has already been added to queue!");

  postInitQueue.push_back(func);
}

void nvnImGui::addNewFrameFunc(NewFrameFunc func) {
  SMASH_ASSERT(func != nullptr, "Function cannot be nullptr!");
  SMASH_ASSERT(!newFrameQueue.contains(func), "Function has already been added to queue!");

  newFrameQueue.push_back(func);
}

bool show_mouse_on_screen = false;

extern "C" void imgui_smash_show_mouse(bool show_mouse) {
    show_mouse_on_screen = show_mouse;
}

nvn::CommandHandle nvnImGui::procDraw() {
  for (auto newFrameFunc: newFrameQueue) {
    newFrameFunc();
  }

  ImguiNvnBackend::newFrame();
  ImGui::NewFrame();

  ImGui::GetIO().MouseDrawCursor = show_mouse_on_screen;

  for (auto drawFunc: drawQueue) {
    drawFunc();
  }

  ImGui::Render();

  return ImguiNvnBackend::renderDrawData(ImGui::GetDrawData());
}

void nvnImGui::InstallHooks() {
    // Hooks are installed on the rust side
}

bool nvnImGui::InitImGui() {
  if (nvnDevice && nvnQueue && nvnCmdBuf) {

    Logger::log("Creating ImGui with Ver.\n");

    IMGUI_CHECKVERSION();

    Mem::Init();

    ImGuiMemAllocFunc allocFunc = [](size_t size, void *user_data) {
      return Mem::Allocate(size);
    };

    ImGuiMemFreeFunc freeFunc = [](void *ptr, void *user_data) {
      Mem::Deallocate(ptr);
    };

    ImGui::SetAllocatorFunctions(allocFunc, freeFunc, nullptr);

    Logger::log("Creating ImGui context.\n");
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void) io;
    Logger::log("Created ImGui context.\n");

    for (auto init: contextSetupQueue) {
      init();
    }

    ImGui::StyleColorsDark();

    ImguiNvnBackend::NvnBackendInitInfo initInfo = {
        .device = nvnDevice,
        .queue = nvnQueue,
        .cmdBuf = nvnCmdBuf
    };

    for (auto init: preInitQueue) {
      init();
    }

    Logger::log("Initializing Backend.\n");

    ImguiNvnBackend::InitBackend(initInfo);

    InputHelper::initKBM();

    // set input helpers default port
    InputHelper::setPort(IMGUI_SMASH_DEFAULT_INPUT_PORT);

    for (auto init: postInitQueue) {
      init();
    }

// #if IMGUI_SMASH_DRAW_DEMO
    // addDrawFunc([]() { ImGui::ShowDemoWindow(); });
// #endif

    return true;

  } else {
    Logger::log("nvnDevice: %p - nvnQueue: %p - nvnCmdBuf: %p", nvnDevice, nvnQueue, nvnCmdBuf);
    Logger::log("Unable to create ImGui Renderer!\n");

    return false;
  }
}