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

// Returns the same fresh world-space HMD camera position and rotation used by
// head aim. Combat diagnostics consume position read-only to measure XZ
// stand-off from a Retail contact point; it never drives collision.
bool ReadTrackedHeadWorldPose(
    float (&position)[3],
    float (&rotation)[4]) noexcept;

// Game-thread-only read used by the Collision X-ray. This reuses the already
// identity-verified ILTClient GetRigidTransform interface and never writes the
// object. Unknown, stale, or invalid objects simply return false.
bool ReadDiagnosticObjectRigidTransform(
    void* object,
    float (&position)[3],
    float (&rotation)[4]) noexcept;

// Returns the separately tracked right-controller aim rotation in the same
// world basis. Weapon fire vectors use this snapshot, never the HMD snapshot.
bool ReadTrackedControllerAimRotation(float (&rotation)[4]) noexcept;

// Returns the coherent OpenXR right-aim position and rotation transformed to
// LithTech world space. Forensic target acquisition uses this ray, while
// physical melee continues to use the separately published grip position.
bool ReadTrackedControllerAimWorldPose(
    float (&position)[3],
    float (&rotation)[4],
    std::uint64_t& sampleId,
    std::uint64_t& timestampNs) noexcept;

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

// Enables the profile-driven dominant/right plus support/left handle solver.
// The secondary select state is reset on every enable transition and never
// scales the authored weapon to match controller separation.
void SetTwoHandedMeleeEnabled(bool enabled) noexcept;

// True only while the left support grip is attached. The binding overlay uses
// this to consume the otherwise conflicting left-squeeze Run action while
// preserving keyboard input and all non-conflicting controller actions.
bool PhysicalMeleeSecondaryGripConsumesLeftSqueeze() noexcept;
bool PhysicalMeleeSecondaryGripCapturesInput(
    const FearVrInputState& input,
    bool sampleFresh) noexcept;

// Candidate/attached slide interaction consumes only its configured off-hand
// activation value. Keyboard/mouse and unrelated controller inputs remain
// Retail-owned.
bool SlideGrabCapturesOffHandInput(
    const FearVrInputState& input,
    bool sampleFresh,
    bool gripInput) noexcept;

// Applies the currently attached support constraint in OpenXR tracking space
// for swing-speed kinematics. This excludes Retail locomotion and turning in
// the same way as the original one-hand tracking-space path.
bool ResolvePhysicalMeleeTrackedTwoHandPose(
    const FearVrInputState& input,
    fearvr::TrackingVector& gripPositionMeters,
    fearvr::TrackingQuaternion& weaponRotation) noexcept;

// Adds renderer-owned two-hand state to the shared in-headset telemetry
// snapshot without exposing renderer globals to the binding hooks.
void ReadPhysicalMeleeTwoHandTelemetry(
    ToolMenuMeleeTelemetry& telemetry) noexcept;

// Arms the opt-in live grip setup controls. Calibration is session-local and
// keyed by stable Retail weapon index; process-local weapon/model pointers are
// refreshed when the same weapon is reacquired. Final profile-ready values are
// emitted through the loader event log.
void SetWeaponGripCalibrationEnabled(bool enabled) noexcept;

// True only while the opt-in setup tool is armed and not paused. Binding
// hooks combine this with the shared two-grip chord resolver so calibration
// stick/button input is not also injected as gameplay input.
bool WeaponGripCalibrationAcceptsControllerInput() noexcept;

// The stereo developer menu centralizes current VR tuning. Its opening
// shortcuts (both grips + Y and F12) are controlled by the persisted Retail
// VR Settings Developer Tools toggle. An already-open menu retains its close
// path and release capture even if the preference changes.
bool VrToolMenuCapturesControllerInput(
    const FearVrInputState& input,
    bool sampleFresh) noexcept;
bool VrToolMenuIsOpen() noexcept;
bool ReadVrToolMenuShortcutEnabled(bool& enabled) noexcept;
bool SetVrToolMenuShortcutEnabled(bool enabled) noexcept;
// Returns the isolated session settings for one stable Retail weapon index.
// Unknown indices use conservative handling defaults and cannot inherit a
// verified melee profile's enabled swing adapter.
ToolMenuMeleeSettings ReadVrToolMenuMeleeSettings(
    std::int32_t weaponIndex) noexcept;

// Returns the saved head-yaw-relative guard pose for the equipped weapon.
// Missing mapped one-handed records may read the accepted Pipe baseline;
// unconfigured and unsupported identities remain disabled.
PhysicalMeleeBlockPoseSettings ReadVrToolMenuBlockPoseSettings(
    std::int32_t weaponIndex) noexcept;

// Returns the optional finite native block-window override. Disabled is the
// release-safe default and leaves Retail's authored duration untouched.
ToolMenuBlockTimingSettings ReadVrToolMenuBlockTimingSettings(
    std::int32_t weaponIndex) noexcept;

// Returns the persistent per-weapon swept-capsule calibration used by both
// the collision solver and its stereo wireframe.
ToolMenuColliderSettings ReadVrToolMenuColliderSettings(
    std::int32_t weaponIndex) noexcept;

// A missing dedicated record follows the weapon's current attack collider;
// the optional flag makes that fallback visible to the tool menu.
ToolMenuColliderSettings ReadVrToolMenuBlockColliderSettings(
    std::int32_t weaponIndex,
    bool* usesAttackColliderFallback = nullptr) noexcept;

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

// Returns one lifetime-validated snapshot of the exact equipped model and
// model-local grip calibration used by the temporary stereo visual override.
// The expected weapon must still be current through both engine-owned
// references. Firearm aiming combines this with the separately published
// weighted held-weapon pose; a switch/drop/load race fails closed.
bool ReadEquippedWeaponVisualSourceForFire(
    const void* expectedWeapon,
    std::int32_t& weaponIndex,
    void*& modelObject,
    float (&modelLocalGripPositionUnits)[3],
    float (&modelLocalGripRotation)[4],
    std::uint64_t& sourceGeneration) noexcept;

// Drops the retained model calibration without disabling the feature. Used
// at menu/load transitions; the equipped-weapon observer can arm it again on
// the first subsequent gameplay update without requiring an attack.
void InvalidatePhysicalMeleeVisualProxySource() noexcept;

} // namespace condemnedvr
