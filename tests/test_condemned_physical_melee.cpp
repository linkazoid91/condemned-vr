#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "condemned_calibration_gizmo.h"
#include "condemned_physical_melee_collider_gizmo.h"
#include "condemned_physical_melee.h"
#include "weapon_weight.h"

namespace {

bool Near(float left, float right, float tolerance = 0.001F) {
    return std::fabs(left - right) <= tolerance;
}

int Fail(const char* message) {
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

condemnedvr::PhysicalMeleePose Pose(
    float x, float y, float z,
    fearvr::TrackingQuaternion rotation = {}) {
    return {{x, y, z}, rotation};
}

condemnedvr::PhysicalMeleeRigidTransform Compose(
    const condemnedvr::PhysicalMeleeRigidTransform& left,
    const condemnedvr::PhysicalMeleeRigidTransform& right) {
    return {
        condemnedvr::PhysicalMeleeAdd(
            left.positionUnits,
            fearvr::Rotate(left.rotation, right.positionUnits)),
        fearvr::Multiply(left.rotation, right.rotation)};
}

} // namespace

int main() {
    using namespace condemnedvr;

    struct CalibrationCacheSlot {
        std::int32_t weaponIndex{-1};
        std::uint64_t lastUsed{0};
        bool occupied{false};
    };
    CalibrationCacheSlot cacheSlots[4]{
        {17, 10, true},
        {},
        {32, 2, true},
        {61, 7, true}};
    if (SelectPhysicalMeleeCalibrationSlot(cacheSlots, 17) != 0U ||
        SelectPhysicalMeleeCalibrationSlot(cacheSlots, 99) != 1U) {
        return Fail(
            "calibration cache must retain a weapon and fill empty slots before eviction");
    }
    cacheSlots[1] = {99, 5, true};
    if (SelectPhysicalMeleeCalibrationSlot(cacheSlots, 100) != 2U) {
        return Fail(
            "full calibration cache must evict the least-recently-used slot");
    }

    PhysicalMeleeProfile profile{};
    profile.localTipOffsetUnits = {0.0F, 0.0F, 100.0F};
    profile.massKilograms = 2.0F;

    // The first complete pose establishes history but cannot manufacture a
    // sweep or impact.
    PhysicalMeleeKinematicsState state{};
    auto frame = UpdatePhysicalMeleeKinematics(
        state, Pose(10.0F, 20.0F, 30.0F), true,
        1'000'000'000ULL, profile);
    if (!frame.poseValid || frame.sweepValid || frame.damageQualified ||
        frame.resetReason != PhysicalMeleeResetReason::FirstPose ||
        !Near(frame.currentBaseUnits.x, 10.0F) ||
        !Near(frame.currentTipUnits.z, 130.0F)) {
        return Fail("first physical-melee pose must only prime history");
    }
    const PhysicalMeleeNativeCapsuleShape firstNativeCapsule =
        ResolvePhysicalMeleeNativeCapsuleShape(frame, true);
    const PhysicalMeleeWallProxyTransform firstProxy =
        ResolvePhysicalMeleeWallProxyTransform(frame, true);
    const fearvr::TrackingVector nativeBase = PhysicalMeleeAdd(
        firstProxy.positionUnits,
        fearvr::Rotate(
            firstProxy.rotation,
            {0.0F, -firstNativeCapsule.lengthDownUnits, 0.0F}));
    const fearvr::TrackingVector nativeTip = PhysicalMeleeAdd(
        firstProxy.positionUnits,
        fearvr::Rotate(
            firstProxy.rotation,
            {0.0F, firstNativeCapsule.lengthUpUnits, 0.0F}));
    const fearvr::TrackingVector nativePositiveY = fearvr::Rotate(
        firstProxy.rotation, {0.0F, 1.0F, 0.0F});
    if (!firstNativeCapsule.valid || !firstProxy.active ||
        !Near(firstNativeCapsule.lengthUpUnits, 0.0F) ||
        !Near(firstNativeCapsule.lengthDownUnits, 100.0F) ||
        !Near(firstNativeCapsule.radiusUnits, profile.radiusUnits) ||
        !Near(nativeBase.x, frame.currentBaseUnits.x) ||
        !Near(nativeBase.y, frame.currentBaseUnits.y) ||
        !Near(nativeBase.z, frame.currentBaseUnits.z) ||
        !Near(nativeTip.x, frame.currentTipUnits.x) ||
        !Near(nativeTip.y, frame.currentTipUnits.y) ||
        !Near(nativeTip.z, frame.currentTipUnits.z) ||
        !Near(nativePositiveY.x, 0.0F) ||
        !Near(nativePositiveY.y, 0.0F) ||
        !Near(nativePositiveY.z, 1.0F)) {
        return Fail(
            "native local-Y capsule must reproduce configured endpoints");
    }
    if (ResolvePhysicalMeleeNativeCapsuleShape(
            frame, false).valid ||
        ResolvePhysicalMeleeWallProxyTransform(frame, false).active) {
        return Fail("stale native capsule samples must fail closed");
    }
    PhysicalMeleeFrame invalidProxyFrame = frame;
    invalidProxyFrame.currentTipUnits =
        invalidProxyFrame.currentBaseUnits;
    if (ResolvePhysicalMeleeNativeCapsuleShape(
            invalidProxyFrame, true).valid ||
        ResolvePhysicalMeleeWallProxyTransform(
            invalidProxyFrame, true).active) {
        return Fail("degenerate native capsule transforms must fail closed");
    }
    if (!Near(
            ResolvePhysicalMeleeNativeCapsuleProperty(
                firstNativeCapsule,
                PhysicalMeleeNativeCapsuleProperty::LengthUp, 99.0F),
            0.0F) ||
        !Near(
            ResolvePhysicalMeleeNativeCapsuleProperty(
                firstNativeCapsule,
                PhysicalMeleeNativeCapsuleProperty::LengthDown, 99.0F),
            100.0F) ||
        !Near(
            ResolvePhysicalMeleeNativeCapsuleProperty(
                firstNativeCapsule,
                PhysicalMeleeNativeCapsuleProperty::Radius, 99.0F),
            profile.radiusUnits) ||
        !Near(
            ResolvePhysicalMeleeNativeCapsuleProperty(
                firstNativeCapsule,
                PhysicalMeleeNativeCapsuleProperty::Retail, 99.0F),
            99.0F) ||
        !Near(
            ResolvePhysicalMeleeNativeCapsuleProperty(
                PhysicalMeleeNativeCapsuleShape{},
                PhysicalMeleeNativeCapsuleProperty::Radius, 99.0F),
            99.0F)) {
        return Fail(
            "native capsule properties must override only valid scoped fields");
    }
    if (!PhysicalMeleeCollisionBelongsToEquippedWeapon(
            0x1234U, 0x1234U) ||
        PhysicalMeleeCollisionBelongsToEquippedWeapon(
            0U, 0x1234U) ||
        PhysicalMeleeCollisionBelongsToEquippedWeapon(
            0x1234U, 0x5678U)) {
        return Fail(
            "physical-melee ownership must require the equipped weapon model");
    }
    if (!ShouldApplyPhysicalMeleePlayerOverride(true, true) ||
        ShouldApplyPhysicalMeleePlayerOverride(true, false) ||
        ShouldApplyPhysicalMeleePlayerOverride(false, true)) {
        return Fail(
            "physical-melee overrides must be limited to player collisions");
    }
    if (ShouldDispatchPhysicalMeleeNativeImpact(true, true) ||
        !ShouldDispatchPhysicalMeleeNativeImpact(true, false) ||
        !ShouldDispatchPhysicalMeleeNativeImpact(false, true)) {
        return Fail(
            "wall proxy gate must preserve non-player native impacts");
    }
    if (!ShouldDispatchPhysicalMeleeNativeImpact(
            true, true, true, true) ||
        ShouldDispatchPhysicalMeleeNativeImpact(
            true, true, true, false) ||
        ShouldDispatchPhysicalMeleeNativeImpact(
            true, true, false, true)) {
        return Fail(
            "player native impact must require enabled accepted contact");
    }
    if (!ShouldMaintainPhysicalMeleeCollision(
            true, true, true, true) ||
        ShouldMaintainPhysicalMeleeCollision(
            false, true, true, true) ||
        ShouldMaintainPhysicalMeleeCollision(
            true, false, true, true) ||
        ShouldMaintainPhysicalMeleeCollision(
            true, true, false, true) ||
        ShouldMaintainPhysicalMeleeCollision(
            true, true, true, false)) {
        return Fail(
            "continuous collision lifetime must fail closed by context");
    }

    const auto emptyReferenceVector =
        ResolveRetailMeleeTargetReferenceVectorSpan(0U, 0U, 0U);
    const auto oneReferenceVector =
        ResolveRetailMeleeTargetReferenceVectorSpan(
            0x1000U, 0x1010U, 0x1040U);
    if (!emptyReferenceVector.valid || emptyReferenceVector.count != 0U ||
        !oneReferenceVector.valid || oneReferenceVector.count != 1U ||
        ResolveRetailMeleeTargetReferenceVectorSpan(
            0x1000U, 0x0FF0U, 0x1040U).valid ||
        ResolveRetailMeleeTargetReferenceVectorSpan(
            0x1000U, 0x1008U, 0x1040U).valid ||
        ResolveRetailMeleeTargetReferenceVectorSpan(
            0x1000U, 0x1410U, 0x1410U).valid ||
        ResolveRetailMeleeTargetReferenceVectorSpan(
            0U, 0U, 0x1040U).valid) {
        return Fail(
            "Retail target-reference vectors must be bounded and aligned");
    }

    // Held models use an explicit, reusable grip anchor. Solving the model
    // transform must place that local anchor exactly on the controller pose;
    // identity remains the safe fallback for hand-socket-authored assets.
    constexpr float kHalfSqrt = 0.70710678118F;
    const PhysicalMeleeRigidTransform desiredGrip{
        {100.0F, 200.0F, 300.0F},
        {0.0F, 0.0F, -kHalfSqrt, kHalfSqrt}};
    const PhysicalMeleeRigidTransform localModelGrip{
        {2.0F, -3.0F, 8.0F},
        {kHalfSqrt, 0.0F, 0.0F, kHalfSqrt}};
    const PhysicalMeleeVisualProxyTransform heldModel =
        ResolvePhysicalMeleeHeldModelTransform(
            desiredGrip, localModelGrip.positionUnits,
            localModelGrip.rotation, true);
    const PhysicalMeleeRigidTransform resolvedGrip =
        Compose(heldModel.objectWorld, localModelGrip);
    if (!heldModel.active ||
        !Near(resolvedGrip.positionUnits.x, desiredGrip.positionUnits.x) ||
        !Near(resolvedGrip.positionUnits.y, desiredGrip.positionUnits.y) ||
        !Near(resolvedGrip.positionUnits.z, desiredGrip.positionUnits.z) ||
        std::fabs(fearvr::Dot(
            fearvr::Normalize(resolvedGrip.rotation),
            fearvr::Normalize(desiredGrip.rotation))) < 0.999F) {
        return Fail("profile grip anchor must land on the controller pose");
    }
    const PhysicalMeleeVisualProxyTransform identityHeldModel =
        ResolvePhysicalMeleeHeldModelTransform(
            desiredGrip, {}, {0.0F, 0.0F, 0.0F, 1.0F}, true);
    if (!identityHeldModel.active ||
        !Near(identityHeldModel.objectWorld.positionUnits.x,
              desiredGrip.positionUnits.x) ||
        !Near(identityHeldModel.objectWorld.positionUnits.y,
              desiredGrip.positionUnits.y) ||
        !Near(identityHeldModel.objectWorld.positionUnits.z,
              desiredGrip.positionUnits.z)) {
        return Fail("identity grip profiles must put model origin in hand");
    }
    if (ResolvePhysicalMeleeHeldModelTransform(
            desiredGrip, localModelGrip.positionUnits,
            localModelGrip.rotation, false).active) {
        return Fail("stale held-model poses must fail closed");
    }

    // Two-hand support is a select-style handle constraint. The dominant
    // grip remains fixed, the authored spacing never scales, and the support
    // hand rotates the shaft through the shortest stable arc.
    const PhysicalMeleeSecondaryGripSettings twoHandSettings{
        {0.0F, 0.0F, 40.0F}, 100.0F, 0.15F, 0.25F,
        0.65F, 0.35F, true};
    PhysicalMeleeSecondaryGripState twoHandState{};
    const PhysicalMeleePose primaryGrip =
        Pose(10.0F, 20.0F, 30.0F);
    auto twoHand = UpdatePhysicalMeleeSecondaryGrip(
        twoHandState, primaryGrip, {10.0F, 20.0F, 70.0F},
        0.70F, true, true, twoHandSettings);
    if (!twoHand.attached || !twoHand.justAttached ||
        !twoHand.poseValid ||
        !Near(twoHand.pose.gripPositionUnits.x, 10.0F) ||
        !Near(twoHand.targetSecondaryPositionUnits.z, 70.0F) ||
        !Near(twoHand.handSeparationMeters, 0.40F) ||
        !Near(twoHand.anchorErrorMeters, 0.0F)) {
        return Fail("near support-hand squeeze must attach at fixed spacing");
    }
    twoHand = UpdatePhysicalMeleeSecondaryGrip(
        twoHandState, primaryGrip, {50.0F, 20.0F, 30.0F},
        0.80F, true, true, twoHandSettings);
    const fearvr::TrackingVector solvedShaft = fearvr::Rotate(
        twoHand.pose.rotation, twoHandSettings.offsetUnits);
    if (!twoHand.attached || !twoHand.poseValid ||
        !Near(solvedShaft.x, 40.0F, 0.01F) ||
        !Near(solvedShaft.y, 0.0F, 0.01F) ||
        !Near(solvedShaft.z, 0.0F, 0.01F) ||
        !Near(PhysicalMeleeLength(solvedShaft), 40.0F, 0.01F) ||
        !Near(twoHand.targetSecondaryPositionUnits.x, 50.0F, 0.01F)) {
        return Fail("support hand must aim the shaft without weapon scaling");
    }
    twoHand = UpdatePhysicalMeleeSecondaryGrip(
        twoHandState, primaryGrip, {50.0F, 20.0F, 30.0F},
        0.20F, true, true, twoHandSettings);
    if (twoHand.attached || !twoHand.justReleased ||
        twoHand.releaseReason !=
            PhysicalMeleeSecondaryGripReleaseReason::Released ||
        std::fabs(fearvr::Dot(
            fearvr::Normalize(twoHand.pose.rotation),
            fearvr::Normalize(primaryGrip.rotation))) < 0.999F) {
        return Fail("support release must return a valid one-hand target");
    }

    // The visible support hand keeps the tracked controller position, while
    // attachment captures its orientation relative to the weapon. Subsequent
    // controller twist cannot swivel the hand around the grip point.
    PhysicalMeleeSupportHandOrientationState supportOrientation{};
    const fearvr::TrackingQuaternion quarterTurnZ{
        0.0F, 0.0F, kHalfSqrt, kHalfSqrt};
    fearvr::TrackingQuaternion visibleSupportRotation{};
    if (!ResolvePhysicalMeleeSupportHandRotation(
            supportOrientation,
            {0.0F, 0.0F, 0.0F, 1.0F}, quarterTurnZ,
            true, true, visibleSupportRotation) ||
        std::fabs(fearvr::Dot(
            visibleSupportRotation, quarterTurnZ)) < 0.999F) {
        return Fail(
            "support attachment must preserve its initial controller orientation");
    }
    if (!ResolvePhysicalMeleeSupportHandRotation(
            supportOrientation,
            {0.0F, 0.0F, 0.0F, 1.0F},
            {0.0F, 0.0F, 0.0F, 1.0F},
            true, false, visibleSupportRotation) ||
        std::fabs(fearvr::Dot(
            visibleSupportRotation, quarterTurnZ)) < 0.999F) {
        return Fail(
            "attached support orientation must ignore later controller twist");
    }
    if (!ResolvePhysicalMeleeSupportHandRotation(
            supportOrientation, quarterTurnZ,
            {0.0F, 0.0F, 0.0F, 1.0F},
            true, false, visibleSupportRotation) ||
        std::fabs(visibleSupportRotation.z) < 0.999F) {
        return Fail(
            "attached support orientation must remain rigid to weapon rotation");
    }
    if (!ResolvePhysicalMeleeSupportHandRotation(
            supportOrientation, quarterTurnZ,
            {0.0F, 0.0F, 0.0F, 1.0F},
            false, false, visibleSupportRotation) ||
        supportOrientation.attachedRotationValid ||
        std::fabs(visibleSupportRotation.w) < 0.999F) {
        return Fail(
            "support release must restore raw controller orientation");
    }

    // Pressing away from the handle consumes that press. Moving closer while
    // still squeezed must not create a remote snap-grab.
    twoHand = UpdatePhysicalMeleeSecondaryGrip(
        twoHandState, primaryGrip, {110.0F, 20.0F, 30.0F},
        0.80F, true, true, twoHandSettings);
    if (twoHand.attached) {
        return Fail("support hand outside the radius must not attach");
    }
    twoHand = UpdatePhysicalMeleeSecondaryGrip(
        twoHandState, primaryGrip, {10.0F, 20.0F, 70.0F},
        0.80F, true, true, twoHandSettings);
    if (twoHand.attached) {
        return Fail("held squeeze must not become a remote snap-grab");
    }
    UpdatePhysicalMeleeSecondaryGrip(
        twoHandState, primaryGrip, {10.0F, 20.0F, 70.0F},
        0.20F, true, true, twoHandSettings);
    twoHand = UpdatePhysicalMeleeSecondaryGrip(
        twoHandState, primaryGrip, {10.0F, 20.0F, 70.0F},
        0.80F, true, true, twoHandSettings);
    if (!twoHand.attached) {
        return Fail("release and near re-press must re-arm support grab");
    }
    twoHand = UpdatePhysicalMeleeSecondaryGrip(
        twoHandState, primaryGrip, {90.0F, 20.0F, 30.0F},
        0.80F, true, true, twoHandSettings);
    if (twoHand.attached || !twoHand.justReleased ||
        twoHand.releaseReason !=
            PhysicalMeleeSecondaryGripReleaseReason::ExcessiveStretch) {
        return Fail("excessive controller spacing must release safely");
    }

    fearvr::TrackingVector capturedOffset{};
    if (!ResolvePhysicalMeleeSecondaryGripOffset(
            primaryGrip, {10.0F, 20.0F, 70.0F},
            capturedOffset) ||
        !Near(capturedOffset.x, 0.0F) ||
        !Near(capturedOffset.y, 0.0F) ||
        !Near(capturedOffset.z, 40.0F)) {
        return Fail("two-controller capture must produce a local grip offset");
    }
    const PhysicalMeleeTwoHandPoseResult opposite =
        ResolvePhysicalMeleeTwoHandPose(
            primaryGrip, {10.0F, 20.0F, -10.0F},
            twoHandSettings);
    const fearvr::TrackingVector oppositeShaft = fearvr::Rotate(
        opposite.pose.rotation, twoHandSettings.offsetUnits);
    if (!opposite.poseValid ||
        !Near(oppositeShaft.z, -40.0F, 0.01F)) {
        return Fail("opposite handle directions must remain deterministic");
    }

    // Live setup stores readable local Euler corrections but feeds the shared
    // held-model solver a normalized quaternion. A 90-degree local X change
    // must rotate model +Y onto +Z, and wrap-around stays deterministic.
    PhysicalMeleeGripCalibration calibration{};
    calibration.basePositionUnits = {2.0F, -3.0F, 8.0F};
    calibration.positionUnits = calibration.basePositionUnits;
    calibration.localRotationDegrees = {90.0F, 0.0F, 0.0F};
    const fearvr::TrackingQuaternion calibratedRotation =
        ResolvePhysicalMeleeGripCalibrationRotation(calibration);
    const fearvr::TrackingVector calibratedUp = fearvr::Rotate(
        calibratedRotation, {0.0F, 1.0F, 0.0F});
    if (!Near(calibratedUp.x, 0.0F) ||
        !Near(calibratedUp.y, 0.0F) ||
        !Near(calibratedUp.z, 1.0F) ||
        !Near(PhysicalMeleeWrapDegrees(181.0F), -179.0F) ||
        !Near(PhysicalMeleeWrapDegrees(-181.0F), 179.0F)) {
        return Fail("live grip calibration axes must be deterministic");
    }
    calibration.baseRotation = localModelGrip.rotation;
    calibration.localRotationDegrees = {};
    if (std::fabs(fearvr::Dot(
            ResolvePhysicalMeleeGripCalibrationRotation(calibration),
            fearvr::Normalize(localModelGrip.rotation))) < 0.999F) {
        return Fail("zero live correction must preserve profile rotation");
    }

    // The alignment reference is generated in controller space and projected
    // through the verified per-eye camera before the bridge draws it. Neutral
    // +Z must remain screen centre, while world +X projects rightward.
    const WeaponGripCalibrationGizmo controllerGizmo =
        BuildWeaponGripCalibrationGizmo(
            {0.0F, 0.0F, 100.0F}, {}, {});
    WeaponGripCalibrationGizmoCamera gizmoCamera{};
    gizmoCamera.rotation = {};
    gizmoCamera.horizontalFovRadians =
        3.14159265358979323846F * 0.5F;
    gizmoCamera.verticalFovRadians =
        3.14159265358979323846F * 0.5F;
    float projectedX = 0.0F;
    float projectedY = 0.0F;
    if (!controllerGizmo.valid || controllerGizmo.count < 30 ||
        !ProjectWeaponGripCalibrationPointToNdc(
            {10.0F, 0.0F, 100.0F}, gizmoCamera,
            projectedX, projectedY) ||
        !Near(projectedX, 0.1F) || !Near(projectedY, 0.0F)) {
        return Fail("controller gizmo must use stereo camera projection");
    }
    FearVrOverlayLineVertex projectedGizmo[
        kWeaponGripCalibrationGizmoMaximumLines * 2]{};
    const std::size_t projectedVertexCount =
        ProjectWeaponGripCalibrationGizmoToNdc(
            controllerGizmo, gizmoCamera, projectedGizmo,
            sizeof(projectedGizmo) / sizeof(projectedGizmo[0]));
    if (projectedVertexCount < 60 ||
        (projectedVertexCount % 2) != 0) {
        return Fail("controller gizmo must produce complete overlay lines");
    }
    if (ProjectWeaponGripCalibrationPointToNdc(
            {0.0F, 0.0F, -10.0F}, gizmoCamera,
            projectedX, projectedY)) {
        return Fail("controller gizmo must reject points behind the eye");
    }
    const WeaponGripCalibrationGizmo invalidControllerGizmo =
        BuildWeaponGripCalibrationGizmo(
            {}, {0.0F, 0.0F, 0.0F, 0.0F}, {});
    if (invalidControllerGizmo.valid) {
        return Fail("invalid controller poses must not draw a gizmo");
    }


    // The collider diagnostic is a fixed-size swept-volume wireframe. Amber
    // is an unseeded preview; green means the same volume is backed by a
    // fresh player-owned Retail collision body.
    const WeaponGripCalibrationGizmo colliderPreview =
        BuildPhysicalMeleeColliderGizmo(
            {0.0F, 0.0F, 100.0F},
            {0.0F, 0.0F, 175.0F},
            {0.0F, 0.0F, 175.0F}, 4.0F, false);
    const WeaponGripCalibrationGizmo colliderLive =
        BuildPhysicalMeleeColliderGizmo(
            {0.0F, 0.0F, 100.0F},
            {0.0F, 0.0F, 175.0F},
            {0.0F, 0.0F, 175.0F}, 4.0F, true);
    if (!colliderPreview.valid || colliderPreview.count != 44U ||
        !colliderLive.valid || colliderLive.count != 44U ||
        colliderPreview.lines[0].argb != 0xE0FFB040U ||
        colliderLive.lines[0].argb != 0xE050FF90U) {
        return Fail(
            "collider gizmo must distinguish preview and live bodies");
    }
    FearVrOverlayLineVertex projectedCollider[
        kWeaponGripCalibrationGizmoMaximumLines * 2]{};
    if (ProjectWeaponGripCalibrationGizmoToNdc(
            colliderLive, gizmoCamera, projectedCollider,
            sizeof(projectedCollider) /
                sizeof(projectedCollider[0])) != 88U) {
        return Fail(
            "collider gizmo must project all fixed wireframe lines");
    }
    if (BuildPhysicalMeleeColliderGizmo(
            {}, {}, {}, 4.0F, false).valid ||
        BuildPhysicalMeleeColliderGizmo(
            {0.0F, 0.0F, 100.0F},
            {0.0F, 0.0F, 175.0F},
            {0.0F, 0.0F, 175.0F},
            std::numeric_limits<float>::quiet_NaN(), false).valid) {
        return Fail(
            "collider gizmo must reject invalid geometry");
    }
    // The diagnostic visible model keeps its measured animated node-to-model
    // relationship. Moving the solved object must put that node exactly on
    // the same controller endpoint used by the collision proxy.
    const PhysicalMeleeRigidTransform sourceObject{
        {10.0F, 20.0F, 30.0F},
        {0.0F, kHalfSqrt, 0.0F, kHalfSqrt}};
    const PhysicalMeleeRigidTransform nodeInObject{
        {0.0F, 4.0F, 75.0F},
        {kHalfSqrt, 0.0F, 0.0F, kHalfSqrt}};
    const PhysicalMeleeRigidTransform sourceNode =
        Compose(sourceObject, nodeInObject);
    const PhysicalMeleeRigidTransform desiredNode{
        {100.0F, 200.0F, 300.0F},
        {0.0F, 0.0F, -kHalfSqrt, kHalfSqrt}};
    const PhysicalMeleeVisualProxyTransform visibleProxy =
        ResolvePhysicalMeleeVisualProxyTransform(
            sourceObject, sourceNode, desiredNode, true);
    const PhysicalMeleeRigidTransform resolvedNode =
        Compose(visibleProxy.objectWorld, nodeInObject);
    if (!visibleProxy.active ||
        !Near(resolvedNode.positionUnits.x, desiredNode.positionUnits.x) ||
        !Near(resolvedNode.positionUnits.y, desiredNode.positionUnits.y) ||
        !Near(resolvedNode.positionUnits.z, desiredNode.positionUnits.z) ||
        std::fabs(fearvr::Dot(
            fearvr::Normalize(resolvedNode.rotation),
            fearvr::Normalize(desiredNode.rotation))) < 0.999F) {
        return Fail("visible weapon node must align with collision endpoint");
    }
    if (ResolvePhysicalMeleeVisualProxyTransform(
            sourceObject, sourceNode, desiredNode, false).active) {
        return Fail("stale visible weapon sources must fail closed");
    }
    PhysicalMeleeRigidTransform invalidSource = sourceObject;
    invalidSource.positionUnits.x =
        std::numeric_limits<float>::infinity();
    if (ResolvePhysicalMeleeVisualProxyTransform(
            invalidSource, sourceNode, desiredNode, true).active) {
        return Fail("invalid visible weapon transforms must fail closed");
    }

    // Two world units in 10 ms at 100 units/m is 2 m/s. With a 2 kg
    // profile, the endpoint carries 4 joules and qualifies.
    frame = UpdatePhysicalMeleeKinematics(
        state, Pose(12.0F, 20.0F, 30.0F), true,
        1'010'000'000ULL, profile);
    if (!frame.sweepValid || !frame.damageQualified ||
        !Near(frame.deltaSeconds, 0.01F) ||
        !Near(frame.sweepDistanceMeters, 0.02F) ||
        !Near(frame.impactSpeedMetersPerSecond, 2.0F) ||
        !Near(frame.impactEnergyJoules, 4.0F) ||
        !Near(frame.tipVelocityUnitsPerSecond.x, 200.0F)) {
        return Fail("translation must produce deterministic impact kinematics");
    }

    PhysicalMeleeFrame distanceFrame = frame;
    distanceFrame.currentBaseUnits = {0.0F, 0.0F, 0.0F};
    distanceFrame.currentTipUnits = {0.0F, 0.0F, 100.0F};
    distanceFrame.radiusUnits = 5.0F;
    distanceFrame.poseValid = true;
    const PhysicalMeleeContactDistance onCapsule =
        MeasurePhysicalMeleeContactDistance(
            distanceFrame, {3.0F, 4.0F, 50.0F}, 100.0F);
    const PhysicalMeleeContactDistance outsideCapsule =
        MeasurePhysicalMeleeContactDistance(
            distanceFrame, {15.0F, 0.0F, 50.0F}, 100.0F);
    const PhysicalMeleeContactDistance pastTip =
        MeasurePhysicalMeleeContactDistance(
            distanceFrame, {0.0F, 0.0F, 120.0F}, 100.0F);
    if (!onCapsule.valid || !Near(onCapsule.axisFraction, 0.5F) ||
        !Near(onCapsule.centerlineToContactMeters, 0.05F) ||
        !Near(onCapsule.capsuleSurfaceGapMeters, 0.0F) ||
        !outsideCapsule.valid ||
        !Near(outsideCapsule.capsuleSurfaceGapMeters, 0.10F) ||
        !pastTip.valid || !Near(pastTip.axisFraction, 1.0F) ||
        !Near(pastTip.tipToContactMeters, 0.20F) ||
        !Near(pastTip.capsuleSurfaceGapMeters, 0.15F) ||
        MeasurePhysicalMeleeContactDistance(
            distanceFrame, {0.0F, 0.0F, 0.0F}, 0.0F).valid) {
        return Fail(
            "contact distance must measure the finite capsule surface gap");
    }

    PhysicalMeleeContactDistance nearCapsule = onCapsule;
    nearCapsule.capsuleSurfaceGapMeters = 0.009F;
    if (!PhysicalMeleeContactWithinConfiguredCollider(onCapsule) ||
        !PhysicalMeleeContactWithinConfiguredCollider(nearCapsule) ||
        PhysicalMeleeContactWithinConfiguredCollider(outsideCapsule) ||
        PhysicalMeleeContactWithinConfiguredCollider(
            PhysicalMeleeContactDistance{}) ||
        PhysicalMeleeContactWithinConfiguredCollider(
            onCapsule, -1.0F) ||
        PhysicalMeleeContactWithinConfiguredCollider(
            onCapsule, std::numeric_limits<float>::quiet_NaN())) {
        return Fail(
            "configured capsule gate must allow only finite surface overlap");
    }

    PhysicalMeleeContactState distanceGateState{};
    PhysicalMeleeContactQualification gatedContact =
        QualifyPhysicalMeleeContactAtDistance(
            distanceGateState, 0x1234U, frame, 2U,
            outsideCapsule, profile, false);
    if (gatedContact.accepted ||
        gatedContact.reason !=
            PhysicalMeleeContactReason::OutsideConfiguredCollider ||
        distanceGateState.targetCount != 0U ||
        distanceGateState.haveContact || !distanceGateState.armed) {
        return Fail(
            "distant native callback must not latch the Retail target");
    }
    gatedContact = QualifyPhysicalMeleeContactAtDistance(
        distanceGateState, 0x1234U, frame, 2U,
        onCapsule, profile, false);
    if (!gatedContact.accepted ||
        gatedContact.reason != PhysicalMeleeContactReason::Accepted ||
        distanceGateState.targetCount != 1U) {
        return Fail(
            "real configured-capsule overlap must remain accepted");
    }
    PhysicalMeleeContactState invalidDistanceState{};
    gatedContact = QualifyPhysicalMeleeContactAtDistance(
        invalidDistanceState, 0x1234U, frame, 2U,
        PhysicalMeleeContactDistance{}, profile, false);
    if (gatedContact.accepted ||
        gatedContact.reason != PhysicalMeleeContactReason::InvalidContact ||
        invalidDistanceState.targetCount != 0U) {
        return Fail("invalid contact position must fail closed before latch");
    }

    PhysicalMeleeContactState contactState{};
    PhysicalMeleeContactQualification contact =
        QualifyPhysicalMeleeContact(
            contactState, 0x1234U, frame, 2U, profile);
    if (!contact.accepted ||
        contact.reason != PhysicalMeleeContactReason::Accepted ||
        !Near(contact.swingSpeedMetersPerSecond, 2.0F) ||
        !Near(contact.swingEnergyJoules, 4.0F) ||
        contactState.armed || !contactState.haveContact) {
        return Fail("speed-qualified swing must latch exactly once");
    }
    contact = QualifyPhysicalMeleeContact(
        contactState, 0x1234U, frame, 2U, profile);
    if (contact.accepted ||
        contact.reason != PhysicalMeleeContactReason::ContactLatched) {
        return Fail("latched contact must reject duplicate callbacks");
    }
    contact = QualifyPhysicalMeleeContact(
        contactState, 0x5678U, frame, 2U, profile);
    if (!contact.accepted || contactState.targetCount != 2U) {
        return Fail("one sweep must accept each distinct target once");
    }
    PhysicalMeleeFrame contactRearmFrame = frame;
    contactRearmFrame.currentTipUnits = PhysicalMeleeAdd(
        frame.currentTipUnits, {0.0F, 11.0F, 0.0F});
    PhysicalMeleeContactRearmUpdate contactRearm =
        UpdatePhysicalMeleeContactRearm(
            contactState, contactRearmFrame, true, profile);
    if (contactRearm.rearmed || contactRearm.distanceReached ||
        contactState.armed) {
        return Fail("sub-threshold tip travel must not re-arm contact");
    }
    contactRearmFrame.currentTipUnits = PhysicalMeleeAdd(
        frame.currentTipUnits, {0.0F, 13.0F, 0.0F});
    contactRearm = UpdatePhysicalMeleeContactRearm(
        contactState, contactRearmFrame, true, profile);
    if (contactRearm.rearmed || !contactRearm.distanceReached ||
        !contactRearm.distanceReachedThisSample ||
        contactRearm.releaseSampleCount != 0U ||
        contactState.armed) {
        return Fail(
            "fast follow-through must stay latched after rearm travel");
    }
    contact = QualifyPhysicalMeleeContact(
        contactState, 0x1234U, contactRearmFrame, 3U, profile);
    if (contact.accepted ||
        contact.reason != PhysicalMeleeContactReason::ContactLatched) {
        return Fail(
            "same target must remain blocked throughout the fast swing");
    }
    PhysicalMeleeFrame transientInvalidFrame = contactRearmFrame;
    transientInvalidFrame.sweepValid = false;
    contactState.releaseSampleCount = 2U;
    contactRearm = UpdatePhysicalMeleeContactRearm(
        contactState, transientInvalidFrame, true, profile);
    if (contactRearm.rearmed || !contactRearm.invalidSampleHeld ||
        !contactRearm.distanceReached ||
        contactRearm.releaseSampleCount != 0U ||
        !contactState.haveContact || contactState.armed ||
        contactState.targetCount != 2U ||
        contactState.releaseSampleCount != 0U) {
        return Fail(
            "transient invalid sample must hold latch and reset dwell");
    }
    contact = QualifyPhysicalMeleeContact(
        contactState, 0x1234U, contactRearmFrame, 4U, profile);
    if (contact.accepted ||
        contact.reason != PhysicalMeleeContactReason::ContactLatched) {
        return Fail(
            "invalid sweep sample must not reopen same-target damage");
    }
    contactRearmFrame.impactSpeedMetersPerSecond = 0.5F;
    contactRearmFrame.damageQualified = false;
    contactRearm = UpdatePhysicalMeleeContactRearm(
        contactState, contactRearmFrame, true, profile);
    if (contactRearm.rearmed ||
        contactRearm.releaseSampleCount != 1U ||
        !Near(contactRearm.releaseSpeedMetersPerSecond, 0.625F)) {
        return Fail("one low-speed sample must not re-arm contact");
    }
    contactRearmFrame.impactSpeedMetersPerSecond = 2.0F;
    contactRearmFrame.damageQualified = true;
    contactRearm = UpdatePhysicalMeleeContactRearm(
        contactState, contactRearmFrame, true, profile);
    if (contactRearm.rearmed ||
        contactRearm.releaseSampleCount != 0U) {
        return Fail("renewed fast motion must cancel a partial reset");
    }
    contactRearmFrame.impactSpeedMetersPerSecond = 0.5F;
    contactRearmFrame.damageQualified = false;
    contactRearm = UpdatePhysicalMeleeContactRearm(
        contactState, contactRearmFrame, true, profile);
    if (contactRearm.rearmed ||
        contactRearm.releaseSampleCount != 1U) {
        return Fail("release dwell must restart after speed rises");
    }
    contactRearm = UpdatePhysicalMeleeContactRearm(
        contactState, contactRearmFrame, true, profile);
    if (contactRearm.rearmed ||
        contactRearm.releaseSampleCount != 2U) {
        return Fail("release hysteresis must reject a two-sample dip");
    }
    contactRearm = UpdatePhysicalMeleeContactRearm(
        contactState, contactRearmFrame, true, profile);
    if (!contactRearm.rearmed ||
        contactRearm.releaseSampleCount !=
            kPhysicalMeleeContactReleaseSampleCount ||
        !contactState.armed || contactState.haveContact) {
        return Fail(
            "completed low-speed reset must re-arm physical contact");
    }
    PhysicalMeleeFrame crossSurfaceFrame = frame;
    crossSurfaceFrame.tipVelocityUnitsPerSecond = {
        0.0F, 200.0F, 0.0F};
    contact = QualifyPhysicalMeleeContact(
        contactState, 0x1234U, crossSurfaceFrame, 3U, profile);
    if (!contact.accepted ||
        contact.reason != PhysicalMeleeContactReason::Accepted) {
        return Fail("qualified swing must not depend on contact normal");
    }
    ResetPhysicalMeleeContactState(contactState);
    PhysicalMeleeFrame slowContactFrame = frame;
    slowContactFrame.damageQualified = false;
    slowContactFrame.impactSpeedMetersPerSecond = 0.5F;
    slowContactFrame.impactEnergyJoules = 0.25F;
    contact = QualifyPhysicalMeleeContact(
        contactState, 0x1234U, slowContactFrame, 4U, profile);
    if (contact.accepted ||
        contact.reason != PhysicalMeleeContactReason::SwingNotQualified) {
        return Fail("slow swing must fail contact qualification");
    }
    contact = QualifyPhysicalMeleeContact(
        contactState, 0x1234U, slowContactFrame, 4U, profile, false);
    if (!contact.accepted ||
        contact.reason != PhysicalMeleeContactReason::Accepted) {
        return Fail("overlap-only contact must not require swing force");
    }

    // The first configurable contact gate is intentionally speed-only.
    // A light weapon at 1.5 m/s carries less than the legacy one-joule
    // threshold but must still qualify once it clears Hit Speed.
    PhysicalMeleeProfile speedOnlyProfile = profile;
    speedOnlyProfile.massKilograms = 0.5F;
    PhysicalMeleeKinematicsState speedOnlyState{};
    UpdatePhysicalMeleeKinematics(
        speedOnlyState, Pose(0.0F, 0.0F, 0.0F), true,
        5'000'000'000ULL, speedOnlyProfile);
    const PhysicalMeleeFrame speedOnlyFrame =
        UpdatePhysicalMeleeKinematics(
            speedOnlyState, Pose(1.5F, 0.0F, 0.0F), true,
            5'010'000'000ULL, speedOnlyProfile);
    PhysicalMeleeContactState speedOnlyContactState{};
    const PhysicalMeleeContactQualification speedOnlyContact =
        QualifyPhysicalMeleeContact(
            speedOnlyContactState, 0x7777U, speedOnlyFrame, 5U,
            speedOnlyProfile);
    if (!speedOnlyFrame.damageQualified ||
        speedOnlyFrame.impactEnergyJoules >=
            speedOnlyProfile.minimumImpactEnergyJoules ||
        !speedOnlyContact.accepted) {
        return Fail(
            "physical hit qualification must use Hit Speed, not energy");
    }

    // Rearm Distance is the per-weapon travel guard, but it cannot end an
    // otherwise continuous fast swing by itself.
    PhysicalMeleeProfile shortRearmProfile = profile;
    shortRearmProfile.contactRearmSeparationMeters = 0.05F;
    PhysicalMeleeContactState shortRearmState{};
    contact = QualifyPhysicalMeleeContact(
        shortRearmState, 0x8888U, frame, 6U, shortRearmProfile);
    contactRearmFrame = frame;
    contactRearmFrame.currentTipUnits = PhysicalMeleeAdd(
        frame.currentTipUnits, {0.0F, 4.0F, 0.0F});
    contactRearm = UpdatePhysicalMeleeContactRearm(
        shortRearmState, contactRearmFrame, true, shortRearmProfile);
    if (!contact.accepted || contactRearm.rearmed ||
        contactRearm.distanceReached) {
        return Fail(
            "configured rearm travel must reject a short follow-through");
    }
    contactRearmFrame.currentTipUnits = PhysicalMeleeAdd(
        frame.currentTipUnits, {0.0F, 6.0F, 0.0F});
    contactRearm = UpdatePhysicalMeleeContactRearm(
        shortRearmState, contactRearmFrame, true, shortRearmProfile);
    if (contactRearm.rearmed || !contactRearm.distanceReached) {
        return Fail(
            "configured rearm travel must mark the distance guard");
    }
    contactRearmFrame.currentTipUnits = PhysicalMeleeAdd(
        frame.currentTipUnits, {0.0F, 106.0F, 0.0F});
    contactRearmFrame.impactSpeedMetersPerSecond = 8.0F;
    contactRearmFrame.damageQualified = true;
    contactRearm = UpdatePhysicalMeleeContactRearm(
        shortRearmState, contactRearmFrame, true, shortRearmProfile);
    if (contactRearm.rearmed ||
        contactRearm.maximumTipDisplacementMeters < 1.0F ||
        shortRearmState.armed) {
        return Fail(
            "one metre of fast follow-through must remain one swing");
    }
    contactRearmFrame.impactSpeedMetersPerSecond = 0.5F;
    contactRearmFrame.damageQualified = false;
    contactRearm = UpdatePhysicalMeleeContactRearm(
        shortRearmState, contactRearmFrame, true, shortRearmProfile);
    if (contactRearm.rearmed) {
        return Fail("first release sample must keep contact latched");
    }
    contactRearm = UpdatePhysicalMeleeContactRearm(
        shortRearmState, contactRearmFrame, true, shortRearmProfile);
    if (contactRearm.rearmed) {
        return Fail("second release sample must keep contact latched");
    }
    contactRearm = UpdatePhysicalMeleeContactRearm(
        shortRearmState, contactRearmFrame, true, shortRearmProfile);
    if (!contactRearm.rearmed || !shortRearmState.armed) {
        return Fail(
            "distance plus a completed swing must re-arm contact");
    }


    // Slow motion remains a valid collision sweep but does not qualify for
    // damage.
    frame = UpdatePhysicalMeleeKinematics(
        state, Pose(12.5F, 20.0F, 30.0F), true,
        1'020'000'000ULL, profile);
    if (!frame.sweepValid || frame.damageQualified ||
        !Near(frame.impactSpeedMetersPerSecond, 0.5F)) {
        return Fail("slow weapon motion must not qualify as a strike");
    }

    // A slash rotating around a stationary grip must be visible at the tip;
    // a grip-only velocity test would miss it.
    PhysicalMeleeKinematicsState rotationState{};
    UpdatePhysicalMeleeKinematics(
        rotationState, Pose(0.0F, 0.0F, 0.0F), true,
        2'000'000'000ULL, profile);
    constexpr float kTenDegreesRadians =
        10.0F * 3.14159265358979323846F / 180.0F;
    const fearvr::TrackingQuaternion tenDegreesY{
        0.0F, std::sin(kTenDegreesRadians * 0.5F), 0.0F,
        std::cos(kTenDegreesRadians * 0.5F)};
    frame = UpdatePhysicalMeleeKinematics(
        rotationState, Pose(0.0F, 0.0F, 0.0F, tenDegreesY), true,
        2'010'000'000ULL, profile);
    if (!frame.sweepValid || !frame.damageQualified ||
        !Near(PhysicalMeleeLength(frame.gripVelocityUnitsPerSecond), 0.0F) ||
        frame.impactSpeedMetersPerSecond < 17.0F ||
        !Near(
            frame.angularVelocityRadiansPerSecond.y,
            kTenDegreesRadians / 0.01F, 0.01F)) {
        return Fail("rotational slashes must use endpoint and angular velocity");
    }

    // The same physical speed is invariant across common runtime frame rates.
    const auto SpeedAtDelta = [&](std::uint64_t deltaNs) {
        PhysicalMeleeKinematicsState rateState{};
        UpdatePhysicalMeleeKinematics(
            rateState, Pose(0.0F, 0.0F, 0.0F), true,
            3'000'000'000ULL, profile);
        const float seconds = static_cast<float>(
            static_cast<double>(deltaNs) / 1'000'000'000.0);
        return UpdatePhysicalMeleeKinematics(
            rateState, Pose(200.0F * seconds, 0.0F, 0.0F), true,
            3'000'000'000ULL + deltaNs, profile)
            .impactSpeedMetersPerSecond;
    };
    if (!Near(SpeedAtDelta(11'111'111ULL), 2.0F) ||
        !Near(SpeedAtDelta(13'888'889ULL), 2.0F)) {
        return Fail("impact speed must not depend on OpenXR refresh rate");
    }

    // Some runtimes can publish a newly numbered input sample with almost the
    // same predicted display time. Never divide tiny pose noise by that clock
    // delta and manufacture an extreme impact velocity.
    PhysicalMeleeKinematicsState tinyDeltaState{};
    UpdatePhysicalMeleeKinematics(
        tinyDeltaState, Pose(0.0F, 0.0F, 0.0F), true,
        4'000'000'000ULL, profile);
    frame = UpdatePhysicalMeleeKinematics(
        tinyDeltaState, Pose(0.01F, 0.0F, 0.0F), true,
        4'000'000'001ULL, profile);
    if (frame.sweepValid || frame.damageQualified ||
        frame.impactSpeedMetersPerSecond != 0.0F ||
        frame.resetReason !=
            PhysicalMeleeResetReason::InsufficientSampleInterval) {
        return Fail("sub-millisecond timestamp deltas must fail closed");
    }

    // Tracking loss clears history. Reacquisition primes a pose rather than
    // sweeping through the missing interval.
    frame = UpdatePhysicalMeleeKinematics(
        state, Pose(12.5F, 20.0F, 30.0F), false,
        1'030'000'000ULL, profile);
    if (frame.poseValid || frame.sweepValid || state.havePose ||
        frame.resetReason != PhysicalMeleeResetReason::TrackingLost) {
        return Fail("tracking loss must clear physical-melee history");
    }
    frame = UpdatePhysicalMeleeKinematics(
        state, Pose(1000.0F, 20.0F, 30.0F), true,
        1'040'000'000ULL, profile);
    if (!frame.poseValid || frame.sweepValid ||
        frame.resetReason != PhysicalMeleeResetReason::TrackingReacquired) {
        return Fail("tracking reacquisition must not create a teleport hit");
    }

    // Long frame gaps and implausibly large one-frame travel both snap the
    // history without exposing a damage-capable sweep.
    frame = UpdatePhysicalMeleeKinematics(
        state, Pose(1002.0F, 20.0F, 30.0F), true,
        1'200'000'001ULL, profile);
    if (frame.sweepValid || frame.damageQualified ||
        frame.resetReason !=
            PhysicalMeleeResetReason::ExcessiveSampleGap) {
        return Fail("long sample gaps must fail closed");
    }
    frame = UpdatePhysicalMeleeKinematics(
        state, Pose(1100.0F, 20.0F, 30.0F), true,
        1'210'000'001ULL, profile);
    if (frame.sweepValid || frame.damageQualified ||
        frame.resetReason != PhysicalMeleeResetReason::ExcessiveTravel) {
        return Fail("large pose jumps must not become weapon sweeps");
    }

    PhysicalMeleePose invalidPose = Pose(0.0F, 0.0F, 0.0F);
    invalidPose.gripPositionUnits.x =
        std::numeric_limits<float>::infinity();
    frame = UpdatePhysicalMeleeKinematics(
        state, invalidPose, true, 1'220'000'001ULL, profile);
    if (frame.poseValid || state.havePose ||
        frame.resetReason != PhysicalMeleeResetReason::InvalidPose) {
        return Fail("non-finite weapon poses must fail closed");
    }
    PhysicalMeleeProfile invalidProfile = profile;
    invalidProfile.massKilograms = 0.0F;
    frame = UpdatePhysicalMeleeKinematics(
        state, Pose(0.0F, 0.0F, 0.0F), true,
        1'230'000'001ULL, invalidProfile);
    if (frame.poseValid || state.havePose ||
        frame.resetReason != PhysicalMeleeResetReason::InvalidProfile) {
        return Fail("invalid weapon profiles must fail closed");
    }
    const PhysicalMeleeProfile fallbackProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(999);
    if (fallbackProfile.id !=
            PhysicalMeleeProfileId::GenericOneHanded ||
        !Near(fallbackProfile.modelLocalGripPositionUnits.x, 0.0F) ||
        !Near(fallbackProfile.handlingWeight, 1.0F)) {
        return Fail("unknown weapon indices must retain the safe profile");
    }
    const PhysicalMeleeProfile pipeProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(
            kCondemnedPipeLeverWeaponIndex);
    if (!PhysicalMeleeProfileIsValid(pipeProfile) ||
        pipeProfile.id != PhysicalMeleeProfileId::Pipe ||
        std::strcmp(
            PhysicalMeleeProfileName(pipeProfile.id), "pipe") != 0 ||
        !Near(pipeProfile.localTipOffsetUnits.z, 75.0F) ||
        !Near(pipeProfile.modelLocalGripPositionUnits.x, 0.0F) ||
        !Near(pipeProfile.modelLocalGripPositionUnits.y, 3.0F) ||
        !Near(pipeProfile.modelLocalGripPositionUnits.z, -5.5F) ||
        !Near(pipeProfile.modelLocalGripRotation.x, -0.319308F) ||
        !Near(pipeProfile.modelLocalGripRotation.y, 0.423837F) ||
        !Near(pipeProfile.modelLocalGripRotation.z, 0.162696F) ||
        !Near(pipeProfile.modelLocalGripRotation.w, 0.831826F) ||
        pipeProfile.secondaryGripEnabled ||
        !Near(pipeProfile.massKilograms, 1.75F) ||
        !Near(pipeProfile.handlingWeight, 1.75F) ||
        !Near(pipeProfile.positionalFollow, 10.0F) ||
        !Near(pipeProfile.rotationalFollow, 8.0F) ||
        !Near(pipeProfile.catchUpStrength, 0.80F) ||
        !Near(pipeProfile.dampingRatio, 0.65F) ||
        !pipeProfile.swingAttackEnabled ||
        !Near(
            pipeProfile.swingAttackTriggerSpeedMetersPerSecond,
            3.00F)) {
        return Fail(
            "weapon index 32 must resolve the provisional one-hand pipe profile");
    }
    const PhysicalMeleeProfile axeProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(
            kCondemnedFireAxeWeaponIndex);
    if (!PhysicalMeleeProfileIsValid(axeProfile) ||
        std::strcmp(
            PhysicalMeleeProfileName(axeProfile.id),
            "fire_axe") != 0 ||
        !Near(axeProfile.modelLocalGripPositionUnits.x, -0.117F) ||
        !Near(axeProfile.modelLocalGripPositionUnits.y, -3.053F) ||
        !Near(axeProfile.modelLocalGripPositionUnits.z, -6.982F) ||
        !Near(axeProfile.modelLocalGripRotation.x, -0.052973F) ||
        !Near(axeProfile.modelLocalGripRotation.y, 0.840891F) ||
        !Near(axeProfile.modelLocalGripRotation.z, 0.248921F) ||
        !Near(axeProfile.modelLocalGripRotation.w, 0.477635F) ||
        !axeProfile.secondaryGripEnabled ||
        !Near(axeProfile.secondaryGripOffsetUnits.x, 3.114F) ||
        !Near(axeProfile.secondaryGripOffsetUnits.y, -30.258F) ||
        !Near(axeProfile.secondaryGripOffsetUnits.z, -14.828F) ||
        !Near(axeProfile.secondaryGripGrabRadiusMeters, 0.15F) ||
        !Near(axeProfile.massKilograms, 4.5F) ||
        !Near(axeProfile.handlingWeight, 4.0F) ||
        !Near(axeProfile.dampingRatio, 0.55F) ||
        !axeProfile.swingAttackEnabled ||
        !Near(
            axeProfile.swingAttackTriggerSpeedMetersPerSecond,
            3.00F) ||
        !Near(
            axeProfile.swingAttackRearmSpeedMetersPerSecond,
            0.75F) ||
        axeProfile.swingAttackPulseMilliseconds != 100U ||
        axeProfile.swingAttackCooldownMilliseconds != 450U ||
        axeProfile.positionalFollow >= profile.positionalFollow ||
        axeProfile.rotationalFollow >= profile.rotationalFollow) {
        return Fail(
            "weapon index 17 must resolve the persistent heavy axe profile");
    }
    for (const std::int32_t plankIndex :
         kCondemned2x4WeaponIndices) {
        const PhysicalMeleeProfile plankProfile =
            ResolvePhysicalMeleeProfileForRetailWeaponIndex(
                plankIndex);
        if (!PhysicalMeleeProfileIsValid(plankProfile) ||
            plankProfile.id != PhysicalMeleeProfileId::Plank ||
            !plankProfile.swingAttackEnabled ||
            plankProfile.secondaryGripEnabled ||
            !Near(
                plankProfile.modelLocalGripPositionUnits.y,
                pipeProfile.modelLocalGripPositionUnits.y) ||
            !Near(
                plankProfile.modelLocalGripPositionUnits.z,
                pipeProfile.modelLocalGripPositionUnits.z) ||
            !Near(
                plankProfile.modelLocalGripRotation.x,
                pipeProfile.modelLocalGripRotation.x) ||
            !Near(
                plankProfile.modelLocalGripRotation.w,
                pipeProfile.modelLocalGripRotation.w) ||
            !Near(
                plankProfile.handlingWeight,
                pipeProfile.handlingWeight) ||
            !Near(
                plankProfile.positionalFollow,
                pipeProfile.positionalFollow) ||
            !Near(
                plankProfile.rotationalFollow,
                pipeProfile.rotationalFollow) ||
            !Near(
                plankProfile.dampingRatio,
                pipeProfile.dampingRatio)) {
            return Fail(
                "every verified 2x4 variant must share the pipe one-hand preset");
        }
    }
    if (IsCondemned2x4WeaponIndex(2) ||
        IsCondemned2x4WeaponIndex(63) ||
        IsCondemned2x4WeaponIndex(66)) {
        return Fail("the 2x4 catalog mapping must not absorb adjacent weapons");
    }

    // A deliberate weighted sweep emits one short Retail attack pulse. It
    // must expire even if the weapon remains fast, then observe a slow
    // release and the cooldown before another swing can attack.
    PhysicalMeleeSwingAttackState swingAttackState{};
    PhysicalMeleeFrame swingAttackFrame{};
    swingAttackFrame.poseValid = true;
    swingAttackFrame.sweepValid = true;
    swingAttackFrame.impactSpeedMetersPerSecond = 2.99F;
    auto swingAttack = UpdatePhysicalMeleeSwingAttack(
        swingAttackState, swingAttackFrame, 1'000U, true,
        axeProfile);
    if (swingAttack.active || swingAttack.triggered ||
        !swingAttackState.armed) {
        return Fail("sub-threshold axe movement must not attack");
    }
    swingAttackFrame.impactSpeedMetersPerSecond = 3.00F;
    swingAttack = UpdatePhysicalMeleeSwingAttack(
        swingAttackState, swingAttackFrame, 1'010U, true,
        axeProfile);
    if (!swingAttack.active || !swingAttack.triggered ||
        swingAttackState.armed ||
        !PhysicalMeleeSwingAttackPulseIsActive(
            swingAttackState, 1'109U)) {
        return Fail("threshold crossing must start one attack pulse");
    }
    swingAttack = UpdatePhysicalMeleeSwingAttack(
        swingAttackState, swingAttackFrame, 1'110U, true,
        axeProfile);
    if (swingAttack.active || swingAttack.triggered ||
        PhysicalMeleeSwingAttackPulseIsActive(
            swingAttackState, 1'110U)) {
        return Fail("attack pulse must end after its bounded duration");
    }
    swingAttack = UpdatePhysicalMeleeSwingAttack(
        swingAttackState, swingAttackFrame, 1'300U, true,
        axeProfile);
    if (swingAttack.active || swingAttack.triggered) {
        return Fail("one fast sweep must not repeat while still raised");
    }
    swingAttackFrame.impactSpeedMetersPerSecond = 0.75F;
    swingAttack = UpdatePhysicalMeleeSwingAttack(
        swingAttackState, swingAttackFrame, 1'310U, true,
        axeProfile);
    if (!swingAttack.rearmed || !swingAttackState.armed) {
        return Fail("slow movement must re-arm the next deliberate swing");
    }
    swingAttackFrame.impactSpeedMetersPerSecond = 3.5F;
    swingAttack = UpdatePhysicalMeleeSwingAttack(
        swingAttackState, swingAttackFrame, 1'459U, true,
        axeProfile);
    if (swingAttack.active || swingAttack.triggered) {
        return Fail("re-armed movement must still respect attack cooldown");
    }
    swingAttack = UpdatePhysicalMeleeSwingAttack(
        swingAttackState, swingAttackFrame, 1'460U, true,
        axeProfile);
    if (!swingAttack.active || !swingAttack.triggered) {
        return Fail("a new post-cooldown swing must attack exactly once");
    }
    swingAttack = UpdatePhysicalMeleeSwingAttack(
        swingAttackState, swingAttackFrame, 1'461U, false,
        axeProfile);
    if (swingAttack.active || swingAttack.triggered ||
        !swingAttackState.armed) {
        return Fail("background or menu state must cancel swing attacks");
    }
    swingAttack = UpdatePhysicalMeleeSwingAttack(
        swingAttackState, swingAttackFrame, 2'000U, true,
        fallbackProfile);
    if (swingAttack.active || swingAttack.triggered) {
        return Fail("unmeasured weapon profiles must not gesture-attack");
    }
    fearvr::WeaponWeightFilterState lightFilter{};
    fearvr::WeaponWeightFilterState heavyFilter{};
    fearvr::WeaponWeightPose lightOutput{};
    fearvr::WeaponWeightPose heavyOutput{};
    const fearvr::WeaponWeightPose weightStart{
        {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}};
    const fearvr::WeaponWeightPose weightTarget{
        {0.20F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}};
    const fearvr::WeaponWeightProfile lightHandling{
        fallbackProfile.handlingWeight,
        fallbackProfile.positionalFollow,
        fallbackProfile.rotationalFollow,
        fallbackProfile.catchUpStrength,
        fallbackProfile.dampingRatio};
    const fearvr::WeaponWeightProfile heavyHandling{
        axeProfile.handlingWeight,
        axeProfile.positionalFollow,
        axeProfile.rotationalFollow,
        axeProfile.catchUpStrength,
        axeProfile.dampingRatio};
    fearvr::UpdateWeaponWeightFilter(
        lightFilter, weightStart, true, 5'000'000'000ULL,
        true, lightHandling, lightOutput);
    fearvr::UpdateWeaponWeightFilter(
        heavyFilter, weightStart, true, 5'000'000'000ULL,
        true, heavyHandling, heavyOutput);
    fearvr::UpdateWeaponWeightFilter(
        lightFilter, weightTarget, true, 5'011'111'111ULL,
        true, lightHandling, lightOutput);
    fearvr::UpdateWeaponWeightFilter(
        heavyFilter, weightTarget, true, 5'011'111'111ULL,
        true, heavyHandling, heavyOutput);
    if (!(heavyOutput.position.x > 0.0F &&
          heavyOutput.position.x < lightOutput.position.x &&
          lightOutput.position.x < weightTarget.position.x)) {
        return Fail(
            "the axe handling profile must visibly follow more slowly");
    }
    fearvr::WeaponWeightFilterState pipeFilter{};
    fearvr::WeaponWeightPose pipeOutput{};
    const fearvr::WeaponWeightProfile pipeHandling{
        pipeProfile.handlingWeight,
        pipeProfile.positionalFollow,
        pipeProfile.rotationalFollow,
        pipeProfile.catchUpStrength,
        pipeProfile.dampingRatio};
    fearvr::UpdateWeaponWeightFilter(
        pipeFilter, weightStart, true, 5'000'000'000ULL,
        true, pipeHandling, pipeOutput);
    fearvr::UpdateWeaponWeightFilter(
        pipeFilter, weightTarget, true, 5'011'111'111ULL,
        true, pipeHandling, pipeOutput);
    if (!(heavyOutput.position.x < pipeOutput.position.x &&
          pipeOutput.position.x < lightOutput.position.x)) {
        return Fail(
            "the pipe must have perceptible inertia between direct follow and the axe");
    }
    PhysicalMeleeProfile invalidHandling = profile;
    invalidHandling.handlingWeight = 4.1F;
    if (PhysicalMeleeProfileIsValid(invalidHandling)) {
        return Fail("out-of-range handling profiles must fail closed");
    }
    PhysicalMeleeProfile invalidSwingAttack = profile;
    invalidSwingAttack.swingAttackRearmSpeedMetersPerSecond =
        invalidSwingAttack.swingAttackTriggerSpeedMetersPerSecond;
    if (PhysicalMeleeProfileIsValid(invalidSwingAttack)) {
        return Fail("invalid swing-attack hysteresis must fail closed");
    }
    PhysicalMeleeProfile unknownProfile = profile;
    unknownProfile.id = static_cast<PhysicalMeleeProfileId>(255U);
    if (PhysicalMeleeProfileIsValid(unknownProfile)) {
        return Fail("unknown melee profile identities must fail closed");
    }

    return 0;
}
