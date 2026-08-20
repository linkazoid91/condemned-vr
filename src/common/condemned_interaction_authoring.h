#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "condemned_calibration_gizmo.h"
#include "condemned_physical_melee.h"

namespace condemnedvr {

// Phase 1 deliberately owns only mod-authored geometry. It describes a
// magazine insertion socket in the held model's local frame; it is never a
// claim about a Retail object layout, bone, attachment, or native function.
// One LithTech unit is one centimetre at the project's verified 100 units/m.
struct MagazineInsertionSocketSettings {
    fearvr::TrackingVector positionUnits{};
    fearvr::TrackingVector rotationDegrees{};
    // These neutral one-centimetre half-extents are an authoring placeholder,
    // not inferred magazine dimensions. Nothing is drawn until the user
    // explicitly captures or loads a configured socket.
    fearvr::TrackingVector halfExtentsUnits{1.0F, 1.0F, 1.0F};
    float approachLengthUnits{10.0F};
    float snapDistanceUnits{2.0F};
    float snapAngleDegrees{15.0F};
    bool configured{false};
};

constexpr float kMagazineSocketMaximumPositionUnits = 300.0F;
constexpr float kMagazineSocketMinimumHalfExtentUnits = 0.25F;
constexpr float kMagazineSocketMaximumHalfExtentUnits = 50.0F;
constexpr float kMagazineSocketMinimumApproachLengthUnits = 1.0F;
constexpr float kMagazineSocketMaximumApproachLengthUnits = 150.0F;
constexpr float kMagazineSocketMinimumSnapDistanceUnits = 0.25F;
constexpr float kMagazineSocketMaximumSnapDistanceUnits = 30.0F;
constexpr float kMagazineSocketMinimumSnapAngleDegrees = 1.0F;
constexpr float kMagazineSocketMaximumSnapAngleDegrees = 90.0F;

inline bool MagazineInsertionSocketSettingsAreValid(
    const MagazineInsertionSocketSettings& settings) noexcept {
    const auto InRange = [](float value, float minimum, float maximum) {
        return std::isfinite(value) && value >= minimum && value <= maximum;
    };
    return fearvr::IsFinite(settings.positionUnits) &&
        fearvr::IsFinite(settings.rotationDegrees) &&
        fearvr::IsFinite(settings.halfExtentsUnits) &&
        std::fabs(settings.positionUnits.x) <=
            kMagazineSocketMaximumPositionUnits &&
        std::fabs(settings.positionUnits.y) <=
            kMagazineSocketMaximumPositionUnits &&
        std::fabs(settings.positionUnits.z) <=
            kMagazineSocketMaximumPositionUnits &&
        settings.rotationDegrees.x >= -180.0F &&
        settings.rotationDegrees.x <= 180.0F &&
        settings.rotationDegrees.y >= -180.0F &&
        settings.rotationDegrees.y <= 180.0F &&
        settings.rotationDegrees.z >= -180.0F &&
        settings.rotationDegrees.z <= 180.0F &&
        InRange(settings.halfExtentsUnits.x,
                kMagazineSocketMinimumHalfExtentUnits,
                kMagazineSocketMaximumHalfExtentUnits) &&
        InRange(settings.halfExtentsUnits.y,
                kMagazineSocketMinimumHalfExtentUnits,
                kMagazineSocketMaximumHalfExtentUnits) &&
        InRange(settings.halfExtentsUnits.z,
                kMagazineSocketMinimumHalfExtentUnits,
                kMagazineSocketMaximumHalfExtentUnits) &&
        InRange(settings.approachLengthUnits,
                kMagazineSocketMinimumApproachLengthUnits,
                kMagazineSocketMaximumApproachLengthUnits) &&
        InRange(settings.snapDistanceUnits,
                kMagazineSocketMinimumSnapDistanceUnits,
                kMagazineSocketMaximumSnapDistanceUnits) &&
        InRange(settings.snapAngleDegrees,
                kMagazineSocketMinimumSnapAngleDegrees,
                kMagazineSocketMaximumSnapAngleDegrees);
}

inline bool MagazineInsertionSocketSettingsEqual(
    const MagazineInsertionSocketSettings& left,
    const MagazineInsertionSocketSettings& right) noexcept {
    return left.positionUnits.x == right.positionUnits.x &&
        left.positionUnits.y == right.positionUnits.y &&
        left.positionUnits.z == right.positionUnits.z &&
        left.rotationDegrees.x == right.rotationDegrees.x &&
        left.rotationDegrees.y == right.rotationDegrees.y &&
        left.rotationDegrees.z == right.rotationDegrees.z &&
        left.halfExtentsUnits.x == right.halfExtentsUnits.x &&
        left.halfExtentsUnits.y == right.halfExtentsUnits.y &&
        left.halfExtentsUnits.z == right.halfExtentsUnits.z &&
        left.approachLengthUnits == right.approachLengthUnits &&
        left.snapDistanceUnits == right.snapDistanceUnits &&
        left.snapAngleDegrees == right.snapAngleDegrees &&
        left.configured == right.configured;
}

enum class MagazineSocketComponent : std::uint8_t {
    PositionX,
    PositionY,
    PositionZ,
    RotationX,
    RotationY,
    RotationZ,
    HalfExtentX,
    HalfExtentY,
    HalfExtentZ,
    ApproachLength,
    SnapDistance,
    SnapAngle,
    Count
};

inline const char* MagazineSocketComponentName(
    MagazineSocketComponent component) noexcept {
    switch (component) {
    case MagazineSocketComponent::PositionX: return "POSITION X";
    case MagazineSocketComponent::PositionY: return "POSITION Y";
    case MagazineSocketComponent::PositionZ: return "POSITION Z";
    case MagazineSocketComponent::RotationX: return "ROTATION X";
    case MagazineSocketComponent::RotationY: return "ROTATION Y";
    case MagazineSocketComponent::RotationZ: return "ROTATION Z";
    case MagazineSocketComponent::HalfExtentX: return "BOX HALF X";
    case MagazineSocketComponent::HalfExtentY: return "BOX HALF Y";
    case MagazineSocketComponent::HalfExtentZ: return "BOX HALF Z";
    case MagazineSocketComponent::ApproachLength: return "RAIL LENGTH";
    case MagazineSocketComponent::SnapDistance: return "SNAP DISTANCE";
    case MagazineSocketComponent::SnapAngle: return "SNAP ANGLE";
    default: return "INVALID";
    }
}

inline bool MagazineSocketComponentUsesDegrees(
    MagazineSocketComponent component) noexcept {
    return component == MagazineSocketComponent::RotationX ||
        component == MagazineSocketComponent::RotationY ||
        component == MagazineSocketComponent::RotationZ ||
        component == MagazineSocketComponent::SnapAngle;
}

inline float MagazineSocketComponentValue(
    const MagazineInsertionSocketSettings& settings,
    MagazineSocketComponent component) noexcept {
    switch (component) {
    case MagazineSocketComponent::PositionX: return settings.positionUnits.x;
    case MagazineSocketComponent::PositionY: return settings.positionUnits.y;
    case MagazineSocketComponent::PositionZ: return settings.positionUnits.z;
    case MagazineSocketComponent::RotationX: return settings.rotationDegrees.x;
    case MagazineSocketComponent::RotationY: return settings.rotationDegrees.y;
    case MagazineSocketComponent::RotationZ: return settings.rotationDegrees.z;
    case MagazineSocketComponent::HalfExtentX: return settings.halfExtentsUnits.x;
    case MagazineSocketComponent::HalfExtentY: return settings.halfExtentsUnits.y;
    case MagazineSocketComponent::HalfExtentZ: return settings.halfExtentsUnits.z;
    case MagazineSocketComponent::ApproachLength: return settings.approachLengthUnits;
    case MagazineSocketComponent::SnapDistance: return settings.snapDistanceUnits;
    case MagazineSocketComponent::SnapAngle: return settings.snapAngleDegrees;
    default: return 0.0F;
    }
}

constexpr std::size_t kMagazineSocketUndoCapacity = 32U;

struct MagazineSocketEditorState {
    MagazineInsertionSocketSettings current{};
    MagazineInsertionSocketSettings baseline{};
    std::array<MagazineInsertionSocketSettings,
               kMagazineSocketUndoCapacity> undo{};
    std::size_t undoCount{0U};
    MagazineSocketComponent component{
        MagazineSocketComponent::PositionX};
    bool baselineAvailable{false};
    bool coarse{false};
};

inline void SetMagazineSocketEditorValue(
    MagazineSocketEditorState& editor,
    const MagazineInsertionSocketSettings& settings,
    bool makeBaseline) noexcept {
    editor.current = settings;
    editor.undoCount = 0U;
    if (makeBaseline) {
        editor.baseline = settings;
        editor.baselineAvailable = true;
    }
}

inline void PushMagazineSocketUndo(
    MagazineSocketEditorState& editor) noexcept {
    if (editor.undoCount == editor.undo.size()) {
        for (std::size_t index = 1U; index < editor.undo.size(); ++index) {
            editor.undo[index - 1U] = editor.undo[index];
        }
        --editor.undoCount;
    }
    editor.undo[editor.undoCount++] = editor.current;
}

inline bool UndoMagazineSocketEdit(
    MagazineSocketEditorState& editor) noexcept {
    if (editor.undoCount == 0U) {
        return false;
    }
    editor.current = editor.undo[--editor.undoCount];
    return true;
}

inline bool ResetMagazineSocketEdit(
    MagazineSocketEditorState& editor) noexcept {
    if (!editor.baselineAvailable ||
        MagazineInsertionSocketSettingsEqual(
            editor.current, editor.baseline)) {
        return false;
    }
    PushMagazineSocketUndo(editor);
    editor.current = editor.baseline;
    return true;
}

inline void CycleMagazineSocketComponent(
    MagazineSocketEditorState& editor,
    int delta) noexcept {
    if (delta == 0) {
        return;
    }
    constexpr int count = static_cast<int>(MagazineSocketComponent::Count);
    int index = static_cast<int>(editor.component);
    index = (index + (delta > 0 ? 1 : -1) + count) % count;
    editor.component = static_cast<MagazineSocketComponent>(index);
}

inline bool AdjustMagazineSocketComponent(
    MagazineSocketEditorState& editor,
    int delta) noexcept {
    if (delta == 0 ||
        !MagazineInsertionSocketSettingsAreValid(editor.current)) {
        return false;
    }
    MagazineInsertionSocketSettings next = editor.current;
    const float amount = static_cast<float>(delta) *
        (MagazineSocketComponentUsesDegrees(editor.component)
             ? (editor.coarse ? 5.0F : 0.25F)
             : (editor.coarse ? 1.0F : 0.1F));
    switch (editor.component) {
    case MagazineSocketComponent::PositionX: next.positionUnits.x += amount; break;
    case MagazineSocketComponent::PositionY: next.positionUnits.y += amount; break;
    case MagazineSocketComponent::PositionZ: next.positionUnits.z += amount; break;
    case MagazineSocketComponent::RotationX:
        next.rotationDegrees.x = PhysicalMeleeWrapDegrees(
            next.rotationDegrees.x + amount); break;
    case MagazineSocketComponent::RotationY:
        next.rotationDegrees.y = PhysicalMeleeWrapDegrees(
            next.rotationDegrees.y + amount); break;
    case MagazineSocketComponent::RotationZ:
        next.rotationDegrees.z = PhysicalMeleeWrapDegrees(
            next.rotationDegrees.z + amount); break;
    case MagazineSocketComponent::HalfExtentX:
        next.halfExtentsUnits.x = std::clamp(
            next.halfExtentsUnits.x + amount,
            kMagazineSocketMinimumHalfExtentUnits,
            kMagazineSocketMaximumHalfExtentUnits); break;
    case MagazineSocketComponent::HalfExtentY:
        next.halfExtentsUnits.y = std::clamp(
            next.halfExtentsUnits.y + amount,
            kMagazineSocketMinimumHalfExtentUnits,
            kMagazineSocketMaximumHalfExtentUnits); break;
    case MagazineSocketComponent::HalfExtentZ:
        next.halfExtentsUnits.z = std::clamp(
            next.halfExtentsUnits.z + amount,
            kMagazineSocketMinimumHalfExtentUnits,
            kMagazineSocketMaximumHalfExtentUnits); break;
    case MagazineSocketComponent::ApproachLength:
        next.approachLengthUnits = std::clamp(
            next.approachLengthUnits + amount,
            kMagazineSocketMinimumApproachLengthUnits,
            kMagazineSocketMaximumApproachLengthUnits); break;
    case MagazineSocketComponent::SnapDistance:
        next.snapDistanceUnits = std::clamp(
            next.snapDistanceUnits + amount,
            kMagazineSocketMinimumSnapDistanceUnits,
            kMagazineSocketMaximumSnapDistanceUnits); break;
    case MagazineSocketComponent::SnapAngle:
        next.snapAngleDegrees = std::clamp(
            next.snapAngleDegrees + amount,
            kMagazineSocketMinimumSnapAngleDegrees,
            kMagazineSocketMaximumSnapAngleDegrees); break;
    default:
        return false;
    }
    if (!MagazineInsertionSocketSettingsAreValid(next) ||
        MagazineInsertionSocketSettingsEqual(next, editor.current)) {
        return false;
    }
    PushMagazineSocketUndo(editor);
    editor.current = next;
    return true;
}

struct MagazineSocketSnapPreview {
    PhysicalMeleeRigidTransform ghostModelLocal{};
    float railProgress{0.0F};
    float lateralErrorUnits{0.0F};
    float angleErrorDegrees{0.0F};
    bool cursorValid{false};
    bool snapped{false};
    bool seated{false};
};

inline MagazineSocketSnapPreview ResolveMagazineSocketSnapPreview(
    const MagazineInsertionSocketSettings& settings,
    const PhysicalMeleeRigidTransform& cursorModelLocal) noexcept {
    MagazineSocketSnapPreview result{};
    if (!settings.configured ||
        !MagazineInsertionSocketSettingsAreValid(settings) ||
        !PhysicalMeleeRigidTransformIsValid(cursorModelLocal)) {
        return result;
    }
    result.cursorValid = true;
    const fearvr::TrackingQuaternion socketRotation =
        PhysicalMeleeLocalRotationFromDegrees(settings.rotationDegrees);
    const fearvr::TrackingVector railAxis = fearvr::Rotate(
        socketRotation, {0.0F, 0.0F, 1.0F});
    const fearvr::TrackingVector railStart{
        settings.positionUnits.x -
            railAxis.x * settings.approachLengthUnits,
        settings.positionUnits.y -
            railAxis.y * settings.approachLengthUnits,
        settings.positionUnits.z -
            railAxis.z * settings.approachLengthUnits};
    const fearvr::TrackingVector fromStart{
        cursorModelLocal.positionUnits.x - railStart.x,
        cursorModelLocal.positionUnits.y - railStart.y,
        cursorModelLocal.positionUnits.z - railStart.z};
    const float along = fromStart.x * railAxis.x +
        fromStart.y * railAxis.y + fromStart.z * railAxis.z;
    const float clampedAlong = std::clamp(
        along, 0.0F, settings.approachLengthUnits);
    const fearvr::TrackingVector railPoint{
        railStart.x + railAxis.x * clampedAlong,
        railStart.y + railAxis.y * clampedAlong,
        railStart.z + railAxis.z * clampedAlong};
    const fearvr::TrackingVector lateral{
        cursorModelLocal.positionUnits.x - railPoint.x,
        cursorModelLocal.positionUnits.y - railPoint.y,
        cursorModelLocal.positionUnits.z - railPoint.z};
    result.lateralErrorUnits = std::sqrt(
        lateral.x * lateral.x + lateral.y * lateral.y +
        lateral.z * lateral.z);
    result.railProgress = clampedAlong / settings.approachLengthUnits;
    const float quaternionDot = std::clamp(std::fabs(
        cursorModelLocal.rotation.x * socketRotation.x +
        cursorModelLocal.rotation.y * socketRotation.y +
        cursorModelLocal.rotation.z * socketRotation.z +
        cursorModelLocal.rotation.w * socketRotation.w), 0.0F, 1.0F);
    constexpr float kRadiansToDegrees = 57.29577951308232F;
    result.angleErrorDegrees =
        2.0F * std::acos(quaternionDot) * kRadiansToDegrees;
    const bool alongRail = along >= -settings.snapDistanceUnits &&
        along <= settings.approachLengthUnits + settings.snapDistanceUnits;
    result.snapped = alongRail &&
        result.lateralErrorUnits <= settings.snapDistanceUnits &&
        result.angleErrorDegrees <= settings.snapAngleDegrees;
    result.ghostModelLocal = result.snapped
        ? PhysicalMeleeRigidTransform{railPoint, socketRotation}
        : cursorModelLocal;
    const fearvr::TrackingVector seatError{
        cursorModelLocal.positionUnits.x - settings.positionUnits.x,
        cursorModelLocal.positionUnits.y - settings.positionUnits.y,
        cursorModelLocal.positionUnits.z - settings.positionUnits.z};
    result.seated = result.snapped &&
        std::sqrt(seatError.x * seatError.x +
                  seatError.y * seatError.y +
                  seatError.z * seatError.z) <=
            settings.snapDistanceUnits;
    return result;
}

inline bool ResolveMagazineSocketCursorModelLocal(
    const PhysicalMeleeRigidTransform& modelWorld,
    const PhysicalMeleeRigidTransform& cursorWorld,
    PhysicalMeleeRigidTransform& cursorModelLocal) noexcept {
    PhysicalMeleeRigidTransform worldToModel{};
    return InvertPhysicalMeleeRigidTransform(modelWorld, worldToModel) &&
        ComposePhysicalMeleeRigidTransforms(
            worldToModel, cursorWorld, cursorModelLocal);
}

inline void AddMagazineSocketBox(
    WeaponGripCalibrationGizmo& gizmo,
    const PhysicalMeleeRigidTransform& boxWorld,
    const fearvr::TrackingVector& halfExtents,
    std::uint32_t color) noexcept {
    const fearvr::TrackingVector local[8]{
        {-halfExtents.x, -halfExtents.y, -halfExtents.z},
        { halfExtents.x, -halfExtents.y, -halfExtents.z},
        { halfExtents.x,  halfExtents.y, -halfExtents.z},
        {-halfExtents.x,  halfExtents.y, -halfExtents.z},
        {-halfExtents.x, -halfExtents.y,  halfExtents.z},
        { halfExtents.x, -halfExtents.y,  halfExtents.z},
        { halfExtents.x,  halfExtents.y,  halfExtents.z},
        {-halfExtents.x,  halfExtents.y,  halfExtents.z}};
    constexpr std::size_t edges[12][2]{
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},
        {6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (const auto& edge : edges) {
        AddWeaponGripCalibrationGizmoLine(
            gizmo,
            CalibrationGizmoTransformPoint(
                boxWorld.positionUnits, boxWorld.rotation,
                local[edge[0]]),
            CalibrationGizmoTransformPoint(
                boxWorld.positionUnits, boxWorld.rotation,
                local[edge[1]]), color);
    }
}

inline WeaponGripCalibrationGizmo BuildMagazineSocketAuthoringGizmo(
    const MagazineInsertionSocketSettings& settings,
    const PhysicalMeleeRigidTransform& modelWorld,
    const MagazineSocketSnapPreview* preview = nullptr) noexcept {
    WeaponGripCalibrationGizmo gizmo{};
    if (!settings.configured ||
        !MagazineInsertionSocketSettingsAreValid(settings) ||
        !PhysicalMeleeRigidTransformIsValid(modelWorld)) {
        return gizmo;
    }
    const PhysicalMeleeRigidTransform socketModelLocal{
        settings.positionUnits,
        PhysicalMeleeLocalRotationFromDegrees(settings.rotationDegrees)};
    PhysicalMeleeRigidTransform socketWorld{};
    if (!ComposePhysicalMeleeRigidTransforms(
            modelWorld, socketModelLocal, socketWorld)) {
        return gizmo;
    }
    constexpr std::uint32_t kSocketColor = 0xFFE0F8FFU;
    AddMagazineSocketBox(
        gizmo, socketWorld, settings.halfExtentsUnits, kSocketColor);
    const fearvr::TrackingVector axisEnds[3]{
        {5.0F, 0.0F, 0.0F},
        {0.0F, 5.0F, 0.0F},
        {0.0F, 0.0F, 5.0F}};
    constexpr std::uint32_t axisColors[3]{
        0xFFFF4040U, 0xFF40FF60U, 0xFF4080FFU};
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        AddWeaponGripCalibrationGizmoLine(
            gizmo, socketWorld.positionUnits,
            CalibrationGizmoTransformPoint(
                socketWorld.positionUnits, socketWorld.rotation,
                axisEnds[axis]), axisColors[axis]);
    }
    const fearvr::TrackingVector railStart =
        CalibrationGizmoTransformPoint(
            socketWorld.positionUnits, socketWorld.rotation,
            {0.0F, 0.0F, -settings.approachLengthUnits});
    AddWeaponGripCalibrationGizmoLine(
        gizmo, railStart, socketWorld.positionUnits, 0xFFFFFF40U);

    if (preview != nullptr && preview->cursorValid &&
        PhysicalMeleeRigidTransformIsValid(
            preview->ghostModelLocal)) {
        PhysicalMeleeRigidTransform ghostWorld{};
        if (ComposePhysicalMeleeRigidTransforms(
                modelWorld, preview->ghostModelLocal, ghostWorld)) {
            AddMagazineSocketBox(
                gizmo, ghostWorld, settings.halfExtentsUnits,
                preview->seated ? 0xFF50FF80U
                    : preview->snapped ? 0xFF70E8FFU
                                       : 0xFFFFB040U);
        }
    }
    gizmo.valid = gizmo.count != 0U;
    return gizmo;
}

constexpr std::uint32_t kLiveMagazineSocketCommandVersion = 2U;
constexpr std::size_t kMagazineSocketWeaponNameCapacity = 64U;

struct LiveMagazineSocketCommand {
    std::uint32_t version{0U};
    std::uint64_t revision{0U};
    std::uint64_t baseRevision{0U};
    std::uint32_t processId{0U};
    std::int32_t weaponIndex{-1};
    char weaponName[kMagazineSocketWeaponNameCapacity]{};
    MagazineInsertionSocketSettings settings{};
};

enum class LiveMagazineSocketCommandParseResult : std::uint8_t {
    Ok,
    Missing,
    Malformed,
    InvalidValue
};

inline const char* LiveMagazineSocketCommandParseResultName(
    LiveMagazineSocketCommandParseResult result) noexcept {
    switch (result) {
    case LiveMagazineSocketCommandParseResult::Ok: return "ok";
    case LiveMagazineSocketCommandParseResult::Missing: return "missing";
    case LiveMagazineSocketCommandParseResult::Malformed: return "malformed";
    case LiveMagazineSocketCommandParseResult::InvalidValue: return "invalid_value";
    default: return "unknown";
    }
}

inline bool MagazineSocketWeaponNameIsValid(const char* name) noexcept {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    for (std::size_t index = 0U;
         index < kMagazineSocketWeaponNameCapacity; ++index) {
        const unsigned char value =
            static_cast<unsigned char>(name[index]);
        if (value == 0U) {
            return true;
        }
        const bool valid =
            (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') ||
            value == '_' || value == '-' || value == '.';
        if (!valid) {
            return false;
        }
    }
    return false;
}

inline LiveMagazineSocketCommandParseResult
ParseLiveMagazineSocketCommand(
    const char* text,
    LiveMagazineSocketCommand& command) noexcept {
    command = {};
    if (text == nullptr || text[0] == '\0') {
        return LiveMagazineSocketCommandParseResult::Missing;
    }
    unsigned int version = 0U;
    unsigned long long revision = 0ULL;
    unsigned long long baseRevision = 0ULL;
    unsigned int processId = 0U;
    long weaponIndex = -1L;
    unsigned int configured = 0U;
    char weaponName[kMagazineSocketWeaponNameCapacity]{};
    int consumed = 0;
    MagazineInsertionSocketSettings settings{};
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const int parsed = std::sscanf(
        text,
        "version=%u revision=%llu base_revision=%llu pid=%u "
        "weapon_index=%ld weapon_name=%63s configured=%u "
        "pos=%f,%f,%f rot=%f,%f,%f half=%f,%f,%f "
        "rail=%f snap_distance=%f snap_angle=%f %n",
        &version, &revision, &baseRevision, &processId,
        &weaponIndex, weaponName, &configured,
        &settings.positionUnits.x, &settings.positionUnits.y,
        &settings.positionUnits.z, &settings.rotationDegrees.x,
        &settings.rotationDegrees.y, &settings.rotationDegrees.z,
        &settings.halfExtentsUnits.x, &settings.halfExtentsUnits.y,
        &settings.halfExtentsUnits.z, &settings.approachLengthUnits,
        &settings.snapDistanceUnits, &settings.snapAngleDegrees,
        &consumed);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (parsed != 19 || consumed <= 0) {
        return LiveMagazineSocketCommandParseResult::Malformed;
    }
    for (const char* tail = text + consumed; *tail != '\0'; ++tail) {
        if (*tail != ' ' && *tail != '\t' &&
            *tail != '\r' && *tail != '\n') {
            return LiveMagazineSocketCommandParseResult::Malformed;
        }
    }
    settings.configured = configured != 0U;
    command.version = version;
    command.revision = static_cast<std::uint64_t>(revision);
    command.baseRevision = static_cast<std::uint64_t>(baseRevision);
    command.processId = processId;
    command.weaponIndex = weaponIndex >= 0L &&
            weaponIndex <= static_cast<long>(
                std::numeric_limits<std::int32_t>::max())
        ? static_cast<std::int32_t>(weaponIndex) : -1;
    std::memcpy(command.weaponName, weaponName,
                sizeof(command.weaponName));
    command.settings = settings;
    if (version != kLiveMagazineSocketCommandVersion ||
        revision == 0ULL || processId == 0U || weaponIndex < 0L ||
        weaponIndex > static_cast<long>(
            std::numeric_limits<std::int32_t>::max()) ||
        configured > 1U ||
        !MagazineSocketWeaponNameIsValid(weaponName) ||
        !MagazineInsertionSocketSettingsAreValid(settings)) {
        return LiveMagazineSocketCommandParseResult::InvalidValue;
    }
    return LiveMagazineSocketCommandParseResult::Ok;
}

} // namespace condemnedvr
