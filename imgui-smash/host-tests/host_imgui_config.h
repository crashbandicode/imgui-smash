// Host copy of user_config/imgui_user_config.h: the same ImGui feature macros; IM_ASSERT reports and aborts
// instead of going through nn::diag.
#pragma once
#include <cstdio>
#include <cstdlib>

#define IM_ASSERT(_EXPR)                                                                    \
  do {                                                                                      \
    if (!(_EXPR)) {                                                                         \
      std::fprintf(stderr, "IM_ASSERT(%s) failed at %s:%d\n", #_EXPR, __FILE__, __LINE__); \
      std::abort();                                                                         \
    }                                                                                       \
  } while (0)

#define IMGUI_DISABLE_DEMO_WINDOWS
#define IMGUI_DISABLE_DEBUG_TOOLS
#define IMGUI_DISABLE_DEFAULT_FONT
