#include <cassert>
#include <cstdint>
#include <limits>

#include "condemned_arm_ik_lifecycle.h"

int main() {
    using condemnedvr::ArmIkLifecycleAction;
    using condemnedvr::ArmIkLifecycleAllowsInstall;
    using condemnedvr::ArmIkLifecycleState;
    using condemnedvr::ObserveArmIkGameState;

    ArmIkLifecycleState lifecycle{};
    assert(lifecycle.generation == 1U);
    assert(ArmIkLifecycleAllowsInstall(lifecycle));

    auto transition = ObserveArmIkGameState(
        lifecycle, condemnedvr::kCondemnedGameStatePlaying);
    assert(transition.action == ArmIkLifecycleAction::None);
    assert(lifecycle.generation == 1U);
    assert(ArmIkLifecycleAllowsInstall(lifecycle));

    transition = ObserveArmIkGameState(
        lifecycle, condemnedvr::kCondemnedGameStateMenu);
    assert(transition.action == ArmIkLifecycleAction::None);
    assert(lifecycle.generation == 1U);
    assert(!ArmIkLifecycleAllowsInstall(lifecycle));
    transition = ObserveArmIkGameState(
        lifecycle, condemnedvr::kCondemnedGameStatePlaying);
    assert(transition.action == ArmIkLifecycleAction::None);
    assert(lifecycle.generation == 1U);
    assert(ArmIkLifecycleAllowsInstall(lifecycle));

    // Match the observed save path: pause menu -> front-end screen -> load.
    transition = ObserveArmIkGameState(
        lifecycle, condemnedvr::kCondemnedGameStateMenu);
    assert(transition.action == ArmIkLifecycleAction::None);
    transition = ObserveArmIkGameState(
        lifecycle, condemnedvr::kCondemnedGameStateScreen);
    assert(transition.action == ArmIkLifecycleAction::None);
    assert(lifecycle.generation == 1U);
    assert(!ArmIkLifecycleAllowsInstall(lifecycle));
    transition = ObserveArmIkGameState(
        lifecycle, condemnedvr::kCondemnedGameStateLoading);
    assert(transition.action ==
        ArmIkLifecycleAction::InvalidateForLoad);
    assert(transition.previousGameState ==
        condemnedvr::kCondemnedGameStateScreen);
    assert(lifecycle.generation == 2U);
    assert(!ArmIkLifecycleAllowsInstall(lifecycle));

    transition = ObserveArmIkGameState(
        lifecycle, condemnedvr::kCondemnedGameStateLoading);
    assert(transition.action == ArmIkLifecycleAction::None);
    assert(lifecycle.generation == 2U);
    transition = ObserveArmIkGameState(
        lifecycle, condemnedvr::kCondemnedGameStateScreen);
    assert(transition.action == ArmIkLifecycleAction::None);
    assert(lifecycle.generation == 2U);
    assert(!ArmIkLifecycleAllowsInstall(lifecycle));
    transition = ObserveArmIkGameState(lifecycle, -1);
    assert(transition.action == ArmIkLifecycleAction::None);
    assert(lifecycle.lastKnownGameState ==
        condemnedvr::kCondemnedGameStateScreen);

    transition = ObserveArmIkGameState(
        lifecycle, condemnedvr::kCondemnedGameStatePlaying);
    assert(transition.action == ArmIkLifecycleAction::ResumeAfterLoad);
    assert(lifecycle.generation == 2U);
    assert(ArmIkLifecycleAllowsInstall(lifecycle));

    ArmIkLifecycleState initialLoad{};
    transition = ObserveArmIkGameState(
        initialLoad, condemnedvr::kCondemnedGameStateLoading);
    assert(transition.action ==
        ArmIkLifecycleAction::InvalidateForLoad);
    assert(initialLoad.generation == 2U);

    assert(condemnedvr::NextArmIkLifecycleGeneration(
               std::numeric_limits<std::uint32_t>::max()) == 1U);
    return 0;
}
