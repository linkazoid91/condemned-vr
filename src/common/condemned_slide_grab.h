#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "condemned_interaction_authoring.h"

namespace condemnedvr {

constexpr std::int32_t kColtSlideGrabWeaponIndex = 76;
constexpr char kColtSlideGrabWeaponName[] = "colt45_Unbreakable";
constexpr char kColtSlideNodeName[] = "SlideJnt";
constexpr char kColtSlideParentName[] = "anim_cult45";
constexpr std::size_t kSlideNodeNameCapacity = 32U;

enum class InteractionAuthoringPrimitive : std::uint8_t {
    MagazineInsertSocket,
    SlideGrabRail
};

inline void CycleInteractionAuthoringPrimitive(
    InteractionAuthoringPrimitive& primitive,
    int delta) noexcept {
    if (delta == 0) return;
    primitive = primitive ==
            InteractionAuthoringPrimitive::MagazineInsertSocket
        ? InteractionAuthoringPrimitive::SlideGrabRail
        : InteractionAuthoringPrimitive::MagazineInsertSocket;
}

enum class SlideGrabActivationInput : std::uint8_t {
    Grip,
    Trigger,
    Either
};

inline const char* SlideGrabActivationInputName(
    SlideGrabActivationInput input) noexcept {
    switch (input) {
    case SlideGrabActivationInput::Grip: return "GRIP";
    case SlideGrabActivationInput::Trigger: return "TRIGGER";
    case SlideGrabActivationInput::Either: return "EITHER";
    default: return "INVALID";
    }
}

struct SlideGrabRailSettings {
    char nodeName[kSlideNodeNameCapacity]{};
    PhysicalMeleeRigidTransform grabVolumeModelLocal{};
    fearvr::TrackingVector halfExtentsUnits{2.0F, 2.0F, 3.0F};
    PhysicalMeleeRigidTransform handPoseModelLocal{};
    fearvr::TrackingVector closedPositionUnits{};
    fearvr::TrackingVector closedToRearAxis{};
    float maximumTravelUnits{0.0F};
    float rearThresholdUnits{0.0F};
    SlideGrabActivationInput activationInput{
        SlideGrabActivationInput::Either};
    bool configured{false};
};

inline SlideGrabRailSettings ColtSlideGrabSeedSettings() noexcept {
    SlideGrabRailSettings settings{};
    std::memcpy(
        settings.nodeName, kColtSlideNodeName,
        sizeof(kColtSlideNodeName));
    settings.closedPositionUnits = {14.1689F, 2.8062F, -8.7261F};
    settings.closedToRearAxis = {-0.989379F, 0.007748F, 0.145151F};
    settings.maximumTravelUnits = 3.8651F;
    settings.rearThresholdUnits = 3.50F;
    // The animation pivot is deliberately not copied into either authored
    // contact pose. Capture must supply both from the physical off hand.
    settings.grabVolumeModelLocal = {};
    settings.handPoseModelLocal = {};
    settings.configured = false;
    return settings;
}

inline bool SlideNodeNameIsValid(const char* name) noexcept {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    for (std::size_t index = 0U; index < kSlideNodeNameCapacity; ++index) {
        const unsigned char value = static_cast<unsigned char>(name[index]);
        if (value == 0U) {
            return true;
        }
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '_')) {
            return false;
        }
    }
    return false;
}

inline bool NormalizeSlideAxis(
    const fearvr::TrackingVector& axis,
    fearvr::TrackingVector& normalized) noexcept {
    const float length = PhysicalMeleeLength(axis);
    if (!fearvr::IsFinite(axis) || !std::isfinite(length) ||
        length < 0.5F || length > 1.5F) {
        normalized = {};
        return false;
    }
    normalized = PhysicalMeleeScale(axis, 1.0F / length);
    return fearvr::IsFinite(normalized);
}

inline bool SlideGrabRailSettingsAreValid(
    const SlideGrabRailSettings& settings) noexcept {
    fearvr::TrackingVector normalizedAxis{};
    const float authoredAxisLength =
        PhysicalMeleeLength(settings.closedToRearAxis);
    const bool activationValid =
        settings.activationInput == SlideGrabActivationInput::Grip ||
        settings.activationInput == SlideGrabActivationInput::Trigger ||
        settings.activationInput == SlideGrabActivationInput::Either;
    return SlideNodeNameIsValid(settings.nodeName) &&
        PhysicalMeleeRigidTransformIsValid(
            settings.grabVolumeModelLocal) &&
        PhysicalMeleeRigidTransformIsValid(
            settings.handPoseModelLocal) &&
        fearvr::IsFinite(settings.halfExtentsUnits) &&
        settings.halfExtentsUnits.x >= 0.25F &&
        settings.halfExtentsUnits.x <= 25.0F &&
        settings.halfExtentsUnits.y >= 0.25F &&
        settings.halfExtentsUnits.y <= 25.0F &&
        settings.halfExtentsUnits.z >= 0.25F &&
        settings.halfExtentsUnits.z <= 25.0F &&
        fearvr::IsFinite(settings.closedPositionUnits) &&
        PhysicalMeleeLength(settings.closedPositionUnits) <= 300.0F &&
        NormalizeSlideAxis(settings.closedToRearAxis, normalizedAxis) &&
        std::isfinite(authoredAxisLength) &&
        std::fabs(authoredAxisLength - 1.0F) <= 0.001F &&
        std::isfinite(settings.maximumTravelUnits) &&
        settings.maximumTravelUnits >= 0.25F &&
        settings.maximumTravelUnits <= 25.0F &&
        std::isfinite(settings.rearThresholdUnits) &&
        settings.rearThresholdUnits >= 0.0F &&
        settings.rearThresholdUnits <= settings.maximumTravelUnits &&
        activationValid;
}

