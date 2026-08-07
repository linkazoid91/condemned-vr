#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "condemned_calibration_gizmo.h"
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
    const PhysicalMeleeWallProxyTransform firstProxy =
        ResolvePhysicalMeleeWallProxyTransform(frame, true);
    if (!firstProxy.active ||
        !Near(firstProxy.positionUnits.x, frame.currentTipUnits.x) ||
        !Near(firstProxy.positionUnits.y, frame.currentTipUnits.y) ||
        !Near(firstProxy.positionUnits.z, frame.currentTipUnits.z) ||
        !Near(firstProxy.rotation.w, 1.0F)) {
        return Fail("fresh wall proxy must follow the weapon endpoint");
    }
    if (ResolvePhysicalMeleeWallProxyTransform(frame, false).active) {
        return Fail("stale wall proxy samples must fail closed");
    }
    PhysicalMeleeFrame invalidProxyFrame = frame;
    invalidProxyFrame.currentRotation.w =
        std::numeric_limits<float>::infinity();
    if (ResolvePhysicalMeleeWallProxyTransform(
            invalidProxyFrame, true).active) {
        return Fail("invalid wall proxy transforms must fail closed");
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

    PhysicalMeleeContactState contactState{};
    PhysicalMeleeContactQualification contact =
        QualifyPhysicalMeleeContact(
            contactState, 0x1234U, frame.currentTipUnits,
            {1.0F, 0.0F, 0.0F}, frame, 2U, profile);
    if (!contact.accepted ||
        contact.reason != PhysicalMeleeContactReason::Accepted ||
        !Near(contact.normalSpeedMetersPerSecond, 2.0F) ||
        !Near(contact.normalEnergyJoules, 4.0F) ||
        contactState.armed || !contactState.haveContact) {
        return Fail("qualified normal impact must latch exactly once");
    }
    contact = QualifyPhysicalMeleeContact(
        contactState, 0x1234U, frame.currentTipUnits,
        {1.0F, 0.0F, 0.0F}, frame, 2U, profile);
    if (contact.accepted ||
        contact.reason != PhysicalMeleeContactReason::ContactLatched) {
        return Fail("latched contact must reject duplicate callbacks");
    }
    if (UpdatePhysicalMeleeContactSeparation(
            contactState,
            PhysicalMeleeAdd(
                frame.currentTipUnits, {0.0F, 50.0F, 0.0F}),
            true, profile) || contactState.armed) {
        return Fail("tangential wall motion must not re-arm contact");
    }
    if (!UpdatePhysicalMeleeContactSeparation(
            contactState,
            PhysicalMeleeAdd(
                frame.currentTipUnits, {13.0F, 0.0F, 0.0F}),
            true, profile) || !contactState.armed ||
        contactState.haveContact) {
        return Fail("normal separation must re-arm physical contact");
    }
    PhysicalMeleeFrame tangentialFrame = frame;
    tangentialFrame.tipVelocityUnitsPerSecond = {
        0.0F, 200.0F, 0.0F};
    contact = QualifyPhysicalMeleeContact(
        contactState, 0x1234U, frame.currentTipUnits,
        {1.0F, 0.0F, 0.0F}, tangentialFrame, 3U, profile);
    if (contact.accepted ||
        contact.reason != PhysicalMeleeContactReason::BelowNormalSpeed) {
        return Fail("tangential speed must not qualify as impact energy");
    }
    contact = QualifyPhysicalMeleeContact(
        contactState, 0x1234U, frame.currentTipUnits,
        {0.0F, 0.0F, 0.0F}, frame, 3U, profile);
    if (contact.accepted ||
        contact.reason != PhysicalMeleeContactReason::InvalidContact) {
        return Fail("invalid contact normals must fail closed");
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
