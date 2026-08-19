#pragma once

#include <cstdint>
#include <limits>

#include "condemned_controller_input.h"

namespace condemnedvr {

enum class ArmIkLifecycleAction : std::uint32_t {
    None = 0U,
    InvalidateForLoad,
    ResumeAfterLoad
};

struct ArmIkLifecycleState {
    int lastKnownGameState{-1};
    std::uint32_t generation{1U};
    bool gameStateObserved{false};
    bool awaitingGameplayAfterLoad{false};
};

struct ArmIkLifecycleTransition {
    ArmIkLifecycleAction action{ArmIkLifecycleAction::None};
    int previousGameState{-1};
    int currentGameState{-1};
    std::uint32_t generation{1U};
};

inline std::uint32_t NextArmIkLifecycleGeneration(
    std::uint32_t generation) noexcept {
    return generation == std::numeric_limits<std::uint32_t>::max()
        ? 1U : generation + 1U;
}

// Retail can clear a model's node-control table while reusing the same
// m_hPlayerBody address during save/load. Treat a witnessed Loading state as
// the authoritative callback-lifetime discontinuity. Menu/Paused transitions
// deliberately retain the generation so an ordinary pause cannot duplicate
// callback registration.
inline ArmIkLifecycleTransition ObserveArmIkGameState(
    ArmIkLifecycleState& state,
    int gameState) noexcept {
    ArmIkLifecycleTransition transition{};
    transition.previousGameState = state.lastKnownGameState;
    transition.currentGameState = gameState;
    transition.generation = state.generation;

    if (!IsKnownCondemnedGameState(gameState)) {
        return transition;
    }

    const bool stateChanged = !state.gameStateObserved ||
        state.lastKnownGameState != gameState;
    state.gameStateObserved = true;
    state.lastKnownGameState = gameState;
    if (!stateChanged) {
        return transition;
    }

    if (gameState == kCondemnedGameStateLoading &&
        !state.awaitingGameplayAfterLoad) {
        state.awaitingGameplayAfterLoad = true;
        state.generation = NextArmIkLifecycleGeneration(
            state.generation);
        transition.action = ArmIkLifecycleAction::InvalidateForLoad;
        transition.generation = state.generation;
    } else if (gameState == kCondemnedGameStatePlaying &&
               state.awaitingGameplayAfterLoad) {
        state.awaitingGameplayAfterLoad = false;
        transition.action = ArmIkLifecycleAction::ResumeAfterLoad;
        transition.generation = state.generation;
    }
    return transition;
}

inline bool ArmIkLifecycleAllowsInstall(
    const ArmIkLifecycleState& state) noexcept {
    return !state.gameStateObserved ||
        (!state.awaitingGameplayAfterLoad &&
         state.lastKnownGameState == kCondemnedGameStatePlaying);
}

} // namespace condemnedvr
