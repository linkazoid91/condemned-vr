#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "head_tracking_math.h"

namespace condemnedvr {

struct WeaponGripCalibrationGizmoLine {
    fearvr::TrackingVector startUnits{};
    fearvr::TrackingVector endUnits{};
    std::uint32_t argb{0xFFFFFFFFU};
};

constexpr std::size_t kWeaponGripCalibrationGizmoMaximumLines = 48;

struct WeaponGripCalibrationGizmo {
    std::array<
        WeaponGripCalibrationGizmoLine,
        kWeaponGripCalibrationGizmoMaximumLines> lines{};
    std::size_t count{0};
    bool valid{false};
};

inline fearvr::TrackingVector CalibrationGizmoAdd(
    const fearvr::TrackingVector& left,
    const fearvr::TrackingVector& right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

inline bool CalibrationGizmoQuaternionIsValid(
    const fearvr::TrackingQuaternion& rotation) noexcept {
    const float lengthSquared =
        rotation.x * rotation.x + rotation.y * rotation.y +
        rotation.z * rotation.z + rotation.w * rotation.w;
    return fearvr::IsFinite(rotation) &&
        std::isfinite(lengthSquared) &&
        lengthSquared >= 0.25F && lengthSquared <= 4.0F;
}

inline fearvr::TrackingVector CalibrationGizmoTransformPoint(
    const fearvr::TrackingVector& position,
    const fearvr::TrackingQuaternion& rotation,
    const fearvr::TrackingVector& localPoint) noexcept {
    return CalibrationGizmoAdd(
        position, fearvr::Rotate(rotation, localPoint));
}

inline void AddWeaponGripCalibrationGizmoLine(
    WeaponGripCalibrationGizmo& gizmo,
    const fearvr::TrackingVector& start,
    const fearvr::TrackingVector& end,
    std::uint32_t argb) noexcept {
    if (gizmo.count >= gizmo.lines.size()) {
        return;
    }
    gizmo.lines[gizmo.count++] = {start, end, argb};
}

// A compact generic controller silhouette is more stable for calibration
// than a runtime/vendor-specific shell asset. The outline follows the OpenXR
// grip pose; the long yellow ray separately shows the aim pose used by the
// weapon solver.
inline WeaponGripCalibrationGizmo BuildWeaponGripCalibrationGizmo(
    const fearvr::TrackingVector& gripWorldPosition,
    const fearvr::TrackingQuaternion& gripWorldRotation,
    const fearvr::TrackingQuaternion& aimWorldRotation) noexcept {
    WeaponGripCalibrationGizmo gizmo{};
    if (!fearvr::IsFinite(gripWorldPosition) ||
        !CalibrationGizmoQuaternionIsValid(gripWorldRotation) ||
        !CalibrationGizmoQuaternionIsValid(aimWorldRotation)) {
        return gizmo;
    }
    const fearvr::TrackingQuaternion gripRotation =
        fearvr::Normalize(gripWorldRotation);
    const fearvr::TrackingQuaternion aimRotation =
        fearvr::Normalize(aimWorldRotation);
    const auto GripPoint = [&](const fearvr::TrackingVector& local) {
        return CalibrationGizmoTransformPoint(
            gripWorldPosition, gripRotation, local);
    };

    constexpr std::uint32_t kOutline = 0xE0D8ECFFU;
    constexpr std::uint32_t kFace = 0xE080C8FFU;
    constexpr std::uint32_t kGrip = 0xFFFF40FFU;
    constexpr std::uint32_t kAim = 0xFFFFFF20U;
    constexpr std::uint32_t kAxisX = 0xFFFF4040U;
    constexpr std::uint32_t kAxisY = 0xFF40FF60U;
    constexpr std::uint32_t kAxisZ = 0xFF4080FFU;

    const fearvr::TrackingVector handleLocal[] = {
        {-1.8F, -8.0F, -1.5F}, {1.8F, -8.0F, -1.5F},
        {1.8F, 2.0F, -1.5F}, {-1.8F, 2.0F, -1.5F},
        {-1.8F, -8.0F, 1.5F}, {1.8F, -8.0F, 1.5F},
        {1.8F, 2.0F, 1.5F}, {-1.8F, 2.0F, 1.5F}};
    constexpr std::size_t handleEdges[][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& edge : handleEdges) {
        AddWeaponGripCalibrationGizmoLine(
            gizmo, GripPoint(handleLocal[edge[0]]),
            GripPoint(handleLocal[edge[1]]), kOutline);
    }

    const fearvr::TrackingVector faceLocal[] = {
        {-3.2F, 2.2F, -2.0F}, {3.2F, 2.2F, -2.0F},
        {3.2F, 2.2F, 4.0F}, {-3.2F, 2.2F, 4.0F}};
    constexpr std::size_t faceEdges[][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}};
    for (const auto& edge : faceEdges) {
        AddWeaponGripCalibrationGizmoLine(
            gizmo, GripPoint(faceLocal[edge[0]]),
            GripPoint(faceLocal[edge[1]]), kFace);
    }
    AddWeaponGripCalibrationGizmoLine(
        gizmo, GripPoint(handleLocal[3]), GripPoint(faceLocal[0]), kFace);
    AddWeaponGripCalibrationGizmoLine(
        gizmo, GripPoint(handleLocal[2]), GripPoint(faceLocal[1]), kFace);
    AddWeaponGripCalibrationGizmoLine(
        gizmo, GripPoint(handleLocal[6]), GripPoint(faceLocal[2]), kFace);
    AddWeaponGripCalibrationGizmoLine(
        gizmo, GripPoint(handleLocal[7]), GripPoint(faceLocal[3]), kFace);

    constexpr std::size_t kRingSegments = 12;
    constexpr float kTwoPi = 6.28318530717958647692F;
    for (std::size_t segment = 0; segment < kRingSegments; ++segment) {
        const float angle0 = kTwoPi * static_cast<float>(segment) /
            static_cast<float>(kRingSegments);
        const float angle1 = kTwoPi * static_cast<float>(segment + 1) /
            static_cast<float>(kRingSegments);
        const fearvr::TrackingVector local0{
            std::cos(angle0) * 4.4F, 3.0F,
            1.0F + std::sin(angle0) * 4.4F};
        const fearvr::TrackingVector local1{
            std::cos(angle1) * 4.4F, 3.0F,
            1.0F + std::sin(angle1) * 4.4F};
        AddWeaponGripCalibrationGizmoLine(
            gizmo, GripPoint(local0), GripPoint(local1), kOutline);
    }

    AddWeaponGripCalibrationGizmoLine(
        gizmo, GripPoint({-2.5F, 0.0F, 0.0F}),
        GripPoint({2.5F, 0.0F, 0.0F}), kGrip);
    AddWeaponGripCalibrationGizmoLine(
        gizmo, GripPoint({0.0F, -2.5F, 0.0F}),
        GripPoint({0.0F, 2.5F, 0.0F}), kGrip);
    AddWeaponGripCalibrationGizmoLine(
        gizmo, GripPoint({0.0F, 0.0F, -2.5F}),
        GripPoint({0.0F, 0.0F, 2.5F}), kGrip);
    AddWeaponGripCalibrationGizmoLine(
        gizmo, gripWorldPosition, GripPoint({5.0F, 0.0F, 0.0F}),
        kAxisX);
    AddWeaponGripCalibrationGizmoLine(
        gizmo, gripWorldPosition, GripPoint({0.0F, 5.0F, 0.0F}),
        kAxisY);
    AddWeaponGripCalibrationGizmoLine(
        gizmo, gripWorldPosition, GripPoint({0.0F, 0.0F, 5.0F}),
        kAxisZ);
    AddWeaponGripCalibrationGizmoLine(
        gizmo, gripWorldPosition,
        CalibrationGizmoTransformPoint(
            gripWorldPosition, aimRotation, {0.0F, 0.0F, 25.0F}),
        kAim);
    gizmo.valid = gizmo.count != 0;
    return gizmo;
}

struct WeaponGripCalibrationGizmoCamera {
    fearvr::TrackingVector positionUnits{};
    fearvr::TrackingQuaternion rotation{};
    float horizontalFovRadians{0.0F};
    float verticalFovRadians{0.0F};
};

inline bool ProjectWeaponGripCalibrationPointToNdc(
    const fearvr::TrackingVector& worldPoint,
    const WeaponGripCalibrationGizmoCamera& camera,
    float& ndcX,
    float& ndcY) noexcept {
    ndcX = 0.0F;
    ndcY = 0.0F;
    if (!fearvr::IsFinite(worldPoint) ||
        !fearvr::IsFinite(camera.positionUnits) ||
        !CalibrationGizmoQuaternionIsValid(camera.rotation) ||
        !std::isfinite(camera.horizontalFovRadians) ||
        !std::isfinite(camera.verticalFovRadians) ||
        camera.horizontalFovRadians <= 0.05F ||
        camera.verticalFovRadians <= 0.05F ||
        camera.horizontalFovRadians >= 3.10F ||
        camera.verticalFovRadians >= 3.10F) {
        return false;
    }
    const fearvr::TrackingVector delta{
        worldPoint.x - camera.positionUnits.x,
        worldPoint.y - camera.positionUnits.y,
        worldPoint.z - camera.positionUnits.z};
    const fearvr::TrackingVector local = fearvr::Rotate(
        fearvr::Conjugate(fearvr::Normalize(camera.rotation)), delta);
    if (!fearvr::IsFinite(local) || local.z <= 0.5F) {
        return false;
    }
    const float tangentX = std::tan(
        camera.horizontalFovRadians * 0.5F);
    const float tangentY = std::tan(
        camera.verticalFovRadians * 0.5F);
    if (!std::isfinite(tangentX) || !std::isfinite(tangentY) ||
        tangentX <= 0.0F || tangentY <= 0.0F) {
        return false;
    }
    ndcX = local.x / (local.z * tangentX);
    ndcY = local.y / (local.z * tangentY);
    return std::isfinite(ndcX) && std::isfinite(ndcY);
}

inline std::size_t ProjectWeaponGripCalibrationGizmoToNdc(
    const WeaponGripCalibrationGizmo& gizmo,
    const WeaponGripCalibrationGizmoCamera& camera,
    FearVrOverlayLineVertex* output,
    std::size_t outputCapacity) noexcept {
    if (!gizmo.valid || output == nullptr || outputCapacity < 2) {
        return 0;
    }
    std::size_t used = 0;
    for (std::size_t index = 0;
         index < gizmo.count && used + 2 <= outputCapacity; ++index) {
        float startX = 0.0F;
        float startY = 0.0F;
        float endX = 0.0F;
        float endY = 0.0F;
        if (!ProjectWeaponGripCalibrationPointToNdc(
                gizmo.lines[index].startUnits, camera,
                startX, startY) ||
            !ProjectWeaponGripCalibrationPointToNdc(
                gizmo.lines[index].endUnits, camera,
                endX, endY)) {
            continue;
        }
        output[used++] = {
            startX, startY, gizmo.lines[index].argb};
        output[used++] = {
            endX, endY, gizmo.lines[index].argb};
    }
    return used;
}

} // namespace condemnedvr