inline bool SlideGrabRailSettingsEqual(
    const SlideGrabRailSettings& left,
    const SlideGrabRailSettings& right) noexcept {
    return std::strcmp(left.nodeName, right.nodeName) == 0 &&
        std::memcmp(&left.grabVolumeModelLocal,
                    &right.grabVolumeModelLocal,
                    sizeof(left.grabVolumeModelLocal)) == 0 &&
        std::memcmp(&left.halfExtentsUnits, &right.halfExtentsUnits,
                    sizeof(left.halfExtentsUnits)) == 0 &&
        std::memcmp(&left.handPoseModelLocal,
                    &right.handPoseModelLocal,
                    sizeof(left.handPoseModelLocal)) == 0 &&
        std::memcmp(&left.closedPositionUnits,
                    &right.closedPositionUnits,
                    sizeof(left.closedPositionUnits)) == 0 &&
        std::memcmp(&left.closedToRearAxis,
                    &right.closedToRearAxis,
                    sizeof(left.closedToRearAxis)) == 0 &&
        left.maximumTravelUnits == right.maximumTravelUnits &&
        left.rearThresholdUnits == right.rearThresholdUnits &&
        left.activationInput == right.activationInput &&
        left.configured == right.configured;
}

inline fearvr::TrackingVector SlideRearEndpoint(
    const SlideGrabRailSettings& settings) noexcept {
    return PhysicalMeleeAdd(
        settings.closedPositionUnits,
        PhysicalMeleeScale(
            settings.closedToRearAxis, settings.maximumTravelUnits));
}

inline bool SlideGrabVolumeContains(
    const SlideGrabRailSettings& settings,
    const fearvr::TrackingVector& controllerModelLocal) noexcept {
    if (!settings.configured ||
        !SlideGrabRailSettingsAreValid(settings) ||
        !fearvr::IsFinite(controllerModelLocal)) {
        return false;
    }
    const fearvr::TrackingVector delta = PhysicalMeleeSubtract(
        controllerModelLocal,
        settings.grabVolumeModelLocal.positionUnits);
    const fearvr::TrackingVector local = fearvr::Rotate(
        fearvr::Conjugate(fearvr::Normalize(
            settings.grabVolumeModelLocal.rotation)), delta);
    constexpr float kOverlapEpsilon = 0.001F;
    return fearvr::IsFinite(local) &&
        std::fabs(local.x) <= settings.halfExtentsUnits.x + kOverlapEpsilon &&
        std::fabs(local.y) <= settings.halfExtentsUnits.y + kOverlapEpsilon &&
        std::fabs(local.z) <= settings.halfExtentsUnits.z + kOverlapEpsilon;
}

struct SlideRailProjection {
    fearvr::TrackingVector slidePositionModelLocal{};
    PhysicalMeleeRigidTransform handTargetModelLocal{};
    float projectedTravelUnits{0.0F};
    float clampedTravelUnits{0.0F};
    bool rearReached{false};
    bool valid{false};
};

inline SlideRailProjection ProjectSlideControllerDisplacement(
    const SlideGrabRailSettings& settings,
    const fearvr::TrackingVector& attachControllerModelLocal,
    const fearvr::TrackingVector& controllerModelLocal) noexcept {
    SlideRailProjection result{};
    if (!settings.configured ||
        !SlideGrabRailSettingsAreValid(settings) ||
        !fearvr::IsFinite(attachControllerModelLocal) ||
        !fearvr::IsFinite(controllerModelLocal)) {
        return result;
    }
    const fearvr::TrackingVector displacement = PhysicalMeleeSubtract(
        controllerModelLocal, attachControllerModelLocal);
    result.projectedTravelUnits = PhysicalMeleeDot(
        displacement, settings.closedToRearAxis);
    result.clampedTravelUnits = std::clamp(
        result.projectedTravelUnits, 0.0F, settings.maximumTravelUnits);
    const fearvr::TrackingVector railOffset = PhysicalMeleeScale(
        settings.closedToRearAxis, result.clampedTravelUnits);
    result.slidePositionModelLocal = PhysicalMeleeAdd(
        settings.closedPositionUnits, railOffset);
    result.handTargetModelLocal = settings.handPoseModelLocal;
    result.handTargetModelLocal.positionUnits = PhysicalMeleeAdd(
        result.handTargetModelLocal.positionUnits, railOffset);
    result.rearReached =
        result.clampedTravelUnits >= settings.rearThresholdUnits;
    result.valid = fearvr::IsFinite(result.slidePositionModelLocal) &&
        PhysicalMeleeRigidTransformIsValid(result.handTargetModelLocal);
    return result;
}

