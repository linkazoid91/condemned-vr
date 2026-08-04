#pragma once

#include <cmath>

#include "head_tracking_math.h"
#include "input_state.h"

namespace condemnedvr {

constexpr std::uint32_t kCondemnedYawAccelCommand = 23U;
constexpr std::uint32_t kCondemnedPitchCommand = 11U;
constexpr std::uint32_t kCondemnedYawCommand = 12U;
constexpr std::uint32_t kCondemnedActivateCommand = 87U;
constexpr std::uint32_t kCondemnedRunCommand = 16U;
constexpr std::uint32_t kCondemnedFireCommand = 17U;
constexpr std::uint32_t kCondemnedBlockCommand = 28U;
constexpr std::uint32_t kCondemnedToggleMeleeCommand = 60U;
constexpr std::uint32_t kCondemnedAmmoCheckCommand = 61U;
constexpr std::uint32_t kCondemnedStunGunCommand = 62U;
constexpr std::uint32_t kCondemnedFlashlightCommand = 114U;
constexpr float kCondemnedTurnDeadzone = 0.22F;
constexpr float kCondemnedActivateSqueezeThreshold = 0.65F;
constexpr float kCondemnedActionTriggerThreshold = 0.55F;
constexpr float kCondemnedActionSqueezeThreshold = 0.65F;
constexpr float kWeaponGripCalibrationChordThreshold = 0.75F;
constexpr float kWeaponGripCalibrationAxisDeadzone = 0.25F;
constexpr int kCondemnedGameStateUndefined = 0;
constexpr int kCondemnedGameStatePlaying = 1;
constexpr int kCondemnedGameStateExiting = 2;
constexpr int kCondemnedGameStateLoading = 3;
constexpr int kCondemnedGameStateSplash = 4;
constexpr int kCondemnedGameStateMenu = 5;
constexpr int kCondemnedGameStateScreen = 6;
constexpr int kCondemnedGameStatePaused = 7;
constexpr int kCondemnedGameStateDemo = 8;
constexpr int kCondemnedGameStateMovie = 9;
constexpr int kCondemnedGameStateCount = 10;

inline bool IsKnownCondemnedGameState(int state) noexcept {
    return state >= kCondemnedGameStateUndefined &&
        state < kCondemnedGameStateCount;
}

// Only live gameplay is safe for native stereo. Retail draws every other
// state after its world-camera pass, so those states (and an unreadable state)
// must use the completed desktop backbuffer on the comfort panel.
inline bool CondemnedGameStateUsesFlatPanel(int state) noexcept {
    return !IsKnownCondemnedGameState(state) ||
        state != kCondemnedGameStatePlaying;
}

// Keep synthetic Escape deliberately narrow. In other states Retail assigns
// Escape to loading, movies, modal screens, or shutdown behavior.
inline bool CondemnedGameStateAllowsMenuToggle(int state) noexcept {
    return state == kCondemnedGameStatePlaying ||
        state == kCondemnedGameStateMenu;
}

struct TurningValue {
    float value{0.0F};
    bool active{false};
};

inline TurningValue ResolveTurningValue(
    const FearVrInputState& state,
    bool sampleFresh,
    float deadzone = kCondemnedTurnDeadzone) noexcept {
    if (!fearvr::IsInputStateUsable(state, sampleFresh) ||
        (state.activeHands & FEARVR_HAND_MASK_RIGHT) == 0) {
        return {};
    }
    const float value = fearvr::ApplyInputDeadzone(
        state.turnX, deadzone);
    return {value, value != 0.0F};
}

inline float MergeTurningWithRetail(
    float retailValue,
    const TurningValue& turning) noexcept {
    if (!turning.active || !std::isfinite(turning.value) ||
        !std::isfinite(retailValue)) {
        return retailValue;
    }
    return std::fabs(turning.value) > std::fabs(retailValue)
        ? turning.value
        : retailValue;
}

struct ActivateValue {
    float value{0.0F};
    bool active{false};
};

inline ActivateValue ResolveActivateValue(
    const FearVrInputState& state,
    bool sampleFresh) noexcept {
    const bool active =
        fearvr::IsInputStateUsable(state, sampleFresh) &&
        (state.activeHands & FEARVR_HAND_MASK_RIGHT) != 0 &&
        state.squeeze[FEARVR_HAND_RIGHT] >=
            kCondemnedActivateSqueezeThreshold;
    return {active ? 1.0F : 0.0F, active};
}

inline float MergeActivateWithRetail(
    float retailValue,
    const ActivateValue& activate) noexcept {
    if (!activate.active || !std::isfinite(retailValue)) {
        return retailValue;
    }
    return std::fabs(retailValue) >= std::fabs(activate.value)
        ? retailValue
        : activate.value;
}

struct CoreActionValue {
    float value{0.0F};
    bool active{false};
};

inline int CondemnedCoreActionIndex(std::uint32_t command) noexcept {
    switch (command) {
    case kCondemnedRunCommand:
        return 0;
    case kCondemnedFireCommand:
        return 1;
    case kCondemnedBlockCommand:
        return 2;
    case kCondemnedToggleMeleeCommand:
        return 3;
    case kCondemnedAmmoCheckCommand:
        return 4;
    case kCondemnedStunGunCommand:
        return 5;
    case kCondemnedFlashlightCommand:
        return 6;
    default:
        return -1;
    }
}

inline const char* CondemnedCoreActionControlName(
    std::uint32_t command) noexcept {
    switch (command) {
    case kCondemnedRunCommand:
        return "left_squeeze";
    case kCondemnedFireCommand:
        return "right_trigger";
    case kCondemnedBlockCommand:
        return "left_trigger";
    case kCondemnedToggleMeleeCommand:
        return "right_primary";
    case kCondemnedAmmoCheckCommand:
        return "right_secondary";
    case kCondemnedStunGunCommand:
        return "left_stick";
    case kCondemnedFlashlightCommand:
        return "left_primary";
    default:
        return "unmapped";
    }
}

inline CoreActionValue ResolveCoreActionValue(
    const FearVrInputState& state,
    bool sampleFresh,
    std::uint32_t command) noexcept {
    if (!fearvr::IsInputStateUsable(state, sampleFresh)) {
        return {};
    }
    const bool leftActive =
        (state.activeHands & FEARVR_HAND_MASK_LEFT) != 0;
    const bool rightActive =
        (state.activeHands & FEARVR_HAND_MASK_RIGHT) != 0;
    bool active = false;
    switch (command) {
    case kCondemnedRunCommand:
        active = leftActive &&
            std::isfinite(state.squeeze[FEARVR_HAND_LEFT]) &&
            state.squeeze[FEARVR_HAND_LEFT] >=
                kCondemnedActionSqueezeThreshold;
        break;
    case kCondemnedFireCommand:
        active = rightActive &&
            std::isfinite(state.trigger[FEARVR_HAND_RIGHT]) &&
            state.trigger[FEARVR_HAND_RIGHT] >=
                kCondemnedActionTriggerThreshold;
        break;
    case kCondemnedBlockCommand:
        active = leftActive &&
            std::isfinite(state.trigger[FEARVR_HAND_LEFT]) &&
            state.trigger[FEARVR_HAND_LEFT] >=
                kCondemnedActionTriggerThreshold;
        break;
    case kCondemnedToggleMeleeCommand:
        active = rightActive &&
            (state.buttons & FEARVR_IB_RIGHT_PRIMARY) != 0;
        break;
    case kCondemnedAmmoCheckCommand:
        active = rightActive &&
            (state.buttons & FEARVR_IB_RIGHT_SECONDARY) != 0;
        break;
    case kCondemnedStunGunCommand:
        active = leftActive &&
            (state.buttons & FEARVR_IB_LEFT_STICK) != 0;
        break;
    case kCondemnedFlashlightCommand:
        active = leftActive &&
            (state.buttons & FEARVR_IB_LEFT_PRIMARY) != 0;
        break;
    default:
        break;
    }
    return {active ? 1.0F : 0.0F, active};
}

inline float MergeCoreActionWithRetail(
    float retailValue,
    const CoreActionValue& action) noexcept {
    if (!action.active || !std::isfinite(action.value) ||
        !std::isfinite(retailValue)) {
        return retailValue;
    }
    return std::fabs(retailValue) >= std::fabs(action.value)
        ? retailValue
        : action.value;
}

struct WeaponGripCalibrationControls {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
    bool positionDown{false};
    bool rotationDown{false};
    bool resetDown{false};
    bool snapshotDown{false};
    bool finerDown{false};
    bool coarserDown{false};
    bool captured{false};
};

// Both grips form an intentional setup chord. While held, the right stick
// edits local X/Y and left-stick vertical edits local Z. Face/stick buttons
// select modes and actions. Callers that inject gameplay commands use the
// same `captured` result to keep these controls from leaking into the game.
inline WeaponGripCalibrationControls
ResolveWeaponGripCalibrationControls(
    const FearVrInputState& state,
    bool sampleFresh) noexcept {
    WeaponGripCalibrationControls result{};
    if (!fearvr::IsInputStateUsable(state, sampleFresh) ||
        (state.activeHands &
         (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT)) !=
            (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT) ||
        !std::isfinite(state.squeeze[FEARVR_HAND_LEFT]) ||
        !std::isfinite(state.squeeze[FEARVR_HAND_RIGHT]) ||
        state.squeeze[FEARVR_HAND_LEFT] <
            kWeaponGripCalibrationChordThreshold ||
        state.squeeze[FEARVR_HAND_RIGHT] <
            kWeaponGripCalibrationChordThreshold) {
        return result;
    }

    result.x = fearvr::ApplyInputDeadzone(
        state.turnX, kWeaponGripCalibrationAxisDeadzone);
    result.y = fearvr::ApplyInputDeadzone(
        state.turnY, kWeaponGripCalibrationAxisDeadzone);
    result.z = fearvr::ApplyInputDeadzone(
        state.moveY, kWeaponGripCalibrationAxisDeadzone);
    result.positionDown =
        (state.buttons & FEARVR_IB_RIGHT_PRIMARY) != 0;
    result.rotationDown =
        (state.buttons & FEARVR_IB_RIGHT_SECONDARY) != 0;
    result.resetDown =
        (state.buttons & FEARVR_IB_LEFT_PRIMARY) != 0;
    result.snapshotDown =
        (state.buttons & FEARVR_IB_LEFT_SECONDARY) != 0;
    result.finerDown =
        (state.buttons & FEARVR_IB_LEFT_STICK) != 0;
    result.coarserDown =
        (state.buttons & FEARVR_IB_RIGHT_STICK) != 0;
    result.captured = true;
    return result;
}

struct ControllerAimRotation {
    fearvr::TrackingQuaternion worldRotation{};
    bool active{false};
};

struct ControllerAimWorldPose {
    fearvr::TrackingVector worldPosition{};
    fearvr::TrackingQuaternion worldRotation{};
    bool active{false};
};

struct ControllerRelativeMeleeTransform {
    fearvr::TrackingVector position{};
    fearvr::TrackingQuaternion rotation{};
    bool active{false};
};

inline bool UsableAimQuaternion(
    const fearvr::TrackingQuaternion& value) noexcept {
    const float magnitudeSquared =
        value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w;
    return fearvr::IsFinite(value) &&
        std::isfinite(magnitudeSquared) &&
        magnitudeSquared >= 0.25F && magnitudeSquared <= 4.0F;
}

// Redirects Retail's complete animation-derived melee arc from the untouched
// camera base toward the right controller. Rotating position around the
// camera pivot preserves the swing curve; rotating the node quaternion keeps
// the collision shape aligned with that redirected curve.
inline ControllerRelativeMeleeTransform
ResolveControllerRelativeMeleeTransform(
    const fearvr::TrackingVector& retailPosition,
    const fearvr::TrackingQuaternion& retailRotation,
    const fearvr::TrackingVector& cameraPivot,
    const fearvr::TrackingQuaternion& retailCameraBaseRotation,
    const fearvr::TrackingQuaternion& controllerWorldRotation,
    bool trackingFresh) noexcept {
    if (!trackingFresh || !fearvr::IsFinite(retailPosition) ||
        !fearvr::IsFinite(cameraPivot) ||
        !UsableAimQuaternion(retailRotation) ||
        !UsableAimQuaternion(retailCameraBaseRotation) ||
        !UsableAimQuaternion(controllerWorldRotation)) {
        return {};
    }
    const fearvr::TrackingQuaternion delta = fearvr::Multiply(
        controllerWorldRotation,
        fearvr::Conjugate(fearvr::Normalize(
            retailCameraBaseRotation)));
    const fearvr::TrackingVector retailOffset{
        retailPosition.x - cameraPivot.x,
        retailPosition.y - cameraPivot.y,
        retailPosition.z - cameraPivot.z};
    const fearvr::TrackingVector redirectedOffset =
        fearvr::Rotate(delta, retailOffset);
    const fearvr::TrackingVector redirectedPosition{
        cameraPivot.x + redirectedOffset.x,
        cameraPivot.y + redirectedOffset.y,
        cameraPivot.z + redirectedOffset.z};
    const fearvr::TrackingQuaternion redirectedRotation =
        fearvr::Multiply(delta, retailRotation);
    const bool active = fearvr::IsFinite(redirectedPosition) &&
        UsableAimQuaternion(redirectedRotation);
    return {redirectedPosition, redirectedRotation, active};
}

// Converts the focused weapon-hand OpenXR aim pose into the same LithTech
// world basis as the stereo camera. OpenXR -Z forward becomes LithTech +Z
// forward inside TrackedPoseRelativeToRecenter. Position is measured from
// the same untouched Retail camera transform used as the stereo world base.
inline ControllerAimWorldPose ResolveControllerWorldPose(
    const FearVrInputState& state,
    bool sampleFresh,
    const FearVrPose& trackingRecenter,
    const fearvr::TrackingVector& retailCameraPosition,
    const fearvr::TrackingQuaternion& retailBaseRotation,
    std::uint32_t poseValidHands,
    const FearVrPose& trackedPose,
    float unitsPerMeter = 100.0F) noexcept {
    const float baseMagnitudeSquared =
        retailBaseRotation.x * retailBaseRotation.x +
        retailBaseRotation.y * retailBaseRotation.y +
        retailBaseRotation.z * retailBaseRotation.z +
        retailBaseRotation.w * retailBaseRotation.w;
    if (!fearvr::IsInputStateUsable(state, sampleFresh) ||
        (state.activeHands & FEARVR_HAND_MASK_RIGHT) == 0 ||
        (poseValidHands & FEARVR_HAND_MASK_RIGHT) == 0 ||
        !fearvr::IsFinite(retailCameraPosition) ||
        !fearvr::IsFinite(retailBaseRotation) ||
        !std::isfinite(baseMagnitudeSquared) ||
        baseMagnitudeSquared < 0.25F ||
        baseMagnitudeSquared > 4.0F ||
        !std::isfinite(unitsPerMeter) ||
        unitsPerMeter <= 0.0F || unitsPerMeter > 1000.0F) {
        return {};
    }
    const fearvr::RelativeEyePose relative =
        fearvr::TrackedPoseRelativeToRecenter(
            trackingRecenter, trackedPose);
    if (!relative.valid) {
        return {};
    }
    const fearvr::TrackingQuaternion baseRotation = fearvr::Normalize(
        retailBaseRotation);
    const fearvr::TrackingVector worldOffset = fearvr::Rotate(
        baseRotation,
        {relative.positionMeters.x * unitsPerMeter,
         relative.positionMeters.y * unitsPerMeter,
         relative.positionMeters.z * unitsPerMeter});
    const fearvr::TrackingVector worldPosition{
        retailCameraPosition.x + worldOffset.x,
        retailCameraPosition.y + worldOffset.y,
        retailCameraPosition.z + worldOffset.z};
    const fearvr::TrackingQuaternion worldRotation = fearvr::Multiply(
        baseRotation, relative.rotation);
    const bool active = fearvr::IsFinite(worldPosition) &&
        fearvr::IsFinite(worldRotation);
    return {worldPosition, worldRotation, active};
}

inline ControllerAimWorldPose ResolveControllerAimWorldPose(
    const FearVrInputState& state,
    bool sampleFresh,
    const FearVrPose& trackingRecenter,
    const fearvr::TrackingVector& retailCameraPosition,
    const fearvr::TrackingQuaternion& retailBaseRotation,
    float unitsPerMeter = 100.0F) noexcept {
    return ResolveControllerWorldPose(
        state, sampleFresh, trackingRecenter,
        retailCameraPosition, retailBaseRotation,
        state.aimPoseValidHands,
        state.handAimPose[FEARVR_HAND_RIGHT], unitsPerMeter);
}

// The OpenXR grip pose is the physical hand/controller attachment point.
// Keep it separate from the aim pose: ranged direction follows aim, while
// held model position and melee kinematics originate at the actual grip.
inline ControllerAimWorldPose ResolveControllerGripWorldPose(
    const FearVrInputState& state,
    bool sampleFresh,
    const FearVrPose& trackingRecenter,
    const fearvr::TrackingVector& retailCameraPosition,
    const fearvr::TrackingQuaternion& retailBaseRotation,
    float unitsPerMeter = 100.0F) noexcept {
    return ResolveControllerWorldPose(
        state, sampleFresh, trackingRecenter,
        retailCameraPosition, retailBaseRotation,
        state.gripPoseValidHands,
        state.handGripPose[FEARVR_HAND_RIGHT], unitsPerMeter);
}

inline ControllerAimRotation ResolveControllerAimRotation(
    const FearVrInputState& state,
    bool sampleFresh,
    const FearVrPose& trackingRecenter,
    const fearvr::TrackingQuaternion& retailBaseRotation) noexcept {
    const ControllerAimWorldPose pose = ResolveControllerAimWorldPose(
        state, sampleFresh, trackingRecenter, {},
        retailBaseRotation, 1.0F);
    return {pose.worldRotation, pose.active};
}

struct CoreActionHapticPulse {
    std::uint64_t durationNs{0};
    float amplitude{0.0F};
    std::uint32_t handMask{0};
    bool active{false};
};

// This M4 gate confirms only controller-command acceptance. Weapon recoil
// belongs to a later verified shot-event hook so dry fire and automatic fire
// are not represented incorrectly.
inline CoreActionHapticPulse ResolveCoreActionHapticPulse(
    std::uint32_t command) noexcept {
    switch (command) {
    case kCondemnedFireCommand:
        return {35'000'000ULL, 0.25F, FEARVR_HAND_MASK_RIGHT, true};
    case kCondemnedBlockCommand:
        return {25'000'000ULL, 0.18F, FEARVR_HAND_MASK_LEFT, true};
    case kCondemnedActivateCommand:
        return {20'000'000ULL, 0.15F, FEARVR_HAND_MASK_RIGHT, true};
    default:
        return {};
    }
}

struct RecenterLatch {
    bool releaseRequired{true};
    bool wasDown{false};
};

inline bool ConsumeRecenterPress(
    RecenterLatch& latch,
    const FearVrInputState& state,
    bool sampleFresh) noexcept {
    const bool usable =
        fearvr::IsInputStateUsable(state, sampleFresh) &&
        (state.activeHands & FEARVR_HAND_MASK_RIGHT) != 0;
    if (!usable) {
        latch.releaseRequired = true;
        latch.wasDown = false;
        return false;
    }

    const bool down =
        (state.buttons & FEARVR_IB_RIGHT_STICK) != 0;
    if (!down) {
        latch.releaseRequired = false;
        latch.wasDown = false;
        return false;
    }
    if (latch.releaseRequired || latch.wasDown) {
        latch.wasDown = true;
        return false;
    }

    latch.wasDown = true;
    return true;
}

struct MenuToggleLatch {
    bool releaseRequired{true};
    bool wasDown{false};
};

inline bool ConsumeMenuTogglePress(
    MenuToggleLatch& latch,
    const FearVrInputState& state,
    bool sampleFresh) noexcept {
    const bool usable =
        fearvr::IsInputStateUsable(state, sampleFresh) &&
        (state.activeHands & FEARVR_HAND_MASK_LEFT) != 0;
    if (!usable) {
        latch.releaseRequired = true;
        latch.wasDown = false;
        return false;
    }

    const bool down =
        (state.buttons & FEARVR_IB_LEFT_SECONDARY) != 0;
    if (!down) {
        latch.releaseRequired = false;
        latch.wasDown = false;
        return false;
    }
    if (latch.releaseRequired || latch.wasDown) {
        latch.wasDown = true;
        return false;
    }

    latch.wasDown = true;
    return true;
}

} // namespace condemnedvr
