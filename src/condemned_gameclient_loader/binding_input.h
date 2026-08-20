#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "condemned_player_collision.h"
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

// Retail's verified CMoveMgr handoff and every other native dimension update
// remain authoritative. This option scales only the local player's requested
// horizontal X/Z dimensions; the live Retail Y dimension, posture changes,
// collision interface, and failure result remain engine-owned. A readback
// audit reports initial/changed update-boundary values and separately labels
// the forced readback after a processed pending setter attempt or no-op.
// A scale of 1.0 is exact pass-through.
struct PlayerColliderTelemetry {
    PlayerColliderDimensions retailDimensions{};
    PlayerColliderDimensions requestedDimensions{};
    PlayerColliderDimensions actualDimensions{};
    float widthScale{kPlayerColliderWidthScaleDefault};
    std::uintptr_t playerObject{0U};
    std::uint32_t nativeHandoffCount{0U};
    bool hookReady{false};
    bool retailDimensionsValid{false};
    bool actualDimensionsValid{false};
    bool lastRequestSatisfied{false};
    bool reapplyPending{false};
    bool runtimeDriftObserved{false};
};

bool ConfigurePlayerColliderSettings(
    const PlayerColliderSettings& settings) noexcept;

PlayerColliderSettings ReadPlayerColliderSettings() noexcept;

void ReadPlayerColliderTelemetry(
    PlayerColliderTelemetry& telemetry) noexcept;

struct PlayerCollisionXraySnapshot {
    PlayerCollisionDiagnosticPoint playerOrigin{};
    PlayerCollisionDiagnosticPoint targetOrigin{};
    PlayerCollisionDiagnosticPoint headOrigin{};
    PlayerCollisionDiagnosticPoint contactPoint{};
    PlayerColliderDimensions playerDimensions{};
    PlayerColliderDimensions targetDimensions{};
    std::uintptr_t playerObject{0U};
    std::uintptr_t targetObject{0U};
    std::uint64_t updateSequence{0U};
    std::uint64_t tickMilliseconds{0U};
    std::uint64_t targetAgeMilliseconds{0U};
    std::uint32_t locomotionDirectionMask{0U};
    float headToPlayerHorizontalUnits{-1.0F};
    float playerToTargetHorizontalUnits{-1.0F};
    float diagnosticProxyHorizontalGapUnits{-1.0F};
    bool enabled{false};
    bool movementTraceReady{false};
    bool playerValid{false};
    bool targetValid{false};
    bool headValid{false};
    bool contactValid{false};
};

void SetPlayerCollisionXrayEnabled(bool enabled) noexcept;
bool PlayerCollisionXrayEnabled() noexcept;
bool ReadPlayerCollisionXraySnapshot(
    PlayerCollisionXraySnapshot& snapshot) noexcept;

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
// While the verified Scanner or Item Camera is equipped, its desktop target-
// query callsites borrow the matching fresh Retail Camera-socket segment
// geometry used by the white alignment arrows and live preview. Retail keeps
// the original range, collision filter, result classification, and action
// dispatch; a missing or stale matching pose falls back to the untouched
// desktop query.
// The optional forensic-memory probe observes bounded Scanner/Camera object
// spans after Tool, Fire, and Activate edges; it never writes game memory.
bool InstallBindingCoreActionsHook(
    void* masterDatabase,
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log,
    bool forensicMemoryProbe = false) noexcept;

// Enables short OpenXR confirmation pulses for VR-applied Fire, Block, and
// Activate edges. This is not a weapon-impact or per-shot haptic path.
bool InstallControllerHaptics(
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept;

// Suppresses Retail mouse pitch/yaw only while a fresh HMD look snapshot is
// available. The first verified handgun candidate derives its fire basis from
// the visible model: Breach -> Flash when both sockets exist, otherwise the
// authored Flash-socket +Z axis used by Flash-only Retail handguns. The saved
// grip transform is applied first. Other weapons and any failed runtime gate
// retain the freshness-gated controller basis. Retail owns the fire origin.
// Automatic guard-pose entry uses command 28. Verified Retail has no
// command-28 off action, so Retail's finite block window owns pose exit. The
// physical-melee lifetime extension is limited to positively classified
// attack records and never extends block records.
bool InstallHeadAimHooks(
    void* masterDatabase,
    HMODULE gameClientModule,
    RendererProbeLogFunction log,
    bool aimPathProbe = false,
    bool controllerMeleeAim = false,
    bool physicalMeleeProbe = false,
    bool physicalMeleeWallProxy = false,
    bool physicalMeleeColliderDebug = false,
    bool physicalMeleeContactDamage = false,
    bool physicalMeleeVisualProxy = false,
    bool weaponGripCalibration = false,
    bool twoHandedMelee = false) noexcept;

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


// Coherent renderer-facing view of the configured swept weapon volume and
// the exact proxy origin currently supplied to Retail's collision transform.
// A body is live only while the player-owned collision record remains fresh;
// while automatic equip-time verification is pending the same geometry is an
// explicit preview.
struct PhysicalMeleeColliderDebugSnapshot {
    fearvr::TrackingVector baseUnits{};
    fearvr::TrackingVector tipUnits{};
    fearvr::TrackingVector collisionOriginUnits{};
    float radiusUnits{0.0F};
    std::uint64_t sampleId{0U};
    bool enabled{false};
    bool trackingFresh{false};
    bool collisionBodyLive{false};
};

bool ReadPhysicalMeleeColliderDebugSnapshot(
    PhysicalMeleeColliderDebugSnapshot& snapshot) noexcept;

// Returns the same weighted pose reprojected through the dedicated block
// capsule. Its live flag is role-specific and never borrows an attack record.
bool ReadPhysicalMeleeBlockColliderDebugSnapshot(
    PhysicalMeleeColliderDebugSnapshot& snapshot) noexcept;
} // namespace condemnedvr