enum class SlideGrabComponent : std::uint8_t {
    GrabPositionX, GrabPositionY, GrabPositionZ,
    GrabRotationX, GrabRotationY, GrabRotationZ,
    HalfExtentX, HalfExtentY, HalfExtentZ,
    HandPositionX, HandPositionY, HandPositionZ,
    HandRotationX, HandRotationY, HandRotationZ,
    ClosedPositionX, ClosedPositionY, ClosedPositionZ,
    AxisX, AxisY, AxisZ, MaximumTravel, RearThreshold, Activation, Count
};

inline const char* SlideGrabComponentName(SlideGrabComponent component) noexcept {
    static constexpr const char* names[] = {
        "GRAB POSITION X", "GRAB POSITION Y", "GRAB POSITION Z",
        "GRAB ROTATION X", "GRAB ROTATION Y", "GRAB ROTATION Z",
        "BOX HALF X", "BOX HALF Y", "BOX HALF Z",
        "HAND POSITION X", "HAND POSITION Y", "HAND POSITION Z",
        "HAND ROTATION X", "HAND ROTATION Y", "HAND ROTATION Z",
        "CLOSED POSITION X", "CLOSED POSITION Y", "CLOSED POSITION Z",
        "RAIL AXIS X", "RAIL AXIS Y", "RAIL AXIS Z",
        "MAX TRAVEL", "REAR THRESHOLD", "ACTIVATION"};
    const std::size_t index = static_cast<std::size_t>(component);
    return index < static_cast<std::size_t>(SlideGrabComponent::Count)
        ? names[index] : "INVALID";
}

constexpr std::size_t kSlideGrabUndoCapacity = 32U;
struct SlideGrabEditorState {
    SlideGrabRailSettings current{};
    SlideGrabRailSettings baseline{};
    std::array<SlideGrabRailSettings, kSlideGrabUndoCapacity> undo{};
    std::size_t undoCount{0U};
    SlideGrabComponent component{SlideGrabComponent::GrabPositionX};
    bool baselineAvailable{false};
    bool coarse{false};
    bool dirty{false};
};

inline void SetSlideGrabEditorValue(
    SlideGrabEditorState& editor,
    const SlideGrabRailSettings& settings,
    bool makeBaseline) noexcept {
    editor.current = settings;
    editor.undoCount = 0U;
    editor.dirty = false;
    if (makeBaseline) {
        editor.baseline = settings;
        editor.baselineAvailable = true;
    }
}

inline void PushSlideGrabUndo(SlideGrabEditorState& editor) noexcept {
    if (editor.undoCount == editor.undo.size()) {
        for (std::size_t i = 1U; i < editor.undo.size(); ++i) {
            editor.undo[i - 1U] = editor.undo[i];
        }
        --editor.undoCount;
    }
    editor.undo[editor.undoCount++] = editor.current;
}

inline bool UndoSlideGrabEdit(SlideGrabEditorState& editor) noexcept {
    if (editor.undoCount == 0U) return false;
    editor.current = editor.undo[--editor.undoCount];
    editor.dirty = !editor.baselineAvailable ||
        !SlideGrabRailSettingsEqual(editor.current, editor.baseline);
    return true;
}

inline bool ResetSlideGrabEdit(SlideGrabEditorState& editor) noexcept {
    if (!editor.baselineAvailable ||
        SlideGrabRailSettingsEqual(editor.current, editor.baseline)) return false;
    PushSlideGrabUndo(editor);
    editor.current = editor.baseline;
    editor.dirty = false;
    return true;
}

inline bool CaptureSlideGrabFromController(
    SlideGrabEditorState& editor,
    const PhysicalMeleeRigidTransform& controllerModelLocal) noexcept {
    if (!SlideGrabRailSettingsAreValid(editor.current) ||
        !PhysicalMeleeRigidTransformIsValid(controllerModelLocal)) {
        return false;
    }
    PushSlideGrabUndo(editor);
    editor.current.grabVolumeModelLocal = controllerModelLocal;
    editor.current.handPoseModelLocal = controllerModelLocal;
    editor.current.configured = true;
    editor.dirty = true;
    return true;
}

