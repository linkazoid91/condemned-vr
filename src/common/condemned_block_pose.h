#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "head_tracking_math.h"

namespace condemnedvr {

constexpr float kPhysicalMeleeBlockDefaultPositionToleranceMeters = 0.18F;
constexpr float kPhysicalMeleeBlockDefaultAngleToleranceDegrees = 25.0F;
constexpr float kPhysicalMeleeBlockPositionReleaseMarginMeters = 0.06F;
constexpr float kPhysicalMeleeBlockAngleReleaseMarginDegrees = 10.0F;
constexpr float kPhysicalMeleeBlockMinimumPositionToleranceMeters = 0.05F;
constexpr float kPhysicalMeleeBlockMaximumPositionToleranceMeters = 0.50F;
constexpr float kPhysicalMeleeBlockMinimumAngleToleranceDegrees = 5.0F;
constexpr float kPhysicalMeleeBlockMaximumAngleToleranceDegrees = 90.0F;
constexpr float kPhysicalMeleeBlockMaximumHeadRelativeDistanceMeters = 2.50F;

struct PhysicalMeleeBlockWorldPose {
    fearvr::TrackingVector positionUnits{};
    fearvr::TrackingQuaternion rotation{};
};

struct PhysicalMeleeBlockPoseSettings {
    fearvr::TrackingVector headRelativePositionMeters{};
    fearvr::TrackingQuaternion headRelativeRotation{
        0.0F, 0.0F, 0.0F, 1.0F};
    float positionToleranceMeters{
        kPhysicalMeleeBlockDefaultPositionToleranceMeters};
    float angleToleranceDegrees{
        kPhysicalMeleeBlockDefaultAngleToleranceDegrees};
    bool enabled{false};
    bool captured{false};
};

enum class PhysicalMeleeBlockPoseReason : std::uint8_t {
    None,
    Matched,
    ContextInactive,
    Disabled,
    NotCaptured,
    InvalidSettings,
    InvalidPose,
    InvalidWorldScale,
    PositionOutside,
    AngleOutside,
    PositionAndAngleOutside
};

inline const char* PhysicalMeleeBlockPoseReasonName(
    PhysicalMeleeBlockPoseReason reason) noexcept {
    switch (reason) {
    case PhysicalMeleeBlockPoseReason::None:
        return "none";
    case PhysicalMeleeBlockPoseReason::Matched:
        return "matched";
    case PhysicalMeleeBlockPoseReason::ContextInactive:
        return "context_inactive";
    case PhysicalMeleeBlockPoseReason::Disabled:
        return "disabled";
    case PhysicalMeleeBlockPoseReason::NotCaptured:
        return "not_captured";
    case PhysicalMeleeBlockPoseReason::InvalidSettings:
        return "invalid_settings";
    case PhysicalMeleeBlockPoseReason::InvalidPose:
        return "invalid_pose";
    case PhysicalMeleeBlockPoseReason::InvalidWorldScale:
        return "invalid_world_scale";
    case PhysicalMeleeBlockPoseReason::PositionOutside:
        return "position_outside";
    case PhysicalMeleeBlockPoseReason::AngleOutside:
        return "angle_outside";
    case PhysicalMeleeBlockPoseReason::PositionAndAngleOutside:
        return "position_and_angle_outside";
    default:
        return "unknown";
    }
}

struct PhysicalMeleeBlockPoseState {
    bool active{false};
};

struct PhysicalMeleeBlockPoseResult {
    float positionErrorMeters{0.0F};
    float angleErrorDegrees{0.0F};
    PhysicalMeleeBlockPoseReason reason{
        PhysicalMeleeBlockPoseReason::None};
    bool poseValid{false};
    bool active{false};
    bool entered{false};
    bool exited{false};
};

// Tracks an automatic pose-started block across its exit. Retail command 28
// has no CommandOff handler, and live evidence rejected a second CS_Block
// stimulus as an authoritative release path. Retail therefore owns its finite
// block window; this lifecycle only preserves same-weapon exit diagnostics.
// Manual/Retail input always takes ownership.
struct PhysicalMeleeBlockNativeLifecycleState {
    std::int32_t weaponIndex{-1};
    std::uintptr_t weaponToken{0U};
    bool automaticOwned{false};
    bool releasePending{false};
};

struct PhysicalMeleeBlockNativeLifecycleTransition {
    bool ownershipAcquired{false};
    bool ownershipRelinquished{false};
    bool releaseQueued{false};
    bool pendingReleaseCleared{false};
};

enum class PhysicalMeleeBlockNativeReleaseDecision : std::uint8_t {
    None,
    WaitForWeapon,
    Dispatch,
    DropWeaponChanged
};

inline PhysicalMeleeBlockNativeLifecycleTransition
ObservePhysicalMeleeBlockNativeLifecycle(
    const PhysicalMeleeBlockPoseResult& pose,
    bool manualControllerActive,
    bool retailBindingActive,
    std::int32_t weaponIndex,
    std::uintptr_t weaponToken,
    PhysicalMeleeBlockNativeLifecycleState& state) noexcept {
    PhysicalMeleeBlockNativeLifecycleTransition transition{};
    const bool manualOwner =
        manualControllerActive || retailBindingActive;

    if (state.releasePending && (pose.active || manualOwner)) {
        state.releasePending = false;
        transition.pendingReleaseCleared = true;
    }

    if (pose.entered) {
        state.automaticOwned = false;
        state.weaponIndex = -1;
        state.weaponToken = 0U;
        if (!manualOwner && weaponIndex >= 0 && weaponToken != 0U) {
            state.automaticOwned = true;
            state.weaponIndex = weaponIndex;
            state.weaponToken = weaponToken;
            transition.ownershipAcquired = true;
        }
    }

    if (pose.exited && state.automaticOwned) {
        state.automaticOwned = false;
        transition.ownershipRelinquished = true;
        if (!manualOwner) {
            state.releasePending = true;
            transition.releaseQueued = true;
        } else {
            state.weaponIndex = -1;
            state.weaponToken = 0U;
        }
    } else if (state.automaticOwned && manualOwner) {
        state.automaticOwned = false;
        state.weaponIndex = -1;
        state.weaponToken = 0U;
        transition.ownershipRelinquished = true;
    }

    return transition;
}

inline PhysicalMeleeBlockNativeReleaseDecision
ConsumePhysicalMeleeBlockNativeRelease(
    bool currentWeaponReadable,
    std::int32_t currentWeaponIndex,
    std::uintptr_t currentWeaponToken,
    PhysicalMeleeBlockNativeLifecycleState& state) noexcept {
    if (!state.releasePending) {
        return PhysicalMeleeBlockNativeReleaseDecision::None;
    }
    if (!currentWeaponReadable || currentWeaponToken == 0U) {
        return PhysicalMeleeBlockNativeReleaseDecision::WaitForWeapon;
    }
    if (currentWeaponIndex != state.weaponIndex ||
        currentWeaponToken != state.weaponToken) {
        state = {};
        return PhysicalMeleeBlockNativeReleaseDecision::DropWeaponChanged;
    }
    state = {};
    return PhysicalMeleeBlockNativeReleaseDecision::Dispatch;
}

inline bool PhysicalMeleeBlockQuaternionIsValid(
    const fearvr::TrackingQuaternion& rotation) noexcept {
    if (!fearvr::IsFinite(rotation)) {
        return false;
    }
    const float magnitudeSquared =
        rotation.x * rotation.x + rotation.y * rotation.y +
        rotation.z * rotation.z + rotation.w * rotation.w;
    return std::isfinite(magnitudeSquared) &&
        magnitudeSquared >= 0.25F && magnitudeSquared <= 4.0F;
}

inline bool PhysicalMeleeBlockPoseSettingsAreValid(
    const PhysicalMeleeBlockPoseSettings& settings) noexcept {
    if (!fearvr::IsFinite(settings.headRelativePositionMeters) ||
        !PhysicalMeleeBlockQuaternionIsValid(
            settings.headRelativeRotation) ||
        !std::isfinite(settings.positionToleranceMeters) ||
        !std::isfinite(settings.angleToleranceDegrees) ||
        settings.positionToleranceMeters <
            kPhysicalMeleeBlockMinimumPositionToleranceMeters ||
        settings.positionToleranceMeters >
            kPhysicalMeleeBlockMaximumPositionToleranceMeters ||
        settings.angleToleranceDegrees <
            kPhysicalMeleeBlockMinimumAngleToleranceDegrees ||
        settings.angleToleranceDegrees >
            kPhysicalMeleeBlockMaximumAngleToleranceDegrees ||
        (settings.enabled && !settings.captured)) {
        return false;
    }
    const float distanceSquared =
        settings.headRelativePositionMeters.x *
            settings.headRelativePositionMeters.x +
        settings.headRelativePositionMeters.y *
            settings.headRelativePositionMeters.y +
        settings.headRelativePositionMeters.z *
            settings.headRelativePositionMeters.z;
    return std::isfinite(distanceSquared) &&
        distanceSquared <=
            kPhysicalMeleeBlockMaximumHeadRelativeDistanceMeters *
            kPhysicalMeleeBlockMaximumHeadRelativeDistanceMeters;
}

inline bool PhysicalMeleeBlockWorldPoseIsValid(
    const PhysicalMeleeBlockWorldPose& pose) noexcept {
    return fearvr::IsFinite(pose.positionUnits) &&
        PhysicalMeleeBlockQuaternionIsValid(pose.rotation);
}

inline bool ResolvePhysicalMeleeBlockHeadYaw(
    const PhysicalMeleeBlockWorldPose& head,
    fearvr::TrackingQuaternion& yawRotation) noexcept {
    yawRotation = {};
    if (!PhysicalMeleeBlockWorldPoseIsValid(head)) {
        return false;
    }
    const fearvr::TrackingVector forward = fearvr::Rotate(
        fearvr::Normalize(head.rotation), {0.0F, 0.0F, 1.0F});
    const float horizontalLengthSquared =
        forward.x * forward.x + forward.z * forward.z;
    if (!fearvr::IsFinite(forward) ||
        !std::isfinite(horizontalLengthSquared) ||
        horizontalLengthSquared < 0.0001F) {
        return false;
    }
    const float yaw = std::atan2(forward.x, forward.z);
    const float halfYaw = yaw * 0.5F;
    yawRotation = fearvr::Normalize(
        {0.0F, std::sin(halfYaw), 0.0F, std::cos(halfYaw)});
    return PhysicalMeleeBlockQuaternionIsValid(yawRotation);
}

inline bool ResolvePhysicalMeleeHeadRelativeBlockPose(
    const PhysicalMeleeBlockWorldPose& head,
    const PhysicalMeleeBlockWorldPose& weapon,
    float unitsPerMeter,
    fearvr::TrackingVector& relativePositionMeters,
    fearvr::TrackingQuaternion& relativeRotation) noexcept {
    relativePositionMeters = {};
    relativeRotation = {};
    if (!PhysicalMeleeBlockWorldPoseIsValid(head) ||
        !PhysicalMeleeBlockWorldPoseIsValid(weapon) ||
        !std::isfinite(unitsPerMeter) || unitsPerMeter <= 0.0F) {
        return false;
    }
    fearvr::TrackingQuaternion headYaw{};
    if (!ResolvePhysicalMeleeBlockHeadYaw(head, headYaw)) {
        return false;
    }
    const fearvr::TrackingVector worldDelta{
        weapon.positionUnits.x - head.positionUnits.x,
        weapon.positionUnits.y - head.positionUnits.y,
        weapon.positionUnits.z - head.positionUnits.z};
    const fearvr::TrackingVector localUnits = fearvr::Rotate(
        fearvr::Conjugate(headYaw), worldDelta);
    relativePositionMeters = {
        localUnits.x / unitsPerMeter,
        localUnits.y / unitsPerMeter,
        localUnits.z / unitsPerMeter};
    relativeRotation = fearvr::Multiply(
        fearvr::Conjugate(headYaw),
        fearvr::Normalize(weapon.rotation));
    return fearvr::IsFinite(relativePositionMeters) &&
        PhysicalMeleeBlockQuaternionIsValid(relativeRotation);
}

inline bool CapturePhysicalMeleeBlockPose(
    const PhysicalMeleeBlockWorldPose& head,
    const PhysicalMeleeBlockWorldPose& weapon,
    float unitsPerMeter,
    PhysicalMeleeBlockPoseSettings& settings) noexcept {
    fearvr::TrackingVector relativePositionMeters{};
    fearvr::TrackingQuaternion relativeRotation{};
    if (!ResolvePhysicalMeleeHeadRelativeBlockPose(
            head, weapon, unitsPerMeter,
            relativePositionMeters, relativeRotation)) {
        return false;
    }
    const float positionTolerance =
        std::isfinite(settings.positionToleranceMeters) &&
            settings.positionToleranceMeters >=
                kPhysicalMeleeBlockMinimumPositionToleranceMeters &&
            settings.positionToleranceMeters <=
                kPhysicalMeleeBlockMaximumPositionToleranceMeters
        ? settings.positionToleranceMeters
        : kPhysicalMeleeBlockDefaultPositionToleranceMeters;
    const float angleTolerance =
        std::isfinite(settings.angleToleranceDegrees) &&
            settings.angleToleranceDegrees >=
                kPhysicalMeleeBlockMinimumAngleToleranceDegrees &&
            settings.angleToleranceDegrees <=
                kPhysicalMeleeBlockMaximumAngleToleranceDegrees
        ? settings.angleToleranceDegrees
        : kPhysicalMeleeBlockDefaultAngleToleranceDegrees;
    settings = {};
    settings.headRelativePositionMeters = relativePositionMeters;
    settings.headRelativeRotation = fearvr::Normalize(relativeRotation);
    settings.positionToleranceMeters = positionTolerance;
    settings.angleToleranceDegrees = angleTolerance;
    settings.enabled = true;
    settings.captured = true;
    return PhysicalMeleeBlockPoseSettingsAreValid(settings);
}

inline float PhysicalMeleeBlockQuaternionAngleDegrees(
    const fearvr::TrackingQuaternion& left,
    const fearvr::TrackingQuaternion& right) noexcept {
    if (!PhysicalMeleeBlockQuaternionIsValid(left) ||
        !PhysicalMeleeBlockQuaternionIsValid(right)) {
        return 180.0F;
    }
    constexpr float kRadiansToDegrees = 57.29577951308232F;
    const float dot = std::clamp(
        std::fabs(fearvr::Dot(
            fearvr::Normalize(left), fearvr::Normalize(right))),
        0.0F, 1.0F);
    return 2.0F * std::acos(dot) * kRadiansToDegrees;
}

inline PhysicalMeleeBlockPoseResult EvaluatePhysicalMeleeBlockPose(
    const PhysicalMeleeBlockPoseSettings& settings,
    const PhysicalMeleeBlockWorldPose& head,
    const PhysicalMeleeBlockWorldPose& weapon,
    float unitsPerMeter,
    bool contextEligible,
    PhysicalMeleeBlockPoseState& state) noexcept {
    PhysicalMeleeBlockPoseResult result{};
    const bool wasActive = state.active;
    const auto Deactivate = [&](PhysicalMeleeBlockPoseReason reason) {
        state.active = false;
        result.reason = reason;
        result.exited = wasActive;
        return result;
    };
    if (!contextEligible) {
        return Deactivate(PhysicalMeleeBlockPoseReason::ContextInactive);
    }
    if (!PhysicalMeleeBlockPoseSettingsAreValid(settings)) {
        return Deactivate(PhysicalMeleeBlockPoseReason::InvalidSettings);
    }
    if (!settings.captured) {
        return Deactivate(PhysicalMeleeBlockPoseReason::NotCaptured);
    }
    if (!settings.enabled) {
        return Deactivate(PhysicalMeleeBlockPoseReason::Disabled);
    }
    if (!std::isfinite(unitsPerMeter) || unitsPerMeter <= 0.0F) {
        return Deactivate(PhysicalMeleeBlockPoseReason::InvalidWorldScale);
    }
    fearvr::TrackingVector relativePositionMeters{};
    fearvr::TrackingQuaternion relativeRotation{};
    if (!ResolvePhysicalMeleeHeadRelativeBlockPose(
            head, weapon, unitsPerMeter,
            relativePositionMeters, relativeRotation)) {
        return Deactivate(PhysicalMeleeBlockPoseReason::InvalidPose);
    }
    result.poseValid = true;
    const float dx = relativePositionMeters.x -
        settings.headRelativePositionMeters.x;
    const float dy = relativePositionMeters.y -
        settings.headRelativePositionMeters.y;
    const float dz = relativePositionMeters.z -
        settings.headRelativePositionMeters.z;
    result.positionErrorMeters = std::sqrt(dx * dx + dy * dy + dz * dz);
    result.angleErrorDegrees =
        PhysicalMeleeBlockQuaternionAngleDegrees(
            settings.headRelativeRotation, relativeRotation);
    const float positionLimit = settings.positionToleranceMeters +
        (wasActive ? kPhysicalMeleeBlockPositionReleaseMarginMeters : 0.0F);
    const float angleLimit = settings.angleToleranceDegrees +
        (wasActive ? kPhysicalMeleeBlockAngleReleaseMarginDegrees : 0.0F);
    const bool positionInside =
        result.positionErrorMeters <= positionLimit;
    const bool angleInside = result.angleErrorDegrees <= angleLimit;
    state.active = positionInside && angleInside;
    result.active = state.active;
    result.entered = state.active && !wasActive;
    result.exited = !state.active && wasActive;
    result.reason = state.active
        ? PhysicalMeleeBlockPoseReason::Matched
        : !positionInside && !angleInside
            ? PhysicalMeleeBlockPoseReason::PositionAndAngleOutside
            : !positionInside
                ? PhysicalMeleeBlockPoseReason::PositionOutside
                : PhysicalMeleeBlockPoseReason::AngleOutside;
    return result;
}

} // namespace condemnedvr
