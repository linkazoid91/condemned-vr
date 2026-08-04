#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "condemned_tool_menu.h"
#include "renderer_probe.h"

namespace condemnedvr {

// Installs the verified Steam 1.0.314.0 CBindMgr value overlay. The hook
// changes only the return value for the four discrete movement commands and
// leaves Retail responsible for command-state storage and callbacks.
bool InstallBindingLocomotionHook(
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept;

// Overlays only the verified Retail YawAccel extremal command query. A
// stronger Retail command-23 value wins; mouse command 12 remains untouched
// and is added independently by Retail.
bool InstallBindingTurningHook(
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept;

// Overlays the verified Retail Activate binding (command 87) from the right
// controller squeeze while preserving a stronger Retail value. Retail's own
// binding update remains responsible for command edges and callbacks.
bool InstallBindingInteractionHook(
    void* masterDatabase,
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept;

// Overlays the verified Retail run, fire, block, melee-toggle, ammo-check,
// stun-gun, and flashlight bindings. Every command remains
// state-gated and Retail retains command-edge and callback ownership.
bool InstallBindingCoreActionsHook(
    void* masterDatabase,
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept;

// Enables short OpenXR confirmation pulses for VR-applied Fire, Block, and
// Activate edges. This is not a weapon-impact or per-shot haptic path.
bool InstallControllerHaptics(
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept;

// Suppresses Retail mouse pitch/yaw only while a fresh HMD look snapshot is
// available and redirects the verified weapon fire-vector path to a separate
// freshness-gated right-controller world-space aim basis.
bool InstallHeadAimHooks(
    HMODULE gameClientModule,
    RendererProbeLogFunction log,
    bool aimPathProbe = false,
    bool controllerMeleeAim = false,
    bool physicalMeleeProbe = false,
    bool physicalMeleeWallProxy = false,
    bool physicalMeleeVisualProxy = false,
    bool weaponGripCalibration = false) noexcept;

// Polls a release-gated left-secondary edge from the Retail client-shell
// update and routes it through the game's own Escape key callbacks. The
// optional menu-control overlay uses the same verified callbacks for arrow,
// Enter, and Escape edges only while Retail reports its menu state.
bool InstallMenuToggleHook(
    void* masterDatabase,
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log,
    bool menuControls = false) noexcept;

// Snapshot consumed by the in-headset Debug and Melee tabs. Values are
// copied under the physical-melee lock; the renderer never reads hook-owned
// state directly.
void ReadPhysicalMeleeToolTelemetry(
    ToolMenuMeleeTelemetry& telemetry) noexcept;

} // namespace condemnedvr