inline void CycleSlideGrabComponent(
    SlideGrabEditorState& editor, int delta) noexcept {
    if (delta == 0) return;
    constexpr int count = static_cast<int>(SlideGrabComponent::Count);
    int value = static_cast<int>(editor.component);
    value = (value + (delta > 0 ? 1 : -1) + count) % count;
    editor.component = static_cast<SlideGrabComponent>(value);
}

inline bool SlideGrabComponentUsesDegrees(SlideGrabComponent component) noexcept {
    return (component >= SlideGrabComponent::GrabRotationX &&
            component <= SlideGrabComponent::GrabRotationZ) ||
        (component >= SlideGrabComponent::HandRotationX &&
         component <= SlideGrabComponent::HandRotationZ);
}

inline bool AdjustSlideGrabComponent(
    SlideGrabEditorState& editor, int delta) noexcept {
    if (delta == 0 || !SlideGrabRailSettingsAreValid(editor.current)) return false;
    SlideGrabRailSettings next = editor.current;
    if (editor.component == SlideGrabComponent::Activation) {
        int value = static_cast<int>(next.activationInput);
        value = (value + (delta > 0 ? 1 : -1) + 3) % 3;
        next.activationInput = static_cast<SlideGrabActivationInput>(value);
    } else {
        const float amount = static_cast<float>(delta) *
            (SlideGrabComponentUsesDegrees(editor.component)
                 ? (editor.coarse ? 5.0F : 0.25F)
                 : (editor.coarse ? 1.0F : 0.1F));
        auto rotate = [amount](float& value) {
            value = PhysicalMeleeWrapDegrees(value + amount);
        };
        switch (editor.component) {
        case SlideGrabComponent::GrabPositionX: next.grabVolumeModelLocal.positionUnits.x += amount; break;
        case SlideGrabComponent::GrabPositionY: next.grabVolumeModelLocal.positionUnits.y += amount; break;
        case SlideGrabComponent::GrabPositionZ: next.grabVolumeModelLocal.positionUnits.z += amount; break;
        case SlideGrabComponent::GrabRotationX: { fearvr::TrackingVector d{}; PhysicalMeleeLocalRotationDegreesFromQuaternion(next.grabVolumeModelLocal.rotation,d); rotate(d.x); next.grabVolumeModelLocal.rotation=PhysicalMeleeLocalRotationFromDegrees(d); break; }
        case SlideGrabComponent::GrabRotationY: { fearvr::TrackingVector d{}; PhysicalMeleeLocalRotationDegreesFromQuaternion(next.grabVolumeModelLocal.rotation,d); rotate(d.y); next.grabVolumeModelLocal.rotation=PhysicalMeleeLocalRotationFromDegrees(d); break; }
        case SlideGrabComponent::GrabRotationZ: { fearvr::TrackingVector d{}; PhysicalMeleeLocalRotationDegreesFromQuaternion(next.grabVolumeModelLocal.rotation,d); rotate(d.z); next.grabVolumeModelLocal.rotation=PhysicalMeleeLocalRotationFromDegrees(d); break; }
        case SlideGrabComponent::HalfExtentX: next.halfExtentsUnits.x=std::clamp(next.halfExtentsUnits.x+amount,0.25F,25.0F); break;
        case SlideGrabComponent::HalfExtentY: next.halfExtentsUnits.y=std::clamp(next.halfExtentsUnits.y+amount,0.25F,25.0F); break;
        case SlideGrabComponent::HalfExtentZ: next.halfExtentsUnits.z=std::clamp(next.halfExtentsUnits.z+amount,0.25F,25.0F); break;
        case SlideGrabComponent::HandPositionX: next.handPoseModelLocal.positionUnits.x += amount; break;
        case SlideGrabComponent::HandPositionY: next.handPoseModelLocal.positionUnits.y += amount; break;
        case SlideGrabComponent::HandPositionZ: next.handPoseModelLocal.positionUnits.z += amount; break;
        case SlideGrabComponent::HandRotationX: { fearvr::TrackingVector d{}; PhysicalMeleeLocalRotationDegreesFromQuaternion(next.handPoseModelLocal.rotation,d); rotate(d.x); next.handPoseModelLocal.rotation=PhysicalMeleeLocalRotationFromDegrees(d); break; }
        case SlideGrabComponent::HandRotationY: { fearvr::TrackingVector d{}; PhysicalMeleeLocalRotationDegreesFromQuaternion(next.handPoseModelLocal.rotation,d); rotate(d.y); next.handPoseModelLocal.rotation=PhysicalMeleeLocalRotationFromDegrees(d); break; }
        case SlideGrabComponent::HandRotationZ: { fearvr::TrackingVector d{}; PhysicalMeleeLocalRotationDegreesFromQuaternion(next.handPoseModelLocal.rotation,d); rotate(d.z); next.handPoseModelLocal.rotation=PhysicalMeleeLocalRotationFromDegrees(d); break; }
        case SlideGrabComponent::ClosedPositionX: next.closedPositionUnits.x += amount; break;
        case SlideGrabComponent::ClosedPositionY: next.closedPositionUnits.y += amount; break;
        case SlideGrabComponent::ClosedPositionZ: next.closedPositionUnits.z += amount; break;
        case SlideGrabComponent::AxisX: next.closedToRearAxis.x += amount * 0.1F; break;
        case SlideGrabComponent::AxisY: next.closedToRearAxis.y += amount * 0.1F; break;
        case SlideGrabComponent::AxisZ: next.closedToRearAxis.z += amount * 0.1F; break;
        case SlideGrabComponent::MaximumTravel: next.maximumTravelUnits=std::clamp(next.maximumTravelUnits+amount,0.25F,25.0F); next.rearThresholdUnits=std::min(next.rearThresholdUnits,next.maximumTravelUnits); break;
        case SlideGrabComponent::RearThreshold: next.rearThresholdUnits=std::clamp(next.rearThresholdUnits+amount,0.0F,next.maximumTravelUnits); break;
        default: break;
        }
        if (editor.component >= SlideGrabComponent::AxisX &&
            editor.component <= SlideGrabComponent::AxisZ) {
            const float length = PhysicalMeleeLength(next.closedToRearAxis);
            if (!std::isfinite(length) || length < 0.01F) return false;
            next.closedToRearAxis = PhysicalMeleeScale(next.closedToRearAxis, 1.0F / length);
        }
    }
    if (!SlideGrabRailSettingsAreValid(next) ||
        SlideGrabRailSettingsEqual(next, editor.current)) return false;
    PushSlideGrabUndo(editor);
    editor.current = next;
    editor.dirty = true;
    return true;
}

