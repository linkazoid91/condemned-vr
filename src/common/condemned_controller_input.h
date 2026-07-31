#pragma once

#include <cmath>

#include "input_state.h"

namespace condemnedvr {

constexpr std::uint32_t kCondemnedYawAccelCommand = 23U;
constexpr std::uint32_t kCondemnedActivateCommand = 87U;
constexpr float kCondemnedTurnDeadzone = 0.22F;
constexpr float kCondemnedActivateSqueezeThreshold = 0.65F;
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
