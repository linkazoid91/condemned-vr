#include <cmath>
#include <cstdio>
#include <limits>

#include "condemned_interaction_authoring.h"

namespace {

int Fail(const char* message) {
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

bool Near(float left, float right, float tolerance = 0.001F) {
    return std::fabs(left - right) <= tolerance;
}

} // namespace

int main() {
    using namespace condemnedvr;

    MagazineInsertionSocketSettings settings{};
    if (!MagazineInsertionSocketSettingsAreValid(settings) ||
        settings.configured) {
        return Fail("default socket must be bounded but unconfigured");
    }
    MagazineInsertionSocketSettings invalidSettings = settings;
    invalidSettings.halfExtentsUnits.x = 0.0F;
    if (MagazineInsertionSocketSettingsAreValid(invalidSettings)) {
        return Fail("out-of-range primitive dimensions must fail closed");
    }

    MagazineSocketEditorState editor{};
    SetMagazineSocketEditorValue(editor, settings, true);
    editor.current.configured = true;
    if (!AdjustMagazineSocketComponent(editor, 1) ||
        !Near(editor.current.positionUnits.x, 0.1F) ||
        editor.undoCount != 1U ||
        !UndoMagazineSocketEdit(editor) ||
        !Near(editor.current.positionUnits.x, 0.0F)) {
        return Fail("fine edits must be undoable in 0.1 cm steps");
    }
    editor.coarse = true;
    editor.component = MagazineSocketComponent::RotationY;
    if (!AdjustMagazineSocketComponent(editor, -1) ||
        !Near(editor.current.rotationDegrees.y, -5.0F) ||
        !ResetMagazineSocketEdit(editor) ||
        editor.current.configured) {
        return Fail("coarse rotation and reset must restore the loaded baseline");
    }
    MagazineSocketEditorState boundedUndo{};
    MagazineInsertionSocketSettings boundedSettings{};
    boundedSettings.configured = true;
    SetMagazineSocketEditorValue(
        boundedUndo, boundedSettings, true);
    for (int edit = 0; edit < 40; ++edit) {
        if (!AdjustMagazineSocketComponent(boundedUndo, 1)) {
            return Fail("bounded undo setup edits must succeed");
        }
    }
    if (boundedUndo.undoCount != kMagazineSocketUndoCapacity) {
        return Fail("undo history must remain within its fixed capacity");
    }
    for (std::size_t undo = 0U;
         undo < kMagazineSocketUndoCapacity; ++undo) {
        if (!UndoMagazineSocketEdit(boundedUndo)) {
            return Fail("every retained undo snapshot must be available");
        }
    }
    if (!Near(boundedUndo.current.positionUnits.x, 0.8F)) {
        return Fail("undo overflow must discard only the oldest snapshots");
    }

    settings.configured = true;
    settings.positionUnits = {};
    settings.rotationDegrees = {};
    settings.approachLengthUnits = 10.0F;
    settings.snapDistanceUnits = 2.0F;
    settings.snapAngleDegrees = 15.0F;
    const PhysicalMeleeRigidTransform cursorOnRail{
        {0.5F, 0.0F, -5.0F},
        {0.0F, 0.0F, 0.0F, 1.0F}};
    const MagazineSocketSnapPreview snapped =
        ResolveMagazineSocketSnapPreview(settings, cursorOnRail);
    if (!snapped.cursorValid || !snapped.snapped || snapped.seated ||
        !Near(snapped.railProgress, 0.5F) ||
        !Near(snapped.lateralErrorUnits, 0.5F) ||
        !Near(snapped.ghostModelLocal.positionUnits.x, 0.0F) ||
        !Near(snapped.ghostModelLocal.positionUnits.z, -5.0F)) {
        return Fail("a valid cursor must project onto the constrained rail");
    }
    const MagazineSocketSnapPreview outside =
        ResolveMagazineSocketSnapPreview(
            settings,
            {{3.0F, 0.0F, -5.0F},
             {0.0F, 0.0F, 0.0F, 1.0F}});
    if (outside.snapped || !Near(outside.lateralErrorUnits, 3.0F)) {
        return Fail("a cursor outside the tolerance must remain unsnapped");
    }
    const MagazineSocketSnapPreview seated =
        ResolveMagazineSocketSnapPreview(
            settings,
            {{0.0F, 0.0F, 0.0F},
             {0.0F, 0.0F, 0.0F, 1.0F}});
    if (!seated.snapped || !seated.seated ||
        !Near(seated.railProgress, 1.0F)) {
        return Fail(
            "the visual seat classification must occur at the endpoint");
    }
    const MagazineSocketSnapPreview wrongAngle =
        ResolveMagazineSocketSnapPreview(
            settings,
            {{0.0F, 0.0F, -5.0F},
             PhysicalMeleeLocalRotationFromDegrees(
                 {0.0F, 30.0F, 0.0F})});
    if (wrongAngle.snapped ||
        !Near(wrongAngle.angleErrorDegrees, 30.0F, 0.01F)) {
        return Fail("angular tolerance must gate a rail snap");
    }
    const MagazineSocketSnapPreview invalidCursor =
        ResolveMagazineSocketSnapPreview(
            settings,
            {{std::numeric_limits<float>::quiet_NaN(),
              0.0F, 0.0F},
             {0.0F, 0.0F, 0.0F, 1.0F}});
    if (invalidCursor.cursorValid) {
        return Fail("a non-finite cursor must not produce a ghost state");
    }

    PhysicalMeleeRigidTransform cursorLocal{};
    if (!ResolveMagazineSocketCursorModelLocal(
            {{10.0F, 20.0F, 30.0F},
             {0.0F, 0.0F, 0.0F, 1.0F}},
            {{11.0F, 22.0F, 33.0F},
             {0.0F, 0.0F, 0.0F, 1.0F}},
            cursorLocal) ||
        !Near(cursorLocal.positionUnits.x, 1.0F) ||
        !Near(cursorLocal.positionUnits.y, 2.0F) ||
        !Near(cursorLocal.positionUnits.z, 3.0F)) {
        return Fail("cursor capture must be expressed in model-local space");
    }

    const WeaponGripCalibrationGizmo gizmo =
        BuildMagazineSocketAuthoringGizmo(
            settings,
            {{0.0F, 0.0F, 50.0F},
             {0.0F, 0.0F, 0.0F, 1.0F}},
            &snapped);
    if (!gizmo.valid || gizmo.count != 28U ||
        gizmo.count > kWeaponGripCalibrationGizmoMaximumLines) {
        return Fail("socket, axes, rail and ghost must fit the line budget");
    }

    LiveMagazineSocketCommand command{};
    const char* const validCommand =
        "version=2 revision=7 base_revision=6 pid=1234 "
        "weapon_index=76 weapon_name=colt45_Unbreakable configured=1 "
        "pos=1,-2,3 rot=4,5,-6 half=1,2,3 "
        "rail=12 snap_distance=2.5 snap_angle=20";
    if (ParseLiveMagazineSocketCommand(validCommand, command) !=
            LiveMagazineSocketCommandParseResult::Ok ||
        command.revision != 7U || command.baseRevision != 6U ||
        command.processId != 1234U || command.weaponIndex != 76 ||
        !command.settings.configured ||
        !Near(command.settings.positionUnits.y, -2.0F) ||
        !Near(command.settings.halfExtentsUnits.z, 3.0F)) {
        return Fail("version-2 authoring commands must parse exact bounded values");
    }
    const LiveMagazineSocketCommandParseResult invalidNameResult =
        ParseLiveMagazineSocketCommand(
            "version=2 revision=7 base_revision=6 pid=1234 "
            "weapon_index=76 weapon_name=bad/name configured=1 "
            "pos=0,0,0 rot=0,0,0 half=1,1,1 "
            "rail=10 snap_distance=2 snap_angle=15",
            command);
    if (invalidNameResult !=
            LiveMagazineSocketCommandParseResult::InvalidValue ||
        command.revision != 7U || command.baseRevision != 6U ||
        command.processId != 1234U || command.weaponIndex != 76) {
        return Fail("invalid-value acknowledgements must retain command identity");
    }
    if (ParseLiveMagazineSocketCommand(
            "version=2 revision=7 base_revision=6 pid=1234 "
            "weapon_index=76 weapon_name=valid configured=1 "
            "pos=0,0,0 rot=0,0,0 half=1,1,1 "
            "rail=10 snap_distance=2 snap_angle=15 trailing",
            command) != LiveMagazineSocketCommandParseResult::Malformed) {
        return Fail("authoring commands must reject unsafe names and trailing data");
    }

    return 0;
}
