#pragma once

#include "input_state.h"

namespace condemnedvr {

struct LocomotionDirections {
    bool forward{false};
    bool backward{false};
    bool left{false};
    bool right{false};
};

inline LocomotionDirections ResolveLocomotionDirections(
    const FearVrInputState& state, bool sampleFresh,
    float threshold = 0.30F) noexcept {
    LocomotionDirections directions;
    if (!fearvr::IsInputStateUsable(state, sampleFresh) ||
        (state.activeHands & FEARVR_HAND_MASK_LEFT) == 0) {
        return directions;
    }

    directions.forward = state.moveY > threshold;
    directions.backward = state.moveY < -threshold;
    directions.left = state.moveX < -threshold;
    directions.right = state.moveX > threshold;
    return directions;
}

} // namespace condemnedvr