inline float SlideGrabComponentValue(
    const SlideGrabRailSettings& settings,
    SlideGrabComponent component) noexcept {
    fearvr::TrackingVector grabDegrees{};
    fearvr::TrackingVector handDegrees{};
    PhysicalMeleeLocalRotationDegreesFromQuaternion(
        settings.grabVolumeModelLocal.rotation, grabDegrees);
    PhysicalMeleeLocalRotationDegreesFromQuaternion(
        settings.handPoseModelLocal.rotation, handDegrees);
    switch (component) {
    case SlideGrabComponent::GrabPositionX: return settings.grabVolumeModelLocal.positionUnits.x;
    case SlideGrabComponent::GrabPositionY: return settings.grabVolumeModelLocal.positionUnits.y;
    case SlideGrabComponent::GrabPositionZ: return settings.grabVolumeModelLocal.positionUnits.z;
    case SlideGrabComponent::GrabRotationX: return grabDegrees.x;
    case SlideGrabComponent::GrabRotationY: return grabDegrees.y;
    case SlideGrabComponent::GrabRotationZ: return grabDegrees.z;
    case SlideGrabComponent::HalfExtentX: return settings.halfExtentsUnits.x;
    case SlideGrabComponent::HalfExtentY: return settings.halfExtentsUnits.y;
    case SlideGrabComponent::HalfExtentZ: return settings.halfExtentsUnits.z;
    case SlideGrabComponent::HandPositionX: return settings.handPoseModelLocal.positionUnits.x;
    case SlideGrabComponent::HandPositionY: return settings.handPoseModelLocal.positionUnits.y;
    case SlideGrabComponent::HandPositionZ: return settings.handPoseModelLocal.positionUnits.z;
    case SlideGrabComponent::HandRotationX: return handDegrees.x;
    case SlideGrabComponent::HandRotationY: return handDegrees.y;
    case SlideGrabComponent::HandRotationZ: return handDegrees.z;
    case SlideGrabComponent::ClosedPositionX: return settings.closedPositionUnits.x;
    case SlideGrabComponent::ClosedPositionY: return settings.closedPositionUnits.y;
    case SlideGrabComponent::ClosedPositionZ: return settings.closedPositionUnits.z;
    case SlideGrabComponent::AxisX: return settings.closedToRearAxis.x;
    case SlideGrabComponent::AxisY: return settings.closedToRearAxis.y;
    case SlideGrabComponent::AxisZ: return settings.closedToRearAxis.z;
    case SlideGrabComponent::MaximumTravel: return settings.maximumTravelUnits;
    case SlideGrabComponent::RearThreshold: return settings.rearThresholdUnits;
    case SlideGrabComponent::Activation:
        return static_cast<float>(settings.activationInput);
    default: return 0.0F;
    }
}

