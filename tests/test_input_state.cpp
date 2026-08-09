#include <cmath>
#include <cstring>
#include <cstdio>
#include <limits>

#include "input_state.h"
#include "condemned_controller_input.h"
#include "condemned_locomotion.h"

namespace {

int Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

bool Near(float left, float right) {
    return std::fabs(left - right) < 0.0001F;
}

} // namespace

int main() {
    FearVrInputState state{};
    state.sampleId = 9;
    state.flags = FEARVR_IF_VALID | FEARVR_IF_FOCUSED;
    state.activeHands =
        FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT;
    state.buttons =
        FEARVR_IB_LEFT_PRIMARY | FEARVR_IB_RIGHT_STICK;
    state.moveX = 0.7F;
    state.moveY = -0.8F;
    state.turnX = 0.5F;
    state.turnY = -0.4F;
    state.trigger[0] = 1.0F;
    state.squeeze[1] = 0.6F;
    state.aimPoseValidHands = FEARVR_HAND_MASK_RIGHT;
    state.handAimPose[FEARVR_HAND_RIGHT].px = 0.25F;
    state.handAimPose[FEARVR_HAND_RIGHT].qw = 1.0F;
    state.gripPoseValidHands = FEARVR_HAND_MASK_RIGHT;
    state.handGripPose[FEARVR_HAND_RIGHT].py = -0.15F;
    state.handGripPose[FEARVR_HAND_RIGHT].qw = 1.0F;

    if (!fearvr::IsInputStateUsable(state, true) ||
        fearvr::IsInputStateUsable(state, false)) {
        return Fail("fresh and focused validity is incorrect");
    }
    fearvr::NeutralizeInputState(state);
    if (state.sampleId != 9 ||
        (state.flags & FEARVR_IF_VALID) == 0 ||
        (state.flags & FEARVR_IF_FOCUSED) != 0 ||
        state.activeHands != 0 || state.buttons != 0 ||
        state.aimPoseValidHands != 0 ||
        state.gripPoseValidHands != 0 ||
        state.handAimPose[FEARVR_HAND_RIGHT].px != 0.0F ||
        state.handAimPose[FEARVR_HAND_RIGHT].qw != 0.0F ||
        state.handGripPose[FEARVR_HAND_RIGHT].py != 0.0F ||
        state.handGripPose[FEARVR_HAND_RIGHT].qw != 0.0F ||
        state.moveX != 0.0F || state.moveY != 0.0F ||
        state.turnX != 0.0F || state.turnY != 0.0F ||
        state.trigger[0] != 0.0F ||
        state.squeeze[1] != 0.0F) {
        return Fail("focus loss must publish a complete neutral state");
    }

    if (!Near(fearvr::ApplyInputDeadzone(0.1F, 0.2F), 0.0F) ||
        !Near(fearvr::ApplyInputDeadzone(0.6F, 0.2F), 0.5F) ||
        !Near(fearvr::ApplyInputDeadzone(-0.6F, 0.2F), -0.5F) ||
        !Near(fearvr::ApplyInputDeadzone(2.0F, 0.2F), 1.0F) ||
        fearvr::ApplyInputDeadzone(
            std::numeric_limits<float>::quiet_NaN(), 0.2F) != 0.0F) {
        return Fail("deadzone normalization is incorrect");
    }

    // A roll about the pose's own forward axis must come back signed, with
    // positive meaning the top of the hand is tipped to the user's left.
    FearVrPose upright{};
    upright.qw = 1.0F;
    if (!Near(fearvr::PoseRollRadians(upright), 0.0F)) {
        return Fail("an upright pose must report zero roll");
    }
    FearVrPose zeroQuaternion{};
    if (fearvr::PoseRollRadians(zeroQuaternion) != 0.0F) {
        return Fail("a degenerate quaternion must report zero roll");
    }
    for (int degrees = -80; degrees <= 80; degrees += 20) {
        const float angle =
            static_cast<float>(degrees) * 3.14159265F / 180.0F;
        FearVrPose rolled{};
        rolled.qz = std::sin(angle * 0.5F);
        rolled.qw = std::cos(angle * 0.5F);
        if (!Near(fearvr::PoseRollRadians(rolled), angle)) {
            return Fail("roll extraction must match the applied angle");
        }
        // An unnormalised quaternion must yield the same angle.
        FearVrPose scaled{};
        scaled.qz = rolled.qz * 3.0F;
        scaled.qw = rolled.qw * 3.0F;
        if (!Near(fearvr::PoseRollRadians(scaled), angle)) {
            return Fail("roll extraction must ignore quaternion scale");
        }
    }

    // Levelness is 1 for any pure roll and collapses to 0 when the pose points
    // straight down, which is what makes the roll unusable there.
    if (!Near(fearvr::PoseLevelness(upright), 1.0F)) {
        return Fail("an upright pose must be fully level");
    }
    FearVrPose rolledFlat{};
    rolledFlat.qz = std::sin(0.7853981F);
    rolledFlat.qw = std::cos(0.7853981F);
    if (!Near(fearvr::PoseLevelness(rolledFlat), 1.0F)) {
        return Fail("a pure roll must not reduce levelness");
    }
    FearVrPose pitchedDown{};
    pitchedDown.qx = std::sin(-0.7853981F);
    pitchedDown.qw = std::cos(-0.7853981F);
    if (!Near(fearvr::PoseLevelness(pitchedDown), 0.0F)) {
        return Fail("a pose pointing straight down must not be level");
    }
    if (fearvr::PoseLevelness(zeroQuaternion) != 0.0F) {
        return Fail("a degenerate quaternion must report zero levelness");
    }

    FearVrInputState lean{};
    lean.handAimPose[FEARVR_HAND_LEFT].qz = std::sin(0.5F * 0.5F);
    lean.handAimPose[FEARVR_HAND_LEFT].qw = std::cos(0.5F * 0.5F);
    if (fearvr::LeftHandLeanRollRadians(lean) != 0.0F) {
        return Fail("lean roll must be zero while the left hand is idle");
    }
    lean.activeHands = FEARVR_HAND_MASK_LEFT;
    if (fearvr::LeftHandLeanRollRadians(lean) != 0.0F) {
        return Fail("lean roll must be zero without a valid aim pose");
    }
    lean.aimPoseValidHands = FEARVR_HAND_MASK_LEFT;
    if (!Near(fearvr::LeftHandLeanRollRadians(lean), 0.5F)) {
        return Fail("lean roll must follow the left aim pose");
    }

    FearVrInputState locomotion{};
    locomotion.sampleId = 12;
    locomotion.flags = FEARVR_IF_VALID | FEARVR_IF_FOCUSED;
    locomotion.activeHands = FEARVR_HAND_MASK_LEFT;
    locomotion.moveX = -0.75F;
    locomotion.moveY = 0.80F;
    auto directions = condemnedvr::ResolveLocomotionDirections(
        locomotion, true);
    if (!directions.forward || directions.backward ||
        !directions.left || directions.right) {
        return Fail("left stick must resolve to forward-left locomotion");
    }
    directions = condemnedvr::ResolveLocomotionDirections(
        locomotion, false);
    if (directions.forward || directions.backward ||
        directions.left || directions.right) {
        return Fail("stale controller input must resolve to neutral");
    }
    locomotion.activeHands = FEARVR_HAND_MASK_RIGHT;
    directions = condemnedvr::ResolveLocomotionDirections(
        locomotion, true);
    if (directions.forward || directions.backward ||
        directions.left || directions.right) {
        return Fail("an inactive left hand must resolve to neutral");
    }

    FearVrInputState turning{};
    turning.flags = FEARVR_IF_VALID | FEARVR_IF_FOCUSED;
    turning.activeHands = FEARVR_HAND_MASK_RIGHT;
    turning.turnX = 0.61F;
    turning.turnY = -1.0F;
    auto turnValue = condemnedvr::ResolveTurningValue(
        turning, true);
    if (!turnValue.active || !Near(turnValue.value, 0.5F)) {
        return Fail("right stick must resolve to deadzone-adjusted turning");
    }
    turning.turnX = -0.61F;
    turnValue = condemnedvr::ResolveTurningValue(turning, true);
    if (!turnValue.active || !Near(turnValue.value, -0.5F)) {
        return Fail("right-stick turning must preserve polarity");
    }
    turning.turnX = 0.21F;
    if (condemnedvr::ResolveTurningValue(turning, true).active) {
        return Fail("right-stick turning must respect its deadzone");
    }
    turning.turnX = std::numeric_limits<float>::quiet_NaN();
    if (condemnedvr::ResolveTurningValue(turning, true).active) {
        return Fail("non-finite right-stick input must resolve to neutral");
    }
    turning.turnX = 0.75F;
    if (condemnedvr::ResolveTurningValue(turning, false).active) {
        return Fail("stale right-stick input must resolve to neutral");
    }
    turning.activeHands = FEARVR_HAND_MASK_LEFT;
    if (condemnedvr::ResolveTurningValue(turning, true).active) {
        return Fail("an inactive right hand must not turn");
    }

    const condemnedvr::TurningValue weakTurn{0.4F, true};
    const condemnedvr::TurningValue strongTurn{-0.8F, true};
    if (!Near(
            condemnedvr::MergeTurningWithRetail(0.6F, weakTurn),
            0.6F) ||
        !Near(
            condemnedvr::MergeTurningWithRetail(0.6F, strongTurn),
            -0.8F) ||
        !Near(
            condemnedvr::MergeTurningWithRetail(
                0.4F,
                condemnedvr::TurningValue{-0.4F, true}),
            0.4F) ||
        !Near(
            condemnedvr::MergeTurningWithRetail(
                0.6F, condemnedvr::TurningValue{}),
            0.6F)) {
        return Fail("turning must preserve the strongest Retail or VR value");
    }
    const float nonFiniteRetail =
        std::numeric_limits<float>::quiet_NaN();
    if (!std::isnan(condemnedvr::MergeTurningWithRetail(
            nonFiniteRetail, strongTurn))) {
        return Fail("a non-finite Retail turn value must fail closed");
    }

    FearVrInputState interaction{};
    interaction.flags = FEARVR_IF_VALID | FEARVR_IF_FOCUSED;
    interaction.activeHands = FEARVR_HAND_MASK_RIGHT;
    interaction.squeeze[FEARVR_HAND_RIGHT] = 0.64F;
    if (condemnedvr::ResolveActivateValue(
            interaction, true).active) {
        return Fail("right grip must respect the Activate threshold");
    }
    interaction.squeeze[FEARVR_HAND_RIGHT] = 0.65F;
    const condemnedvr::ActivateValue activate =
        condemnedvr::ResolveActivateValue(interaction, true);
    if (!activate.active || !Near(activate.value, 1.0F)) {
        return Fail("right grip must resolve to the Activate command");
    }
    if (condemnedvr::ResolveActivateValue(
            interaction, false).active) {
        return Fail("stale input must not activate interactions");
    }
    interaction.activeHands = FEARVR_HAND_MASK_LEFT;
    if (condemnedvr::ResolveActivateValue(
            interaction, true).active) {
        return Fail("an inactive right hand must not activate interactions");
    }
    if (!Near(
            condemnedvr::MergeActivateWithRetail(0.25F, activate),
            1.0F) ||
        !Near(
            condemnedvr::MergeActivateWithRetail(1.25F, activate),
            1.25F) ||
        !Near(
            condemnedvr::MergeActivateWithRetail(
                0.25F, condemnedvr::ActivateValue{}),
            0.25F)) {
        return Fail("Activate must preserve the strongest Retail or VR value");
    }
    if (!std::isnan(condemnedvr::MergeActivateWithRetail(
            nonFiniteRetail, activate))) {
        return Fail("a non-finite Retail Activate value must fail closed");
    }

    FearVrInputState actions{};
    actions.flags = FEARVR_IF_VALID | FEARVR_IF_FOCUSED;
    actions.activeHands =
        FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT;
    actions.squeeze[FEARVR_HAND_LEFT] = 0.65F;
    actions.trigger[FEARVR_HAND_RIGHT] = 0.55F;
    actions.trigger[FEARVR_HAND_LEFT] = 0.55F;
    actions.turnY = 0.75F;
    actions.buttons = FEARVR_IB_RIGHT_PRIMARY |
        FEARVR_IB_RIGHT_SECONDARY |
        FEARVR_IB_LEFT_STICK |
        FEARVR_IB_LEFT_PRIMARY;
    const std::uint32_t coreCommands[] = {
        condemnedvr::kCondemnedRunCommand,
        condemnedvr::kCondemnedFireCommand,
        condemnedvr::kCondemnedBlockCommand,
        condemnedvr::kCondemnedToggleMeleeCommand,
        condemnedvr::kCondemnedAmmoCheckCommand,
        condemnedvr::kCondemnedStunGunCommand,
        condemnedvr::kCondemnedFlashlightCommand,
        condemnedvr::kCondemnedToolsCommand};
    for (const std::uint32_t command : coreCommands) {
        const condemnedvr::CoreActionValue action =
            condemnedvr::ResolveCoreActionValue(actions, true, command);
        if (!action.active || !Near(action.value, 1.0F) ||
            condemnedvr::CondemnedCoreActionIndex(command) < 0 ||
            std::strcmp(
                condemnedvr::CondemnedCoreActionControlName(command),
                "unmapped") == 0) {
            return Fail("every guarded core control must map exactly once");
        }
        if (!Near(
                condemnedvr::MergeCoreActionWithRetail(0.25F, action),
                1.0F) ||
            !Near(
                condemnedvr::MergeCoreActionWithRetail(1.25F, action),
                1.25F)) {
            return Fail(
                "core actions must preserve the strongest Retail or VR value");
        }
    }
    if (condemnedvr::ResolveCoreActionValue(
            actions, false,
            condemnedvr::kCondemnedFireCommand).active) {
        return Fail("stale input must neutralize every core action");
    }
    actions.flags = FEARVR_IF_VALID;
    if (condemnedvr::ResolveCoreActionValue(
            actions, true,
            condemnedvr::kCondemnedBlockCommand).active) {
        return Fail("unfocused input must neutralize every core action");
    }
    actions.flags = FEARVR_IF_VALID | FEARVR_IF_FOCUSED;
    actions.activeHands = FEARVR_HAND_MASK_LEFT;
    if (condemnedvr::ResolveCoreActionValue(
            actions, true,
            condemnedvr::kCondemnedFireCommand).active) {
        return Fail("an inactive right hand must not fire");
    }
    if (condemnedvr::ResolveCoreActionValue(
            actions, true,
            condemnedvr::kCondemnedToolsCommand).active) {
        return Fail("an inactive right hand must not ready forensic tools");
    }
    actions.activeHands = FEARVR_HAND_MASK_RIGHT;
    if (condemnedvr::ResolveCoreActionValue(
            actions, true,
            condemnedvr::kCondemnedRunCommand).active) {
        return Fail("an inactive left hand must not run");
    }
    actions.activeHands =
        FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT;
    actions.turnY = 0.74F;
    if (condemnedvr::ResolveCoreActionValue(
            actions, true,
            condemnedvr::kCondemnedToolsCommand).active) {
        return Fail("forensic tools must require a deliberate stick-up gesture");
    }
    actions.turnY = 0.0F;
    actions.turnX = 1.0F;
    if (condemnedvr::ResolveCoreActionValue(
            actions, true,
            condemnedvr::kCondemnedToolsCommand).active) {
        return Fail("ordinary right-stick turning must not ready forensic tools");
    }
    if (condemnedvr::CondemnedCoreActionIndex(999U) != -1 ||
        condemnedvr::ResolveCoreActionValue(
            actions, true, 999U).active ||
        !std::isnan(condemnedvr::MergeCoreActionWithRetail(
            nonFiniteRetail,
            condemnedvr::CoreActionValue{1.0F, true}))) {
        return Fail("unknown or non-finite core actions must fail closed");
    }

    FearVrInputState calibrationInput{};
    calibrationInput.flags = FEARVR_IF_VALID | FEARVR_IF_FOCUSED;
    calibrationInput.activeHands =
        FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT;
    calibrationInput.squeeze[FEARVR_HAND_LEFT] = 0.75F;
    calibrationInput.squeeze[FEARVR_HAND_RIGHT] = 0.75F;
    calibrationInput.turnX = 0.625F;
    calibrationInput.turnY = -0.625F;
    calibrationInput.moveY = 1.0F;
    calibrationInput.buttons =
        FEARVR_IB_RIGHT_PRIMARY | FEARVR_IB_RIGHT_SECONDARY |
        FEARVR_IB_LEFT_PRIMARY | FEARVR_IB_LEFT_SECONDARY |
        FEARVR_IB_LEFT_STICK | FEARVR_IB_RIGHT_STICK;
    const auto calibrationControls =
        condemnedvr::ResolveWeaponGripCalibrationControls(
            calibrationInput, true);
    if (!calibrationControls.captured ||
        !Near(calibrationControls.x, 0.5F) ||
        !Near(calibrationControls.y, -0.5F) ||
        !Near(calibrationControls.z, 1.0F) ||
        !calibrationControls.positionDown ||
        !calibrationControls.rotationDown ||
        !calibrationControls.resetDown ||
        !calibrationControls.snapshotDown ||
        !calibrationControls.finerDown ||
        !calibrationControls.coarserDown) {
        return Fail(
            "the two-grip calibration chord must map every setup control");
    }
    calibrationInput.squeeze[FEARVR_HAND_LEFT] = 0.74F;
    if (condemnedvr::ResolveWeaponGripCalibrationControls(
            calibrationInput, true).captured) {
        return Fail("one loose grip must release calibration capture");
    }
    calibrationInput.squeeze[FEARVR_HAND_LEFT] = 0.75F;
    if (condemnedvr::ResolveWeaponGripCalibrationControls(
            calibrationInput, false).captured) {
        return Fail("stale controller state must not capture calibration");
    }
    calibrationInput.activeHands = FEARVR_HAND_MASK_RIGHT;
    if (condemnedvr::ResolveWeaponGripCalibrationControls(
            calibrationInput, true).captured) {
        return Fail("both active hands are required for calibration capture");
    }

    const auto firePulse = condemnedvr::ResolveCoreActionHapticPulse(
        condemnedvr::kCondemnedFireCommand);
    const auto blockPulse = condemnedvr::ResolveCoreActionHapticPulse(
        condemnedvr::kCondemnedBlockCommand);
    const auto activatePulse = condemnedvr::ResolveCoreActionHapticPulse(
        condemnedvr::kCondemnedActivateCommand);
    const auto unsupportedPulse =
        condemnedvr::ResolveCoreActionHapticPulse(
            condemnedvr::kCondemnedFlashlightCommand);
    if (!firePulse.active ||
        firePulse.handMask != FEARVR_HAND_MASK_RIGHT ||
        firePulse.durationNs != 35'000'000ULL ||
        !Near(firePulse.amplitude, 0.25F) ||
        !blockPulse.active ||
        blockPulse.handMask != FEARVR_HAND_MASK_LEFT ||
        blockPulse.durationNs != 25'000'000ULL ||
        !Near(blockPulse.amplitude, 0.18F) ||
        !activatePulse.active ||
        activatePulse.handMask != FEARVR_HAND_MASK_RIGHT ||
        activatePulse.durationNs != 20'000'000ULL ||
        !Near(activatePulse.amplitude, 0.15F) ||
        unsupportedPulse.active) {
        return Fail("M4 haptic pulses must stay bounded and hand-specific");
    }

    FearVrInputState controllerAimInput{};
    controllerAimInput.flags = FEARVR_IF_VALID | FEARVR_IF_FOCUSED;
    controllerAimInput.activeHands = FEARVR_HAND_MASK_RIGHT;
    controllerAimInput.aimPoseValidHands = FEARVR_HAND_MASK_RIGHT;
    controllerAimInput.handAimPose[FEARVR_HAND_RIGHT].qw = 1.0F;
    FearVrPose trackingRecenter{};
    trackingRecenter.qw = 1.0F;
    const fearvr::TrackingQuaternion retailBase{};
    auto controllerAim = condemnedvr::ResolveControllerAimRotation(
        controllerAimInput, true, trackingRecenter, retailBase);
    auto controllerForward = fearvr::Rotate(
        controllerAim.worldRotation, {0.0F, 0.0F, 1.0F});
    if (!controllerAim.active || !Near(controllerForward.x, 0.0F) ||
        !Near(controllerForward.y, 0.0F) ||
        !Near(controllerForward.z, 1.0F)) {
        return Fail(
            "neutral right-controller aim must map to engine +Z forward");
    }

    constexpr float kHalfSqrtTwo = 0.70710678F;
    controllerAimInput.handAimPose[FEARVR_HAND_RIGHT].qy =
        kHalfSqrtTwo;
    controllerAimInput.handAimPose[FEARVR_HAND_RIGHT].qw =
        kHalfSqrtTwo;
    controllerAim = condemnedvr::ResolveControllerAimRotation(
        controllerAimInput, true, trackingRecenter, retailBase);
    controllerForward = fearvr::Rotate(
        controllerAim.worldRotation, {0.0F, 0.0F, 1.0F});
    if (!controllerAim.active || !Near(controllerForward.x, -1.0F) ||
        !Near(controllerForward.y, 0.0F) ||
        !Near(controllerForward.z, 0.0F)) {
        return Fail(
            "OpenXR controller -Z forward must preserve a leftward aim");
    }

    FearVrInputState controllerPoseInput = controllerAimInput;
    controllerPoseInput.handAimPose[FEARVR_HAND_RIGHT].px = 0.2F;
    controllerPoseInput.handAimPose[FEARVR_HAND_RIGHT].py = -0.1F;
    controllerPoseInput.handAimPose[FEARVR_HAND_RIGHT].pz = -0.5F;
    controllerPoseInput.handAimPose[FEARVR_HAND_RIGHT].qx = 0.0F;
    controllerPoseInput.handAimPose[FEARVR_HAND_RIGHT].qy = 0.0F;
    controllerPoseInput.handAimPose[FEARVR_HAND_RIGHT].qz = 0.0F;
    controllerPoseInput.handAimPose[FEARVR_HAND_RIGHT].qw = 1.0F;
    const fearvr::TrackingVector retailCameraPosition{
        1000.0F, 2000.0F, 3000.0F};
    auto controllerWorldPose = condemnedvr::ResolveControllerAimWorldPose(
        controllerPoseInput, true, trackingRecenter,
        retailCameraPosition, retailBase, 100.0F);
    if (!controllerWorldPose.active ||
        !Near(controllerWorldPose.worldPosition.x, 1020.0F) ||
        !Near(controllerWorldPose.worldPosition.y, 1990.0F) ||
        !Near(controllerWorldPose.worldPosition.z, 3050.0F)) {
        return Fail(
            "controller position must share the stereo LithTech world basis");
    }
    FearVrInputState controllerGripInput = controllerPoseInput;
    controllerGripInput.gripPoseValidHands =
        FEARVR_HAND_MASK_RIGHT;
    controllerGripInput.handGripPose[FEARVR_HAND_RIGHT].px = -0.1F;
    controllerGripInput.handGripPose[FEARVR_HAND_RIGHT].py = 0.05F;
    controllerGripInput.handGripPose[FEARVR_HAND_RIGHT].pz = -0.25F;
    controllerGripInput.handGripPose[FEARVR_HAND_RIGHT].qw = 1.0F;
    const auto controllerGripWorldPose =
        condemnedvr::ResolveControllerGripWorldPose(
            controllerGripInput, true, trackingRecenter,
            retailCameraPosition, retailBase, 100.0F);
    if (!controllerGripWorldPose.active ||
        !Near(controllerGripWorldPose.worldPosition.x, 990.0F) ||
        !Near(controllerGripWorldPose.worldPosition.y, 2005.0F) ||
        !Near(controllerGripWorldPose.worldPosition.z, 3025.0F)) {
        return Fail(
            "held models must originate at the distinct OpenXR grip pose");
    }
    controllerGripInput.activeHands |= FEARVR_HAND_MASK_LEFT;
    controllerGripInput.gripPoseValidHands |= FEARVR_HAND_MASK_LEFT;
    controllerGripInput.handGripPose[FEARVR_HAND_LEFT].px = 0.3F;
    controllerGripInput.handGripPose[FEARVR_HAND_LEFT].py = 0.1F;
    controllerGripInput.handGripPose[FEARVR_HAND_LEFT].pz = -0.4F;
    controllerGripInput.handGripPose[FEARVR_HAND_LEFT].qw = 1.0F;
    const auto leftGripWorldPose =
        condemnedvr::ResolveControllerGripWorldPoseForHand(
            controllerGripInput, true, trackingRecenter,
            retailCameraPosition, retailBase,
            FEARVR_HAND_LEFT, 100.0F);
    if (!leftGripWorldPose.active ||
        !Near(leftGripWorldPose.worldPosition.x, 1030.0F) ||
        !Near(leftGripWorldPose.worldPosition.y, 2010.0F) ||
        !Near(leftGripWorldPose.worldPosition.z, 3040.0F) ||
        condemnedvr::ResolveControllerGripWorldPoseForHand(
            controllerGripInput, true, trackingRecenter,
            retailCameraPosition, retailBase,
            FEARVR_HAND_COUNT, 100.0F).active) {
        return Fail(
            "support-hand grip must resolve independently and fail closed");
    }
    controllerGripInput.gripPoseValidHands = 0;
    if (condemnedvr::ResolveControllerGripWorldPose(
            controllerGripInput, true, trackingRecenter,
            retailCameraPosition, retailBase, 100.0F).active) {
        return Fail("an invalid grip pose must not drive a held model");
    }
    controllerWorldPose = condemnedvr::ResolveControllerAimWorldPose(
        controllerPoseInput, true, trackingRecenter,
        retailCameraPosition,
        fearvr::TrackingQuaternion{
            0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo},
        100.0F);
    if (!controllerWorldPose.active ||
        !Near(controllerWorldPose.worldPosition.x, 1050.0F) ||
        !Near(controllerWorldPose.worldPosition.y, 1990.0F) ||
        !Near(controllerWorldPose.worldPosition.z, 2980.0F)) {
        return Fail(
            "controller position must rotate through the Retail camera base");
    }
    if (condemnedvr::ResolveControllerAimWorldPose(
            controllerPoseInput, false, trackingRecenter,
            retailCameraPosition, retailBase, 100.0F).active ||
        condemnedvr::ResolveControllerAimWorldPose(
            controllerPoseInput, true, trackingRecenter,
            retailCameraPosition, retailBase, 0.0F).active) {
        return Fail(
            "stale controller poses and invalid world scales must fail closed");
    }
    if (condemnedvr::ResolveControllerAimRotation(
            controllerAimInput, false,
            trackingRecenter, retailBase).active) {
        return Fail("stale controller tracking must not drive weapon aim");
    }
    controllerAimInput.aimPoseValidHands = 0;
    if (condemnedvr::ResolveControllerAimRotation(
            controllerAimInput, true,
            trackingRecenter, retailBase).active) {
        return Fail("an invalid right aim pose must preserve Retail aim");
    }
    controllerAimInput.aimPoseValidHands = FEARVR_HAND_MASK_RIGHT;
    if (condemnedvr::ResolveControllerAimRotation(
            controllerAimInput, true, trackingRecenter,
            fearvr::TrackingQuaternion{0.0F, 0.0F, 0.0F, 0.0F}).active) {
        return Fail("an invalid Retail base must preserve Retail aim");
    }

    const fearvr::TrackingVector meleePivot{10.0F, 20.0F, 30.0F};
    const fearvr::TrackingVector retailMeleePosition{
        10.0F, 20.0F, 35.0F};
    const fearvr::TrackingQuaternion identityRotation{};
    const fearvr::TrackingQuaternion rightwardRotation{
        0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};
    const auto redirectedMelee =
        condemnedvr::ResolveControllerRelativeMeleeTransform(
            retailMeleePosition, identityRotation, meleePivot,
            identityRotation, rightwardRotation, true);
    const auto redirectedMeleeForward = fearvr::Rotate(
        redirectedMelee.rotation, {0.0F, 0.0F, 1.0F});
    if (!redirectedMelee.active ||
        !Near(redirectedMelee.position.x, 15.0F) ||
        !Near(redirectedMelee.position.y, 20.0F) ||
        !Near(redirectedMelee.position.z, 30.0F) ||
        !Near(redirectedMeleeForward.x, 1.0F) ||
        !Near(redirectedMeleeForward.y, 0.0F) ||
        !Near(redirectedMeleeForward.z, 0.0F)) {
        return Fail(
            "controller melee aim must redirect the complete Retail arc");
    }
    if (condemnedvr::ResolveControllerRelativeMeleeTransform(
            retailMeleePosition, identityRotation, meleePivot,
            identityRotation, rightwardRotation, false).active ||
        condemnedvr::ResolveControllerRelativeMeleeTransform(
            retailMeleePosition, identityRotation, meleePivot,
            fearvr::TrackingQuaternion{0.0F, 0.0F, 0.0F, 0.0F},
            rightwardRotation, true).active) {
        return Fail(
            "stale or invalid tracking must preserve Retail melee transforms");
    }

    FearVrInputState recenter{};
    recenter.flags = FEARVR_IF_VALID | FEARVR_IF_FOCUSED;
    recenter.activeHands = FEARVR_HAND_MASK_RIGHT;
    recenter.buttons = FEARVR_IB_RIGHT_STICK;
    condemnedvr::RecenterLatch recenterLatch;
    if (condemnedvr::ConsumeRecenterPress(
            recenterLatch, recenter, true)) {
        return Fail("a held recenter button at startup must not trigger");
    }
    recenter.buttons = 0;
    condemnedvr::ConsumeRecenterPress(
        recenterLatch, recenter, true);
    recenter.buttons = FEARVR_IB_RIGHT_STICK;
    if (!condemnedvr::ConsumeRecenterPress(
            recenterLatch, recenter, true) ||
        condemnedvr::ConsumeRecenterPress(
            recenterLatch, recenter, true)) {
        return Fail("recenter must trigger once per released press");
    }
    if (condemnedvr::ConsumeRecenterPress(
            recenterLatch, recenter, false)) {
        return Fail("stale input must not recenter tracking");
    }
    if (condemnedvr::ConsumeRecenterPress(
            recenterLatch, recenter, true)) {
        return Fail("freshness recovery while held must require release");
    }

    FearVrInputState menu{};
    menu.flags = FEARVR_IF_VALID | FEARVR_IF_FOCUSED;
    menu.activeHands = FEARVR_HAND_MASK_LEFT;
    menu.buttons = FEARVR_IB_LEFT_SECONDARY;
    condemnedvr::MenuToggleLatch startupHeldLatch;
    if (condemnedvr::ConsumeMenuTogglePress(
            startupHeldLatch, menu, true)) {
        return Fail("a startup-held menu button must require release");
    }
    menu.buttons = 0;
    condemnedvr::ConsumeMenuTogglePress(
        startupHeldLatch, menu, true);
    menu.buttons = FEARVR_IB_LEFT_SECONDARY;
    if (!condemnedvr::ConsumeMenuTogglePress(
            startupHeldLatch, menu, true)) {
        return Fail("startup-held menu input must re-arm after release");
    }

    menu.buttons = 0;
    condemnedvr::MenuToggleLatch menuLatch;
    if (condemnedvr::ConsumeMenuTogglePress(menuLatch, menu, true)) {
        return Fail("menu input must prime from a released button");
    }
    menu.buttons = FEARVR_IB_LEFT_PRIMARY;
    if (condemnedvr::ConsumeMenuTogglePress(menuLatch, menu, true)) {
        return Fail("only left secondary may toggle the pause menu");
    }
    menu.buttons = FEARVR_IB_LEFT_SECONDARY;
    if (!condemnedvr::ConsumeMenuTogglePress(menuLatch, menu, true) ||
        condemnedvr::ConsumeMenuTogglePress(menuLatch, menu, true)) {
        return Fail("a held menu button must produce one rising edge");
    }
    if (condemnedvr::ConsumeMenuTogglePress(menuLatch, menu, false)) {
        return Fail("stale input must not toggle the pause menu");
    }
    if (condemnedvr::ConsumeMenuTogglePress(menuLatch, menu, true)) {
        return Fail("focus recovery while held must require release");
    }
    menu.buttons = 0;
    if (condemnedvr::ConsumeMenuTogglePress(menuLatch, menu, true)) {
        return Fail("menu-button release must not toggle");
    }
    menu.buttons = FEARVR_IB_LEFT_SECONDARY;
    if (!condemnedvr::ConsumeMenuTogglePress(menuLatch, menu, true)) {
        return Fail("a released menu button must re-arm the next edge");
    }
    menu.flags = FEARVR_IF_VALID;
    if (condemnedvr::ConsumeMenuTogglePress(menuLatch, menu, true)) {
        return Fail("fresh but unfocused menu input must remain neutral");
    }
    menu.flags = FEARVR_IF_VALID | FEARVR_IF_FOCUSED;
    if (condemnedvr::ConsumeMenuTogglePress(menuLatch, menu, true)) {
        return Fail("focus recovery while held must still require release");
    }
    menu.activeHands = FEARVR_HAND_MASK_RIGHT;
    if (condemnedvr::ConsumeMenuTogglePress(menuLatch, menu, true)) {
        return Fail("an inactive left hand must not toggle the menu");
    }

    for (int gameState = condemnedvr::kCondemnedGameStateUndefined;
         gameState < condemnedvr::kCondemnedGameStateCount;
         ++gameState) {
        const bool expectedFlatPanel =
            gameState != condemnedvr::kCondemnedGameStatePlaying;
        if (!condemnedvr::IsKnownCondemnedGameState(gameState) ||
            condemnedvr::CondemnedGameStateUsesFlatPanel(gameState) !=
                expectedFlatPanel) {
            return Fail(
                "only Retail gameplay state may use native stereo");
        }
        const bool expectedMenuToggle =
            gameState == condemnedvr::kCondemnedGameStatePlaying ||
            gameState == condemnedvr::kCondemnedGameStateMenu;
        if (condemnedvr::CondemnedGameStateAllowsMenuToggle(gameState) !=
            expectedMenuToggle) {
            return Fail(
                "synthetic Escape must be limited to gameplay and menu");
        }
    }
    const int unknownGameStates[] = {
        -1, condemnedvr::kCondemnedGameStateCount};
    for (const int unknownState : unknownGameStates) {
        if (condemnedvr::IsKnownCondemnedGameState(unknownState) ||
            !condemnedvr::CondemnedGameStateUsesFlatPanel(unknownState) ||
            condemnedvr::CondemnedGameStateAllowsMenuToggle(unknownState)) {
            return Fail(
                "an unreadable Retail state must fail to a non-interactive panel");
        }
    }

    // Linkshaenderbelegung: ein einziger Tausch dreht Stoecke, Trigger,
    // Griffe, Tasten, Handmasken und Posen.
    FearVrInputState handed{};
    handed.moveX = 0.25F;
    handed.moveY = -0.5F;
    handed.turnX = 0.75F;
    handed.turnY = 1.0F;
    handed.trigger[FEARVR_HAND_LEFT] = 0.1F;
    handed.trigger[FEARVR_HAND_RIGHT] = 0.9F;
    handed.squeeze[FEARVR_HAND_LEFT] = 0.2F;
    handed.squeeze[FEARVR_HAND_RIGHT] = 0.8F;
    handed.buttons =
        FEARVR_IB_LEFT_PRIMARY | FEARVR_IB_RIGHT_SECONDARY |
        FEARVR_IB_RIGHT_STICK;
    handed.activeHands = FEARVR_HAND_MASK_LEFT;
    handed.aimPoseValidHands =
        FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT;
    handed.gripPoseValidHands = FEARVR_HAND_MASK_RIGHT;
    handed.handAimPose[FEARVR_HAND_LEFT].px = 1.0F;
    handed.handAimPose[FEARVR_HAND_RIGHT].px = 2.0F;
    handed.handGripPose[FEARVR_HAND_LEFT].pz = 3.0F;
    handed.handGripPose[FEARVR_HAND_RIGHT].pz = 4.0F;

    const FearVrInputState original = handed;
    fearvr::MirrorInputHandedness(handed);
    if (handed.moveX != original.turnX ||
        handed.turnX != original.moveX ||
        handed.moveY != original.turnY ||
        handed.turnY != original.moveY) {
        return Fail("mirroring must swap both sticks");
    }
    if (handed.trigger[FEARVR_HAND_RIGHT] != 0.1F ||
        handed.squeeze[FEARVR_HAND_RIGHT] != 0.2F) {
        return Fail("mirroring must swap triggers and grips");
    }
    if (handed.buttons !=
        (FEARVR_IB_RIGHT_PRIMARY | FEARVR_IB_LEFT_SECONDARY |
         FEARVR_IB_LEFT_STICK)) {
        return Fail("mirroring must swap the button bits per hand");
    }
    if (handed.activeHands != FEARVR_HAND_MASK_RIGHT ||
        handed.gripPoseValidHands != FEARVR_HAND_MASK_LEFT ||
        handed.aimPoseValidHands !=
            (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT)) {
        return Fail("mirroring must swap the hand masks");
    }
    if (handed.handAimPose[FEARVR_HAND_RIGHT].px != 1.0F ||
        handed.handGripPose[FEARVR_HAND_RIGHT].pz != 3.0F) {
        return Fail("mirroring must swap the tracked poses");
    }

    // Zweimal gespiegelt ist der Ausgangszustand.
    fearvr::MirrorInputHandedness(handed);
    if (handed.moveX != original.moveX ||
        handed.turnY != original.turnY ||
        handed.buttons != original.buttons ||
        handed.activeHands != original.activeHands ||
        handed.aimPoseValidHands != original.aimPoseValidHands ||
        handed.gripPoseValidHands != original.gripPoseValidHands ||
        handed.trigger[FEARVR_HAND_LEFT] !=
            original.trigger[FEARVR_HAND_LEFT] ||
        handed.handAimPose[FEARVR_HAND_LEFT].px !=
            original.handAimPose[FEARVR_HAND_LEFT].px ||
        handed.handGripPose[FEARVR_HAND_LEFT].pz !=
            original.handGripPose[FEARVR_HAND_LEFT].pz) {
        return Fail("mirroring twice must restore the original state");
    }

    std::puts("test_input_state: OK");
    return 0;
}
