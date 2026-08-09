#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "condemned_calibration_gizmo.h"

namespace condemnedvr {

inline fearvr::TrackingVector PhysicalMeleeColliderGizmoSubtract(
    const fearvr::TrackingVector& left,
    const fearvr::TrackingVector& right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

inline fearvr::TrackingVector PhysicalMeleeColliderGizmoScale(
    const fearvr::TrackingVector& value,
    float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

inline fearvr::TrackingVector PhysicalMeleeColliderGizmoCross(
    const fearvr::TrackingVector& left,
    const fearvr::TrackingVector& right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x};
}

inline WeaponGripCalibrationGizmo BuildPhysicalMeleeColliderGizmo(
    const fearvr::TrackingVector& baseUnits,
    const fearvr::TrackingVector& tipUnits,
    const fearvr::TrackingVector& collisionOriginUnits,
    float radiusUnits,
    bool collisionBodyLive) noexcept {
    WeaponGripCalibrationGizmo gizmo{};
    if (!fearvr::IsFinite(baseUnits) || !fearvr::IsFinite(tipUnits) ||
        !fearvr::IsFinite(collisionOriginUnits) ||
        !std::isfinite(radiusUnits) || radiusUnits <= 0.0F ||
        radiusUnits > 100.0F) {
        return gizmo;
    }

    const fearvr::TrackingVector segment =
        PhysicalMeleeColliderGizmoSubtract(tipUnits, baseUnits);
    const float lengthSquared =
        segment.x * segment.x + segment.y * segment.y +
        segment.z * segment.z;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.01F ||
        lengthSquared > 1.0e6F) {
        return gizmo;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    const fearvr::TrackingVector axis =
        PhysicalMeleeColliderGizmoScale(segment, inverseLength);
    const fearvr::TrackingVector reference =
        std::fabs(axis.y) < 0.9F
            ? fearvr::TrackingVector{0.0F, 1.0F, 0.0F}
            : fearvr::TrackingVector{1.0F, 0.0F, 0.0F};
    fearvr::TrackingVector side =
        PhysicalMeleeColliderGizmoCross(axis, reference);
    const float sideLength = std::sqrt(
        side.x * side.x + side.y * side.y + side.z * side.z);
    if (!std::isfinite(sideLength) || sideLength <= 0.001F) {
        return gizmo;
    }
    side = PhysicalMeleeColliderGizmoScale(side, 1.0F / sideLength);
    const fearvr::TrackingVector up =
        PhysicalMeleeColliderGizmoCross(side, axis);
    const std::uint32_t volumeColor = collisionBodyLive
        ? 0xE050FF90U
        : 0xE0FFB040U;
    const std::uint32_t originColor = collisionBodyLive
        ? 0xFFFFFFFFU
        : 0xFFFF6040U;
    constexpr std::size_t kRingSegments = 12U;
    constexpr float kTwoPi = 6.28318530717958647692F;
    const fearvr::TrackingVector centers[] = {
        baseUnits,
        {(baseUnits.x + tipUnits.x) * 0.5F,
         (baseUnits.y + tipUnits.y) * 0.5F,
         (baseUnits.z + tipUnits.z) * 0.5F},
        tipUnits};
    const auto RingPoint = [&](const fearvr::TrackingVector& center,
                               float angle) noexcept {
        return CalibrationGizmoAdd(
            center,
            CalibrationGizmoAdd(
                PhysicalMeleeColliderGizmoScale(
                    side, std::cos(angle) * radiusUnits),
                PhysicalMeleeColliderGizmoScale(
                    up, std::sin(angle) * radiusUnits)));
    };
    for (const fearvr::TrackingVector& center : centers) {
        for (std::size_t segmentIndex = 0;
             segmentIndex < kRingSegments; ++segmentIndex) {
            const float angle0 = kTwoPi *
                static_cast<float>(segmentIndex) /
                static_cast<float>(kRingSegments);
            const float angle1 = kTwoPi *
                static_cast<float>(segmentIndex + 1U) /
                static_cast<float>(kRingSegments);
            AddWeaponGripCalibrationGizmoLine(
                gizmo, RingPoint(center, angle0),
                RingPoint(center, angle1), volumeColor);
        }
    }
    for (std::size_t rail = 0; rail < 4U; ++rail) {
        const float angle = kTwoPi * static_cast<float>(rail) / 4.0F;
        AddWeaponGripCalibrationGizmoLine(
            gizmo, RingPoint(baseUnits, angle),
            RingPoint(tipUnits, angle), volumeColor);
    }
    AddWeaponGripCalibrationGizmoLine(
        gizmo, baseUnits, tipUnits, 0xE0FFFF40U);
    constexpr float kOriginCrossHalfUnits = 6.0F;
    const fearvr::TrackingVector originAxes[] = {
        {kOriginCrossHalfUnits, 0.0F, 0.0F},
        {0.0F, kOriginCrossHalfUnits, 0.0F},
        {0.0F, 0.0F, kOriginCrossHalfUnits}};
    for (const fearvr::TrackingVector& originAxis : originAxes) {
        AddWeaponGripCalibrationGizmoLine(
            gizmo,
            PhysicalMeleeColliderGizmoSubtract(
                collisionOriginUnits, originAxis),
            CalibrationGizmoAdd(collisionOriginUnits, originAxis),
            originColor);
    }
    gizmo.valid = gizmo.count != 0U;
    return gizmo;
}

} // namespace condemnedvr
