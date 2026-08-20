#include <cmath>
#include <cstdio>
#include <cstring>

#include "condemned_tool_menu.h"

namespace {

int Fail(const char* message) {
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

bool Near(float left, float right, float tolerance = 0.001F) {
    return std::fabs(left - right) <= tolerance;
}

bool FitsCompleteMenuOverlay(
    const char* const* rows,
    std::size_t rowCount,
    std::size_t& vertexCount) {
    using namespace condemnedvr;
    static ToolMenuOverlay overlay{};
    overlay.count = 0U;
    overlay.overflowed = false;
    AddToolMenuRectangle(
        overlay, -0.91F, 0.80F, 0.91F, -0.78F, 0xE0141B26U);
    AddToolMenuRectangle(
        overlay, -0.91F, 0.80F, 0.91F, 0.786F, 0xFF53C7E8U);
    AddToolMenuRectangle(
        overlay, -0.91F, -0.766F, 0.91F, -0.78F, 0xFF53C7E8U);
    AddToolMenuText(
        overlay, -0.79F, 0.70F, 0.0054F, 0.0090F,
        "CONDEMNED VR TOOLS", 0xFF76DBF4U);
    const float tabWidth = 1.60F /
        static_cast<float>(ToolMenuTab::Count);
    for (std::uint32_t index = 0;
         index < static_cast<std::uint32_t>(ToolMenuTab::Count);
         ++index) {
        AddToolMenuText(
            overlay, -0.80F + static_cast<float>(index) * tabWidth,
            0.535F, 0.00375F, 0.0064F,
            ToolMenuTabName(static_cast<ToolMenuTab>(index)),
            0xFFFFFFFFU);
    }
    AddToolMenuText(
        overlay, -0.75F, 0.405F, 0.0041F, 0.0068F,
        "EQUIPPED  UNMAPPED WEAPON   INDEX 123",
        0xFF76DBF4U);
    AddToolMenuRectangle(
        overlay, -0.79F, 0.345F, 0.79F, 0.240F, 0xB8327898U);
    for (std::size_t row = 0; row < rowCount; ++row) {
        AddToolMenuText(
            overlay, -0.75F,
            0.31F - static_cast<float>(row) * 0.105F,
            0.0046F, 0.0072F, rows[row], 0xFFFFFFFFU);
    }
    AddToolMenuText(
        overlay, -0.78F, -0.69F, 0.00355F, 0.0060F,
        "TRIGGERS TABS   LEFT STICK ROW   RIGHT STICK VALUE   A SELECT   B CLOSE",
        0xFF95A5B2U);
    const ToolMenuPanelTransform transform =
        ResolveToolMenuPanelTransform(
            FEARVR_EYE_LEFT, 0.064F, 1.570796327F);
    const bool placed = ApplyToolMenuPanelTransform(
        overlay, transform);
    vertexCount = overlay.count;
    return placed && !overlay.overflowed && overlay.count > 0U &&
        overlay.count <=
            FEARVR_OVERLAY_TRIANGLE_MAX_INPUT_VERTICES;
}

} // namespace

int main() {
    using namespace condemnedvr;

    static_assert(
        kToolMenuMaximumTriangleVertices ==
            FEARVR_OVERLAY_TRIANGLE_MAX_INPUT_VERTICES,
        "tool menu and D3D9 bridge must share one triangle-vertex cap");

    if (ToolMenuRowCount(ToolMenuTab::Melee) != 4U ||
        std::strcmp(
            ToolMenuTabName(ToolMenuTab::Melee),
            "MELEE") != 0) {
        return Fail(
            "physical-hit tuning must have four bounded Melee rows");
    }
    if (ToolMenuRowCount(ToolMenuTab::Block) != 7U ||
        std::strcmp(
            ToolMenuTabName(ToolMenuTab::Block),
            "BLOCK") != 0) {
        return Fail(
            "guard setup must expose pose and timing controls");
    }
    if (ToolMenuRowCount(ToolMenuTab::Grip) != 10U ||
        std::strcmp(
            ToolMenuTabName(ToolMenuTab::Grip),
            "GRIP") != 0) {
        return Fail(
            "Grip setup must expose primary and advanced alignment rows");
    }


    if (ToolMenuRowCount(ToolMenuTab::Collider) != 10U ||
        std::strcmp(
            ToolMenuTabName(ToolMenuTab::Collider),
            "COLLIDER") != 0) {
        return Fail("collider setup must have a dedicated bounded menu tab");
    }
    if (ToolMenuRowCount(ToolMenuTab::Author) != 8U ||
        std::strcmp(
            ToolMenuTabName(ToolMenuTab::Author),
            "AUTHOR") != 0) {
        return Fail(
            "interaction authoring must have a dedicated bounded menu tab");
    }
    if (ToolMenuRowCount(ToolMenuTab::PlayerCollider) != 3U ||
        std::strcmp(
            ToolMenuTabName(ToolMenuTab::PlayerCollider),
            "PLAYER COL") != 0) {
        return Fail(
            "player collision must be separate from weapon geometry");
    }
    if (ToolMenuRowCount(ToolMenuTab::BlockCollider) != 10U ||
        std::strcmp(
            ToolMenuTabName(ToolMenuTab::BlockCollider),
            "BLOCK COL") != 0) {
        return Fail(
            "block geometry must have a dedicated bounded menu tab");
    }
    if (ToolMenuRowCount(ToolMenuTab::Debug) != 3U ||
        std::strcmp(
            ToolMenuTabName(ToolMenuTab::Debug),
            "DEBUG") != 0) {
        return Fail(
            "debug draw visibility must have three independent menu rows");
    }
    ToolMenuDebugDrawSettings debugDraw{};
    if (debugDraw.colliderVisible ||
        debugDraw.blockColliderVisible ||
        debugDraw.controllerVisible ||
        !UpdateToolMenuDebugDrawSettings(
            debugDraw, 0U, 0, true) ||
        !debugDraw.colliderVisible ||
        debugDraw.blockColliderVisible ||
        debugDraw.controllerVisible ||
        !UpdateToolMenuDebugDrawSettings(
            debugDraw, 1U, -1, false) ||
        !debugDraw.colliderVisible ||
        !debugDraw.blockColliderVisible ||
        debugDraw.controllerVisible ||
        !UpdateToolMenuDebugDrawSettings(
            debugDraw, 2U, 1, false) ||
        !debugDraw.controllerVisible ||
        !UpdateToolMenuDebugDrawSettings(
            debugDraw, 0U, 1, false) ||
        debugDraw.colliderVisible ||
        !debugDraw.blockColliderVisible ||
        !debugDraw.controllerVisible ||
        !UpdateToolMenuDebugDrawSettings(
            debugDraw, 1U, 0, true) ||
        debugDraw.colliderVisible ||
        debugDraw.blockColliderVisible ||
        !debugDraw.controllerVisible ||
        !UpdateToolMenuDebugDrawSettings(
            debugDraw, 2U, 0, true) ||
        debugDraw.controllerVisible ||
        UpdateToolMenuDebugDrawSettings(
            debugDraw, 2U, 0, false) ||
        UpdateToolMenuDebugDrawSettings(
            debugDraw, 3U, 1, false)) {
        return Fail(
            "debug draw toggles must default hidden and remain independent");
    }
    ToolMenuBlockTimingSettings blockTiming{};
    if (!ToolMenuBlockTimingSettingsAreValid(blockTiming) ||
        blockTiming.overrideEnabled ||
        blockTiming.collisionWindowMilliseconds != 450U ||
        !UpdateToolMenuBlockTimingSettings(
            blockTiming, 0U, 0, true) ||
        !blockTiming.overrideEnabled ||
        !UpdateToolMenuBlockTimingSettings(
            blockTiming, 1U, 2, false) ||
        blockTiming.collisionWindowMilliseconds != 500U ||
        UpdateToolMenuBlockTimingSettings(
            blockTiming, 1U, 0, true) ||
        UpdateToolMenuBlockTimingSettings(
            blockTiming, 2U, 1, false)) {
        return Fail(
            "block timing must preserve Retail by default and tune in bounded steps");
    }
    bool blockWindowOverrideApplied = false;
    if (!Near(ResolveToolMenuBlockWindowSeconds(
                  blockTiming, 0.43F,
                  blockWindowOverrideApplied),
              0.50F) ||
        !blockWindowOverrideApplied) {
        return Fail(
            "enabled block timing must resolve the configured native window");
    }
    blockTiming.overrideEnabled = false;
    if (!Near(ResolveToolMenuBlockWindowSeconds(
                  blockTiming, 0.43F,
                  blockWindowOverrideApplied),
              0.43F) ||
        blockWindowOverrideApplied) {
        return Fail(
            "disabled block timing must preserve the Retail native window");
    }
    LiveColliderAlignmentCommand liveCommand{};
    const char* const validLiveCommand =
        "version=1 revision=42 process_id=1234 weapon_index=32 "
        "position_x=1.5 position_y=-2 position_z=3.25 "
        "rotation_x=-50 rotation_y=10 rotation_z=180 "
        "length=40 radius=4.5 reversed=1";
    if (ParseLiveColliderAlignmentCommand(
            validLiveCommand, liveCommand) !=
            LiveColliderAlignmentCommandParseResult::Ok ||
        liveCommand.revision != 42U ||
        !LiveColliderAlignmentCommandMatchesTarget(
            liveCommand, 1234U, 32) ||
        LiveColliderAlignmentCommandMatchesTarget(
            liveCommand, 1235U, 32) ||
        !Near(liveCommand.settings.positionOffsetUnits.x, 1.5F) ||
        !Near(liveCommand.settings.positionOffsetUnits.y, -2.0F) ||
        !Near(liveCommand.settings.positionOffsetUnits.z, 3.25F) ||
        !Near(liveCommand.settings.rotationOffsetDegrees.x, -50.0F) ||
        !Near(liveCommand.settings.rotationOffsetDegrees.y, 10.0F) ||
        !Near(liveCommand.settings.rotationOffsetDegrees.z, 180.0F) ||
        !Near(liveCommand.settings.lengthUnits, 40.0F) ||
        !Near(liveCommand.settings.radiusUnits, 4.5F) ||
        !liveCommand.settings.reversed) {
        return Fail(
            "live collider commands must parse exact bounded session values");
    }
    if (ParseLiveColliderAlignmentCommand(
            "version=1 revision=42 process_id=1234 weapon_index=32 "
            "position_x=0 position_y=0 position_z=0 "
            "rotation_x=0 rotation_y=0 rotation_z=0 "
            "length=40 radius=4.5 reversed=0 trailing=junk",
            liveCommand) !=
            LiveColliderAlignmentCommandParseResult::Malformed ||
        ParseLiveColliderAlignmentCommand(
            "version=1 revision=42 process_id=1234 weapon_index=32 "
            "position_x=0 position_y=0 position_z=0 "
            "rotation_x=0 rotation_y=0 rotation_z=0 "
            "length=251 radius=4.5 reversed=0",
            liveCommand) !=
            LiveColliderAlignmentCommandParseResult::InvalidValue ||
        ParseLiveColliderAlignmentCommand(nullptr, liveCommand) !=
            LiveColliderAlignmentCommandParseResult::Missing) {
        return Fail(
            "live collider commands must reject malformed or unsafe values");
    }

    if (ToolMenuRowCount(ToolMenuTab::TwoHand) != 8U ||
        std::strcmp(
            ToolMenuTabName(ToolMenuTab::TwoHand),
            "2-HAND") != 0) {
        return Fail("two-hand setup must have a dedicated bounded menu tab");
    }
    if (ToolMenuRowCount(ToolMenuTab::HandIk) != 9U ||
        ToolMenuRowCount(ToolMenuTab::HandIk, true) != 2U ||
        std::strcmp(
            ToolMenuTabName(ToolMenuTab::HandIk),
            "HAND IK") != 0) {
        return Fail(
            "right-hand alignment must have a dedicated bounded menu tab");
    }
    if (ToolMenuRowCount(ToolMenuTab::ElbowIk) != 6U ||
        std::strcmp(
            ToolMenuTabName(ToolMenuTab::ElbowIk),
            "ELBOW") != 0) {
        return Fail(
            "elbow pole tuning must have a dedicated bounded menu tab");
    }
    if (ToolMenuRowCount(ToolMenuTab::LeftHandIk) != 8U ||
        std::strcmp(
            ToolMenuTabName(ToolMenuTab::LeftHandIk),
            "LEFT IK") != 0) {
        return Fail(
            "left-hand alignment must have a dedicated bounded menu tab");
    }
    fearvr::ArmIkTuning leftTuning{};
    leftTuning.leftHandRightMeters = 0.01F;
    leftTuning.leftHandUpMeters = 0.02F;
    leftTuning.leftHandForwardMeters = -0.03F;
    leftTuning.leftHandYawDegrees = 90.0F;
    const PhysicalMeleeRigidTransform leftTarget =
        ResolveToolMenuLeftHandIkTarget(
            {{10.0F, 20.0F, 30.0F},
             {0.0F, 0.0F, 0.0F, 1.0F}},
            leftTuning, 100.0F);
    if (!PhysicalMeleeRigidTransformIsValid(leftTarget) ||
        !Near(leftTarget.positionUnits.x, 11.0F) ||
        !Near(leftTarget.positionUnits.y, 22.0F) ||
        !Near(leftTarget.positionUnits.z, 27.0F) ||
        PhysicalMeleeRigidTransformIsValid(
            ResolveToolMenuLeftHandIkTarget(
                {{10.0F, 20.0F, 30.0F},
                 {0.0F, 0.0F, 0.0F, 1.0F}},
                leftTuning, 0.0F))) {
        return Fail(
            "left-hand correction must remain controller-local and bounded");
    }
    if (ClassifyRetailWeaponAnimationProperty(
            "WEAP_1HandedDebris") !=
            RetailWeaponPoseFamily::OneHandedDebris ||
        ClassifyRetailWeaponAnimationProperty(
            "weap_2handeddebris") !=
            RetailWeaponPoseFamily::TwoHandedDebris ||
        ClassifyRetailWeaponAnimationProperty("WEAP_FireAxe") !=
            RetailWeaponPoseFamily::WeaponSpecific ||
        ClassifyRetailWeaponAnimationProperty(nullptr) !=
            RetailWeaponPoseFamily::Unknown ||
        std::strcmp(
            RetailWeaponPoseFamilyLabel(
                RetailWeaponPoseFamily::OneHandedDebris),
            "1-HANDED DEBRIS") != 0) {
        return Fail(
            "Retail animation properties must retain their diagnostic pose family");
    }

    ToolMenuState state{};
    ToolMenuInputEvent input{};
    input.toggle = true;
    ToolMenuTransition transition = UpdateToolMenuState(state, input);
    if (!state.open || !transition.opened || state.row != 1U) {
        return Fail("tool menu must open on the melee trigger row");
    }
    input = {};
    input.previousTab = true;
    transition = UpdateToolMenuState(state, input);
    if (state.tab != ToolMenuTab::Debug || state.row != 1U ||
        !transition.selectionChanged) {
        return Fail("tool-menu tabs must wrap while retaining a valid row");
    }
    input = {};
    input.nextTab = true;
    UpdateToolMenuState(state, input);
    if (state.tab != ToolMenuTab::Melee) {
        return Fail("next tab must wrap from debug to melee");
    }
    input = {};
    input.nextRow = true;
    UpdateToolMenuState(state, input);
    if (state.row != 2U) {
        return Fail("melee rows must advance deterministically");
    }
    input = {};
    input.increase = true;
    transition = UpdateToolMenuState(state, input);
    if (transition.valueDelta != 1) {
        return Fail("right adjustment must request one positive step");
    }
    input = {};
    input.close = true;
    transition = UpdateToolMenuState(state, input);
    if (state.open || !transition.closed) {
        return Fail("tool-menu back must close the overlay");
    }

    ToolMenuState emptyHandMenuState{
        ToolMenuTab::HandIk, 1U, true};
    ToolMenuInputEvent emptyHandMenuInput{};
    emptyHandMenuInput.nextRow = true;
    UpdateToolMenuState(
        emptyHandMenuState, emptyHandMenuInput, true);
    if (emptyHandMenuState.row != 0U) {
        return Fail(
            "empty-hand Hand IK next row must wrap across two actions");
    }
    emptyHandMenuInput = {};
    emptyHandMenuInput.previousRow = true;
    UpdateToolMenuState(
        emptyHandMenuState, emptyHandMenuInput, true);
    if (emptyHandMenuState.row != 1U) {
        return Fail(
            "empty-hand Hand IK previous row must wrap across two actions");
    }
    emptyHandMenuState.row = 8U;
    UpdateToolMenuState(
        emptyHandMenuState, ToolMenuInputEvent{}, true);
    if (emptyHandMenuState.row != 1U) {
        return Fail(
            "entering empty-hand Hand IK must clamp a weapon-page row");
    }

    ToolMenuMeleeSettings settings{};
    if (!ToolMenuMeleeSettingsAreValid(settings) ||
        !settings.requireSwingForContactDamage ||
        !Near(settings.hitSpeedMetersPerSecond, 1.25F) ||
        !Near(settings.contactRearmDistanceMeters, 0.12F) ||
        !Near(settings.swingTriggerSpeedMetersPerSecond, 3.0F)) {
        return Fail("tool-menu melee defaults must be safe and valid");
    }
    ToolMenuRightHandIkSettings handIk{};
    handIk.positionOffsetUnits = {1.0F, 2.0F, 3.0F};
    handIk.rotationOffsetDegrees = {0.0F, 90.0F, 0.0F};
    const PhysicalMeleeRigidTransform adjustedHandTarget =
        ResolveToolMenuRightHandIkTarget(
            {{10.0F, 20.0F, 30.0F},
             {0.0F, 0.0F, 0.0F, 1.0F}},
            handIk);
    if (!ToolMenuRightHandIkSettingsAreValid(handIk) ||
        !Near(adjustedHandTarget.positionUnits.x, 11.0F) ||
        !Near(adjustedHandTarget.positionUnits.y, 22.0F) ||
        !Near(adjustedHandTarget.positionUnits.z, 33.0F) ||
        !Near(std::fabs(adjustedHandTarget.rotation.y), 0.707107F) ||
        !Near(std::fabs(adjustedHandTarget.rotation.w), 0.707107F)) {
        return Fail(
            "right-hand IK settings must resolve in weapon-local space");
    }
    handIk.positionOffsetUnits.x = 100.01F;
    if (ToolMenuRightHandIkSettingsAreValid(handIk)) {
        return Fail("out-of-range right-hand IK offsets must fail closed");
    }
    PhysicalMeleeProfile axe =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(
            kCondemnedFireAxeWeaponIndex);
    settings.swingTriggerSpeedMetersPerSecond = 4.25F;
    settings.requireSwingForContactDamage = false;
    settings.hitSpeedMetersPerSecond = 2.5F;
    settings.contactRearmDistanceMeters = 0.20F;
    settings.massKilograms = 6.0F;
    settings.handlingWeight = 3.5F;
    ApplyToolMenuMeleeSettings(settings, axe);
    if (axe.requireSwingForContactDamage ||
        !Near(axe.minimumImpactSpeedMetersPerSecond, 2.5F) ||
        !Near(axe.contactRearmSeparationMeters, 0.20F) ||
        !Near(axe.swingAttackTriggerSpeedMetersPerSecond, 4.25F) ||
        !Near(axe.massKilograms, 6.0F) ||
        !Near(axe.handlingWeight, 3.5F)) {
        return Fail("live menu settings must update the axe profile");
    }
    const PhysicalMeleeProfile pipe =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(
            kCondemnedPipeLeverWeaponIndex);
    if (pipe.id != PhysicalMeleeProfileId::Pipe ||
        !ToolMenuProfileSupportsSwingAttack(pipe.id) ||
        !ToolMenuMeleeSettingsFromProfile(pipe).swingAttackEnabled ||
        !ToolMenuMeleeSettingsFromProfile(pipe)
             .requireSwingForContactDamage ||
        !Near(ToolMenuMeleeSettingsFromProfile(pipe)
                  .hitSpeedMetersPerSecond, 1.25F) ||
        std::strcmp(ToolMenuWeaponProfileLabel(pipe.id), "PIPE") != 0) {
        return Fail(
            "pipe_lever must expose its independent one-hand tool profile");
    }
    const PhysicalMeleeProfile oneHandedDebris =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(29);
    if (oneHandedDebris.id !=
            PhysicalMeleeProfileId::OneHandedDebris ||
        !ToolMenuProfileSupportsSwingAttack(oneHandedDebris.id) ||
        !ToolMenuMeleeSettingsFromProfile(
             oneHandedDebris).swingAttackEnabled ||
        std::strcmp(
            ToolMenuWeaponProfileLabel(oneHandedDebris.id),
            "ONE-HANDED") != 0 ||
        !Near(
            oneHandedDebris.modelLocalGripPositionUnits.y,
            pipe.modelLocalGripPositionUnits.y) ||
        !Near(
            oneHandedDebris.handlingWeight,
            pipe.handlingWeight)) {
        return Fail(
            "mapped one-handed debris must expose the temporary pipe baseline");
    }
    ToolMenuColliderSettings collider =
        ToolMenuColliderSettingsFromProfile(pipe);
    if (!ToolMenuColliderSettingsAreValid(collider) ||
        !Near(collider.lengthUnits, 75.0F) ||
        !Near(collider.radiusUnits, 4.0F) ||
        collider.reversed) {
        return Fail("pipe collider defaults must be editable and valid");
    }
    collider.positionOffsetUnits = {1.0F, 2.0F, 3.0F};
    collider.rotationOffsetDegrees = {0.0F, 90.0F, 0.0F};
    collider.lengthUnits = 80.0F;
    collider.radiusUnits = 5.0F;
    PhysicalMeleeProfile configuredPipe = pipe;
    ApplyToolMenuColliderSettings(collider, configuredPipe);
    if (!Near(configuredPipe.localBaseOffsetUnits.x, 1.0F) ||
        !Near(configuredPipe.localBaseOffsetUnits.y, 2.0F) ||
        !Near(configuredPipe.localBaseOffsetUnits.z, 3.0F) ||
        !Near(configuredPipe.localTipOffsetUnits.x, 81.0F) ||
        !Near(configuredPipe.localTipOffsetUnits.y, 2.0F) ||
        !Near(configuredPipe.localTipOffsetUnits.z, 3.0F) ||
        !Near(configuredPipe.radiusUnits, 5.0F)) {
        return Fail("collider settings must resolve in controller-local space");
    }
    const PhysicalMeleePose weightedGripPose{
        {100.0F, 200.0F, 300.0F},
        {0.0F, 0.0F, 0.0F, 1.0F}};
    PhysicalMeleeFrame sourceColliderFrame{};
    sourceColliderFrame.currentRotation = weightedGripPose.rotation;
    sourceColliderFrame.currentBaseUnits = PhysicalMeleeEndpoint(
        weightedGripPose, pipe.localBaseOffsetUnits);
    sourceColliderFrame.currentTipUnits = PhysicalMeleeEndpoint(
        weightedGripPose, pipe.localTipOffsetUnits);
    sourceColliderFrame.radiusUnits = pipe.radiusUnits;
    sourceColliderFrame.poseValid = true;
    PhysicalMeleeFrame blockColliderFrame{};
    if (!ResolveToolMenuColliderFrameAtCurrentPose(
            sourceColliderFrame, pipe, collider,
            blockColliderFrame) ||
        !Near(blockColliderFrame.currentBaseUnits.x, 101.0F) ||
        !Near(blockColliderFrame.currentBaseUnits.y, 202.0F) ||
        !Near(blockColliderFrame.currentBaseUnits.z, 303.0F) ||
        !Near(blockColliderFrame.currentTipUnits.x, 181.0F) ||
        !Near(blockColliderFrame.currentTipUnits.y, 202.0F) ||
        !Near(blockColliderFrame.currentTipUnits.z, 303.0F) ||
        !Near(blockColliderFrame.radiusUnits, 5.0F)) {
        return Fail(
            "block geometry must reproject onto the attack frame's exact weighted grip pose");
    }
    collider.reversed = true;
    ApplyToolMenuColliderSettings(collider, configuredPipe);
    if (!Near(configuredPipe.localTipOffsetUnits.x, -79.0F)) {
        return Fail("collider direction toggle must reverse the capsule");
    }
    collider.lengthUnits = 0.0F;
    if (ToolMenuColliderSettingsAreValid(collider)) {
        return Fail("degenerate collider settings must fail closed");
    }

    const PhysicalMeleeProfile plank =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(
            kCondemned2x4WeaponIndices.front());
    if (plank.id != PhysicalMeleeProfileId::Plank ||
        !ToolMenuProfileSupportsSwingAttack(plank.id) ||
        !ToolMenuMeleeSettingsFromProfile(plank).swingAttackEnabled ||
        std::strcmp(
            ToolMenuWeaponProfileLabel(plank.id), "PLANK") != 0 ||
        !Near(
            plank.modelLocalGripPositionUnits.y,
            pipe.modelLocalGripPositionUnits.y) ||
        !Near(plank.handlingWeight, pipe.handlingWeight)) {
        return Fail(
            "the verified 2x4 family must expose the shared pipe preset as PLANK");
    }
    PhysicalMeleeProfile fallback{};
    ApplyToolMenuMeleeSettings(settings, fallback);
    if (fallback.swingAttackEnabled ||
        fallback.requireSwingForContactDamage ||
        !Near(fallback.minimumImpactSpeedMetersPerSecond, 2.5F) ||
        !Near(fallback.contactRearmSeparationMeters, 0.20F) ||
        !Near(fallback.massKilograms, 6.0F)) {
        return Fail(
            "generic weapons may tune handling but must not arm swing attack");
    }
    settings.hitSpeedMetersPerSecond = 0.24F;
    if (ToolMenuMeleeSettingsAreValid(settings)) {
        return Fail("out-of-range Hit Speed must fail closed");
    }
    settings.hitSpeedMetersPerSecond = 2.5F;
    settings.contactRearmDistanceMeters = 0.01F;
    if (ToolMenuMeleeSettingsAreValid(settings)) {
        return Fail("out-of-range Rearm Distance must fail closed");
    }
    settings.contactRearmDistanceMeters = 0.20F;
    settings.swingRearmSpeedMetersPerSecond =
        settings.swingTriggerSpeedMetersPerSecond;
    if (ToolMenuMeleeSettingsAreValid(settings)) {
        return Fail("invalid menu hysteresis must fail closed");
    }

    ToolMenuWeaponSettingsRegistry registry{};
    const PhysicalMeleeProfile catalogAxe =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(
            kCondemnedFireAxeWeaponIndex);
    ToolMenuWeaponSettingsSlot* axeSlot =
        ResolveToolMenuWeaponSettingsSlot(
            registry, kCondemnedFireAxeWeaponIndex, catalogAxe);
    const PhysicalMeleeProfile genericProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(8);
    ToolMenuWeaponSettingsSlot* genericSlot =
        ResolveToolMenuWeaponSettingsSlot(
            registry, 8, genericProfile);
    if (axeSlot == nullptr || genericSlot == nullptr ||
        axeSlot == genericSlot || !axeSlot->settings.swingAttackEnabled ||
        !Near(axeSlot->settings.massKilograms, 4.5F) ||
        !Near(axeSlot->colliderSettings.lengthUnits, 82.0F) ||
        !Near(axeSlot->blockColliderSettings.lengthUnits, 82.0F) ||
        !axeSlot->blockColliderUsesAttackFallback ||
        axeSlot->blockTimingSettings.overrideEnabled ||
        !Near(genericSlot->colliderSettings.lengthUnits, 75.0F) ||
        !Near(genericSlot->blockColliderSettings.lengthUnits, 75.0F) ||
        genericSlot->settings.swingAttackEnabled ||
        !Near(genericSlot->settings.massKilograms, 1.5F)) {
        return Fail(
            "each Retail weapon index must receive isolated profile defaults");
    }
    axeSlot->settings.swingTriggerSpeedMetersPerSecond = 5.25F;
    axeSlot->blockPoseSettings.captured = true;
    axeSlot->blockPoseSettings.enabled = true;
    axeSlot->blockPoseSettings.headRelativePositionMeters.x = 0.25F;
    axeSlot->rightHandIkSettings.positionOffsetUnits.x = 2.5F;
    axeSlot->colliderSettings.reversed = true;
    genericSlot->settings.massKilograms = 2.75F;
    axeSlot = ResolveToolMenuWeaponSettingsSlot(
        registry, kCondemnedFireAxeWeaponIndex, catalogAxe);
    genericSlot = ResolveToolMenuWeaponSettingsSlot(
        registry, 8, genericProfile);
    if (axeSlot == nullptr || genericSlot == nullptr ||
        !Near(axeSlot->settings.swingTriggerSpeedMetersPerSecond, 5.25F) ||
        !Near(axeSlot->settings.massKilograms, 4.5F) ||
        !axeSlot->blockPoseSettings.captured ||
        !axeSlot->blockPoseSettings.enabled ||
        !Near(
            axeSlot->blockPoseSettings.headRelativePositionMeters.x,
            0.25F) ||
        !Near(axeSlot->rightHandIkSettings.positionOffsetUnits.x, 2.5F) ||
        !axeSlot->colliderSettings.reversed ||
        !Near(genericSlot->settings.massKilograms, 2.75F) ||
        genericSlot->blockPoseSettings.captured ||
        genericSlot->blockPoseSettings.enabled ||
        !Near(genericSlot->rightHandIkSettings.positionOffsetUnits.x, 0.0F) ||
        genericSlot->colliderSettings.reversed ||
        std::strcmp(
            ToolMenuWeaponProfileLabel(axeSlot->profileId),
            "FIRE AXE") != 0) {
        return Fail(
            "switching weapons must restore that weapon's own edited values");
    }

    FearVrInputState controller{};
    controller.flags = FEARVR_IF_VALID | FEARVR_IF_FOCUSED;
    controller.activeHands =
        FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT;
    controller.squeeze[FEARVR_HAND_LEFT] = 0.75F;
    controller.squeeze[FEARVR_HAND_RIGHT] = 0.75F;
    controller.buttons = FEARVR_IB_LEFT_SECONDARY;
    if (!ToolMenuToggleChordDown(controller, true)) {
        return Fail("both grips plus Y must resolve the menu chord");
    }
    controller.squeeze[FEARVR_HAND_RIGHT] = 0.74F;
    if (ToolMenuToggleChordDown(controller, true) ||
        ToolMenuToggleChordDown(controller, false)) {
        return Fail("loose grips or stale input must reject the menu chord");
    }
    if (ShouldActivateToolMenuShortcut(false, false, true) ||
        !ShouldActivateToolMenuShortcut(false, true, true) ||
        !ShouldActivateToolMenuShortcut(true, false, true) ||
        ShouldActivateToolMenuShortcut(false, true, false)) {
        return Fail(
            "Developer Tools must gate opening but preserve an open-menu close");
    }
    if (ShouldCaptureToolMenuShortcutInput(
            false, false, false, true) ||
        !ShouldCaptureToolMenuShortcutInput(
            false, false, true, true) ||
        !ShouldCaptureToolMenuShortcutInput(
            true, false, false, false) ||
        !ShouldCaptureToolMenuShortcutInput(
            false, true, false, false)) {
        return Fail(
            "disabled shortcuts must not capture their chord, while active "
            "menu release capture remains safe");
    }

    ToolMenuOverlay overlay{};
    if (!AddToolMenuRectangle(
            overlay, -0.8F, 0.8F, 0.8F, -0.8F,
            0xCC101820U) ||
        !AddToolMenuText(
            overlay, -0.7F, 0.6F, 0.005F, 0.008F,
            "MELEE 3.00 M/S", 0xFFFFFFFFU) ||
        overlay.count <= 6U || (overlay.count % 3U) != 0U ||
        overlay.overflowed) {
        return Fail("tool-menu panel and font must emit valid triangles");
    }
    if (ToolMenuGlyphRow('A', 0U) == 0U ||
        ToolMenuGlyphRow('0', 3U) == 0U ||
        ToolMenuGlyphRow(' ', 3U) != 0U) {
        return Fail("tool-menu font must cover labels and numeric values");
    }
    const ToolMenuPanelTransform leftPanel =
        ResolveToolMenuPanelTransform(
            FEARVR_EYE_LEFT, 0.064F, 1.570796327F);
    const ToolMenuPanelTransform rightPanel =
        ResolveToolMenuPanelTransform(
            FEARVR_EYE_RIGHT, 0.064F, 1.570796327F);
    ToolMenuPanelPlacement closerPlacement{};
    closerPlacement.distanceMeters = 0.75F;
    const ToolMenuPanelTransform closerLeftPanel =
        ResolveToolMenuPanelTransform(
            FEARVR_EYE_LEFT, 0.064F, 1.570796327F,
            closerPlacement);
    if (!leftPanel.valid || !rightPanel.valid ||
        !closerLeftPanel.valid ||
        !Near(leftPanel.scale, kToolMenuDefaultScale) ||
        !Near(
            leftPanel.verticalOffsetNdc,
            kToolMenuDefaultVerticalOffsetNdc) ||
        leftPanel.horizontalOffsetNdc <= 0.0F ||
        rightPanel.horizontalOffsetNdc >= 0.0F ||
        !Near(
            leftPanel.horizontalOffsetNdc,
            -rightPanel.horizontalOffsetNdc) ||
        closerLeftPanel.horizontalOffsetNdc <=
            leftPanel.horizontalOffsetNdc ||
        ResolveToolMenuPanelTransform(
            FEARVR_EYE_COUNT, 0.064F, 1.570796327F).valid) {
        return Fail(
            "VR menu placement must be smaller, lowered, and converge per eye");
    }

    const char* gripRows[] = {
        "POSITION X                    -123.456",
        "POSITION Y                    -123.456",
        "POSITION Z                    -123.456",
        "ROTATION X                    -180.00 DEG",
        "ROTATION Y                    -180.00 DEG",
        "ROTATION Z                    -180.00 DEG",
        "ADJUSTMENT STEP               10.00",
        "RESET CURRENT GRIP",
        "ALIGN HAND + WEAPON TO CONTROLLER",
        "CANCEL ADVANCED FROZEN ALIGNMENT",
        "READY  APPLIED, SAVE FAILED  G PATH_UNAVAILABLE H WRITE_FAILED C OK"};
    std::size_t completeVertexCount = 0U;
    if (!FitsCompleteMenuOverlay(
            gripRows, sizeof(gripRows) / sizeof(gripRows[0]),
            completeVertexCount)) {
        std::fprintf(
            stderr,
            "complete grip menu exceeded its %zu-vertex budget at %zu\n",
            kToolMenuMaximumTriangleVertices, completeVertexCount);
        return 1;
    }
    const char* blockRows[] = {
        "POSE BLOCKING                 NOT SET",
        "CAPTURE CURRENT GUARD POSE",
        "POSITION TOLERANCE            1.00 M",
        "ANGLE TOLERANCE               90.0 DEG",
        "CUSTOM BLOCK WINDOW           OFF (RETAIL)",
        "BLOCK WINDOW                  2000 MS",
        "CLEAR SAVED GUARD POSE",
        "LIVE IN GUARD POSE            CAPTURE FIRST",
        "ERROR  POSITION 1.00 M   ANGLE 180.0 DEG",
        "ENTER POSE = AUTO BLOCK   NO TRIGGER REQUIRED",
        "GAMEPLAY ACTIVE YES   ACTIVATIONS 4294967295",
        "NATIVE BLOCK LIVE  RETAIL 10000 MS  APPLIED 2000 MS"};
    if (!FitsCompleteMenuOverlay(
            blockRows, sizeof(blockRows) / sizeof(blockRows[0]),
            completeVertexCount)) {
        std::fprintf(
            stderr,
            "complete block menu exceeded the bridge cap at %zu vertices\n",
            completeVertexCount);
        return 1;
    }
    const std::size_t blockVertexCount = completeVertexCount;

    const char* blockColliderRows[] = {
        "POSITION X                    -200.00 U",
        "POSITION Y                    -200.00 U",
        "POSITION Z                    -200.00 U",
        "PITCH X                       -180.0 DEG",
        "YAW Y                         -180.0 DEG",
        "ROLL Z                        -180.0 DEG",
        "LENGTH                        250.0 U",
        "RADIUS                        25.0 U",
        "DIRECTION                     REVERSED",
        "COPY CURRENT ATTACK COLLIDER",
        "SOURCE ATTACK COLLIDER - FOLLOWS UNTIL FIRST EDIT"};
    if (!FitsCompleteMenuOverlay(
            blockColliderRows,
            sizeof(blockColliderRows) / sizeof(blockColliderRows[0]),
            completeVertexCount)) {
        std::fprintf(
            stderr,
            "complete block-collider menu exceeded the bridge cap at %zu vertices\n",
            completeVertexCount);
        return 1;
    }
    const std::size_t blockColliderVertexCount =
        completeVertexCount;

    const char* debugRows[] = {
        "DRAW ATTACK COLLIDER          OFF",
        "DRAW BLOCK COLLIDER           OFF",
        "DRAW CONTROLLERS              OFF",
        "WEAPON PIPE_LEVER   INDEX 32",
        "ANIMATION PROPERTY            MELEEWEAPON",
        "RETAIL POSE FAMILY            ONE HANDED MELEE",
        "SWING FRESH  SPEED 10.00 M/S",
        "CALLBACKS 4294967295  DAMAGE ON  HITS 4294967295",
        "PROXY W ON  MODEL ON  ATTACK PREVIEW  BLOCK LIVE",
        "2-HAND OFF  SUPPORT FREE  HAND 10.00 M  ERR 10.00 M"};
    if (!FitsCompleteMenuOverlay(
            debugRows, sizeof(debugRows) / sizeof(debugRows[0]),
            completeVertexCount)) {
        std::fprintf(
            stderr,
            "complete debug menu exceeded the bridge cap at %zu vertices\n",
            completeVertexCount);
        return 1;
    }
    const std::size_t debugVertexCount = completeVertexCount;
    const char* magazineAuthorRows[] = {
        "PRIMITIVE                     MAG INSERT SOCKET",
        "COMPONENT                     SNAP ANGLE TOLERANCE",
        "VALUE                         -180.000 DEG",
        "MOVEMENT                      COARSE  1.00 CM / 5.00 DEG",
        "CAPTURE SOCKET FROM LEFT GRIP",
        "UNDO                          AVAILABLE  (32)",
        "RESET TO LAST LOADED RECORD",
        "MAG CONFIGURED  AUTO-SAVE  LOAD PATH_UNAVAILABLE  SAVE WRITE_FAILED"};
    if (!FitsCompleteMenuOverlay(
            magazineAuthorRows,
            sizeof(magazineAuthorRows) / sizeof(magazineAuthorRows[0]),
            completeVertexCount)) {
        std::fprintf(
            stderr,
            "complete magazine AUTHOR menu exceeded the bridge cap at %zu vertices\n",
            completeVertexCount);
        return 1;
    }
    const char* slideAuthorRows[] = {
        "PRIMITIVE                     SLIDE GRAB RAIL",
        "COMPONENT                     GRAB ROTATION X",
        "VALUE                         -180.0000 DEG",
        "MOVEMENT                      COARSE  1.00 CM / 5.00 DEG",
        "CAPTURE GRAB BOX + HAND POSE FROM LEFT GRIP",
        "UNDO                          AVAILABLE  (32)",
        "RESET TO LOADED SLIDE SETTINGS",
        "SAVE EXACT WEAPON RECORD       NOT CONFIGURED - CAPTURE FIRST",
        "NODE SLIDEJNT  INPUT EITHER  LOAD PATH_UNAVAILABLE  SAVE WRITE_FAILED",
        "RAIL 25.0000 CM  REAR 25.000  GRAB TRACKING WAIT  CONTROL DISABLED"};
    if (!FitsCompleteMenuOverlay(
            slideAuthorRows,
            sizeof(slideAuthorRows) / sizeof(slideAuthorRows[0]),
            completeVertexCount)) {
        std::fprintf(
            stderr,
            "complete slide AUTHOR menu exceeded the bridge cap at %zu vertices\n",
            completeVertexCount);
        return 1;
    }
    constexpr std::size_t kRejectedLegacyBridgeCap = 24576U;
    if (blockVertexCount <= kRejectedLegacyBridgeCap ||
        debugVertexCount <= kRejectedLegacyBridgeCap ||
        blockColliderVertexCount > kRejectedLegacyBridgeCap) {
        return Fail(
            "menu fixtures must reproduce the live Block/Debug cap rejection");
    }

    return 0;
}
