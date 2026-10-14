#pragma once

#include "helpers/assert.hpp"

#define IM_ASSERT(_EXPR) SMASH_ASSERT(_EXPR)

// Nothing in this backend or its hosts draws the Dear ImGui demo. These macros do not
// change any struct in imgui.h or imgui_internal.h; ShowDemoWindow(), ShowStyleEditor()
// and the other demo entry points remain as empty functions.
#define IMGUI_DISABLE_DEMO_WINDOWS

// Metrics/debugger, ID stack tool, item picker, debug log, the allocation hook in
// MemAlloc/MemFree and the hover-time duplicate-ID highlight. ShowMetricsWindow() and
// the other entry points remain as empty functions; cimgui's igDebugLog/igDebugLogV
// are kept as no-ops. No struct layout changes.
#define IMGUI_DISABLE_DEBUG_TOOLS

// The backend adds JetBrains Mono NL (18 px) before the atlas is first built, so ImGui
// never falls back to AddFontDefault() and the embedded ProggyClean/ProggyForever data
// is unused. With this macro AddFontDefault*() assert instead. Tied to the backend's
// font flag so that turning the JetBrains font off brings the default fonts back.
#include "imgui_backend_config.h"
#if IMGUI_SMASH_LOAD_DEFAULT_FONT
#define IMGUI_DISABLE_DEFAULT_FONT
#endif