inline WeaponGripCalibrationGizmo BuildSlideGrabAuthoringGizmo(
    const SlideGrabRailSettings& settings,
    const PhysicalMeleeRigidTransform& modelWorld,
    const PhysicalMeleeRigidTransform* cursorModelLocal = nullptr) noexcept {
    WeaponGripCalibrationGizmo gizmo{};
    if (!SlideGrabRailSettingsAreValid(settings) ||
        !PhysicalMeleeRigidTransformIsValid(modelWorld)) {
        return gizmo;
    }
    const auto AddModelLine = [&](const fearvr::TrackingVector& a,
                                  const fearvr::TrackingVector& b,
                                  std::uint32_t color) {
        AddWeaponGripCalibrationGizmoLine(
            gizmo,
            CalibrationGizmoTransformPoint(
                modelWorld.positionUnits, modelWorld.rotation, a),
            CalibrationGizmoTransformPoint(
                modelWorld.positionUnits, modelWorld.rotation, b),
            color);
    };
    const fearvr::TrackingVector rear = SlideRearEndpoint(settings);
    AddModelLine(
        settings.closedPositionUnits, rear, 0xFFFFFF40U);
    constexpr float kEndpointHalfSize = 0.75F;
    for (const fearvr::TrackingVector endpoint : {
             settings.closedPositionUnits, rear}) {
        AddModelLine(
            PhysicalMeleeAdd(endpoint, {-kEndpointHalfSize, 0.0F, 0.0F}),
            PhysicalMeleeAdd(endpoint, { kEndpointHalfSize, 0.0F, 0.0F}),
            0xFFFFFF40U);
        AddModelLine(
            PhysicalMeleeAdd(endpoint, {0.0F, -kEndpointHalfSize, 0.0F}),
            PhysicalMeleeAdd(endpoint, {0.0F,  kEndpointHalfSize, 0.0F}),
            0xFFFFFF40U);
        AddModelLine(
            PhysicalMeleeAdd(endpoint, {0.0F, 0.0F, -kEndpointHalfSize}),
            PhysicalMeleeAdd(endpoint, {0.0F, 0.0F,  kEndpointHalfSize}),
            0xFFFFFF40U);
    }
    if (settings.configured) {
        PhysicalMeleeRigidTransform grabWorld{};
        PhysicalMeleeRigidTransform handWorld{};
        if (ComposePhysicalMeleeRigidTransforms(
                modelWorld, settings.grabVolumeModelLocal, grabWorld)) {
            AddMagazineSocketBox(
                gizmo, grabWorld, settings.halfExtentsUnits,
                0xFF70E8FFU);
        }
        if (ComposePhysicalMeleeRigidTransforms(
                modelWorld, settings.handPoseModelLocal, handWorld)) {
            constexpr fearvr::TrackingVector axes[] = {
                {3.0F, 0.0F, 0.0F},
                {0.0F, 3.0F, 0.0F},
                {0.0F, 0.0F, 3.0F}};
            constexpr std::uint32_t colors[] = {
                0xFFFF4040U, 0xFF40FF60U, 0xFF4080FFU};
            for (std::size_t i = 0U; i < 3U; ++i) {
                AddWeaponGripCalibrationGizmoLine(
                    gizmo, handWorld.positionUnits,
                    CalibrationGizmoTransformPoint(
                        handWorld.positionUnits,
                        handWorld.rotation, axes[i]), colors[i]);
            }
        }
    }
    if (cursorModelLocal != nullptr &&
        PhysicalMeleeRigidTransformIsValid(*cursorModelLocal)) {
        PhysicalMeleeRigidTransform cursorWorld{};
        if (ComposePhysicalMeleeRigidTransforms(
                modelWorld, *cursorModelLocal, cursorWorld)) {
            AddMagazineSocketBox(
                gizmo, cursorWorld, {0.5F, 0.5F, 0.5F},
                SlideGrabVolumeContains(
                    settings, cursorModelLocal->positionUnits)
                    ? 0xFF50FF80U : 0xFFFFB040U);
        }
    }
    gizmo.valid = gizmo.count != 0U;
    return gizmo;
}

enum class SlideGrabState : std::uint8_t { Idle, Candidate, Attached, Released };
enum class SlideGrabDetachReason : std::uint8_t {
    None, InputReleased, FocusLost, GameStateInvalid, TrackingStale,
    WeaponChanged, SourceGenerationChanged, ModelUnavailable,
    NodeResolutionFailed, SettingsInvalid, TransformInvalid, MenuOpened,
    RetailAnimationStarted, NodeControlFailed
};

inline const char* SlideGrabDetachReasonName(SlideGrabDetachReason reason) noexcept {
    static constexpr const char* names[] = {"none","input_released","focus_lost","game_state_invalid","tracking_stale","weapon_changed","source_generation_changed","model_unavailable","node_resolution_failed","settings_invalid","transform_invalid","menu_opened","retail_incompatible_animation","node_control_failed"};
    const auto index=static_cast<std::size_t>(reason);
    return index < sizeof(names)/sizeof(names[0]) ? names[index] : "unknown";
}

struct SlideGrabStateMachine {
    SlideGrabState state{SlideGrabState::Idle};
    fearvr::TrackingVector attachControllerModelLocal{};
    std::int32_t weaponIndex{-1};
    std::uint64_t sourceGeneration{0U};
    bool activationArmed{false};
    bool previousGripDown{false};
    bool previousTriggerDown{false};
};

