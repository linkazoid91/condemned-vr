#pragma once

#include <cmath>

#include "head_tracking_math.h"

namespace fearvr {

constexpr float kMonoPanelDistanceMeters = 2.0F;

struct MonoPanelAnchor {
    TrackingVector positionMeters;
    TrackingQuaternion orientation;
    bool valid{false};
};

// Places the centre of a level, world-locked panel exactly on the current
// binocular HMD gaze ray. Pitch changes the centre position, while the panel
// itself remains upright and uses only the current HMD yaw.
inline MonoPanelAnchor ResolveMonoPanelAnchor(
    const FearVrPose& leftEye,
    const FearVrPose& rightEye,
    float distanceMeters = kMonoPanelDistanceMeters) noexcept {
    if (!std::isfinite(distanceMeters) || distanceMeters <= 0.0F) {
        return {};
    }

    FearVrRenderRequest views{};
    views.eye[FEARVR_EYE_LEFT].pose = leftEye;
    views.eye[FEARVR_EYE_RIGHT].pose = rightEye;
    const FearVrPose center = CenterHeadPose(views);
    if (!IsValidPose(center)) {
        return {};
    }

    const TrackingVector forward = Rotate(
        PoseRotation(center), {0.0F, 0.0F, -1.0F});
    const FearVrPose level = YawOnlyRecenterPose(center);
    if (!IsFinite(forward) || !IsValidPose(level)) {
        return {};
    }

    MonoPanelAnchor anchor{};
    anchor.positionMeters = {
        center.px + forward.x * distanceMeters,
        center.py + forward.y * distanceMeters,
        center.pz + forward.z * distanceMeters};
    anchor.orientation = PoseRotation(level);
    anchor.valid = IsFinite(anchor.positionMeters) &&
        IsFinite(anchor.orientation);
    return anchor;
}

struct MonoPanelStartupAnchorState {
    bool gameImageObserved{false};
};

// The host begins rendering before Condemned is launched. Re-anchor exactly
// once when the first connected game image arrives so the pre-game host pose
// cannot determine where the visible startup/menu panel remains world-locked.
// A later game reconnect receives the same one-time treatment.
inline bool ConsumeFirstGameImageAnchor(
    MonoPanelStartupAnchorState& state,
    bool gameConnected,
    bool gameImageReady) noexcept {
    if (!gameConnected) {
        state.gameImageObserved = false;
        return false;
    }
    if (!gameImageReady || state.gameImageObserved) {
        return false;
    }
    state.gameImageObserved = true;
    return true;
}

} // namespace fearvr
