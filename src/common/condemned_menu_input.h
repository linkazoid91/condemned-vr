#pragma once

#include <cmath>
#include <cstdint>

#include "input_state.h"

namespace condemnedvr {

enum class MenuNavigationAction : std::uint8_t {
    none = 0,
    up,
    down,
    left,
    right,
    accept,
    back,
};

constexpr float kMenuNavigationAxisPressThreshold = 0.65F;
constexpr float kMenuNavigationAxisReleaseThreshold = 0.35F;
constexpr float kMenuNavigationTriggerThreshold = 0.65F;
constexpr std::uint64_t kMenuNavigationInitialRepeatMilliseconds = 350;
constexpr std::uint64_t kMenuNavigationRepeatMilliseconds = 110;

struct MenuNavigationState {
    bool releaseRequired{true};
    bool acceptWasDown{false};
    bool backWasDown{false};
    MenuNavigationAction heldAxis{MenuNavigationAction::none};
    std::uint64_t nextAxisRepeatMilliseconds{0};
};

inline const char* MenuNavigationActionName(
    MenuNavigationAction action) noexcept {
    switch (action) {
    case MenuNavigationAction::up:
        return "up";
    case MenuNavigationAction::down:
        return "down";
    case MenuNavigationAction::left:
        return "left";
    case MenuNavigationAction::right:
        return "right";
    case MenuNavigationAction::accept:
        return "accept";
    case MenuNavigationAction::back:
        return "back";
    default:
        return "none";
    }
}

inline void RequireMenuNavigationRelease(
    MenuNavigationState& state) noexcept {
    state.releaseRequired = true;
    state.acceptWasDown = false;
    state.backWasDown = false;
    state.heldAxis = MenuNavigationAction::none;
    state.nextAxisRepeatMilliseconds = 0;
}

inline float FiniteMenuAxis(float value) noexcept {
    return std::isfinite(value) ? value : 0.0F;
}

inline MenuNavigationAction ResolveMenuNavigationAxis(
    float x,
    float y,
    MenuNavigationAction heldAxis) noexcept {
    x = FiniteMenuAxis(x);
    y = FiniteMenuAxis(y);

    switch (heldAxis) {
    case MenuNavigationAction::up:
        if (y >= kMenuNavigationAxisReleaseThreshold) {
            return heldAxis;
        }
        break;
    case MenuNavigationAction::down:
        if (y <= -kMenuNavigationAxisReleaseThreshold) {
            return heldAxis;
        }
        break;
    case MenuNavigationAction::left:
        if (x <= -kMenuNavigationAxisReleaseThreshold) {
            return heldAxis;
        }
        break;
    case MenuNavigationAction::right:
        if (x >= kMenuNavigationAxisReleaseThreshold) {
            return heldAxis;
        }
        break;
    default:
        break;
    }

    const float absoluteX = std::fabs(x);
    const float absoluteY = std::fabs(y);
    if (absoluteX < kMenuNavigationAxisPressThreshold &&
        absoluteY < kMenuNavigationAxisPressThreshold) {
        return MenuNavigationAction::none;
    }
    if (absoluteY >= absoluteX) {
        return y >= 0.0F
            ? MenuNavigationAction::up
            : MenuNavigationAction::down;
    }
    return x >= 0.0F
        ? MenuNavigationAction::right
        : MenuNavigationAction::left;
}

// Returns at most one native menu action per game update. Back and Accept
// edges take priority over stick motion. Entering a menu, losing focus or
// losing either controller requires every mapped input to return to neutral
// before a new action can be emitted.
inline MenuNavigationAction UpdateMenuNavigation(
    MenuNavigationState& state,
    const FearVrInputState& input,
    bool sampleFresh,
    bool menuActive,
    std::uint64_t nowMilliseconds) noexcept {
    const bool usable = menuActive &&
        fearvr::IsInputStateUsable(input, sampleFresh) &&
        (input.activeHands &
         (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT)) ==
            (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT);
    if (!usable) {
        RequireMenuNavigationRelease(state);
        return MenuNavigationAction::none;
    }

    const float x = FiniteMenuAxis(input.moveX);
    const float y = FiniteMenuAxis(input.moveY);
    const float trigger = std::isfinite(
        input.trigger[FEARVR_HAND_RIGHT])
        ? input.trigger[FEARVR_HAND_RIGHT]
        : 0.0F;
    const bool acceptDown =
        (input.buttons & FEARVR_IB_RIGHT_PRIMARY) != 0 ||
        trigger >= kMenuNavigationTriggerThreshold;
    const bool backDown =
        (input.buttons & FEARVR_IB_RIGHT_SECONDARY) != 0;
    const bool controlsReleased =
        std::fabs(x) <= kMenuNavigationAxisReleaseThreshold &&
        std::fabs(y) <= kMenuNavigationAxisReleaseThreshold &&
        !acceptDown && !backDown;

    if (state.releaseRequired) {
        state.acceptWasDown = acceptDown;
        state.backWasDown = backDown;
        state.heldAxis = MenuNavigationAction::none;
        state.nextAxisRepeatMilliseconds = 0;
        if (controlsReleased) {
            state.releaseRequired = false;
            state.acceptWasDown = false;
            state.backWasDown = false;
        }
        return MenuNavigationAction::none;
    }

    const bool acceptPressed = acceptDown && !state.acceptWasDown;
    const bool backPressed = backDown && !state.backWasDown;
    state.acceptWasDown = acceptDown;
    state.backWasDown = backDown;

    const MenuNavigationAction resolvedAxis =
        ResolveMenuNavigationAxis(x, y, state.heldAxis);
    MenuNavigationAction axisAction = MenuNavigationAction::none;
    if (resolvedAxis == MenuNavigationAction::none) {
        state.heldAxis = MenuNavigationAction::none;
        state.nextAxisRepeatMilliseconds = 0;
    } else if (resolvedAxis != state.heldAxis) {
        state.heldAxis = resolvedAxis;
        state.nextAxisRepeatMilliseconds =
            nowMilliseconds + kMenuNavigationInitialRepeatMilliseconds;
        axisAction = resolvedAxis;
    } else if (nowMilliseconds >= state.nextAxisRepeatMilliseconds) {
        state.nextAxisRepeatMilliseconds =
            nowMilliseconds + kMenuNavigationRepeatMilliseconds;
        axisAction = resolvedAxis;
    }

    if (backPressed) {
        return MenuNavigationAction::back;
    }
    if (acceptPressed) {
        return MenuNavigationAction::accept;
    }
    return axisAction;
}

} // namespace condemnedvr
