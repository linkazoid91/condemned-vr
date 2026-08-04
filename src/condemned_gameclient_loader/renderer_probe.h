#pragma once

#include <cstdint>

#include "condemned_tool_menu.h"

namespace condemnedvr {

using RendererProbeLogFunction =
    void (*)(const char* event, const char* detail) noexcept;

// Read-only M3 discovery. This function does not invoke an engine interface,
// replace a vtable entry, or write into the master interface database.
void ProbeRendererInterfaces(
    void* masterDatabase,
    RendererProbeLogFunction log) noexcept;

// Installs a version- and signature-gated pass-through on the confirmed
// one-argument RenderCamera slot. The hook calls Retail exactly once.
bool InstallRendererPassThroughProbe(
    void* masterDatabase,
    RendererProbeLogFunction log,
    void* diagnosticBridgeModule = nullptr,
    bool doubleRenderDiagnostic = false,
    bool cameraReadProbe = false,
    bool eyeOffsetDiagnostic = false,
    bool reverseEyeOffsetDiagnostic = false,
    bool zeroEyeOffsetDiagnostic = false,
    bool continuousStereoTuning = false,
    bool controllerRecenter = false,
    bool headAim = false) noexcept;

// Returns the latest verified world-space HMD look rotation generated from
// the untouched Retail camera basis. It remains the camera/flashlight basis.
// Snapshots expire quickly and are never available on flat/menu frames.
bool ReadTrackedHeadAimRotation(float (&rotation)[4]) noexcept;
bool TrackedHeadAimIsFresh() noexcept;

// Returns the separately tracked right-controller aim rotation in the same
// world basis. Weapon fire vectors use this snapshot, never the HMD snapshot.
bool ReadTrackedControllerAimRotation(float (&rotation)[4]) noexcept;

// Returns the coherent held-weapon pose used by physical melee: position is
// the OpenXR grip attachment point and rotation is the OpenXR aim direction.
// Both components come from one published snapshot and share the same short
// freshness gate.
bool ReadTrackedControllerWorldPose(
    float (&position)[3],
    float (&rotation)[4],
    std::uint64_t& sampleId,
    std::uint64_t& timestampNs) noexcept;

// Returns a coherent camera pivot, untouched Retail camera-base rotation,
// and right-controller world rotation for redirecting animation-driven melee
// transforms. All three snapshots share the normal short freshness gate.
bool ReadTrackedMeleeAimBasis(
    float (&position)[3],
    float (&baseRotation)[4],
    float (&controllerRotation)[4]) noexcept;

// Enables the M5 render-only weapon-model diagnostic. The existing Retail
// model is moved only while the verified world render is executing and its
// exact transform is restored before returning to the game.
void SetPhysicalMeleeVisualProxyEnabled(bool enabled) noexcept;

// Arms the opt-in live grip setup controls. Calibration is session-local and
// keyed by stable Retail weapon index; process-local weapon/model pointers are
// refreshed when the same weapon is reacquired. Final profile-ready values are
// emitted through the loader event log.
void SetWeaponGripCalibrationEnabled(bool enabled) noexcept;

// True only while the opt-in setup tool is armed and not paused. Binding
// hooks combine this with the shared two-grip chord resolver so calibration
// stick/button input is not also injected as gameplay input.
bool WeaponGripCalibrationAcceptsControllerInput() noexcept;

// The always-available stereo developer menu centralizes current VR tuning.
// It captures controller bindings while open and also captures its deliberate
// two-grip + Y toggle chord before that chord can leak into Retail input.
bool VrToolMenuCapturesControllerInput(
    const FearVrInputState& input,
    bool sampleFresh) noexcept;
bool VrToolMenuIsOpen() noexcept;
// Returns the isolated session settings for one stable Retail weapon index.
// Unknown indices use conservative handling defaults and cannot inherit a
// verified melee profile's enabled swing adapter.
ToolMenuMeleeSettings ReadVrToolMenuMeleeSettings(
    std::int32_t weaponIndex) noexcept;

// Publishes the current local-player weapon and its engine-owned model
// reference. Both references are validated during every render, so switching
// or releasing a weapon drops the old model automatically. Grip calibration
// is profile data and never changes the shared render path.
bool PublishEquippedWeaponVisualProxySource(
    void* const* equippedWeaponReference,
    void* equippedWeapon,
    std::int32_t equippedWeaponIndex,
    void* const* modelObjectReference,
    void* modelObject,
    const float (&modelLocalGripPositionUnits)[3],
    const float (&modelLocalGripRotation)[4]) noexcept;

// Drops the retained model calibration without disabling the feature. Used
// at menu/load transitions; the equipped-weapon observer can arm it again on
// the first subsequent gameplay update without requiring an attack.
void InvalidatePhysicalMeleeVisualProxySource() noexcept;

} // namespace condemnedvr
