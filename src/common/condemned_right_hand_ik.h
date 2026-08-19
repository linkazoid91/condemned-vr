#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "condemned_physical_melee.h"

namespace condemnedvr {

// Stable Retail catalog identity. Unarmed has no held model and therefore
// uses the controller grip frame directly instead of the held-weapon frame.
constexpr std::int32_t kCondemnedUnarmedWeaponIndex = 61;

enum class RightHandIkTargetSource : std::uint8_t {
    Invalid,
    EmptyGrip,
    WeaponWeightedAim
};

inline const char* RightHandIkTargetSourceName(
    RightHandIkTargetSource source) noexcept {
    switch (source) {
    case RightHandIkTargetSource::EmptyGrip:
        return "empty_grip";
    case RightHandIkTargetSource::WeaponWeightedAim:
        return "weapon_weighted_aim";
    case RightHandIkTargetSource::Invalid:
    default:
        return "invalid";
    }
}

inline bool RightHandIkRotationIsValid(
    const fearvr::TrackingQuaternion& rotation) noexcept {
    const float lengthSquared =
        rotation.x * rotation.x + rotation.y * rotation.y +
        rotation.z * rotation.z + rotation.w * rotation.w;
    return fearvr::IsFinite(rotation) &&
        std::isfinite(lengthSquared) &&
        lengthSquared >= 0.25F && lengthSquared <= 4.0F;
}

// Global controller-local correction for the unarmed dominant hand. Unlike
// ToolMenuRightHandIkSettings, this is deliberately not keyed by a Retail
// weapon index: an empty hand has no lifetime-validated held model identity.
struct EmptyRightHandAlignmentSettings {
    fearvr::TrackingVector localPositionOffsetUnits{};
    fearvr::TrackingQuaternion localRotationOffset{
        0.0F, 0.0F, 0.0F, 1.0F};
};

constexpr float kEmptyRightHandMaximumPositionOffsetUnits = 100.0F;

inline bool EmptyRightHandAlignmentSettingsAreValid(
    const EmptyRightHandAlignmentSettings& settings) noexcept {
    const float positionLengthSquared =
        settings.localPositionOffsetUnits.x *
            settings.localPositionOffsetUnits.x +
        settings.localPositionOffsetUnits.y *
            settings.localPositionOffsetUnits.y +
        settings.localPositionOffsetUnits.z *
            settings.localPositionOffsetUnits.z;
    return fearvr::IsFinite(settings.localPositionOffsetUnits) &&
        RightHandIkRotationIsValid(settings.localRotationOffset) &&
        std::fabs(settings.localPositionOffsetUnits.x) <=
            kEmptyRightHandMaximumPositionOffsetUnits &&
        std::fabs(settings.localPositionOffsetUnits.y) <=
            kEmptyRightHandMaximumPositionOffsetUnits &&
        std::fabs(settings.localPositionOffsetUnits.z) <=
            kEmptyRightHandMaximumPositionOffsetUnits &&
        std::isfinite(positionLengthSquared) &&
        positionLengthSquared <=
            kEmptyRightHandMaximumPositionOffsetUnits *
                kEmptyRightHandMaximumPositionOffsetUnits;
}

inline PhysicalMeleeRigidTransform ResolveEmptyRightHandAlignmentTarget(
    const PhysicalMeleeRigidTransform& rawGripTarget,
    const EmptyRightHandAlignmentSettings& settings) noexcept {
    if (!PhysicalMeleeRigidTransformIsValid(rawGripTarget) ||
        !EmptyRightHandAlignmentSettingsAreValid(settings)) {
        return {{}, {0.0F, 0.0F, 0.0F, 0.0F}};
    }
    const fearvr::TrackingQuaternion gripRotation =
        fearvr::Normalize(rawGripTarget.rotation);
    const fearvr::TrackingVector worldOffset =
        fearvr::Rotate(
            gripRotation, settings.localPositionOffsetUnits);
    return {
        {
            rawGripTarget.positionUnits.x + worldOffset.x,
            rawGripTarget.positionUnits.y + worldOffset.y,
            rawGripTarget.positionUnits.z + worldOffset.z,
        },
        fearvr::Multiply(
            gripRotation,
            fearvr::Normalize(settings.localRotationOffset))};
}

// Solves C in controllerPose * C = referenceHandPose. The first captured pose
// is the already-displayed, visually correct hand. The second is the raw grip
// pose after the player moves the physical controller to where it should sit.
inline bool SolveEmptyRightHandAlignment(
    const PhysicalMeleeRigidTransform& referenceHandPose,
    const PhysicalMeleeRigidTransform& controllerPose,
    EmptyRightHandAlignmentSettings& settings) noexcept {
    if (!PhysicalMeleeRigidTransformIsValid(referenceHandPose) ||
        !PhysicalMeleeRigidTransformIsValid(controllerPose)) {
        return false;
    }
    const fearvr::TrackingQuaternion controllerRotation =
        fearvr::Normalize(controllerPose.rotation);
    const fearvr::TrackingQuaternion controllerInverse =
        fearvr::Conjugate(controllerRotation);
    const fearvr::TrackingVector worldDelta{
        referenceHandPose.positionUnits.x -
            controllerPose.positionUnits.x,
        referenceHandPose.positionUnits.y -
            controllerPose.positionUnits.y,
        referenceHandPose.positionUnits.z -
            controllerPose.positionUnits.z};
    EmptyRightHandAlignmentSettings solved{};
    solved.localPositionOffsetUnits =
        fearvr::Rotate(controllerInverse, worldDelta);
    solved.localRotationOffset = fearvr::Multiply(
        controllerInverse,
        fearvr::Normalize(referenceHandPose.rotation));
    if (!EmptyRightHandAlignmentSettingsAreValid(solved)) {
        return false;
    }
    settings = solved;
    return true;
}

enum class EmptyRightHandAlignmentPhase : std::uint8_t {
    Idle,
    AwaitReferencePose,
    AwaitControllerPose
};

inline const char* EmptyRightHandAlignmentPhaseName(
    EmptyRightHandAlignmentPhase phase) noexcept {
    switch (phase) {
    case EmptyRightHandAlignmentPhase::AwaitReferencePose:
        return "await_reference_pose";
    case EmptyRightHandAlignmentPhase::AwaitControllerPose:
        return "await_controller_pose";
    case EmptyRightHandAlignmentPhase::Idle:
    default:
        return "idle";
    }
}

enum class EmptyRightHandAlignmentEvent : std::uint8_t {
    None,
    ReferenceCaptured,
    Completed,
    PoseUnavailable,
    SolveRejected
};

struct EmptyRightHandAlignmentState {
    PhysicalMeleeRigidTransform referenceHandPose{};
    EmptyRightHandAlignmentPhase phase{
        EmptyRightHandAlignmentPhase::Idle};
    bool triggerReleaseRequired{true};
    bool triggerWasDown{false};
};

struct EmptyRightHandAlignmentUpdateResult {
    EmptyRightHandAlignmentSettings settings{};
    EmptyRightHandAlignmentEvent event{
        EmptyRightHandAlignmentEvent::None};
};

inline bool EmptyRightHandAlignmentIsActive(
    const EmptyRightHandAlignmentState& state) noexcept {
    return state.phase != EmptyRightHandAlignmentPhase::Idle;
}

inline void BeginEmptyRightHandAlignment(
    EmptyRightHandAlignmentState& state) noexcept {
    state = {};
    state.phase = EmptyRightHandAlignmentPhase::AwaitReferencePose;
    state.triggerReleaseRequired = true;
}

inline bool CancelEmptyRightHandAlignment(
    EmptyRightHandAlignmentState& state) noexcept {
    const bool wasActive = EmptyRightHandAlignmentIsActive(state);
    state = {};
    return wasActive;
}

// A newly started mode always requires a released trigger before capture.
// Each accepted or rejected pull is then release-gated before another pull.
inline EmptyRightHandAlignmentUpdateResult
UpdateEmptyRightHandAlignment(
    EmptyRightHandAlignmentState& state,
    const PhysicalMeleeRigidTransform& rawControllerPose,
    const PhysicalMeleeRigidTransform& displayedHandPose,
    bool poseFresh,
    bool triggerDown) noexcept {
    EmptyRightHandAlignmentUpdateResult result{};
    if (!EmptyRightHandAlignmentIsActive(state)) {
        return result;
    }
    if (!poseFresh ||
        !PhysicalMeleeRigidTransformIsValid(rawControllerPose) ||
        !PhysicalMeleeRigidTransformIsValid(displayedHandPose)) {
        state.triggerReleaseRequired = true;
        state.triggerWasDown = triggerDown;
        if (triggerDown) {
            result.event =
                EmptyRightHandAlignmentEvent::PoseUnavailable;
        }
        return result;
    }
    if (!triggerDown) {
        state.triggerReleaseRequired = false;
        state.triggerWasDown = false;
        return result;
    }
    if (state.triggerReleaseRequired || state.triggerWasDown) {
        state.triggerWasDown = true;
        return result;
    }
    state.triggerWasDown = true;
    state.triggerReleaseRequired = true;
    if (state.phase ==
        EmptyRightHandAlignmentPhase::AwaitReferencePose) {
        state.referenceHandPose = displayedHandPose;
        state.phase =
            EmptyRightHandAlignmentPhase::AwaitControllerPose;
        result.event =
            EmptyRightHandAlignmentEvent::ReferenceCaptured;
        return result;
    }
    if (!SolveEmptyRightHandAlignment(
            state.referenceHandPose,
            rawControllerPose,
            result.settings)) {
        result.event = EmptyRightHandAlignmentEvent::SolveRejected;
        return result;
    }
    state = {};
    result.event = EmptyRightHandAlignmentEvent::Completed;
    return result;
}

// A held item has two local transforms driven from the same controller basis:
// model grip G in O = D * inverse(G), and hand correction C in H = D * C.
// Their fixed model-to-hand attachment is G * C, so the hand can be treated as
// the parent frame without changing the existing controller-driven renderer.
struct HeldObjectAlignmentSolution {
    PhysicalMeleeRigidTransform modelLocalGrip{};
    EmptyRightHandAlignmentSettings rightHandAlignment{};
};

inline bool SolveHeldObjectAlignment(
    const PhysicalMeleeRigidTransform& referenceObjectWorld,
    const PhysicalMeleeRigidTransform& desiredHandWorld,
    const PhysicalMeleeRigidTransform& desiredControllerPose,
    HeldObjectAlignmentSolution& solution) noexcept {
    HeldObjectAlignmentSolution solved{};
    if (!SolvePhysicalMeleeModelLocalGrip(
            referenceObjectWorld, desiredControllerPose,
            solved.modelLocalGrip) ||
        !SolveEmptyRightHandAlignment(
            desiredHandWorld, desiredControllerPose,
            solved.rightHandAlignment)) {
        return false;
    }
    solution = solved;
    return true;
}

// Keeps the model-to-hand attachment A = G * C fixed while a tool-menu edit
// changes the controller-local hand correction. The resulting G makes both
// the hand and held model move together under the same controller pose.
inline bool ResolveHandParentedModelLocalGrip(
    const PhysicalMeleeRigidTransform& currentModelLocalGrip,
    const EmptyRightHandAlignmentSettings& currentHandAlignment,
    const EmptyRightHandAlignmentSettings& nextHandAlignment,
    PhysicalMeleeRigidTransform& nextModelLocalGrip) noexcept {
    if (!PhysicalMeleeRigidTransformIsValid(currentModelLocalGrip) ||
        !EmptyRightHandAlignmentSettingsAreValid(
            currentHandAlignment) ||
        !EmptyRightHandAlignmentSettingsAreValid(
            nextHandAlignment)) {
        return false;
    }
    const PhysicalMeleeRigidTransform currentHandLocal{
        currentHandAlignment.localPositionOffsetUnits,
        currentHandAlignment.localRotationOffset};
    const PhysicalMeleeRigidTransform nextHandLocal{
        nextHandAlignment.localPositionOffsetUnits,
        nextHandAlignment.localRotationOffset};
    PhysicalMeleeRigidTransform attachmentInModel{};
    PhysicalMeleeRigidTransform nextHandInverse{};
    PhysicalMeleeRigidTransform solved{};
    if (!ComposePhysicalMeleeRigidTransforms(
            currentModelLocalGrip, currentHandLocal,
            attachmentInModel) ||
        !InvertPhysicalMeleeRigidTransform(
            nextHandLocal, nextHandInverse) ||
        !ComposePhysicalMeleeRigidTransforms(
            attachmentInModel, nextHandInverse, solved) ||
        PhysicalMeleeLength(solved.positionUnits) > 300.0F) {
        return false;
    }
    nextModelLocalGrip = solved;
    return true;
}

// Aligns the complete held assembly to the current physical controller while
// preserving the authored model-to-hand attachment A = G * C. The hand target
// comes from the raw grip pose plus the global empty-hand correction; the
// model continues to use the hybrid driver D (grip position, aim rotation).
inline bool SolveHandParentedHeldAssemblyControllerAlignment(
    const PhysicalMeleeRigidTransform& currentModelLocalGrip,
    const EmptyRightHandAlignmentSettings& currentHandAlignment,
    const PhysicalMeleeRigidTransform& rawGripWorld,
    const PhysicalMeleeRigidTransform& controllerDriverWorld,
    const EmptyRightHandAlignmentSettings& emptyHandAlignment,
    HeldObjectAlignmentSolution& solution) noexcept {
    if (!PhysicalMeleeRigidTransformIsValid(rawGripWorld) ||
        !PhysicalMeleeRigidTransformIsValid(controllerDriverWorld) ||
        !EmptyRightHandAlignmentSettingsAreValid(emptyHandAlignment)) {
        return false;
    }
    const PhysicalMeleeRigidTransform desiredHandWorld =
        ResolveEmptyRightHandAlignmentTarget(
            rawGripWorld, emptyHandAlignment);
    HeldObjectAlignmentSolution solved{};
    if (!PhysicalMeleeRigidTransformIsValid(desiredHandWorld) ||
        !SolveEmptyRightHandAlignment(
            desiredHandWorld, controllerDriverWorld,
            solved.rightHandAlignment) ||
        !ResolveHandParentedModelLocalGrip(
            currentModelLocalGrip, currentHandAlignment,
            solved.rightHandAlignment, solved.modelLocalGrip)) {
        return false;
    }
    solution = solved;
    return true;
}

enum class HeldObjectAlignmentPhase : std::uint8_t {
    Idle,
    AwaitReferencePoses,
    AwaitControllerPose
};

inline const char* HeldObjectAlignmentPhaseName(
    HeldObjectAlignmentPhase phase) noexcept {
    switch (phase) {
    case HeldObjectAlignmentPhase::AwaitReferencePoses:
        return "await_reference_poses";
    case HeldObjectAlignmentPhase::AwaitControllerPose:
        return "await_controller_pose";
    case HeldObjectAlignmentPhase::Idle:
    default:
        return "idle";
    }
}

enum class HeldObjectAlignmentEvent : std::uint8_t {
    None,
    ReferenceCaptured,
    Completed,
    PoseUnavailable,
    SourceChanged,
    SolveRejected
};

struct HeldObjectAlignmentState {
    PhysicalMeleeRigidTransform referenceObjectWorld{};
    PhysicalMeleeRigidTransform referenceHandWorld{};
    std::int32_t weaponIndex{-1};
    std::uint64_t sourceGeneration{0};
    HeldObjectAlignmentPhase phase{HeldObjectAlignmentPhase::Idle};
    bool triggerReleaseRequired{true};
    bool triggerWasDown{false};
};

struct HeldObjectAlignmentUpdateResult {
    HeldObjectAlignmentSolution solution{};
    HeldObjectAlignmentEvent event{HeldObjectAlignmentEvent::None};
};

inline bool HeldObjectAlignmentIsActive(
    const HeldObjectAlignmentState& state) noexcept {
    return state.phase != HeldObjectAlignmentPhase::Idle;
}

inline bool BeginHeldObjectAlignment(
    HeldObjectAlignmentState& state,
    std::int32_t weaponIndex,
    std::uint64_t sourceGeneration) noexcept {
    state = {};
    if (weaponIndex < 0 || sourceGeneration == 0U) {
        return false;
    }
    state.weaponIndex = weaponIndex;
    state.sourceGeneration = sourceGeneration;
    state.phase = HeldObjectAlignmentPhase::AwaitReferencePoses;
    state.triggerReleaseRequired = true;
    return true;
}

inline bool CancelHeldObjectAlignment(
    HeldObjectAlignmentState& state) noexcept {
    const bool wasActive = HeldObjectAlignmentIsActive(state);
    state = {};
    return wasActive;
}

inline HeldObjectAlignmentUpdateResult UpdateHeldObjectAlignment(
    HeldObjectAlignmentState& state,
    std::int32_t weaponIndex,
    std::uint64_t sourceGeneration,
    const PhysicalMeleeRigidTransform& desiredControllerPose,
    const PhysicalMeleeRigidTransform& displayedObjectWorld,
    const PhysicalMeleeRigidTransform& displayedHandWorld,
    bool posesFresh,
    bool triggerDown) noexcept {
    HeldObjectAlignmentUpdateResult result{};
    if (!HeldObjectAlignmentIsActive(state)) {
        return result;
    }
    if (weaponIndex != state.weaponIndex ||
        sourceGeneration != state.sourceGeneration) {
        state = {};
        result.event = HeldObjectAlignmentEvent::SourceChanged;
        return result;
    }
    if (!posesFresh ||
        !PhysicalMeleeRigidTransformIsValid(desiredControllerPose) ||
        !PhysicalMeleeRigidTransformIsValid(displayedObjectWorld) ||
        !PhysicalMeleeRigidTransformIsValid(displayedHandWorld)) {
        state.triggerReleaseRequired = true;
        state.triggerWasDown = triggerDown;
        if (triggerDown) {
            result.event = HeldObjectAlignmentEvent::PoseUnavailable;
        }
        return result;
    }
    if (!triggerDown) {
        state.triggerReleaseRequired = false;
        state.triggerWasDown = false;
        return result;
    }
    if (state.triggerReleaseRequired || state.triggerWasDown) {
        state.triggerWasDown = true;
        return result;
    }
    state.triggerWasDown = true;
    state.triggerReleaseRequired = true;
    if (state.phase == HeldObjectAlignmentPhase::AwaitReferencePoses) {
        state.referenceObjectWorld = displayedObjectWorld;
        state.referenceHandWorld = displayedHandWorld;
        state.phase = HeldObjectAlignmentPhase::AwaitControllerPose;
        result.event = HeldObjectAlignmentEvent::ReferenceCaptured;
        return result;
    }
    if (!SolveHeldObjectAlignment(
            state.referenceObjectWorld, displayedHandWorld,
            desiredControllerPose, result.solution)) {
        result.event = HeldObjectAlignmentEvent::SolveRejected;
        return result;
    }
    state = {};
    result.event = HeldObjectAlignmentEvent::Completed;
    return result;
}

struct RightHandIkTargetInput {
    fearvr::TrackingVector gripWorldPosition{};
    fearvr::TrackingQuaternion gripWorldRotation{};
    fearvr::TrackingVector weightedWeaponWorldPosition{};
    fearvr::TrackingQuaternion weightedWeaponWorldRotation{};
    std::int32_t equippedWeaponIndex{-1};
    std::uint64_t sourceGeneration{0};
    bool gripPoseFresh{false};
    bool weightedWeaponPoseFresh{false};
    bool liveWeaponModelSource{false};
};

struct RightHandIkTargetResult {
    fearvr::TrackingVector worldPosition{};
    fearvr::TrackingQuaternion worldRotation{};
    std::int32_t equippedWeaponIndex{-1};
    std::uint64_t sourceGeneration{0};
    RightHandIkTargetSource source{RightHandIkTargetSource::Invalid};
    bool liveWeaponModelSource{false};
    bool valid{false};
};

// Empty hands use one coherent OpenXR grip pose. A lifetime-validated held
// model keeps the existing weighted weapon/aim basis. In both cases a stale
// or malformed right-grip pose fails closed, even if aim tracking is valid.
inline RightHandIkTargetResult ResolveRightHandIkTarget(
    const RightHandIkTargetInput& input) noexcept {
    RightHandIkTargetResult result{};
    result.equippedWeaponIndex = input.equippedWeaponIndex;
    result.sourceGeneration = input.sourceGeneration;
    result.liveWeaponModelSource = input.liveWeaponModelSource;

    if (!input.gripPoseFresh ||
        !fearvr::IsFinite(input.gripWorldPosition) ||
        !RightHandIkRotationIsValid(input.gripWorldRotation)) {
        return result;
    }

    const bool emptyHand = !input.liveWeaponModelSource ||
        input.equippedWeaponIndex < 0 ||
        input.equippedWeaponIndex == kCondemnedUnarmedWeaponIndex;
    if (emptyHand) {
        result.worldPosition = input.gripWorldPosition;
        result.worldRotation =
            fearvr::Normalize(input.gripWorldRotation);
        result.source = RightHandIkTargetSource::EmptyGrip;
        result.valid = RightHandIkRotationIsValid(result.worldRotation);
        return result;
    }

    if (!input.weightedWeaponPoseFresh ||
        !fearvr::IsFinite(input.weightedWeaponWorldPosition) ||
        !RightHandIkRotationIsValid(
            input.weightedWeaponWorldRotation)) {
        return result;
    }
    result.worldPosition = input.weightedWeaponWorldPosition;
    result.worldRotation =
        fearvr::Normalize(input.weightedWeaponWorldRotation);
    result.source = RightHandIkTargetSource::WeaponWeightedAim;
    result.valid = RightHandIkRotationIsValid(result.worldRotation);
    return result;
}

inline bool RightHandIkTargetBasisChanged(
    RightHandIkTargetSource previousSource,
    std::int32_t previousWeaponIndex,
    std::uint64_t previousGeneration,
    const RightHandIkTargetResult& current) noexcept {
    if (previousSource != current.source) {
        return true;
    }
    return current.source == RightHandIkTargetSource::WeaponWeightedAim &&
        (previousWeaponIndex != current.equippedWeaponIndex ||
         previousGeneration != current.sourceGeneration);
}

inline bool RightHandIkQuaternionAngularDifferenceDegrees(
    const fearvr::TrackingQuaternion& left,
    const fearvr::TrackingQuaternion& right,
    float& differenceDegrees) noexcept {
    differenceDegrees = 0.0F;
    if (!RightHandIkRotationIsValid(left) ||
        !RightHandIkRotationIsValid(right)) {
        return false;
    }
    const fearvr::TrackingQuaternion normalizedLeft =
        fearvr::Normalize(left);
    const fearvr::TrackingQuaternion normalizedRight =
        fearvr::Normalize(right);
    const float dot = std::clamp(
        std::fabs(fearvr::Dot(normalizedLeft, normalizedRight)),
        0.0F, 1.0F);
    differenceDegrees =
        2.0F * std::acos(dot) * 57.29577951308232F;
    return std::isfinite(differenceDegrees);
}

} // namespace condemnedvr
