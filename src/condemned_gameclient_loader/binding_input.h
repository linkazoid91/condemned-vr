#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "renderer_probe.h"

namespace condemnedvr {

// Installs the verified Steam 1.0.314.0 CBindMgr value overlay. The hook
// changes only the return value for the four discrete movement commands and
// leaves Retail responsible for command-state storage and callbacks.
bool InstallBindingLocomotionHook(
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept;

} // namespace condemnedvr
