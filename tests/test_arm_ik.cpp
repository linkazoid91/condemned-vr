#include <cassert>
#include <cmath>
#include <limits>

#include "arm_ik.h"
#include "condemned_right_hand_ik.h"

namespace {

float Distance(
    const fearvr::ArmIkVector& left,
    const fearvr::ArmIkVector& right) {
    return std::sqrt(fearvr::ArmIkLengthSquared(
        fearvr::ArmIkSubtract(left, right)));
}
float TrackingDistance(
    const fearvr::TrackingVector& left,
    const fearvr::TrackingVector& right) {
    const float dx = left.x - right.x;
    const float dy = left.y - right.y;
    const float dz = left.z - right.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}


} // namespace

int main() {
    using fearvr::ArmIkDot;
    using fearvr::ArmIkVector;
    using fearvr::SolveTwoBoneElbow;

    const fearvr::ArmIkTuning defaults =
        fearvr::SanitizeArmIkTuning({});
    assert(defaults.elbowOutward == 1.0F);
    assert(defaults.elbowDown == 0.45F);
    assert(defaults.elbowBack == 0.15F);
    assert(defaults.preserveElbowContinuity);
    assert(defaults.leftHandPitchDegrees == 0.0F);

    fearvr::ArmIkTuning unsafe{};
    unsafe.elbowOutward = -10.0F;
    unsafe.elbowDown = 20.0F;
    unsafe.elbowBack = std::numeric_limits<float>::quiet_NaN();
    unsafe.leftHandRightMeters = 3.0F;
    unsafe.leftHandUpMeters = -3.0F;
    unsafe.leftHandPitchDegrees = 900.0F;
    unsafe.leftHandYawDegrees =
        std::numeric_limits<float>::infinity();
    const fearvr::ArmIkTuning sanitized =
        fearvr::SanitizeArmIkTuning(unsafe);
    assert(sanitized.elbowOutward == 0.20F);
    assert(sanitized.elbowDown == 1.5F);
    assert(sanitized.elbowBack == 0.15F);
    assert(sanitized.leftHandRightMeters == 0.20F);
    assert(sanitized.leftHandUpMeters == -0.20F);
    assert(sanitized.leftHandPitchDegrees == 180.0F);
    assert(sanitized.leftHandYawDegrees == 0.0F);

    const ArmIkVector shoulder{0.0F, 0.0F, 0.0F};
    const ArmIkVector target{1.0F, 0.0F, 0.0F};
    const auto down = SolveTwoBoneElbow(
        shoulder, target, 1.0F, 1.0F,
        {0.0F, -1.0F, 0.0F}, {}, false,
        {0.0F, 0.0F, 1.0F});
    assert(down.valid);
    assert(!down.targetClamped);
    assert(std::fabs(down.elbow.x - 0.5F) < 0.0001F);
    assert(down.elbow.y < -0.86F);
    assert(std::fabs(Distance(shoulder, down.elbow) - 1.0F) < 0.0001F);
    assert(std::fabs(Distance(target, down.elbow) - 1.0F) < 0.0001F);

    // Mirrored anatomical poles produce mirrored elbows.
    const auto up = SolveTwoBoneElbow(
        shoulder, target, 1.0F, 1.0F,
        {0.0F, 1.0F, 0.0F}, {}, false,
        {0.0F, 0.0F, 1.0F});
    assert(up.valid);
    assert(std::fabs(up.elbow.x - down.elbow.x) < 0.0001F);
    assert(std::fabs(up.elbow.y + down.elbow.y) < 0.0001F);

    // An unreachable controller is clamped to the arm's measured reach.
    const auto far = SolveTwoBoneElbow(
        shoulder, {10.0F, 0.0F, 0.0F}, 1.0F, 1.0F,
        {0.0F, -1.0F, 0.0F}, {}, false,
        {0.0F, -1.0F, 0.0F});
    assert(far.valid);
    assert(far.targetClamped);
    assert(far.solvedDistance < 2.0F);
    const ArmIkVector clampedHand{far.solvedDistance, 0.0F, 0.0F};
    assert(std::fabs(Distance(shoulder, far.elbow) - 1.0F) < 0.0001F);
    assert(std::fabs(Distance(clampedHand, far.elbow) - 1.0F) < 0.0001F);

    // Crossing the pole-axis singularity must retain the prior bend
    // hemisphere instead of flipping the elbow to the other side.
    const ArmIkVector previousBend{0.0F, 0.0F, 1.0F};
    const auto singularCrossing = SolveTwoBoneElbow(
        shoulder, {0.01F, -1.0F, 0.0F}, 1.0F, 1.0F,
        {0.0F, -1.0F, 0.0F}, previousBend, true,
        {0.0F, 0.0F, -1.0F});
    assert(singularCrossing.valid);
    assert(ArmIkDot(
        singularCrossing.bendDirection, previousBend) > 0.0F);

    const auto invalid = SolveTwoBoneElbow(
        shoulder, target,
        std::numeric_limits<float>::quiet_NaN(), 1.0F,
        {0.0F, -1.0F, 0.0F}, {}, false, {});
    assert(!invalid.valid);
    assert(!SolveTwoBoneElbow(
        shoulder, shoulder, 1.0F, 1.0F,
        {0.0F, -1.0F, 0.0F}, {}, false, {}).valid);

    // The empty right hand must use one coherent grip pose. The separate aim
    // orientation can differ substantially without twisting the palm.
    constexpr float kHalfSqrtTwo = 0.70710678118F;
    condemnedvr::RightHandIkTargetInput emptyHand{};
    emptyHand.gripWorldPosition = {10.0F, 20.0F, 30.0F};
    emptyHand.gripWorldRotation =
        {kHalfSqrtTwo, 0.0F, 0.0F, kHalfSqrtTwo};
    emptyHand.weightedWeaponWorldPosition = {40.0F, 50.0F, 60.0F};
    emptyHand.weightedWeaponWorldRotation =
        {0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};
    emptyHand.equippedWeaponIndex = 32;
    emptyHand.gripPoseFresh = true;
    emptyHand.weightedWeaponPoseFresh = true;
    const auto emptyTarget =
        condemnedvr::ResolveRightHandIkTarget(emptyHand);
    assert(emptyTarget.valid);
    assert(emptyTarget.source ==
        condemnedvr::RightHandIkTargetSource::EmptyGrip);
    assert(emptyTarget.worldPosition.x == 10.0F);
    assert(std::fabs(fearvr::Dot(
        emptyTarget.worldRotation,
        emptyHand.gripWorldRotation)) > 0.9999F);

    float aimToGripDegrees = 0.0F;
    assert(condemnedvr::RightHandIkQuaternionAngularDifferenceDegrees(
        emptyHand.gripWorldRotation,
        emptyHand.weightedWeaponWorldRotation,
        aimToGripDegrees));
    assert(std::fabs(aimToGripDegrees - 120.0F) < 0.001F);

    // A current lifetime-validated held model retains the weighted weapon/aim
    // target and its per-weapon correction path.
    auto equippedHand = emptyHand;
    equippedHand.liveWeaponModelSource = true;
    equippedHand.sourceGeneration = 7;
    const auto equippedTarget =
        condemnedvr::ResolveRightHandIkTarget(equippedHand);
    assert(equippedTarget.valid);
    assert(equippedTarget.source ==
        condemnedvr::RightHandIkTargetSource::WeaponWeightedAim);
    assert(equippedTarget.worldPosition.x == 40.0F);
    assert(std::fabs(fearvr::Dot(
        equippedTarget.worldRotation,
        equippedHand.weightedWeaponWorldRotation)) > 0.9999F);
    assert(condemnedvr::RightHandIkTargetBasisChanged(
        condemnedvr::RightHandIkTargetSource::EmptyGrip,
        -1, 0, equippedTarget));
    assert(!condemnedvr::RightHandIkTargetBasisChanged(
        condemnedvr::RightHandIkTargetSource::WeaponWeightedAim,
        32, 7, equippedTarget));

    // Retail Unarmed is empty even if a transient model reference exists.
    auto retailUnarmed = equippedHand;
    retailUnarmed.equippedWeaponIndex =
        condemnedvr::kCondemnedUnarmedWeaponIndex;
    const auto unarmedTarget =
        condemnedvr::ResolveRightHandIkTarget(retailUnarmed);
    assert(unarmedTarget.valid);
    assert(unarmedTarget.source ==
        condemnedvr::RightHandIkTargetSource::EmptyGrip);
    assert(unarmedTarget.worldPosition.x == 10.0F);

    // Stale grip tracking always returns the hand to Retail behavior. Aim or
    // weighted-weapon validity must not keep the IK override alive.
    auto staleGrip = equippedHand;
    staleGrip.gripPoseFresh = false;
    assert(!condemnedvr::ResolveRightHandIkTarget(staleGrip).valid);
    auto malformedGrip = emptyHand;
    malformedGrip.gripWorldRotation = {0.0F, 0.0F, 0.0F, 0.0F};
    assert(!condemnedvr::ResolveRightHandIkTarget(malformedGrip).valid);
    auto staleWeapon = equippedHand;
    staleWeapon.weightedWeaponPoseFresh = false;
    assert(!condemnedvr::ResolveRightHandIkTarget(staleWeapon).valid);

    // The two-pose solver must preserve the first visually-correct hand pose
    // when the raw controller is later held at the second physical pose.
    const condemnedvr::PhysicalMeleeRigidTransform referenceHandPose{
        {12.0F, -4.0F, 30.0F},
        {0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo}};
    const condemnedvr::PhysicalMeleeRigidTransform desiredControllerPose{
        {2.0F, 6.0F, 10.0F},
        {0.0F, 0.0F, kHalfSqrtTwo, kHalfSqrtTwo}};
    condemnedvr::EmptyRightHandAlignmentSettings emptyAlignment{};
    assert(condemnedvr::SolveEmptyRightHandAlignment(
        referenceHandPose, desiredControllerPose, emptyAlignment));
    assert(condemnedvr::EmptyRightHandAlignmentSettingsAreValid(
        emptyAlignment));
    auto overRangeDiagonal = emptyAlignment;
    overRangeDiagonal.localPositionOffsetUnits = {60.0F, 60.0F, 60.0F};
    assert(!condemnedvr::EmptyRightHandAlignmentSettingsAreValid(
        overRangeDiagonal));
    const condemnedvr::PhysicalMeleeRigidTransform resolvedEmptyHand =
        condemnedvr::ResolveEmptyRightHandAlignmentTarget(
            desiredControllerPose, emptyAlignment);
    assert(std::fabs(
        resolvedEmptyHand.positionUnits.x -
        referenceHandPose.positionUnits.x) < 0.0001F);
    assert(std::fabs(
        resolvedEmptyHand.positionUnits.y -
        referenceHandPose.positionUnits.y) < 0.0001F);
    assert(std::fabs(
        resolvedEmptyHand.positionUnits.z -
        referenceHandPose.positionUnits.z) < 0.0001F);
    assert(std::fabs(fearvr::Dot(
        resolvedEmptyHand.rotation,
        referenceHandPose.rotation)) > 0.9999F);
    auto invalidControllerPose = desiredControllerPose;
    invalidControllerPose.rotation = {0.0F, 0.0F, 0.0F, 0.0F};
    assert(!condemnedvr::SolveEmptyRightHandAlignment(
        referenceHandPose, invalidControllerPose, emptyAlignment));

    // Starting the guided mode cannot consume a trigger that was already
    // held. Every capture requires a fresh release/pull edge.
    condemnedvr::EmptyRightHandAlignmentState alignmentState{};
    condemnedvr::BeginEmptyRightHandAlignment(alignmentState);
    assert(condemnedvr::EmptyRightHandAlignmentIsActive(
        alignmentState));
    auto alignmentUpdate =
        condemnedvr::UpdateEmptyRightHandAlignment(
            alignmentState, desiredControllerPose,
            referenceHandPose, true, true);
    assert(alignmentUpdate.event ==
        condemnedvr::EmptyRightHandAlignmentEvent::None);
    alignmentUpdate = condemnedvr::UpdateEmptyRightHandAlignment(
        alignmentState, desiredControllerPose,
        referenceHandPose, true, false);
    assert(alignmentUpdate.event ==
        condemnedvr::EmptyRightHandAlignmentEvent::None);
    alignmentUpdate = condemnedvr::UpdateEmptyRightHandAlignment(
        alignmentState, desiredControllerPose,
        referenceHandPose, true, true);
    assert(alignmentUpdate.event ==
        condemnedvr::EmptyRightHandAlignmentEvent::ReferenceCaptured);
    assert(alignmentState.phase ==
        condemnedvr::EmptyRightHandAlignmentPhase::AwaitControllerPose);
    alignmentUpdate = condemnedvr::UpdateEmptyRightHandAlignment(
        alignmentState, desiredControllerPose,
        referenceHandPose, true, true);
    assert(alignmentUpdate.event ==
        condemnedvr::EmptyRightHandAlignmentEvent::None);
    alignmentUpdate = condemnedvr::UpdateEmptyRightHandAlignment(
        alignmentState, desiredControllerPose,
        referenceHandPose, true, false);
    assert(alignmentUpdate.event ==
        condemnedvr::EmptyRightHandAlignmentEvent::None);
    alignmentUpdate = condemnedvr::UpdateEmptyRightHandAlignment(
        alignmentState, desiredControllerPose,
        referenceHandPose, true, true);
    assert(alignmentUpdate.event ==
        condemnedvr::EmptyRightHandAlignmentEvent::Completed);
    assert(!condemnedvr::EmptyRightHandAlignmentIsActive(
        alignmentState));
    const auto stateMachineResolved =
        condemnedvr::ResolveEmptyRightHandAlignmentTarget(
            desiredControllerPose, alignmentUpdate.settings);
    assert(std::fabs(fearvr::Dot(
        stateMachineResolved.rotation,
        referenceHandPose.rotation)) > 0.9999F);

    // A stale/malformed pose forces another release. A held trigger must not
    // become a delayed capture when tracking becomes valid again.
    condemnedvr::BeginEmptyRightHandAlignment(alignmentState);
    alignmentUpdate = condemnedvr::UpdateEmptyRightHandAlignment(
        alignmentState, invalidControllerPose,
        referenceHandPose, false, true);
    assert(alignmentUpdate.event ==
        condemnedvr::EmptyRightHandAlignmentEvent::PoseUnavailable);
    alignmentUpdate = condemnedvr::UpdateEmptyRightHandAlignment(
        alignmentState, desiredControllerPose,
        referenceHandPose, true, true);
    assert(alignmentUpdate.event ==
        condemnedvr::EmptyRightHandAlignmentEvent::None);
    condemnedvr::UpdateEmptyRightHandAlignment(
        alignmentState, desiredControllerPose,
        referenceHandPose, true, false);
    alignmentUpdate = condemnedvr::UpdateEmptyRightHandAlignment(
        alignmentState, desiredControllerPose,
        referenceHandPose, true, true);
    assert(alignmentUpdate.event ==
        condemnedvr::EmptyRightHandAlignmentEvent::ReferenceCaptured);
    assert(condemnedvr::CancelEmptyRightHandAlignment(
        alignmentState));
    assert(!condemnedvr::CancelEmptyRightHandAlignment(
        alignmentState));

    // Held-object calibration freezes only the displayed model at capture
    // one. The first hand can be wrong; capture two must use the current live
    // hand that the player has moved into the frozen weapon.
    const condemnedvr::PhysicalMeleeRigidTransform heldFirstController{
        {40.0F, 50.0F, 60.0F},
        {0.0F, 0.0F, 0.0F, 1.0F}};
    const condemnedvr::PhysicalMeleeRigidTransform heldCurrentLocalGrip{
        {2.0F, -3.0F, 8.0F},
        {kHalfSqrtTwo, 0.0F, 0.0F, kHalfSqrtTwo}};
    const auto heldReferenceObjectResult =
        condemnedvr::ResolvePhysicalMeleeHeldModelTransform(
            heldFirstController,
            heldCurrentLocalGrip.positionUnits,
            heldCurrentLocalGrip.rotation, true);
    assert(heldReferenceObjectResult.active);
    condemnedvr::EmptyRightHandAlignmentSettings
        heldCapturedWrongHand{};
    heldCapturedWrongHand.localPositionOffsetUnits =
        {7.0F, -4.0F, 9.0F};
    heldCapturedWrongHand.localRotationOffset =
        {kHalfSqrtTwo, 0.0F, 0.0F, kHalfSqrtTwo};
    const auto heldReferenceHand =
        condemnedvr::ResolveEmptyRightHandAlignmentTarget(
            heldFirstController, heldCapturedWrongHand);
    condemnedvr::EmptyRightHandAlignmentSettings heldDesiredHand{};
    heldDesiredHand.localPositionOffsetUnits = {1.0F, 2.0F, 3.0F};
    heldDesiredHand.localRotationOffset =
        {0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};
    const condemnedvr::PhysicalMeleeRigidTransform heldSecondController{
        {48.0F, 42.0F, 75.0F},
        {0.0F, 0.0F, kHalfSqrtTwo, kHalfSqrtTwo}};
    const auto heldSecondObjectResult =
        condemnedvr::ResolvePhysicalMeleeHeldModelTransform(
            heldSecondController,
            heldCurrentLocalGrip.positionUnits,
            heldCurrentLocalGrip.rotation, true);
    const auto heldSecondHand =
        condemnedvr::ResolveEmptyRightHandAlignmentTarget(
            heldSecondController, heldDesiredHand);

    condemnedvr::HeldObjectAlignmentState heldState{};
    assert(!condemnedvr::BeginHeldObjectAlignment(
        heldState, -1, 0U));
    assert(condemnedvr::BeginHeldObjectAlignment(
        heldState, 32, 11U));
    auto heldUpdate = condemnedvr::UpdateHeldObjectAlignment(
        heldState, 32, 11U, heldFirstController,
        heldReferenceObjectResult.objectWorld, heldReferenceHand,
        true, true);
    assert(heldUpdate.event ==
        condemnedvr::HeldObjectAlignmentEvent::None);
    condemnedvr::UpdateHeldObjectAlignment(
        heldState, 32, 11U, heldFirstController,
        heldReferenceObjectResult.objectWorld, heldReferenceHand,
        true, false);
    heldUpdate = condemnedvr::UpdateHeldObjectAlignment(
        heldState, 32, 11U, heldFirstController,
        heldReferenceObjectResult.objectWorld, heldReferenceHand,
        true, true);
    assert(heldUpdate.event ==
        condemnedvr::HeldObjectAlignmentEvent::ReferenceCaptured);
    assert(heldState.phase ==
        condemnedvr::HeldObjectAlignmentPhase::AwaitControllerPose);
    condemnedvr::UpdateHeldObjectAlignment(
        heldState, 32, 11U, heldSecondController,
        heldSecondObjectResult.objectWorld, heldSecondHand,
        true, false);
    heldUpdate = condemnedvr::UpdateHeldObjectAlignment(
        heldState, 32, 11U, heldSecondController,
        heldSecondObjectResult.objectWorld, heldSecondHand,
        true, true);
    assert(heldUpdate.event ==
        condemnedvr::HeldObjectAlignmentEvent::Completed);
    assert(!condemnedvr::HeldObjectAlignmentIsActive(heldState));
    const auto heldResolvedObject =
        condemnedvr::ResolvePhysicalMeleeHeldModelTransform(
            heldSecondController,
            heldUpdate.solution.modelLocalGrip.positionUnits,
            heldUpdate.solution.modelLocalGrip.rotation, true);
    const auto heldResolvedHand =
        condemnedvr::ResolveEmptyRightHandAlignmentTarget(
            heldSecondController,
            heldUpdate.solution.rightHandAlignment);
    assert(heldResolvedObject.active);
    assert(std::fabs(
        heldResolvedObject.objectWorld.positionUnits.x -
        heldReferenceObjectResult.objectWorld.positionUnits.x) < 0.0001F);
    assert(std::fabs(
        heldResolvedObject.objectWorld.positionUnits.y -
        heldReferenceObjectResult.objectWorld.positionUnits.y) < 0.0001F);
    assert(std::fabs(
        heldResolvedObject.objectWorld.positionUnits.z -
        heldReferenceObjectResult.objectWorld.positionUnits.z) < 0.0001F);
    assert(std::fabs(fearvr::Dot(
        heldResolvedObject.objectWorld.rotation,
        heldReferenceObjectResult.objectWorld.rotation)) > 0.9999F);
    assert(std::fabs(
        heldResolvedHand.positionUnits.x -
        heldSecondHand.positionUnits.x) < 0.0001F);
    assert(std::fabs(
        heldResolvedHand.positionUnits.y -
        heldSecondHand.positionUnits.y) < 0.0001F);
    assert(std::fabs(
        heldResolvedHand.positionUnits.z -
        heldSecondHand.positionUnits.z) < 0.0001F);
    assert(std::fabs(fearvr::Dot(
        heldResolvedHand.rotation,
        heldSecondHand.rotation)) > 0.9999F);
    assert(std::fabs(fearvr::Dot(
        heldResolvedHand.rotation,
        heldReferenceHand.rotation)) < 0.999F);

    // A Hand IK edit changes C. Re-solving G keeps A = G * C fixed, and
    // rebasing the collider T keeps B = G * T fixed, so hand, weapon, and
    // collision geometry behave as one attachment.
    condemnedvr::EmptyRightHandAlignmentSettings nextHandAlignment{};
    nextHandAlignment.localPositionOffsetUnits =
        {-4.0F, 1.0F, 6.0F};
    nextHandAlignment.localRotationOffset =
        {0.0F, 0.0F, kHalfSqrtTwo, kHalfSqrtTwo};
    condemnedvr::PhysicalMeleeRigidTransform nextModelLocalGrip{};
    assert(condemnedvr::ResolveHandParentedModelLocalGrip(
        heldCurrentLocalGrip, heldDesiredHand,
        nextHandAlignment, nextModelLocalGrip));
    const condemnedvr::PhysicalMeleeRigidTransform currentHandLocal{
        heldDesiredHand.localPositionOffsetUnits,
        heldDesiredHand.localRotationOffset};
    const condemnedvr::PhysicalMeleeRigidTransform nextHandLocal{
        nextHandAlignment.localPositionOffsetUnits,
        nextHandAlignment.localRotationOffset};
    condemnedvr::PhysicalMeleeRigidTransform currentAttachment{};
    condemnedvr::PhysicalMeleeRigidTransform nextAttachment{};
    assert(condemnedvr::ComposePhysicalMeleeRigidTransforms(
        heldCurrentLocalGrip, currentHandLocal,
        currentAttachment));
    assert(condemnedvr::ComposePhysicalMeleeRigidTransforms(
        nextModelLocalGrip, nextHandLocal,
        nextAttachment));
    assert(std::fabs(
        currentAttachment.positionUnits.x -
        nextAttachment.positionUnits.x) < 0.0001F);
    assert(std::fabs(
        currentAttachment.positionUnits.y -
        nextAttachment.positionUnits.y) < 0.0001F);
    assert(std::fabs(
        currentAttachment.positionUnits.z -
        nextAttachment.positionUnits.z) < 0.0001F);
    assert(std::fabs(fearvr::Dot(
        currentAttachment.rotation,
        nextAttachment.rotation)) > 0.9999F);
    const condemnedvr::PhysicalMeleeRigidTransform currentColliderLocal{
        {0.0F, 0.0F, 25.0F},
        {0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo}};
    condemnedvr::PhysicalMeleeRigidTransform nextColliderLocal{};
    assert(condemnedvr::RebasePhysicalMeleeAttachedLocalTransform(
        heldCurrentLocalGrip, nextModelLocalGrip,
        currentColliderLocal, nextColliderLocal));
    condemnedvr::PhysicalMeleeRigidTransform currentColliderInModel{};
    condemnedvr::PhysicalMeleeRigidTransform nextColliderInModel{};
    assert(condemnedvr::ComposePhysicalMeleeRigidTransforms(
        heldCurrentLocalGrip, currentColliderLocal,
        currentColliderInModel));
    assert(condemnedvr::ComposePhysicalMeleeRigidTransforms(
        nextModelLocalGrip, nextColliderLocal,
        nextColliderInModel));
    assert(std::fabs(
        currentColliderInModel.positionUnits.x -
        nextColliderInModel.positionUnits.x) < 0.0001F);
    assert(std::fabs(
        currentColliderInModel.positionUnits.y -
        nextColliderInModel.positionUnits.y) < 0.0001F);
    assert(std::fabs(
        currentColliderInModel.positionUnits.z -
        nextColliderInModel.positionUnits.z) < 0.0001F);
    assert(std::fabs(fearvr::Dot(
        currentColliderInModel.rotation,
        nextColliderInModel.rotation)) > 0.9999F);

    // Auto alignment must make the complete reset-fit assembly follow the
    // globally corrected raw hand target. With identity authored/reset values
    // and a 60-degree grip/aim disagreement, both hand and model must point
    // with the corrected raw grip rather than the aim-based driver D.
    const condemnedvr::PhysicalMeleeRigidTransform
        onePressRawGripWorld{
            {18.0F, -7.0F, 41.0F},
            {0.0F, 0.0F, 0.0F, 1.0F}};
    const condemnedvr::PhysicalMeleeRigidTransform
        onePressControllerDriverWorld{
            onePressRawGripWorld.positionUnits,
            {0.0F, 0.5F, 0.0F, 0.86602540378F}};
    const condemnedvr::PhysicalMeleeRigidTransform
        onePressIdentityGrip{
            {}, {0.0F, 0.0F, 0.0F, 1.0F}};
    const condemnedvr::EmptyRightHandAlignmentSettings
        onePressIdentityHand{};
    condemnedvr::HeldObjectAlignmentSolution onePressSolution{};
    assert(condemnedvr::
        SolveHandParentedHeldAssemblyControllerAlignment(
            onePressIdentityGrip, onePressRawGripWorld,
            onePressControllerDriverWorld, onePressIdentityHand,
            onePressSolution));
    const auto onePressResolvedObject =
        condemnedvr::ResolvePhysicalMeleeHeldModelTransform(
            onePressControllerDriverWorld,
            onePressSolution.modelLocalGrip.positionUnits,
            onePressSolution.modelLocalGrip.rotation, true);
    const auto onePressResolvedHand =
        condemnedvr::ResolveEmptyRightHandAlignmentTarget(
            onePressControllerDriverWorld,
            onePressSolution.rightHandAlignment);
    assert(onePressResolvedObject.active);
    assert(TrackingDistance(
        onePressResolvedObject.objectWorld.positionUnits,
        onePressRawGripWorld.positionUnits) < 0.0001F);
    assert(TrackingDistance(
        onePressResolvedHand.positionUnits,
        onePressRawGripWorld.positionUnits) < 0.0001F);
    assert(std::fabs(fearvr::Dot(
        onePressResolvedObject.objectWorld.rotation,
        onePressRawGripWorld.rotation)) > 0.9999F);
    assert(std::fabs(fearvr::Dot(
        onePressResolvedHand.rotation,
        onePressRawGripWorld.rotation)) > 0.9999F);
    const condemnedvr::PhysicalMeleeRigidTransform
        onePressSolvedHandLocal{
            onePressSolution.rightHandAlignment.
                localPositionOffsetUnits,
            onePressSolution.rightHandAlignment.
                localRotationOffset};
    condemnedvr::PhysicalMeleeRigidTransform
        onePressSolvedAttachment{};
    assert(condemnedvr::ComposePhysicalMeleeRigidTransforms(
        onePressSolution.modelLocalGrip,
        onePressSolvedHandLocal, onePressSolvedAttachment));
    assert(TrackingDistance(
        onePressSolvedAttachment.positionUnits,
        onePressIdentityGrip.positionUnits) < 0.0001F);
    assert(std::fabs(fearvr::Dot(
        onePressSolvedAttachment.rotation,
        onePressIdentityGrip.rotation)) > 0.9999F);

    // A non-identity authored reset fit and global hand correction must move
    // together: the hand reaches R * E and G1 * C1 remains exactly G_base.
    const condemnedvr::PhysicalMeleeRigidTransform
        onePressAuthoredGrip{
            {2.0F, -1.0F, 3.0F},
            {0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo}};
    condemnedvr::EmptyRightHandAlignmentSettings
        onePressGlobalEmptyHand{};
    onePressGlobalEmptyHand.localPositionOffsetUnits =
        {2.0F, -1.0F, 3.0F};
    onePressGlobalEmptyHand.localRotationOffset =
        {0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};
    const condemnedvr::PhysicalMeleeRigidTransform
        onePressSecondRawGrip{
            {55.0F, 26.0F, -14.0F},
            {kHalfSqrtTwo, 0.0F, 0.0F, kHalfSqrtTwo}};
    const condemnedvr::PhysicalMeleeRigidTransform
        onePressSecondDriver{
            onePressSecondRawGrip.positionUnits,
            {0.0F, 0.0F, kHalfSqrtTwo, kHalfSqrtTwo}};
    condemnedvr::HeldObjectAlignmentSolution
        onePressSecondSolution{};
    assert(condemnedvr::
        SolveHandParentedHeldAssemblyControllerAlignment(
            onePressAuthoredGrip, onePressSecondRawGrip,
            onePressSecondDriver, onePressGlobalEmptyHand,
            onePressSecondSolution));
    const auto onePressDesiredHand =
        condemnedvr::ResolveEmptyRightHandAlignmentTarget(
            onePressSecondRawGrip, onePressGlobalEmptyHand);
    const auto onePressSecondResolvedHand =
        condemnedvr::ResolveEmptyRightHandAlignmentTarget(
            onePressSecondDriver,
            onePressSecondSolution.rightHandAlignment);
    assert(TrackingDistance(
        onePressSecondResolvedHand.positionUnits,
        onePressDesiredHand.positionUnits) < 0.0001F);
    assert(std::fabs(fearvr::Dot(
        onePressSecondResolvedHand.rotation,
        onePressDesiredHand.rotation)) > 0.9999F);
    const auto onePressSecondResolvedObject =
        condemnedvr::ResolvePhysicalMeleeHeldModelTransform(
            onePressSecondDriver,
            onePressSecondSolution.modelLocalGrip.positionUnits,
            onePressSecondSolution.modelLocalGrip.rotation, true);
    assert(onePressSecondResolvedObject.active);
    condemnedvr::PhysicalMeleeRigidTransform
        onePressSecondResolvedAuthoredHand{};
    assert(condemnedvr::ComposePhysicalMeleeRigidTransforms(
        onePressSecondResolvedObject.objectWorld,
        onePressAuthoredGrip,
        onePressSecondResolvedAuthoredHand));
    assert(TrackingDistance(
        onePressSecondResolvedAuthoredHand.positionUnits,
        onePressSecondResolvedHand.positionUnits) < 0.0001F);
    assert(std::fabs(fearvr::Dot(
        onePressSecondResolvedAuthoredHand.rotation,
        onePressSecondResolvedHand.rotation)) > 0.9999F);
    const condemnedvr::PhysicalMeleeRigidTransform
        onePressSecondHandLocal{
            onePressSecondSolution.rightHandAlignment.
                localPositionOffsetUnits,
            onePressSecondSolution.rightHandAlignment.
                localRotationOffset};
    condemnedvr::PhysicalMeleeRigidTransform
        onePressSecondAttachment{};
    assert(condemnedvr::ComposePhysicalMeleeRigidTransforms(
        onePressSecondSolution.modelLocalGrip,
        onePressSecondHandLocal, onePressSecondAttachment));
    assert(TrackingDistance(
        onePressSecondAttachment.positionUnits,
        onePressAuthoredGrip.positionUnits) < 0.0001F);
    assert(std::fabs(fearvr::Dot(
        onePressSecondAttachment.rotation,
        onePressAuthoredGrip.rotation)) > 0.9999F);
    condemnedvr::HeldObjectAlignmentSolution
        onePressRepeatedSolution{};
    assert(condemnedvr::
        SolveHandParentedHeldAssemblyControllerAlignment(
            onePressAuthoredGrip, onePressSecondRawGrip,
            onePressSecondDriver, onePressGlobalEmptyHand,
            onePressRepeatedSolution));
    assert(TrackingDistance(
        onePressRepeatedSolution.modelLocalGrip.positionUnits,
        onePressSecondSolution.modelLocalGrip.positionUnits) < 0.0001F);
    assert(std::fabs(fearvr::Dot(
        onePressRepeatedSolution.modelLocalGrip.rotation,
        onePressSecondSolution.modelLocalGrip.rotation)) > 0.9999F);
    assert(TrackingDistance(
        onePressRepeatedSolution.rightHandAlignment.
            localPositionOffsetUnits,
        onePressSecondSolution.rightHandAlignment.
            localPositionOffsetUnits) < 0.0001F);
    assert(std::fabs(fearvr::Dot(
        onePressRepeatedSolution.rightHandAlignment.
            localRotationOffset,
        onePressSecondSolution.rightHandAlignment.
            localRotationOffset)) > 0.9999F);
    auto onePressInvalidAuthoredGrip = onePressAuthoredGrip;
    onePressInvalidAuthoredGrip.positionUnits = {301.0F, 0.0F, 0.0F};
    assert(!condemnedvr::
        SolveHandParentedHeldAssemblyControllerAlignment(
            onePressInvalidAuthoredGrip, onePressSecondRawGrip,
            onePressSecondDriver, onePressGlobalEmptyHand,
            onePressSecondSolution));
    auto onePressInvalidEmptyHand = onePressGlobalEmptyHand;
    onePressInvalidEmptyHand.localPositionOffsetUnits =
        {100.0F, 100.0F, 100.0F};
    assert(!condemnedvr::
        SolveHandParentedHeldAssemblyControllerAlignment(
            onePressAuthoredGrip, onePressSecondRawGrip,
            onePressSecondDriver, onePressInvalidEmptyHand,
            onePressSecondSolution));

    // A weapon/model lifetime transition invalidates an unfinished capture.
    assert(condemnedvr::BeginHeldObjectAlignment(
        heldState, 32, 12U));
    heldUpdate = condemnedvr::UpdateHeldObjectAlignment(
        heldState, 17, 13U, heldFirstController,
        heldReferenceObjectResult.objectWorld, heldReferenceHand,
        true, false);
    assert(heldUpdate.event ==
        condemnedvr::HeldObjectAlignmentEvent::SourceChanged);
    assert(!condemnedvr::HeldObjectAlignmentIsActive(heldState));

    // Tracking recovery cannot turn a held trigger into a delayed object
    // capture; a fresh release and pull is mandatory.
    assert(condemnedvr::BeginHeldObjectAlignment(
        heldState, 32, 14U));
    heldUpdate = condemnedvr::UpdateHeldObjectAlignment(
        heldState, 32, 14U, heldFirstController,
        heldReferenceObjectResult.objectWorld, heldReferenceHand,
        false, true);
    assert(heldUpdate.event ==
        condemnedvr::HeldObjectAlignmentEvent::PoseUnavailable);
    heldUpdate = condemnedvr::UpdateHeldObjectAlignment(
        heldState, 32, 14U, heldFirstController,
        heldReferenceObjectResult.objectWorld, heldReferenceHand,
        true, true);
    assert(heldUpdate.event ==
        condemnedvr::HeldObjectAlignmentEvent::None);
    condemnedvr::UpdateHeldObjectAlignment(
        heldState, 32, 14U, heldFirstController,
        heldReferenceObjectResult.objectWorld, heldReferenceHand,
        true, false);
    heldUpdate = condemnedvr::UpdateHeldObjectAlignment(
        heldState, 32, 14U, heldFirstController,
        heldReferenceObjectResult.objectWorld, heldReferenceHand,
        true, true);
    assert(heldUpdate.event ==
        condemnedvr::HeldObjectAlignmentEvent::ReferenceCaptured);
    assert(condemnedvr::CancelHeldObjectAlignment(heldState));

    return 0;
}
