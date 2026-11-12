#pragma once

#include "smash_types.h"

/**
 * Registers callbacks that are executed when the backend is fully initialized, and every time ImGui needs to render
 * a frame.
 *
 * **Note**: this is not sufficient to initialize the ImGui backend. A hook must also be registered (using your platform
 * of choice: Skyline, exlaunch, etc.) for the `nvnBootstrapLoader` function. The hook must call
 * `imgui_smash_bootstrap_hook`.
 *
 * @param postInitCallback the function to execute after the ImGui backend has been fully initialized
 * @param renderCallback the function to execute during rendering
 */
extern "C" void imgui_smash_init(InitFunc postInitCallback, ProcDrawFunc renderCallback);

/**
 * The function to call when `nvnBootstrapLoader` is being called by the game.
 *
 * This hook attempts to grab NVN instances from the called functions by redirecting execution to internal functions.
 *
 * @param functionName the function name parameter from the original function
 * @param origFn a pointer to the original function (trampoline functions are also accepted, provided they call the
 *               original)
 * @returns a function pointer that should be used as the hook's return value
 */
extern "C" void* imgui_smash_bootstrap_hook(const char *functionName, OrigNvnBootstrap origFn);

/**
 * Optionally registers a callback that is executed before the backend is initialized.
 *
 * @param preInitCallback the function to execute before the ImGui backend has been fully initialized
 */
extern "C" void imgui_smash_add_on_pre_init(InitFunc preInitCallback);

/**
 * Optionally registers a callback that is executed just before the start of every frame.
 *
 * @param newFrameCallback the function to execute before every frame
 */
extern "C" void imgui_smash_add_on_new_frame(NewFrameFunc newFrameCallback);

/**
 * Registers a custom logging function that both ImGui and imgui-smash can use.
 *
 * By default, if logging is enabled (see `IMGUI_SMASH_LOG_SVC`), messages are logged to the debug system output using
 * supervisor calls.
 *
 * This function can be used to redirect log messages to a user-provided frontend.
 * The callback takes the following parameters:
 *
 * - `const char* buffer`: the nul-terminated message buffer
 * - `size_t len`: the size of the string in the buffer. It is guaranteed that `len == strlen(buffer)`.
 *
 * @param loggerCallback the external logger function
 */
extern "C" void imgui_smash_set_logger(LoggerFunc loggerCallback);

/**
 * Allows or forbids the L3 + R3 chord that toggles ImGui gamepad navigation. It is forbidden until this is called
 * with `true` (1.0.2; earlier versions always allowed it). Forbidding it also turns navigation off.
 *
 * Call it before `imgui_smash_init`, on the thread that initializes the host.
 *
 * @param enabled whether L3 + R3 may toggle gamepad navigation
 */
extern "C" void imgui_smash_set_gamepad_nav_toggle(bool enabled);