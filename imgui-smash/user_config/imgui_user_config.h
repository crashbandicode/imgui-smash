#pragma once

#include "helpers/assert.hpp"

#define IM_ASSERT(_EXPR) SMASH_ASSERT(_EXPR)

// Nothing in this backend or its hosts draws the Dear ImGui demo. These macros do not
// change any struct in imgui.h or imgui_internal.h; ShowDemoWindow(), ShowStyleEditor()
// and the other demo entry points remain as empty functions.
#define IMGUI_DISABLE_DEMO_WINDOWS
