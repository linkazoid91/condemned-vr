#include <cmath>
#include <cstdio>

#include "condemned_block_pose.h"

namespace {
int Fail(const char* message) {
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

fearvr::TrackingQuaternion Yaw(float degrees) {
    constexpr float kToRadians = 0.017453292519943295F;
    const float half = degrees * kToRadians * 0.5F;
    return {0.0F, std::sin(half), 0.0F, std::cos(half)};
}

fearvr::TrackingQuaternion Pitch(float degrees) {
    constexpr float kToRadians = 0.017453292519943295F;
    const float half = degrees * kToRadians * 0.5F;
    return {std::sin(half), 0.0F, 0.0F, std::cos(half)};
}

condemnedvr::PhysicalMeleeBlockWorldPose RotateAroundHead(
    const condemnedvr::PhysicalMeleeBlockWorldPose& head,
    const condemnedvr::PhysicalMeleeBlockWorldPose& weapon,
    const fearvr::TrackingQuaternion& yaw) {
    const fearvr::TrackingVector delta{
        weapon.positionUnits.x - head.positionUnits.x,
        weapon.positionUnits.y - head.positionUnits.y,
        weapon.positionUnits.z - head.positionUnits.z};
    const fearvr::TrackingVector rotated = fearvr::Rotate(yaw, delta);
    return {{head.positionUnits.x + rotated.x,
             head.positionUnits.y + rotated.y,
             head.positionUnits.z + rotated.z},
            fearvr::Multiply(yaw, weapon.rotation)};
}
} // namespace

int main() {
    using namespace condemnedvr;
    const PhysicalMeleeBlockWorldPose head{
        {100.0F, 170.0F, 200.0F}, {0.0F, 0.0F, 0.0F, 1.0F}};
    const PhysicalMeleeBlockWorldPose weapon{
        {130.0F, 135.0F, 250.0F},
        fearvr::Multiply(Yaw(-20.0F), Pitch(35.0F))};
    PhysicalMeleeBlockPoseSettings settings{};
    if (!CapturePhysicalMeleeBlockPose(
            head, weapon, 100.0F, settings) ||
        !settings.enabled || !settings.captured ||
        !PhysicalMeleeBlockPoseSettingsAreValid(settings)) {
        return Fail("a valid guard pose must capture and enable");
    }

    PhysicalMeleeBlockPoseState state{};
    auto result = EvaluatePhysicalMeleeBlockPose(
        settings, head, weapon, 100.0F, true, state);
    if (!result.active || !result.entered || result.exited ||
        result.positionErrorMeters > 0.0001F ||
        result.angleErrorDegrees > 0.01F) {
        return Fail("the captured pose must enter block immediately");
    }
    const auto turn = Yaw(90.0F);
    auto turnedHead = head;
    turnedHead.rotation = turn;
    const auto turnedWeapon = RotateAroundHead(head, weapon, turn);
    result = EvaluatePhysicalMeleeBlockPose(
        settings, turnedHead, turnedWeapon, 100.0F, true, state);
    if (!result.active) {
        return Fail("head and weapon yaw together must preserve guard");
    }
    auto movedHead = turnedHead;
    auto movedWeapon = turnedWeapon;
    movedHead.positionUnits.x += 275.0F;
    movedHead.positionUnits.z -= 125.0F;
    movedWeapon.positionUnits.x += 275.0F;
    movedWeapon.positionUnits.z -= 125.0F;
    result = EvaluatePhysicalMeleeBlockPose(
        settings, movedHead, movedWeapon, 100.0F, true, state);
    if (!result.active || result.positionErrorMeters > 0.0001F) {
        return Fail("world locomotion must preserve head-relative guard");
    }

    auto pitchedHead = head;
    pitchedHead.rotation = Pitch(-35.0F);
    result = EvaluatePhysicalMeleeBlockPose(
        settings, pitchedHead, weapon, 100.0F, true, state);
    if (!result.active || result.angleErrorDegrees > 0.01F) {
        return Fail("head pitch alone must not invalidate guard");
    }
    auto jitteredWeapon = weapon;
    jitteredWeapon.positionUnits.x += 20.0F;
    result = EvaluatePhysicalMeleeBlockPose(
        settings, head, jitteredWeapon, 100.0F, true, state);
    if (!result.active || result.exited) {
        return Fail("an active guard must use release hysteresis");
    }
    PhysicalMeleeBlockPoseState freshState{};
    result = EvaluatePhysicalMeleeBlockPose(
        settings, head, jitteredWeapon, 100.0F, true, freshState);
    if (result.active || result.reason !=
            PhysicalMeleeBlockPoseReason::PositionOutside) {
        return Fail("outside entry tolerance must not newly block");
    }

    auto releasedWeapon = weapon;
    releasedWeapon.positionUnits.x += 30.0F;
    result = EvaluatePhysicalMeleeBlockPose(
        settings, head, releasedWeapon, 100.0F, true, state);
    if (result.active || !result.exited || result.reason !=
            PhysicalMeleeBlockPoseReason::PositionOutside) {
        return Fail("leaving release tolerance must exit guard once");
    }
    auto rotatedWeapon = weapon;
    rotatedWeapon.rotation = fearvr::Multiply(
        weapon.rotation, Yaw(40.0F));
    result = EvaluatePhysicalMeleeBlockPose(
        settings, head, rotatedWeapon, 100.0F, true, state);
    if (result.active || result.reason !=
            PhysicalMeleeBlockPoseReason::AngleOutside) {
        return Fail("weapon angle outside tolerance must not block");
    }

    settings.enabled = false;
    result = EvaluatePhysicalMeleeBlockPose(
        settings, head, weapon, 100.0F, true, state);
    if (result.active || result.reason !=
            PhysicalMeleeBlockPoseReason::Disabled) {
        return Fail("a disabled saved pose must fail closed");
    }
    settings.enabled = true;
    result = EvaluatePhysicalMeleeBlockPose(
        settings, head, weapon, 100.0F, false, state);
    if (result.active || result.reason !=
            PhysicalMeleeBlockPoseReason::ContextInactive) {
        return Fail("an ineligible context must release block");
    }
    result = EvaluatePhysicalMeleeBlockPose(
        settings, head, weapon, 0.0F, true, state);
    if (result.active || result.reason !=
            PhysicalMeleeBlockPoseReason::InvalidWorldScale) {
        return Fail("an invalid world scale must fail closed");
    }
    auto invalid = settings;
    invalid.positionToleranceMeters = 2.0F;
    result = EvaluatePhysicalMeleeBlockPose(
        invalid, head, weapon, 100.0F, true, state);
    if (result.active || result.reason !=
            PhysicalMeleeBlockPoseReason::InvalidSettings) {
        return Fail("unsafe saved tolerances must fail closed");
    }

    PhysicalMeleeBlockNativeLifecycleState lifecycle{};
    PhysicalMeleeBlockPoseResult entered{};
    entered.active = true;
    entered.entered = true;
    auto lifecycleTransition =
        ObservePhysicalMeleeBlockNativeLifecycle(
            entered, false, false, 32, 0x1234U, lifecycle);
    if (!lifecycleTransition.ownershipAcquired ||
        !lifecycle.automaticOwned || lifecycle.releasePending) {
        return Fail("automatic pose entry must own its native block");
    }

    PhysicalMeleeBlockPoseResult exited{};
    exited.exited = true;
    lifecycleTransition = ObservePhysicalMeleeBlockNativeLifecycle(
        exited, false, false, 32, 0x1234U, lifecycle);
    if (!lifecycleTransition.releaseQueued ||
        lifecycle.automaticOwned || !lifecycle.releasePending) {
        return Fail("automatic pose exit must queue one native release");
    }
    if (ConsumePhysicalMeleeBlockNativeRelease(
            false, -1, 0U, lifecycle) !=
            PhysicalMeleeBlockNativeReleaseDecision::WaitForWeapon ||
        !lifecycle.releasePending) {
        return Fail("an unreadable weapon must defer native release");
    }
    if (ConsumePhysicalMeleeBlockNativeRelease(
            true, 32, 0x1234U, lifecycle) !=
            PhysicalMeleeBlockNativeReleaseDecision::Dispatch ||
        lifecycle.releasePending || lifecycle.automaticOwned) {
        return Fail("the same weapon must consume native release once");
    }
    if (ConsumePhysicalMeleeBlockNativeRelease(
            true, 32, 0x1234U, lifecycle) !=
            PhysicalMeleeBlockNativeReleaseDecision::None) {
        return Fail("a consumed native release must not repeat");
    }

    lifecycle = {};
    ObservePhysicalMeleeBlockNativeLifecycle(
        entered, true, false, 32, 0x1234U, lifecycle);
    ObservePhysicalMeleeBlockNativeLifecycle(
        exited, true, false, 32, 0x1234U, lifecycle);
    if (lifecycle.automaticOwned || lifecycle.releasePending) {
        return Fail("manual block input must retain native ownership");
    }

    lifecycle = {};
    ObservePhysicalMeleeBlockNativeLifecycle(
        entered, false, false, 32, 0x1234U, lifecycle);
    ObservePhysicalMeleeBlockNativeLifecycle(
        exited, false, false, 32, 0x1234U, lifecycle);
    if (ConsumePhysicalMeleeBlockNativeRelease(
            true, 17, 0x5678U, lifecycle) !=
            PhysicalMeleeBlockNativeReleaseDecision::DropWeaponChanged ||
        lifecycle.releasePending) {
        return Fail("weapon replacement must drop a stale native release");
    }
    return 0;
}
