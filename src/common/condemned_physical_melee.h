#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>

#include "head_tracking_math.h"

namespace condemnedvr {

enum class PhysicalMeleeResetReason : std::uint8_t {
    None,
    FirstPose,
    TrackingLost,
    TrackingReacquired,
    InvalidPose,
    InvalidProfile,
    NonPositiveTime,
    InsufficientSampleInterval,
    ExcessiveSampleGap,
    ExcessiveTravel
};

struct PhysicalMeleePose {
    fearvr::TrackingVector gripPositionUnits{};
    fearvr::TrackingQuaternion rotation{};
};

// Stable data-layer identity. Geometry and handling remain fields on the
// profile rather than branches in the hooks, so adding a measured weapon only
// requires a catalog entry and a verified Retail identity mapping.
enum class PhysicalMeleeProfileId : std::uint8_t {
    GenericOneHanded,
    Pipe,
    Crowbar,
    FireAxe,
    Plank
};

inline const char* PhysicalMeleeProfileName(
    PhysicalMeleeProfileId id) noexcept {
    switch (id) {
    case PhysicalMeleeProfileId::GenericOneHanded:
        return "generic_one_handed";
    case PhysicalMeleeProfileId::Pipe:
        return "pipe";
    case PhysicalMeleeProfileId::Crowbar:
        return "crowbar";
    case PhysicalMeleeProfileId::FireAxe:
        return "fire_axe";
    case PhysicalMeleeProfileId::Plank:
        return "plank";
    default:
        return "invalid";
    }
}

// Geometry is expressed in LithTech world units. The generic fallback is a
// non-attacking one-handed shape extending 0.75 m along controller +Z. A
// verified Retail identity opts into explicit attack/handling behavior while
// its model-local alignment remains isolated in that weapon's own record.
struct PhysicalMeleeProfile {
    PhysicalMeleeProfileId id{
        PhysicalMeleeProfileId::GenericOneHanded};
    fearvr::TrackingVector localBaseOffsetUnits{};
    fearvr::TrackingVector localTipOffsetUnits{0.0F, 0.0F, 75.0F};
    // Grip frame expressed in the Retail weapon model's local space. Most
    // first-person models are authored with their origin at the hand socket,
    // so the safe fallback is identity. A measured per-model record can
    // correct a non-zero handle offset or asset-axis rotation without
    // changing the shared renderer or collision hooks.
    fearvr::TrackingVector modelLocalGripPositionUnits{};
    fearvr::TrackingQuaternion modelLocalGripRotation{
        0.0F, 0.0F, 0.0F, 1.0F};
    // Optional support-hand anchor expressed in the primary controller/grip
    // frame. The weapon is never scaled to span the controllers: the
    // secondary hand controls the handle direction while the authored
    // distance from the dominant grip remains fixed.
    bool secondaryGripEnabled{false};
    fearvr::TrackingVector secondaryGripOffsetUnits{};
    float secondaryGripGrabRadiusMeters{0.15F};
    float secondaryGripMaximumStretchMeters{0.25F};
    float secondaryGripAttachSqueeze{0.65F};
    float secondaryGripReleaseSqueeze{0.35F};
    float radiusUnits{4.0F};
    float unitsPerMeter{100.0F};
    float massKilograms{1.5F};
    // Dimensionless handling weight and bounded critically damped follow
    // parameters. A value of one keeps the current direct controller follow;
    // heavier profiles opt into visible inertia without changing the shared
    // OpenXR/controller solver.
    float handlingWeight{1.0F};
    float positionalFollow{18.0F};
    float rotationalFollow{20.0F};
    float catchUpStrength{1.5F};
    float dampingRatio{1.0F};
    float minimumImpactSpeedMetersPerSecond{1.25F};
    float minimumImpactEnergyJoules{1.0F};
    // Transitional controller gesture: an intentional fast sweep emits one
    // short Retail Fire/attack command pulse. Retail still owns its attack
    // animation, collision window, hit rules and damage. Unknown profiles
    // leave this disabled until their geometry and handling are measured.
    bool swingAttackEnabled{false};
    float swingAttackTriggerSpeedMetersPerSecond{3.00F};
    float swingAttackRearmSpeedMetersPerSecond{0.75F};
    std::uint32_t swingAttackPulseMilliseconds{100U};
    std::uint32_t swingAttackCooldownMilliseconds{450U};
    float maximumSweepDistanceMeters{0.50F};
    float contactRearmSeparationMeters{0.12F};
    std::uint64_t minimumSampleIntervalNs{1'000'000ULL};
    std::uint64_t maximumSampleGapNs{100'000'000ULL};
};

// Live database telemetry identified player-weapon index 32 as pipe_lever,
// indices 0/1/64/65 as the complete 2x4 family, and index 17 as fireaxe.
// Keep these mappings in the data layer so rendering, collision, and the
// future standalone weapon body consume the same records.
constexpr std::int32_t kCondemnedPipeLeverWeaponIndex = 32;
constexpr std::int32_t kCondemnedFireAxeWeaponIndex = 17;
constexpr std::array<std::int32_t, 4U>
    kCondemned2x4WeaponIndices{{0, 1, 64, 65}};

inline bool IsCondemned2x4WeaponIndex(
    std::int32_t weaponIndex) noexcept {
    return std::find(
               kCondemned2x4WeaponIndices.begin(),
               kCondemned2x4WeaponIndices.end(),
               weaponIndex) != kCondemned2x4WeaponIndices.end();
}

inline PhysicalMeleeProfile
ResolvePhysicalMeleeProfileForRetailWeaponIndex(
    std::int32_t weaponIndex) noexcept {
    PhysicalMeleeProfile profile{};
    if (weaponIndex == kCondemnedPipeLeverWeaponIndex ||
        IsCondemned2x4WeaponIndex(weaponIndex)) {
        profile.id = weaponIndex == kCondemnedPipeLeverWeaponIndex
            ? PhysicalMeleeProfileId::Pipe
            : PhysicalMeleeProfileId::Plank;
        profile.localTipOffsetUnits = {0.0F, 0.0F, 75.0F};
        // Promoted from the accepted 2026-08-04 pipe_lever snapshot. The
        // complete 2x4 family deliberately shares this grip preset at the
        // tester's request; an asset-specific correction can still split a
        // catalog record later without changing the shared solver.
        profile.modelLocalGripPositionUnits = {0.0F, 3.0F, -5.5F};
        profile.modelLocalGripRotation = {
            -0.319308F, 0.423837F, 0.162696F, 0.831826F};
        profile.radiusUnits = 4.0F;
        profile.massKilograms = 1.75F;
        // Use the same virtual-coupling stiffness family as the axe, then
        // make the pipe lighter through handlingWeight and slightly higher
        // damping.  The earlier 18/18 follow and 1.5 catch-up values pulled
        // the pipe back to the controller so aggressively that changing its
        // handling weight was almost imperceptible.
        profile.handlingWeight = 1.75F;
        profile.positionalFollow = 10.0F;
        profile.rotationalFollow = 8.0F;
        profile.catchUpStrength = 0.80F;
        profile.dampingRatio = 0.65F;
        profile.swingAttackEnabled = true;
        return profile;
    }
    if (weaponIndex != kCondemnedFireAxeWeaponIndex) {
        return profile;
    }

    profile.id = PhysicalMeleeProfileId::FireAxe;
    profile.localTipOffsetUnits = {0.0F, 0.0F, 82.0F};
    profile.modelLocalGripPositionUnits = {
        -0.117F, -3.053F, -6.982F};
    profile.modelLocalGripRotation = {
        -0.052973F, 0.840891F, 0.248921F, 0.477635F};
    profile.secondaryGripEnabled = true;
    // Promoted from the accepted 2026-08-04 in-headset alignment run. The
    // support hand sits down the fire-axe haft rather than on the controller
    // aim axis used by the earlier provisional value.
    profile.secondaryGripOffsetUnits = {3.114F, -30.258F, -14.828F};
    profile.radiusUnits = 7.0F;
    profile.massKilograms = 4.5F;
    profile.handlingWeight = 4.0F;
    profile.positionalFollow = 10.0F;
    profile.rotationalFollow = 8.0F;
    profile.catchUpStrength = 0.80F;
    profile.dampingRatio = 0.55F;
    profile.swingAttackEnabled = true;
    return profile;
}

struct PhysicalMeleeKinematicsState {
    PhysicalMeleePose previousPose{};
    std::uint64_t previousTimeNs{0};
    PhysicalMeleeResetReason lastResetReason{
        PhysicalMeleeResetReason::None};
    bool havePose{false};
};

struct PhysicalMeleeFrame {
    fearvr::TrackingVector previousBaseUnits{};
    fearvr::TrackingVector previousTipUnits{};
    fearvr::TrackingVector currentBaseUnits{};
    fearvr::TrackingVector currentTipUnits{};
    fearvr::TrackingVector gripVelocityUnitsPerSecond{};
    fearvr::TrackingVector baseVelocityUnitsPerSecond{};
    fearvr::TrackingVector tipVelocityUnitsPerSecond{};
    fearvr::TrackingVector angularVelocityRadiansPerSecond{};
    fearvr::TrackingQuaternion currentRotation{};
    float deltaSeconds{0.0F};
    float sweepDistanceMeters{0.0F};
    float impactSpeedMetersPerSecond{0.0F};
    float impactEnergyJoules{0.0F};
    float radiusUnits{0.0F};
    PhysicalMeleeResetReason resetReason{
        PhysicalMeleeResetReason::None};
    bool poseValid{false};
    bool sweepValid{false};
    bool damageQualified{false};
};

// Release hysteresis plus a bounded command pulse makes one deliberate
// physical swing look like one ordinary Retail attack press. The clock is
// supplied by the caller so this policy remains platform-independent and
// deterministic under unit tests.
struct PhysicalMeleeSwingAttackState {
    std::uint64_t pulseEndMilliseconds{0};
    std::uint64_t cooldownEndMilliseconds{0};
    bool armed{true};
};

struct PhysicalMeleeSwingAttackResult {
    bool active{false};
    bool triggered{false};
    bool rearmed{false};
};

// Gate-2 uses Retail's existing melee collision body only as a wall-contact
// probe. Its origin follows the measured weapon endpoint, while Retail's
// native impact dispatcher remains disabled until target classification and
// contact qualification have separate live evidence.
struct PhysicalMeleeWallProxyTransform {
    fearvr::TrackingVector positionUnits{};
    fearvr::TrackingQuaternion rotation{};
    bool active{false};
};

// A compact engine-independent rigid transform used to solve the diagnostic
// weapon model pose. The Retail model and its animated melee node are sampled
// together; the solver then places that same node at the controller-driven
// collision endpoint without assuming where the model origin or grip lives.
struct PhysicalMeleeRigidTransform {
    fearvr::TrackingVector positionUnits{};
    fearvr::TrackingQuaternion rotation{};
};

struct PhysicalMeleeVisualProxyTransform {
    PhysicalMeleeRigidTransform objectWorld{};
    bool active{false};
};

// Live setup keeps the authored/profile grip as its immutable base and stores
// a human-readable local XYZ rotation correction beside the tuned position.
// The same record can be copied directly into a weapon profile after a live
// calibration session; renderer code never needs a weapon-specific branch.
struct PhysicalMeleeGripCalibration {
    fearvr::TrackingVector basePositionUnits{};
    fearvr::TrackingQuaternion baseRotation{
        0.0F, 0.0F, 0.0F, 1.0F};
    fearvr::TrackingVector positionUnits{};
    fearvr::TrackingVector localRotationDegrees{};
    fearvr::TrackingVector baseSecondaryGripOffsetUnits{};
    fearvr::TrackingVector secondaryGripOffsetUnits{};
    float baseSecondaryGripGrabRadiusMeters{0.15F};
    float secondaryGripGrabRadiusMeters{0.15F};
    bool baseSecondaryGripEnabled{false};
    bool secondaryGripEnabled{false};
};

// Select an existing stable weapon record first, otherwise consume an empty
// record, and only then evict the least-recently-used entry. Keeping this
// policy alongside the calibration data makes the session cache behavior
// directly testable and prevents weapon swaps from discarding a tuned grip
// while unused capacity remains.
template <typename Slot, std::size_t SlotCount>
inline std::size_t SelectPhysicalMeleeCalibrationSlot(
    const Slot (&slots)[SlotCount],
    std::int32_t weaponIndex) noexcept {
    if (weaponIndex < 0 || SlotCount == 0U) {
        return SlotCount;
    }
    std::size_t emptySlot = SlotCount;
    std::size_t oldestSlot = SlotCount;
    std::uint64_t oldestUse = UINT64_MAX;
    for (std::size_t index = 0; index < SlotCount; ++index) {
        const Slot& slot = slots[index];
        if (slot.occupied && slot.weaponIndex == weaponIndex) {
            return index;
        }
        if (!slot.occupied && emptySlot == SlotCount) {
            emptySlot = index;
        } else if (slot.occupied && slot.lastUsed < oldestUse) {
            oldestSlot = index;
            oldestUse = slot.lastUsed;
        }
    }
    return emptySlot != SlotCount ? emptySlot : oldestSlot;
}

struct PhysicalMeleeSecondaryGripSettings {
    fearvr::TrackingVector offsetUnits{};
    float unitsPerMeter{100.0F};
    float grabRadiusMeters{0.15F};
    float maximumStretchMeters{0.25F};
    float attachSqueeze{0.65F};
    float releaseSqueeze{0.35F};
    bool enabled{false};
};

enum class PhysicalMeleeSecondaryGripReleaseReason : std::uint8_t {
    None,
    Released,
    TrackingLost,
    ContextDisabled,
    Unsupported,
    ExcessiveStretch,
    InvalidPose
};

struct PhysicalMeleeSecondaryGripState {
    bool attached{false};
    bool attachmentArmed{true};
};

struct PhysicalMeleeTwoHandPoseResult {
    PhysicalMeleePose pose{};
    fearvr::TrackingVector targetSecondaryPositionUnits{};
    float grabDistanceMeters{0.0F};
    float handSeparationMeters{0.0F};
    float anchorErrorMeters{0.0F};
    PhysicalMeleeSecondaryGripReleaseReason releaseReason{
        PhysicalMeleeSecondaryGripReleaseReason::None};
    bool poseValid{false};
    bool attached{false};
    bool justAttached{false};
    bool justReleased{false};
};

// The attached rendered support hand becomes a rigid child of the final
// weighted weapon pose. Its position is the authored support anchor and this
// state captures its relative rotation on attachment, avoiding an orientation
// snap while preventing later controller twist from swivelling the hand around
// that fixed grip point. Release returns both channels to the controller.
struct PhysicalMeleeSupportHandOrientationState {
    fearvr::TrackingQuaternion handFromWeaponRotation{
        0.0F, 0.0F, 0.0F, 1.0F};
    bool attachedRotationValid{false};
};

inline bool ResolvePhysicalMeleeSupportHandRotation(
    PhysicalMeleeSupportHandOrientationState& state,
    const fearvr::TrackingQuaternion& weaponWorldRotation,
    const fearvr::TrackingQuaternion& controllerGripWorldRotation,
    bool attached,
    bool justAttached,
    fearvr::TrackingQuaternion& handWorldRotation) noexcept {
    const auto RotationValid = [](
        const fearvr::TrackingQuaternion& rotation) noexcept {
        const float lengthSquared =
            rotation.x * rotation.x + rotation.y * rotation.y +
            rotation.z * rotation.z + rotation.w * rotation.w;
        return fearvr::IsFinite(rotation) &&
            std::isfinite(lengthSquared) &&
            lengthSquared >= 0.25F && lengthSquared <= 4.0F;
    };
    if (!RotationValid(controllerGripWorldRotation)) {
        state = {};
        handWorldRotation = {0.0F, 0.0F, 0.0F, 0.0F};
        return false;
    }
    if (!attached) {
        state = {};
        handWorldRotation =
            fearvr::Normalize(controllerGripWorldRotation);
        return true;
    }
    if (!RotationValid(weaponWorldRotation)) {
        state = {};
        handWorldRotation = {0.0F, 0.0F, 0.0F, 0.0F};
        return false;
    }
    if (justAttached || !state.attachedRotationValid) {
        state.handFromWeaponRotation = fearvr::Multiply(
            fearvr::Conjugate(
                fearvr::Normalize(weaponWorldRotation)),
            fearvr::Normalize(controllerGripWorldRotation));
        state.attachedRotationValid =
            RotationValid(state.handFromWeaponRotation);
    }
    if (!state.attachedRotationValid) {
        handWorldRotation = {0.0F, 0.0F, 0.0F, 0.0F};
        return false;
    }
    handWorldRotation = fearvr::Multiply(
        fearvr::Normalize(weaponWorldRotation),
        state.handFromWeaponRotation);
    return RotationValid(handWorldRotation);
}

enum class PhysicalMeleeContactReason : std::uint8_t {
    None,
    Accepted,
    InvalidProfile,
    MissingTarget,
    InvalidContact,
    InvalidFrame,
    BelowNormalSpeed,
    BelowNormalEnergy,
    ContactLatched
};

struct PhysicalMeleeContactState {
    fearvr::TrackingVector acceptedTipUnits{};
    fearvr::TrackingVector acceptedNormal{};
    std::uintptr_t targetId{0};
    std::uint64_t sampleId{0};
    bool armed{true};
    bool haveContact{false};
};

struct PhysicalMeleeContactQualification {
    float normalSpeedMetersPerSecond{0.0F};
    float normalEnergyJoules{0.0F};
    PhysicalMeleeContactReason reason{PhysicalMeleeContactReason::None};
    bool accepted{false};
};

inline fearvr::TrackingVector PhysicalMeleeAdd(
    const fearvr::TrackingVector& left,
    const fearvr::TrackingVector& right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

inline fearvr::TrackingVector PhysicalMeleeSubtract(
    const fearvr::TrackingVector& left,
    const fearvr::TrackingVector& right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

inline fearvr::TrackingVector PhysicalMeleeScale(
    const fearvr::TrackingVector& value,
    float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

inline float PhysicalMeleeLength(
    const fearvr::TrackingVector& value) noexcept {
    return std::sqrt(
        value.x * value.x + value.y * value.y + value.z * value.z);
}

inline float PhysicalMeleeDot(
    const fearvr::TrackingVector& left,
    const fearvr::TrackingVector& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

inline bool PhysicalMeleePoseIsValid(
    const PhysicalMeleePose& pose) noexcept;

inline fearvr::TrackingVector PhysicalMeleeCross(
    const fearvr::TrackingVector& left,
    const fearvr::TrackingVector& right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x};
}

inline bool PhysicalMeleeNormalizeVector(
    const fearvr::TrackingVector& value,
    fearvr::TrackingVector& normalized) noexcept {
    normalized = {};
    const float length = PhysicalMeleeLength(value);
    if (!fearvr::IsFinite(value) || !std::isfinite(length) ||
        length < 1.0e-4F) {
        return false;
    }
    normalized = PhysicalMeleeScale(value, 1.0F / length);
    return fearvr::IsFinite(normalized);
}

// Returns the shortest stable rotation from one direction to another. The
// opposite-vector case chooses a deterministic perpendicular axis so a
// controller crossing the handle cannot produce a zero quaternion.
inline bool PhysicalMeleeShortestArcRotation(
    const fearvr::TrackingVector& from,
    const fearvr::TrackingVector& to,
    fearvr::TrackingQuaternion& rotation) noexcept {
    rotation = {};
    fearvr::TrackingVector fromUnit{};
    fearvr::TrackingVector toUnit{};
    if (!PhysicalMeleeNormalizeVector(from, fromUnit) ||
        !PhysicalMeleeNormalizeVector(to, toUnit)) {
        return false;
    }
    const float dot = std::clamp(
        PhysicalMeleeDot(fromUnit, toUnit), -1.0F, 1.0F);
    if (dot >= 0.9999F) {
        rotation = {};
        return true;
    }
    if (dot <= -0.9999F) {
        const fearvr::TrackingVector reference =
            std::fabs(fromUnit.x) < 0.75F
            ? fearvr::TrackingVector{1.0F, 0.0F, 0.0F}
            : fearvr::TrackingVector{0.0F, 1.0F, 0.0F};
        fearvr::TrackingVector axis{};
        if (!PhysicalMeleeNormalizeVector(
                PhysicalMeleeCross(fromUnit, reference), axis)) {
            return false;
        }
        rotation = {axis.x, axis.y, axis.z, 0.0F};
        return true;
    }
    const fearvr::TrackingVector cross =
        PhysicalMeleeCross(fromUnit, toUnit);
    rotation = fearvr::Normalize(
        {cross.x, cross.y, cross.z, 1.0F + dot});
    return fearvr::IsFinite(rotation);
}

inline bool PhysicalMeleeSecondaryGripSettingsAreValid(
    const PhysicalMeleeSecondaryGripSettings& settings) noexcept {
    const float offsetLength = PhysicalMeleeLength(settings.offsetUnits);
    return fearvr::IsFinite(settings.offsetUnits) &&
        std::isfinite(offsetLength) &&
        (!settings.enabled || offsetLength >= 5.0F) &&
        offsetLength <= 300.0F &&
        std::isfinite(settings.unitsPerMeter) &&
        settings.unitsPerMeter > 0.0F &&
        settings.unitsPerMeter <= 1000.0F &&
        std::isfinite(settings.grabRadiusMeters) &&
        settings.grabRadiusMeters >= 0.05F &&
        settings.grabRadiusMeters <= 0.50F &&
        std::isfinite(settings.maximumStretchMeters) &&
        settings.maximumStretchMeters >= 0.05F &&
        settings.maximumStretchMeters <= 1.0F &&
        std::isfinite(settings.attachSqueeze) &&
        std::isfinite(settings.releaseSqueeze) &&
        settings.releaseSqueeze >= 0.0F &&
        settings.releaseSqueeze < settings.attachSqueeze &&
        settings.attachSqueeze <= 1.0F;
}

inline PhysicalMeleeSecondaryGripSettings
PhysicalMeleeSecondaryGripSettingsFromProfile(
    const PhysicalMeleeProfile& profile) noexcept {
    return {
        profile.secondaryGripOffsetUnits,
        profile.unitsPerMeter,
        profile.secondaryGripGrabRadiusMeters,
        profile.secondaryGripMaximumStretchMeters,
        profile.secondaryGripAttachSqueeze,
        profile.secondaryGripReleaseSqueeze,
        profile.secondaryGripEnabled};
}

inline bool ResolvePhysicalMeleeSecondaryGripOffset(
    const PhysicalMeleePose& primaryGrip,
    const fearvr::TrackingVector& secondaryGripPositionUnits,
    fearvr::TrackingVector& offsetUnits) noexcept {
    offsetUnits = {};
    if (!PhysicalMeleePoseIsValid(primaryGrip) ||
        !fearvr::IsFinite(secondaryGripPositionUnits)) {
        return false;
    }
    offsetUnits = fearvr::Rotate(
        fearvr::Conjugate(fearvr::Normalize(primaryGrip.rotation)),
        PhysicalMeleeSubtract(
            secondaryGripPositionUnits,
            primaryGrip.gripPositionUnits));
    const float length = PhysicalMeleeLength(offsetUnits);
    return fearvr::IsFinite(offsetUnits) && std::isfinite(length) &&
        length >= 5.0F && length <= 300.0F;
}

// Dominant-hand position stays authoritative. The support hand supplies only
// the handle direction; the shortest-arc correction retains as much of the
// dominant controller's twist as possible. Authored weapon scale and grip
// spacing are therefore invariant under arbitrary controller separation.
inline PhysicalMeleeTwoHandPoseResult ResolvePhysicalMeleeTwoHandPose(
    const PhysicalMeleePose& primaryGrip,
    const fearvr::TrackingVector& secondaryGripPositionUnits,
    const PhysicalMeleeSecondaryGripSettings& settings) noexcept {
    PhysicalMeleeTwoHandPoseResult result{};
    result.pose = primaryGrip;
    if (!PhysicalMeleePoseIsValid(primaryGrip) ||
        !fearvr::IsFinite(secondaryGripPositionUnits) ||
        !PhysicalMeleeSecondaryGripSettingsAreValid(settings) ||
        !settings.enabled) {
        return result;
    }
    result.pose.rotation = fearvr::Normalize(primaryGrip.rotation);
    const fearvr::TrackingVector baselineDirection = fearvr::Rotate(
        result.pose.rotation, settings.offsetUnits);
    const fearvr::TrackingVector desiredDirection =
        PhysicalMeleeSubtract(
            secondaryGripPositionUnits,
            primaryGrip.gripPositionUnits);
    fearvr::TrackingQuaternion correction{};
    if (!PhysicalMeleeShortestArcRotation(
            baselineDirection, desiredDirection, correction)) {
        return result;
    }
    result.pose.rotation = fearvr::Multiply(
        correction, result.pose.rotation);
    result.targetSecondaryPositionUnits = PhysicalMeleeAdd(
        result.pose.gripPositionUnits,
        fearvr::Rotate(result.pose.rotation, settings.offsetUnits));
    result.handSeparationMeters =
        PhysicalMeleeLength(desiredDirection) / settings.unitsPerMeter;
    result.anchorErrorMeters = PhysicalMeleeLength(
        PhysicalMeleeSubtract(
            secondaryGripPositionUnits,
            result.targetSecondaryPositionUnits)) /
        settings.unitsPerMeter;
    result.poseValid = PhysicalMeleePoseIsValid(result.pose) &&
        fearvr::IsFinite(result.targetSecondaryPositionUnits) &&
        std::isfinite(result.handSeparationMeters) &&
        std::isfinite(result.anchorErrorMeters);
    return result;
}

// Select-style lifecycle with press/release hysteresis. A squeeze that began
// away from the handle cannot turn into a remote grab merely by moving the
// hand closer; it must first be released and pressed again near the anchor.
inline PhysicalMeleeTwoHandPoseResult UpdatePhysicalMeleeSecondaryGrip(
    PhysicalMeleeSecondaryGripState& state,
    const PhysicalMeleePose& primaryGrip,
    const fearvr::TrackingVector& secondaryGripPositionUnits,
    float secondarySqueeze,
    bool trackingFresh,
    bool contextEnabled,
    const PhysicalMeleeSecondaryGripSettings& settings) noexcept {
    PhysicalMeleeTwoHandPoseResult result{};
    result.pose = primaryGrip;
    result.poseValid = PhysicalMeleePoseIsValid(primaryGrip);
    const bool wasAttached = state.attached;
    const auto Release = [&](
        PhysicalMeleeSecondaryGripReleaseReason reason) {
        state.attached = false;
        result.attached = false;
        result.justReleased = wasAttached;
        result.releaseReason = reason;
    };

    if (!PhysicalMeleeSecondaryGripSettingsAreValid(settings) ||
        !settings.enabled) {
        state.attachmentArmed = false;
        Release(PhysicalMeleeSecondaryGripReleaseReason::Unsupported);
        return result;
    }
    if (!trackingFresh || !result.poseValid ||
        !fearvr::IsFinite(secondaryGripPositionUnits) ||
        !std::isfinite(secondarySqueeze)) {
        state.attachmentArmed = false;
        Release(PhysicalMeleeSecondaryGripReleaseReason::TrackingLost);
        return result;
    }

    const fearvr::TrackingVector baselineSecondary = PhysicalMeleeAdd(
        primaryGrip.gripPositionUnits,
        fearvr::Rotate(
            fearvr::Normalize(primaryGrip.rotation),
            settings.offsetUnits));
    result.grabDistanceMeters = PhysicalMeleeLength(
        PhysicalMeleeSubtract(
            secondaryGripPositionUnits, baselineSecondary)) /
        settings.unitsPerMeter;
    result.handSeparationMeters = PhysicalMeleeLength(
        PhysicalMeleeSubtract(
            secondaryGripPositionUnits,
            primaryGrip.gripPositionUnits)) /
        settings.unitsPerMeter;
    result.targetSecondaryPositionUnits = baselineSecondary;

    if (!contextEnabled) {
        state.attachmentArmed = false;
        Release(
            PhysicalMeleeSecondaryGripReleaseReason::ContextDisabled);
        return result;
    }

    if (secondarySqueeze <= settings.releaseSqueeze) {
        state.attachmentArmed = true;
        Release(PhysicalMeleeSecondaryGripReleaseReason::Released);
        return result;
    }

    if (!state.attached && secondarySqueeze >= settings.attachSqueeze) {
        if (state.attachmentArmed &&
            result.grabDistanceMeters <= settings.grabRadiusMeters) {
            state.attached = true;
            result.justAttached = true;
        }
        // Consume this press even when it missed the handle. This is what
        // prevents remote snap-grabs after moving an already squeezed hand.
        state.attachmentArmed = false;
    }
    if (!state.attached) {
        return result;
    }

    PhysicalMeleeTwoHandPoseResult solved =
        ResolvePhysicalMeleeTwoHandPose(
            primaryGrip, secondaryGripPositionUnits, settings);
    solved.grabDistanceMeters = result.grabDistanceMeters;
    solved.justAttached = result.justAttached;
    if (!solved.poseValid) {
        state.attached = false;
        solved.justReleased = true;
        solved.releaseReason =
            PhysicalMeleeSecondaryGripReleaseReason::InvalidPose;
        solved.pose = primaryGrip;
        solved.poseValid = result.poseValid;
        return solved;
    }
    if (solved.anchorErrorMeters > settings.maximumStretchMeters) {
        state.attached = false;
        solved.attached = false;
        solved.justReleased = true;
        solved.releaseReason =
            PhysicalMeleeSecondaryGripReleaseReason::ExcessiveStretch;
        solved.pose = primaryGrip;
        solved.poseValid = result.poseValid;
        solved.targetSecondaryPositionUnits = baselineSecondary;
        return solved;
    }
    solved.attached = true;
    return solved;
}

inline bool PhysicalMeleePoseIsValid(
    const PhysicalMeleePose& pose) noexcept {
    const float rotationLengthSquared =
        pose.rotation.x * pose.rotation.x +
        pose.rotation.y * pose.rotation.y +
        pose.rotation.z * pose.rotation.z +
        pose.rotation.w * pose.rotation.w;
    return fearvr::IsFinite(pose.gripPositionUnits) &&
        fearvr::IsFinite(pose.rotation) &&
        std::isfinite(rotationLengthSquared) &&
        rotationLengthSquared >= 0.25F &&
        rotationLengthSquared <= 4.0F;
}

inline bool PhysicalMeleeProfileIsValid(
    const PhysicalMeleeProfile& profile) noexcept {
    const bool knownId =
        profile.id == PhysicalMeleeProfileId::GenericOneHanded ||
        profile.id == PhysicalMeleeProfileId::Pipe ||
        profile.id == PhysicalMeleeProfileId::Crowbar ||
        profile.id == PhysicalMeleeProfileId::FireAxe ||
        profile.id == PhysicalMeleeProfileId::Plank;
    const float modelGripRotationLengthSquared =
        profile.modelLocalGripRotation.x *
            profile.modelLocalGripRotation.x +
        profile.modelLocalGripRotation.y *
            profile.modelLocalGripRotation.y +
        profile.modelLocalGripRotation.z *
            profile.modelLocalGripRotation.z +
        profile.modelLocalGripRotation.w *
            profile.modelLocalGripRotation.w;
    const PhysicalMeleeSecondaryGripSettings secondaryGrip =
        PhysicalMeleeSecondaryGripSettingsFromProfile(profile);
    return knownId &&
        fearvr::IsFinite(profile.localBaseOffsetUnits) &&
        fearvr::IsFinite(profile.localTipOffsetUnits) &&
        fearvr::IsFinite(profile.modelLocalGripPositionUnits) &&
        std::fabs(profile.modelLocalGripPositionUnits.x) <= 300.0F &&
        std::fabs(profile.modelLocalGripPositionUnits.y) <= 300.0F &&
        std::fabs(profile.modelLocalGripPositionUnits.z) <= 300.0F &&
        fearvr::IsFinite(profile.modelLocalGripRotation) &&
        std::isfinite(modelGripRotationLengthSquared) &&
        modelGripRotationLengthSquared >= 0.25F &&
        modelGripRotationLengthSquared <= 4.0F &&
        PhysicalMeleeSecondaryGripSettingsAreValid(secondaryGrip) &&
        std::isfinite(profile.radiusUnits) &&
        profile.radiusUnits > 0.0F && profile.radiusUnits <= 100.0F &&
        std::isfinite(profile.unitsPerMeter) &&
        profile.unitsPerMeter > 0.0F &&
        profile.unitsPerMeter <= 1000.0F &&
        std::isfinite(profile.massKilograms) &&
        profile.massKilograms > 0.0F &&
        profile.massKilograms <= 20.0F &&
        std::isfinite(profile.handlingWeight) &&
        profile.handlingWeight >= 0.10F &&
        profile.handlingWeight <= 4.0F &&
        std::isfinite(profile.positionalFollow) &&
        profile.positionalFollow >= 2.0F &&
        profile.positionalFollow <= 40.0F &&
        std::isfinite(profile.rotationalFollow) &&
        profile.rotationalFollow >= 2.0F &&
        profile.rotationalFollow <= 40.0F &&
        std::isfinite(profile.catchUpStrength) &&
        profile.catchUpStrength >= 0.0F &&
        profile.catchUpStrength <= 4.0F &&
        std::isfinite(profile.dampingRatio) &&
        profile.dampingRatio >= 0.35F &&
        profile.dampingRatio <= 1.0F &&
        std::isfinite(profile.minimumImpactSpeedMetersPerSecond) &&
        profile.minimumImpactSpeedMetersPerSecond >= 0.0F &&
        profile.minimumImpactSpeedMetersPerSecond <= 20.0F &&
        std::isfinite(profile.minimumImpactEnergyJoules) &&
        profile.minimumImpactEnergyJoules >= 0.0F &&
        profile.minimumImpactEnergyJoules <= 1000.0F &&
        std::isfinite(
            profile.swingAttackTriggerSpeedMetersPerSecond) &&
        profile.swingAttackTriggerSpeedMetersPerSecond >= 0.25F &&
        profile.swingAttackTriggerSpeedMetersPerSecond <= 20.0F &&
        std::isfinite(
            profile.swingAttackRearmSpeedMetersPerSecond) &&
        profile.swingAttackRearmSpeedMetersPerSecond >= 0.0F &&
        profile.swingAttackRearmSpeedMetersPerSecond <
            profile.swingAttackTriggerSpeedMetersPerSecond &&
        profile.swingAttackPulseMilliseconds > 0U &&
        profile.swingAttackPulseMilliseconds <= 1000U &&
        profile.swingAttackCooldownMilliseconds >=
            profile.swingAttackPulseMilliseconds &&
        profile.swingAttackCooldownMilliseconds <= 5000U &&
        std::isfinite(profile.maximumSweepDistanceMeters) &&
        profile.maximumSweepDistanceMeters > 0.0F &&
        profile.maximumSweepDistanceMeters <= 5.0F &&
        std::isfinite(profile.contactRearmSeparationMeters) &&
        profile.contactRearmSeparationMeters > 0.0F &&
        profile.contactRearmSeparationMeters <= 2.0F &&
        profile.minimumSampleIntervalNs > 0 &&
        profile.maximumSampleGapNs >= profile.minimumSampleIntervalNs;
}

inline void ResetPhysicalMeleeSwingAttack(
    PhysicalMeleeSwingAttackState& state) noexcept {
    state = {};
}

inline bool PhysicalMeleeSwingAttackPulseIsActive(
    const PhysicalMeleeSwingAttackState& state,
    std::uint64_t nowMilliseconds) noexcept {
    return nowMilliseconds != 0U &&
        state.pulseEndMilliseconds != 0U &&
        nowMilliseconds < state.pulseEndMilliseconds;
}

inline PhysicalMeleeSwingAttackResult UpdatePhysicalMeleeSwingAttack(
    PhysicalMeleeSwingAttackState& state,
    const PhysicalMeleeFrame& frame,
    std::uint64_t nowMilliseconds,
    bool contextEnabled,
    const PhysicalMeleeProfile& profile = {}) noexcept {
    PhysicalMeleeSwingAttackResult result{};
    if (!contextEnabled || !PhysicalMeleeProfileIsValid(profile) ||
        !profile.swingAttackEnabled || nowMilliseconds == 0U ||
        !frame.poseValid || !frame.sweepValid ||
        !std::isfinite(frame.impactSpeedMetersPerSecond) ||
        frame.impactSpeedMetersPerSecond < 0.0F) {
        ResetPhysicalMeleeSwingAttack(state);
        return result;
    }

    if (!state.armed &&
        frame.impactSpeedMetersPerSecond <=
            profile.swingAttackRearmSpeedMetersPerSecond) {
        state.armed = true;
        result.rearmed = true;
    }

    if (state.armed &&
        nowMilliseconds >= state.cooldownEndMilliseconds &&
        frame.impactSpeedMetersPerSecond >=
            profile.swingAttackTriggerSpeedMetersPerSecond) {
        const auto AddBoundedMilliseconds = [](
            std::uint64_t start,
            std::uint32_t duration) noexcept {
            const std::uint64_t maximum =
                std::numeric_limits<std::uint64_t>::max();
            return start > maximum - duration
                ? maximum
                : start + duration;
        };
        state.pulseEndMilliseconds = AddBoundedMilliseconds(
            nowMilliseconds,
            profile.swingAttackPulseMilliseconds);
        state.cooldownEndMilliseconds = AddBoundedMilliseconds(
            nowMilliseconds,
            profile.swingAttackCooldownMilliseconds);
        state.armed = false;
        result.triggered = true;
    }

    result.active = PhysicalMeleeSwingAttackPulseIsActive(
        state, nowMilliseconds);
    return result;
}

inline fearvr::TrackingVector PhysicalMeleeEndpoint(
    const PhysicalMeleePose& pose,
    const fearvr::TrackingVector& localOffsetUnits) noexcept {
    return PhysicalMeleeAdd(
        pose.gripPositionUnits,
        fearvr::Rotate(pose.rotation, localOffsetUnits));
}

inline bool PhysicalMeleeRigidTransformIsValid(
    const PhysicalMeleeRigidTransform& transform) noexcept {
    const float rotationLengthSquared =
        transform.rotation.x * transform.rotation.x +
        transform.rotation.y * transform.rotation.y +
        transform.rotation.z * transform.rotation.z +
        transform.rotation.w * transform.rotation.w;
    return fearvr::IsFinite(transform.positionUnits) &&
        fearvr::IsFinite(transform.rotation) &&
        std::isfinite(rotationLengthSquared) &&
        rotationLengthSquared >= 0.25F &&
        rotationLengthSquared <= 4.0F;
}

inline float PhysicalMeleeWrapDegrees(float degrees) noexcept {
    if (!std::isfinite(degrees)) {
        return 0.0F;
    }
    float wrapped = std::fmod(degrees + 180.0F, 360.0F);
    if (wrapped < 0.0F) {
        wrapped += 360.0F;
    }
    return wrapped - 180.0F;
}

// Builds the local correction in X, then Y, then Z order. Multiplying this
// after the profile's base quaternion makes the three setup axes relative to
// the weapon model instead of the changing controller/world orientation.
inline fearvr::TrackingQuaternion
PhysicalMeleeLocalRotationFromDegrees(
    const fearvr::TrackingVector& localRotationDegrees) noexcept {
    if (!fearvr::IsFinite(localRotationDegrees)) {
        return {};
    }
    constexpr float kDegreesToRadians =
        3.14159265358979323846F / 180.0F;
    const float halfX =
        localRotationDegrees.x * kDegreesToRadians * 0.5F;
    const float halfY =
        localRotationDegrees.y * kDegreesToRadians * 0.5F;
    const float halfZ =
        localRotationDegrees.z * kDegreesToRadians * 0.5F;
    const fearvr::TrackingQuaternion xRotation{
        std::sin(halfX), 0.0F, 0.0F, std::cos(halfX)};
    const fearvr::TrackingQuaternion yRotation{
        0.0F, std::sin(halfY), 0.0F, std::cos(halfY)};
    const fearvr::TrackingQuaternion zRotation{
        0.0F, 0.0F, std::sin(halfZ), std::cos(halfZ)};
    return fearvr::Multiply(
        zRotation, fearvr::Multiply(yRotation, xRotation));
}

inline fearvr::TrackingQuaternion
ResolvePhysicalMeleeGripCalibrationRotation(
    const PhysicalMeleeGripCalibration& calibration) noexcept {
    return fearvr::Multiply(
        fearvr::Normalize(calibration.baseRotation),
        PhysicalMeleeLocalRotationFromDegrees(
            calibration.localRotationDegrees));
}

// Places a model-local grip frame exactly on the tracked controller grip.
// If G is the grip transform inside the model and D is the desired OpenXR
// grip in world space, the model transform is O' = D * inverse(G).
inline PhysicalMeleeVisualProxyTransform
ResolvePhysicalMeleeHeldModelTransform(
    const PhysicalMeleeRigidTransform& desiredGripWorld,
    const fearvr::TrackingVector& modelLocalGripPositionUnits,
    const fearvr::TrackingQuaternion& modelLocalGripRotation,
    bool trackingFresh) noexcept {
    PhysicalMeleeVisualProxyTransform result{};
    const PhysicalMeleeRigidTransform modelLocalGrip{
        modelLocalGripPositionUnits, modelLocalGripRotation};
    if (!trackingFresh ||
        !PhysicalMeleeRigidTransformIsValid(desiredGripWorld) ||
        !PhysicalMeleeRigidTransformIsValid(modelLocalGrip)) {
        return result;
    }

    const fearvr::TrackingQuaternion desiredRotation =
        fearvr::Normalize(desiredGripWorld.rotation);
    const fearvr::TrackingQuaternion localGripInverse =
        fearvr::Conjugate(fearvr::Normalize(modelLocalGripRotation));
    result.objectWorld.rotation = fearvr::Multiply(
        desiredRotation, localGripInverse);
    result.objectWorld.positionUnits = PhysicalMeleeSubtract(
        desiredGripWorld.positionUnits,
        fearvr::Rotate(
            result.objectWorld.rotation,
            modelLocalGripPositionUnits));
    result.active = PhysicalMeleeRigidTransformIsValid(
        result.objectWorld);
    return result;
}

// Solves O' = D * inverse(N) * O, where O is the sampled Retail model-world
// transform, N is its animated melee-node world transform, and D is the
// controller-driven collision endpoint. Applying the original model-to-node
// relationship to O' therefore produces D exactly.
inline PhysicalMeleeVisualProxyTransform
ResolvePhysicalMeleeVisualProxyTransform(
    const PhysicalMeleeRigidTransform& sourceObjectWorld,
    const PhysicalMeleeRigidTransform& sourceNodeWorld,
    const PhysicalMeleeRigidTransform& desiredNodeWorld,
    bool sourceFresh) noexcept {
    PhysicalMeleeVisualProxyTransform result{};
    if (!sourceFresh ||
        !PhysicalMeleeRigidTransformIsValid(sourceObjectWorld) ||
        !PhysicalMeleeRigidTransformIsValid(sourceNodeWorld) ||
        !PhysicalMeleeRigidTransformIsValid(desiredNodeWorld)) {
        return result;
    }

    const fearvr::TrackingQuaternion objectRotation =
        fearvr::Normalize(sourceObjectWorld.rotation);
    const fearvr::TrackingQuaternion nodeInverse =
        fearvr::Conjugate(fearvr::Normalize(sourceNodeWorld.rotation));
    const fearvr::TrackingQuaternion desiredRotation =
        fearvr::Normalize(desiredNodeWorld.rotation);
    const fearvr::TrackingQuaternion desiredFromSourceNode =
        fearvr::Multiply(desiredRotation, nodeInverse);
    result.objectWorld.rotation = fearvr::Multiply(
        desiredFromSourceNode, objectRotation);
    result.objectWorld.positionUnits = PhysicalMeleeAdd(
        desiredNodeWorld.positionUnits,
        fearvr::Rotate(
            desiredFromSourceNode,
            PhysicalMeleeSubtract(
                sourceObjectWorld.positionUnits,
                sourceNodeWorld.positionUnits)));
    result.active = PhysicalMeleeRigidTransformIsValid(
        result.objectWorld);
    return result;
}

inline PhysicalMeleeWallProxyTransform
ResolvePhysicalMeleeWallProxyTransform(
    const PhysicalMeleeFrame& frame,
    bool sampleFresh) noexcept {
    PhysicalMeleeWallProxyTransform result{};
    if (!sampleFresh || !frame.poseValid ||
        !fearvr::IsFinite(frame.currentTipUnits) ||
        !fearvr::IsFinite(frame.currentRotation)) {
        return result;
    }
    const float rotationLengthSquared =
        frame.currentRotation.x * frame.currentRotation.x +
        frame.currentRotation.y * frame.currentRotation.y +
        frame.currentRotation.z * frame.currentRotation.z +
        frame.currentRotation.w * frame.currentRotation.w;
    if (!std::isfinite(rotationLengthSquared) ||
        rotationLengthSquared < 0.25F ||
        rotationLengthSquared > 4.0F) {
        return result;
    }
    result.positionUnits = frame.currentTipUnits;
    result.rotation = fearvr::Normalize(frame.currentRotation);
    result.active = fearvr::IsFinite(result.rotation);
    return result;
}

inline bool PhysicalMeleeCollisionBelongsToEquippedWeapon(
    std::uintptr_t sourceObject,
    std::uintptr_t equippedWeaponModelObject) noexcept {
    return sourceObject != 0U &&
        equippedWeaponModelObject != 0U &&
        sourceObject == equippedWeaponModelObject;
}

inline bool ShouldApplyPhysicalMeleePlayerOverride(
    bool overrideEnabled,
    bool playerOwnedCollision) noexcept {
    return overrideEnabled && playerOwnedCollision;
}

inline bool ShouldDispatchPhysicalMeleeNativeImpact(
    bool wallProxyEnabled,
    bool playerOwnedCollision) noexcept {
    // The physical proxy may suppress the local player's native impact until
    // contact qualification is enabled, but it must never suppress Retail
    // melee owned by enemies or another unrecognised controller.
    return !ShouldApplyPhysicalMeleePlayerOverride(
        wallProxyEnabled, playerOwnedCollision);
}

inline void ResetPhysicalMeleeContactState(
    PhysicalMeleeContactState& state) noexcept {
    state = {};
}

inline bool UpdatePhysicalMeleeContactSeparation(
    PhysicalMeleeContactState& state,
    const fearvr::TrackingVector& currentTipUnits,
    bool trackingFresh,
    const PhysicalMeleeProfile& profile = {}) noexcept {
    if (!trackingFresh || !PhysicalMeleeProfileIsValid(profile) ||
        !fearvr::IsFinite(currentTipUnits)) {
        ResetPhysicalMeleeContactState(state);
        return false;
    }
    if (!state.haveContact || state.armed) {
        return false;
    }
    const float normalLength = PhysicalMeleeLength(
        state.acceptedNormal);
    if (!std::isfinite(normalLength) || normalLength < 0.5F ||
        normalLength > 1.5F) {
        ResetPhysicalMeleeContactState(state);
        return false;
    }
    const fearvr::TrackingVector normal = PhysicalMeleeScale(
        state.acceptedNormal, 1.0F / normalLength);
    const fearvr::TrackingVector travel = PhysicalMeleeSubtract(
        currentTipUnits, state.acceptedTipUnits);
    const float separationMeters = std::fabs(
        PhysicalMeleeDot(travel, normal)) / profile.unitsPerMeter;
    if (!std::isfinite(separationMeters) ||
        separationMeters < profile.contactRearmSeparationMeters) {
        return false;
    }
    ResetPhysicalMeleeContactState(state);
    return true;
}

inline PhysicalMeleeContactQualification QualifyPhysicalMeleeContact(
    PhysicalMeleeContactState& state,
    std::uintptr_t targetId,
    const fearvr::TrackingVector& contactPositionUnits,
    const fearvr::TrackingVector& contactNormal,
    const PhysicalMeleeFrame& frame,
    std::uint64_t sampleId,
    const PhysicalMeleeProfile& profile = {}) noexcept {
    PhysicalMeleeContactQualification result{};
    if (!PhysicalMeleeProfileIsValid(profile)) {
        result.reason = PhysicalMeleeContactReason::InvalidProfile;
        return result;
    }
    if (targetId == 0U) {
        result.reason = PhysicalMeleeContactReason::MissingTarget;
        return result;
    }
    const float normalLength = PhysicalMeleeLength(contactNormal);
    if (!fearvr::IsFinite(contactPositionUnits) ||
        !fearvr::IsFinite(contactNormal) ||
        !std::isfinite(normalLength) || normalLength < 0.5F ||
        normalLength > 1.5F) {
        result.reason = PhysicalMeleeContactReason::InvalidContact;
        return result;
    }
    if (sampleId == 0U || !frame.poseValid || !frame.sweepValid ||
        !fearvr::IsFinite(frame.currentTipUnits) ||
        !fearvr::IsFinite(frame.tipVelocityUnitsPerSecond)) {
        result.reason = PhysicalMeleeContactReason::InvalidFrame;
        return result;
    }
    if (!state.armed || state.haveContact) {
        result.reason = PhysicalMeleeContactReason::ContactLatched;
        return result;
    }

    const fearvr::TrackingVector normal = PhysicalMeleeScale(
        contactNormal, 1.0F / normalLength);
    result.normalSpeedMetersPerSecond = std::fabs(
        PhysicalMeleeDot(
            frame.tipVelocityUnitsPerSecond, normal)) /
        profile.unitsPerMeter;
    result.normalEnergyJoules =
        0.5F * profile.massKilograms *
        result.normalSpeedMetersPerSecond *
        result.normalSpeedMetersPerSecond;
    if (!std::isfinite(result.normalSpeedMetersPerSecond) ||
        result.normalSpeedMetersPerSecond <
            profile.minimumImpactSpeedMetersPerSecond) {
        result.reason = PhysicalMeleeContactReason::BelowNormalSpeed;
        return result;
    }
    if (!std::isfinite(result.normalEnergyJoules) ||
        result.normalEnergyJoules < profile.minimumImpactEnergyJoules) {
        result.reason = PhysicalMeleeContactReason::BelowNormalEnergy;
        return result;
    }

    state.acceptedTipUnits = frame.currentTipUnits;
    state.acceptedNormal = normal;
    state.targetId = targetId;
    state.sampleId = sampleId;
    state.armed = false;
    state.haveContact = true;
    result.reason = PhysicalMeleeContactReason::Accepted;
    result.accepted = true;
    return result;
}

inline void ResetPhysicalMeleeKinematics(
    PhysicalMeleeKinematicsState& state,
    PhysicalMeleeResetReason reason) noexcept {
    state = {};
    state.lastResetReason = reason;
}

inline fearvr::TrackingVector PhysicalMeleeAngularVelocity(
    const fearvr::TrackingQuaternion& previousRotation,
    const fearvr::TrackingQuaternion& currentRotation,
    float deltaSeconds) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
        return {};
    }
    fearvr::TrackingQuaternion delta = fearvr::Multiply(
        fearvr::Normalize(currentRotation),
        fearvr::Conjugate(fearvr::Normalize(previousRotation)));
    if (delta.w < 0.0F) {
        delta = {-delta.x, -delta.y, -delta.z, -delta.w};
    }
    const float vectorLength = std::sqrt(
        delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (!std::isfinite(vectorLength) || vectorLength < 1.0e-6F) {
        return {
            2.0F * delta.x / deltaSeconds,
            2.0F * delta.y / deltaSeconds,
            2.0F * delta.z / deltaSeconds};
    }
    const float angle = 2.0F * std::atan2(
        vectorLength, std::clamp(delta.w, -1.0F, 1.0F));
    const float scale = angle / (vectorLength * deltaSeconds);
    const fearvr::TrackingVector result{
        delta.x * scale, delta.y * scale, delta.z * scale};
    return fearvr::IsFinite(result)
        ? result
        : fearvr::TrackingVector{};
}

// Produces a swept weapon segment and contact-speed evidence from two
// completed controller poses. The function never performs collision queries
// or game writes; the engine adapter consumes only frames with sweepValid.
inline PhysicalMeleeFrame UpdatePhysicalMeleeKinematics(
    PhysicalMeleeKinematicsState& state,
    const PhysicalMeleePose& requestedPose,
    bool trackingFresh,
    std::uint64_t timestampNs,
    const PhysicalMeleeProfile& profile = {}) noexcept {
    PhysicalMeleeFrame frame{};
    if (!PhysicalMeleeProfileIsValid(profile)) {
        ResetPhysicalMeleeKinematics(
            state, PhysicalMeleeResetReason::InvalidProfile);
        frame.resetReason = PhysicalMeleeResetReason::InvalidProfile;
        return frame;
    }
    if (!trackingFresh) {
        ResetPhysicalMeleeKinematics(
            state, PhysicalMeleeResetReason::TrackingLost);
        frame.resetReason = PhysicalMeleeResetReason::TrackingLost;
        return frame;
    }
    if (!PhysicalMeleePoseIsValid(requestedPose)) {
        ResetPhysicalMeleeKinematics(
            state, PhysicalMeleeResetReason::InvalidPose);
        frame.resetReason = PhysicalMeleeResetReason::InvalidPose;
        return frame;
    }

    PhysicalMeleePose pose = requestedPose;
    pose.rotation = fearvr::Normalize(pose.rotation);
    frame.currentRotation = pose.rotation;
    frame.currentBaseUnits = PhysicalMeleeEndpoint(
        pose, profile.localBaseOffsetUnits);
    frame.currentTipUnits = PhysicalMeleeEndpoint(
        pose, profile.localTipOffsetUnits);
    frame.radiusUnits = profile.radiusUnits;
    frame.poseValid = fearvr::IsFinite(frame.currentBaseUnits) &&
        fearvr::IsFinite(frame.currentTipUnits);
    if (!frame.poseValid) {
        ResetPhysicalMeleeKinematics(
            state, PhysicalMeleeResetReason::InvalidPose);
        frame.resetReason = PhysicalMeleeResetReason::InvalidPose;
        return frame;
    }

    if (!state.havePose) {
        const PhysicalMeleeResetReason reason =
            state.lastResetReason == PhysicalMeleeResetReason::TrackingLost
            ? PhysicalMeleeResetReason::TrackingReacquired
            : PhysicalMeleeResetReason::FirstPose;
        state.previousPose = pose;
        state.previousTimeNs = timestampNs;
        state.lastResetReason = reason;
        state.havePose = true;
        frame.previousBaseUnits = frame.currentBaseUnits;
        frame.previousTipUnits = frame.currentTipUnits;
        frame.resetReason = reason;
        return frame;
    }

    if (timestampNs <= state.previousTimeNs) {
        state.previousPose = pose;
        state.previousTimeNs = timestampNs;
        state.lastResetReason = PhysicalMeleeResetReason::NonPositiveTime;
        frame.previousBaseUnits = frame.currentBaseUnits;
        frame.previousTipUnits = frame.currentTipUnits;
        frame.resetReason = PhysicalMeleeResetReason::NonPositiveTime;
        return frame;
    }
    const std::uint64_t deltaNs = timestampNs - state.previousTimeNs;
    if (deltaNs < profile.minimumSampleIntervalNs) {
        state.previousPose = pose;
        state.previousTimeNs = timestampNs;
        state.lastResetReason =
            PhysicalMeleeResetReason::InsufficientSampleInterval;
        frame.previousBaseUnits = frame.currentBaseUnits;
        frame.previousTipUnits = frame.currentTipUnits;
        frame.resetReason =
            PhysicalMeleeResetReason::InsufficientSampleInterval;
        return frame;
    }
    if (deltaNs > profile.maximumSampleGapNs) {
        state.previousPose = pose;
        state.previousTimeNs = timestampNs;
        state.lastResetReason =
            PhysicalMeleeResetReason::ExcessiveSampleGap;
        frame.previousBaseUnits = frame.currentBaseUnits;
        frame.previousTipUnits = frame.currentTipUnits;
        frame.resetReason =
            PhysicalMeleeResetReason::ExcessiveSampleGap;
        return frame;
    }

    frame.previousBaseUnits = PhysicalMeleeEndpoint(
        state.previousPose, profile.localBaseOffsetUnits);
    frame.previousTipUnits = PhysicalMeleeEndpoint(
        state.previousPose, profile.localTipOffsetUnits);
    frame.deltaSeconds = static_cast<float>(
        static_cast<double>(deltaNs) / 1'000'000'000.0);
    const fearvr::TrackingVector gripTravel = PhysicalMeleeSubtract(
        pose.gripPositionUnits,
        state.previousPose.gripPositionUnits);
    const fearvr::TrackingVector baseTravel = PhysicalMeleeSubtract(
        frame.currentBaseUnits, frame.previousBaseUnits);
    const fearvr::TrackingVector tipTravel = PhysicalMeleeSubtract(
        frame.currentTipUnits, frame.previousTipUnits);
    const float maximumTravelUnits = std::max(
        PhysicalMeleeLength(baseTravel),
        PhysicalMeleeLength(tipTravel));
    frame.sweepDistanceMeters =
        maximumTravelUnits / profile.unitsPerMeter;
    if (!std::isfinite(frame.sweepDistanceMeters) ||
        frame.sweepDistanceMeters > profile.maximumSweepDistanceMeters) {
        state.previousPose = pose;
        state.previousTimeNs = timestampNs;
        state.lastResetReason = PhysicalMeleeResetReason::ExcessiveTravel;
        frame.previousBaseUnits = frame.currentBaseUnits;
        frame.previousTipUnits = frame.currentTipUnits;
        frame.deltaSeconds = 0.0F;
        frame.sweepDistanceMeters = 0.0F;
        frame.resetReason = PhysicalMeleeResetReason::ExcessiveTravel;
        return frame;
    }

    const float inverseSeconds = 1.0F / frame.deltaSeconds;
    frame.gripVelocityUnitsPerSecond = PhysicalMeleeScale(
        gripTravel, inverseSeconds);
    frame.baseVelocityUnitsPerSecond = PhysicalMeleeScale(
        baseTravel, inverseSeconds);
    frame.tipVelocityUnitsPerSecond = PhysicalMeleeScale(
        tipTravel, inverseSeconds);
    frame.angularVelocityRadiansPerSecond =
        PhysicalMeleeAngularVelocity(
            state.previousPose.rotation, pose.rotation,
            frame.deltaSeconds);
    const float baseSpeed = PhysicalMeleeLength(
        frame.baseVelocityUnitsPerSecond) / profile.unitsPerMeter;
    const float tipSpeed = PhysicalMeleeLength(
        frame.tipVelocityUnitsPerSecond) / profile.unitsPerMeter;
    frame.impactSpeedMetersPerSecond = std::max(baseSpeed, tipSpeed);
    frame.impactEnergyJoules =
        0.5F * profile.massKilograms *
        frame.impactSpeedMetersPerSecond *
        frame.impactSpeedMetersPerSecond;
    frame.sweepValid = fearvr::IsFinite(
            frame.gripVelocityUnitsPerSecond) &&
        fearvr::IsFinite(frame.baseVelocityUnitsPerSecond) &&
        fearvr::IsFinite(frame.tipVelocityUnitsPerSecond) &&
        fearvr::IsFinite(frame.angularVelocityRadiansPerSecond) &&
        std::isfinite(frame.impactSpeedMetersPerSecond) &&
        std::isfinite(frame.impactEnergyJoules);
    frame.damageQualified = frame.sweepValid &&
        frame.impactSpeedMetersPerSecond >=
            profile.minimumImpactSpeedMetersPerSecond &&
        frame.impactEnergyJoules >=
            profile.minimumImpactEnergyJoules;

    state.previousPose = pose;
    state.previousTimeNs = timestampNs;
    state.lastResetReason = PhysicalMeleeResetReason::None;
    return frame;
}

} // namespace condemnedvr