struct SlideGrabFrameInput {
    SlideGrabRailSettings settings{};
    fearvr::TrackingVector controllerModelLocal{};
    std::int32_t weaponIndex{-1};
    std::uint64_t sourceGeneration{0U};
    bool trackingFresh{false};
    bool focused{false};
    bool gamePlaying{false};
    bool exactWeaponIdentity{false};
    bool modelAvailable{false};
    bool nodeResolved{false};
    bool transformValid{false};
    bool toolMenuOpen{false};
    bool retailAnimationIncompatible{false};
    float gripValue{0.0F};
    float triggerValue{0.0F};
};

struct SlideGrabFrameResult {
    SlideGrabState state{SlideGrabState::Idle};
    SlideGrabDetachReason reason{SlideGrabDetachReason::None};
    SlideRailProjection projection{};
    bool overlap{false};
    bool requestNodeControlAttach{false};
    bool requestNodeControlDetach{false};
    bool captureGrip{false};
    bool captureTrigger{false};
    bool stateChanged{false};
};

inline SlideGrabFrameResult UpdateSlideGrabStateMachine(
    SlideGrabStateMachine& machine,
    const SlideGrabFrameInput& input) noexcept {
    SlideGrabFrameResult result{};
    const SlideGrabState prior = machine.state;
    if (machine.state == SlideGrabState::Released) machine.state = SlideGrabState::Idle;
    const bool gripDown = std::isfinite(input.gripValue) && input.gripValue >= 0.65F;
    const bool triggerDown = std::isfinite(input.triggerValue) && input.triggerValue >= 0.55F;
    const bool selectedDown = input.settings.activationInput == SlideGrabActivationInput::Grip ? gripDown : input.settings.activationInput == SlideGrabActivationInput::Trigger ? triggerDown : gripDown || triggerDown;
    const bool selectedEdge = input.settings.activationInput == SlideGrabActivationInput::Grip ? gripDown && !machine.previousGripDown : input.settings.activationInput == SlideGrabActivationInput::Trigger ? triggerDown && !machine.previousTriggerDown : (gripDown && !machine.previousGripDown) || (triggerDown && !machine.previousTriggerDown);
    const bool wasAttached = machine.state == SlideGrabState::Attached;
    auto cancel = [&](SlideGrabDetachReason reason) {
        result.reason = reason;
        result.requestNodeControlDetach = wasAttached;
        machine.state = wasAttached ? SlideGrabState::Released : SlideGrabState::Idle;
        machine.activationArmed = false;
    };
    if (!SlideGrabRailSettingsAreValid(input.settings) || !input.settings.configured) cancel(SlideGrabDetachReason::SettingsInvalid);
    else if (!input.focused) cancel(SlideGrabDetachReason::FocusLost);
    else if (!input.gamePlaying) cancel(SlideGrabDetachReason::GameStateInvalid);
    else if (!input.trackingFresh) cancel(SlideGrabDetachReason::TrackingStale);
    else if (!input.exactWeaponIdentity) cancel(SlideGrabDetachReason::WeaponChanged);
    else if (!input.modelAvailable) cancel(SlideGrabDetachReason::ModelUnavailable);
    else if (!input.nodeResolved) cancel(SlideGrabDetachReason::NodeResolutionFailed);
    else if (!input.transformValid) cancel(SlideGrabDetachReason::TransformInvalid);
    else if (input.toolMenuOpen) cancel(SlideGrabDetachReason::MenuOpened);
    else if (input.retailAnimationIncompatible) cancel(SlideGrabDetachReason::RetailAnimationStarted);
    else if (wasAttached && (input.weaponIndex != machine.weaponIndex || input.sourceGeneration != machine.sourceGeneration)) cancel(input.weaponIndex != machine.weaponIndex ? SlideGrabDetachReason::WeaponChanged : SlideGrabDetachReason::SourceGenerationChanged);
    else {
        result.overlap = SlideGrabVolumeContains(input.settings, input.controllerModelLocal);
        if (machine.state == SlideGrabState::Attached) {
            result.captureGrip = input.settings.activationInput != SlideGrabActivationInput::Trigger;
            result.captureTrigger = input.settings.activationInput != SlideGrabActivationInput::Grip;
            if (!selectedDown) cancel(SlideGrabDetachReason::InputReleased);
            else result.projection = ProjectSlideControllerDisplacement(input.settings, machine.attachControllerModelLocal, input.controllerModelLocal);
        } else if (result.overlap) {
            machine.state = SlideGrabState::Candidate;
            if (!selectedDown) machine.activationArmed = true;
            result.captureGrip = input.settings.activationInput != SlideGrabActivationInput::Trigger;
            result.captureTrigger = input.settings.activationInput != SlideGrabActivationInput::Grip;
            if (machine.activationArmed && selectedEdge) {
                machine.state = SlideGrabState::Attached;
                machine.attachControllerModelLocal = input.controllerModelLocal;
                machine.weaponIndex = input.weaponIndex;
                machine.sourceGeneration = input.sourceGeneration;
                machine.activationArmed = false;
                result.requestNodeControlAttach = true;
                result.projection = ProjectSlideControllerDisplacement(input.settings, machine.attachControllerModelLocal, input.controllerModelLocal);
            }
        } else {
            machine.state = SlideGrabState::Idle;
            // Require one released Candidate sample inside the box before an
            // activation edge. This gives the binding hook a frame to consume
            // the configured VR command before attachment can occur.
            machine.activationArmed = false;
        }
    }
    machine.previousGripDown = gripDown;
    machine.previousTriggerDown = triggerDown;
    result.state = machine.state;
    result.stateChanged = prior != machine.state;
    return result;
}

