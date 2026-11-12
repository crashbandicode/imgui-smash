#include "imgui_smash.h"
#include "helpers/InputHelper.h"
#include "imgui_backend/imgui_nvn.h"
#include "logger/Logger.hpp"
#include "cimgui.h"

extern "C" void imgui_smash_init(InitFunc postInitCallback, ProcDrawFunc renderCallback) {
  if (postInitCallback != 0)
    nvnImGui::addPostInitFunc(postInitCallback);

  nvnImGui::InstallHooks();

  if (renderCallback != 0)
    nvnImGui::addDrawFunc(renderCallback);
}

extern "C" void* imgui_smash_bootstrap_hook(const char *functionName, nvnImGui::OrigNvnBootstrap origFn) {
  return nvnImGui::NvnBootstrapHook(functionName, origFn);
}

extern "C" void imgui_smash_add_on_pre_init(InitFunc preInitCallback) {
  if (preInitCallback != 0)
    nvnImGui::addPreInitFunc(preInitCallback);
}

extern "C" void imgui_smash_add_on_new_frame(NewFrameFunc newFrameCallback) {
  if (newFrameCallback != 0)
    nvnImGui::addNewFrameFunc(newFrameCallback);
}

extern "C" void imgui_smash_add_on_draw_frame(ProcDrawFunc renderCallback) {
    if (renderCallback != 0)
        nvnImGui::addDrawFunc(renderCallback);
}

extern "C" void imgui_smash_context_setup(InitFunc contextCallback) {
    if (contextCallback != 0)
        nvnImGui::addContextSetupFunc(contextCallback);
}

extern "C" void imgui_smash_set_logger(LoggerFunc loggerCallback) {
  Logger::instance().forward(loggerCallback);
}

extern "C" void imgui_smash_set_gamepad_nav_toggle(bool enabled) {
  InputHelper::setNavToggleEnabled(enabled);
}