inline void RejectSlideGrabNodeControl(
    SlideGrabStateMachine& machine,
    SlideGrabFrameResult& result) noexcept {
    if (machine.state == SlideGrabState::Attached) {
        machine.state = SlideGrabState::Released;
        machine.activationArmed = false;
        result.state = machine.state;
        result.reason = SlideGrabDetachReason::NodeControlFailed;
        result.requestNodeControlAttach = false;
        result.requestNodeControlDetach = true;
        result.stateChanged = true;
    }
}

constexpr float kSlideGrabSoundReturnTravelThresholdUnits = 0.10F;
constexpr float kSlideGrabSoundRearRearmHysteresisUnits = 0.25F;

enum class SlideGrabSoundCue : std::uint8_t {
    None,
    Pull,
    Return
};

inline const char* SlideGrabSoundCueName(
    SlideGrabSoundCue cue) noexcept {
    switch (cue) {
    case SlideGrabSoundCue::Pull: return "pull";
    case SlideGrabSoundCue::Return: return "return";
    default: return "none";
    }
}

struct SlideGrabSoundCueState {
    float lastTravelUnits{0.0F};
    std::uint32_t pullCueCount{0U};
    bool attachmentActive{false};
    bool pullCueArmed{false};
    bool anyPullPlayed{false};
};

struct SlideGrabSoundCueResult {
    SlideGrabSoundCue cue{SlideGrabSoundCue::None};
    float travelUnits{0.0F};
    std::uint32_t pullCycle{0U};
    bool stopPlayback{false};
};

inline SlideGrabSoundCueResult UpdateSlideGrabSoundCueState(
    SlideGrabSoundCueState& state,
    const SlideGrabFrameResult& frame,
    float rearThresholdUnits,
    bool nodeControlSucceeded) noexcept {
    SlideGrabSoundCueResult result{};
    auto reset = [&]() noexcept {
        state = {};
    };

    if (frame.requestNodeControlAttach) {
        reset();
        if (frame.state == SlideGrabState::Attached &&
            nodeControlSucceeded) {
            state.attachmentActive = true;
            state.pullCueArmed = true;
        }
        return result;
    }

    if (frame.state == SlideGrabState::Attached) {
        if (!state.attachmentActive) {
            return result;
        }
        if (!nodeControlSucceeded || !frame.projection.valid ||
            !std::isfinite(frame.projection.clampedTravelUnits) ||
            !std::isfinite(rearThresholdUnits) ||
            rearThresholdUnits < 0.0F) {
            result.stopPlayback = state.anyPullPlayed;
            reset();
            return result;
        }
        state.lastTravelUnits =
            frame.projection.clampedTravelUnits;
        result.travelUnits = state.lastTravelUnits;
        const float rearmTravelUnits = std::max(
            0.0F, rearThresholdUnits -
                kSlideGrabSoundRearRearmHysteresisUnits);
        if (!state.pullCueArmed &&
            !frame.projection.rearReached &&
            state.lastTravelUnits <= rearmTravelUnits) {
            state.pullCueArmed = true;
        }
        if (state.pullCueArmed &&
            frame.projection.rearReached) {
            state.pullCueArmed = false;
            state.anyPullPlayed = true;
            ++state.pullCueCount;
            result.cue = SlideGrabSoundCue::Pull;
            result.pullCycle = state.pullCueCount;
        }
        return result;
    }

    if (frame.requestNodeControlDetach ||
        frame.state == SlideGrabState::Released) {
        result.travelUnits = state.lastTravelUnits;
        result.pullCycle = state.pullCueCount;
        if (state.attachmentActive && state.anyPullPlayed) {
            if (frame.reason == SlideGrabDetachReason::InputReleased &&
                nodeControlSucceeded &&
                state.lastTravelUnits >=
                    kSlideGrabSoundReturnTravelThresholdUnits) {
                result.cue = SlideGrabSoundCue::Return;
            } else {
                result.stopPlayback = true;
            }
        }
        reset();
        return result;
    }

    if (state.attachmentActive) {
        result.stopPlayback = state.anyPullPlayed;
        reset();
    }
    return result;
}

} // namespace condemnedvr
