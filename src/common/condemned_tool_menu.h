#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "arm_ik.h"
#include "condemned_block_pose.h"
#include "condemned_physical_melee.h"
#include "condemned_player_collision.h"
#include "condemned_weapon_identity.h"
#include "input_state.h"
#include "protocol.h"

namespace condemnedvr {

enum class ToolMenuTab : std::uint8_t {
    Melee,
    Block,
    BlockCollider,
    Weapon,
    Grip,
    Collider,
    Author,
    PlayerCollider,
    TwoHand,
    HandIk,
    LeftHandIk,
    ElbowIk,
    Display,
    Controls,
    Debug,
    Count
};

inline const char* ToolMenuTabName(ToolMenuTab tab) noexcept {
    switch (tab) {
    case ToolMenuTab::Melee:
        return "MELEE";
    case ToolMenuTab::Block:
        return "BLOCK";
    case ToolMenuTab::BlockCollider:
        return "BLOCK COL";
    case ToolMenuTab::Weapon:
        return "WEAPON";
    case ToolMenuTab::Grip:
        return "GRIP";
    case ToolMenuTab::Collider:
        return "COLLIDER";
    case ToolMenuTab::Author:
        return "AUTHOR";
    case ToolMenuTab::PlayerCollider:
        return "PLAYER COL";
    case ToolMenuTab::TwoHand:
        return "2-HAND";
    case ToolMenuTab::HandIk:
        return "HAND IK";
    case ToolMenuTab::LeftHandIk:
        return "LEFT IK";
    case ToolMenuTab::ElbowIk:
        return "ELBOW";
    case ToolMenuTab::Display:
        return "DISPLAY";
    case ToolMenuTab::Controls:
        return "CONTROLS";
    case ToolMenuTab::Debug:
        return "DEBUG";
    default:
        return "INVALID";
    }
}

inline std::uint32_t ToolMenuRowCount(ToolMenuTab tab) noexcept {
    switch (tab) {
    case ToolMenuTab::Melee:
        return 4U;
    case ToolMenuTab::Block:
        return 7U;
    case ToolMenuTab::BlockCollider:
        return 10U;
    case ToolMenuTab::Weapon:
        return 7U;
    case ToolMenuTab::Grip:
        return 10U;
    case ToolMenuTab::Collider:
        return 10U;
    case ToolMenuTab::Author:
        return 8U;
    case ToolMenuTab::PlayerCollider:
        return 3U;
    case ToolMenuTab::TwoHand:
        return 8U;
    case ToolMenuTab::HandIk:
        return 9U;
    case ToolMenuTab::LeftHandIk:
        return 8U;
    case ToolMenuTab::ElbowIk:
        return 6U;
    case ToolMenuTab::Display:
        return 9U;
    case ToolMenuTab::Controls:
        return 1U;
    case ToolMenuTab::Debug:
        return 3U;
    default:
        return 0U;
    }
}

inline std::uint32_t ToolMenuRowCount(
    ToolMenuTab tab,
    bool emptyHandIkPage) noexcept {
    if (tab == ToolMenuTab::HandIk && emptyHandIkPage) {
        return 2U;
    }
    return ToolMenuRowCount(tab);
}

struct ToolMenuState {
    ToolMenuTab tab{ToolMenuTab::Melee};
    std::uint32_t row{1U};
    bool open{false};
};

struct ToolMenuInputEvent {
    bool toggle{false};
    bool close{false};
    bool previousTab{false};
    bool nextTab{false};
    bool previousRow{false};
    bool nextRow{false};
    bool decrease{false};
    bool increase{false};
    bool activate{false};
};

struct ToolMenuTransition {
    bool opened{false};
    bool closed{false};
    bool selectionChanged{false};
    int valueDelta{0};
    bool activate{false};
};

inline ToolMenuTransition UpdateToolMenuState(
    ToolMenuState& state,
    const ToolMenuInputEvent& input,
    bool emptyHandIkPage = false) noexcept {
    ToolMenuTransition result{};
    if (input.toggle) {
        state.open = !state.open;
        result.opened = state.open;
        result.closed = !state.open;
        return result;
    }
    if (!state.open) {
        return result;
    }
    if (input.close) {
        state.open = false;
        result.closed = true;
        return result;
    }

    const std::uint32_t tabCount =
        static_cast<std::uint32_t>(ToolMenuTab::Count);
    std::uint32_t tab = static_cast<std::uint32_t>(state.tab);
    if (input.previousTab) {
        tab = (tab + tabCount - 1U) % tabCount;
        result.selectionChanged = true;
    } else if (input.nextTab) {
        tab = (tab + 1U) % tabCount;
        result.selectionChanged = true;
    }
    state.tab = static_cast<ToolMenuTab>(tab);

    const std::uint32_t rows =
        ToolMenuRowCount(state.tab, emptyHandIkPage);
    if (rows == 0U) {
        state.row = 0U;
    } else {
        state.row = std::min(state.row, rows - 1U);
        if (input.previousRow) {
            state.row = (state.row + rows - 1U) % rows;
            result.selectionChanged = true;
        } else if (input.nextRow) {
            state.row = (state.row + 1U) % rows;
            result.selectionChanged = true;
        }
    }
    result.valueDelta = input.decrease ? -1 : input.increase ? 1 : 0;
    result.activate = input.activate;
    return result;
}

struct ToolMenuDebugDrawSettings {
    bool colliderVisible{false};
    bool blockColliderVisible{false};
    bool controllerVisible{false};
};

inline bool UpdateToolMenuDebugDrawSettings(
    ToolMenuDebugDrawSettings& settings,
    std::uint32_t row,
    int delta,
    bool activate) noexcept {
    if (delta == 0 && !activate) {
        return false;
    }
    switch (row) {
    case 0U:
        settings.colliderVisible = !settings.colliderVisible;
        return true;
    case 1U:
        settings.blockColliderVisible =
            !settings.blockColliderVisible;
        return true;
    case 2U:
        settings.controllerVisible = !settings.controllerVisible;
        return true;
    default:
        return false;
    }
}

// Retail owns the native block lifetime unless this explicit per-weapon
// override is enabled. Keeping the override disabled by default preserves the
// verified Retail window while still making its duration practical to tune in
// headset. The bounded range prevents a malformed setting from creating a
// near-permanent guard or a zero-length collision record.
constexpr std::uint32_t kToolMenuBlockWindowDefaultMilliseconds = 450U;
constexpr std::uint32_t kToolMenuBlockWindowMinimumMilliseconds = 100U;
constexpr std::uint32_t kToolMenuBlockWindowMaximumMilliseconds = 2000U;
constexpr std::uint32_t kToolMenuBlockWindowStepMilliseconds = 25U;

struct ToolMenuBlockTimingSettings {
    std::uint32_t collisionWindowMilliseconds{
        kToolMenuBlockWindowDefaultMilliseconds};
    bool overrideEnabled{false};
};

inline bool ToolMenuBlockTimingSettingsAreValid(
    const ToolMenuBlockTimingSettings& settings) noexcept {
    return settings.collisionWindowMilliseconds >=
            kToolMenuBlockWindowMinimumMilliseconds &&
        settings.collisionWindowMilliseconds <=
            kToolMenuBlockWindowMaximumMilliseconds;
}

inline bool UpdateToolMenuBlockTimingSettings(
    ToolMenuBlockTimingSettings& settings,
    std::uint32_t row,
    int delta,
    bool activate) noexcept {
    switch (row) {
    case 0U:
        if (delta == 0 && !activate) {
            return false;
        }
        settings.overrideEnabled = !settings.overrideEnabled;
        return true;
    case 1U: {
        if (delta == 0) {
            return false;
        }
        const std::int64_t requested =
            static_cast<std::int64_t>(
                settings.collisionWindowMilliseconds) +
            static_cast<std::int64_t>(delta) *
                kToolMenuBlockWindowStepMilliseconds;
        const std::uint32_t next = static_cast<std::uint32_t>(
            std::clamp<std::int64_t>(
                requested,
                kToolMenuBlockWindowMinimumMilliseconds,
                kToolMenuBlockWindowMaximumMilliseconds));
        if (next == settings.collisionWindowMilliseconds) {
            return false;
        }
        settings.collisionWindowMilliseconds = next;
        return true;
    }
    default:
        return false;
    }
}

inline float ResolveToolMenuBlockWindowSeconds(
    const ToolMenuBlockTimingSettings& settings,
    float retailSeconds,
    bool& overrideApplied) noexcept {
    overrideApplied = false;
    if (!std::isfinite(retailSeconds) || retailSeconds <= 0.0F ||
        retailSeconds > 10.0F ||
        !ToolMenuBlockTimingSettingsAreValid(settings) ||
        !settings.overrideEnabled) {
        return retailSeconds;
    }
    overrideApplied = true;
    return static_cast<float>(
        settings.collisionWindowMilliseconds) / 1000.0F;
}

struct ToolMenuMeleeSettings {
    bool requireSwingForContactDamage{true};
    float hitSpeedMetersPerSecond{1.25F};
    float contactRearmDistanceMeters{0.12F};
    bool swingAttackEnabled{false};
    float swingTriggerSpeedMetersPerSecond{3.00F};
    float swingRearmSpeedMetersPerSecond{0.75F};
    std::uint32_t swingPulseMilliseconds{100U};
    std::uint32_t swingCooldownMilliseconds{450U};
    float massKilograms{1.5F};
    float handlingWeight{1.0F};
    float positionalFollow{18.0F};
    float rotationalFollow{20.0F};
    float catchUpStrength{1.50F};
    float dampingRatio{1.0F};
};

// Per-weapon swept capsule expressed in the weighted controller's local
// space. The explicit reverse toggle makes a 180-degree correction possible
// in one headset action without losing editable rotation or length.
struct ToolMenuColliderSettings {
    fearvr::TrackingVector positionOffsetUnits{};
    fearvr::TrackingVector rotationOffsetDegrees{};
    float lengthUnits{75.0F};
    float radiusUnits{4.0F};
    bool reversed{false};
};

constexpr float kToolMenuColliderMaximumOffsetUnits = 200.0F;
constexpr float kToolMenuColliderMaximumLengthUnits = 250.0F;
constexpr float kToolMenuColliderMaximumRadiusUnits = 25.0F;

inline bool ToolMenuColliderSettingsAreValid(
    const ToolMenuColliderSettings& settings) noexcept {
    return fearvr::IsFinite(settings.positionOffsetUnits) &&
        fearvr::IsFinite(settings.rotationOffsetDegrees) &&
        std::fabs(settings.positionOffsetUnits.x) <=
            kToolMenuColliderMaximumOffsetUnits &&
        std::fabs(settings.positionOffsetUnits.y) <=
            kToolMenuColliderMaximumOffsetUnits &&
        std::fabs(settings.positionOffsetUnits.z) <=
            kToolMenuColliderMaximumOffsetUnits &&
        settings.rotationOffsetDegrees.x >= -180.0F &&
        settings.rotationOffsetDegrees.x <= 180.0F &&
        settings.rotationOffsetDegrees.y >= -180.0F &&
        settings.rotationOffsetDegrees.y <= 180.0F &&
        settings.rotationOffsetDegrees.z >= -180.0F &&
        settings.rotationOffsetDegrees.z <= 180.0F &&
        std::isfinite(settings.lengthUnits) &&
        settings.lengthUnits >= 5.0F &&
        settings.lengthUnits <= kToolMenuColliderMaximumLengthUnits &&
        std::isfinite(settings.radiusUnits) &&
        settings.radiusUnits >= 0.5F &&
        settings.radiusUnits <= kToolMenuColliderMaximumRadiusUnits;
}

constexpr std::uint32_t kLiveColliderAlignmentCommandVersion = 1U;

struct LiveColliderAlignmentCommand {
    std::uint32_t version{0U};
    std::uint64_t revision{0U};
    std::uint32_t processId{0U};
    std::int32_t weaponIndex{-1};
    ToolMenuColliderSettings settings{};
};

enum class LiveColliderAlignmentCommandParseResult : std::uint8_t {
    Ok,
    Missing,
    Malformed,
    InvalidValue
};

inline const char* LiveColliderAlignmentCommandParseResultName(
    LiveColliderAlignmentCommandParseResult result) noexcept {
    switch (result) {
    case LiveColliderAlignmentCommandParseResult::Ok:
        return "ok";
    case LiveColliderAlignmentCommandParseResult::Missing:
        return "missing";
    case LiveColliderAlignmentCommandParseResult::Malformed:
        return "malformed";
    case LiveColliderAlignmentCommandParseResult::InvalidValue:
        return "invalid_value";
    default:
        return "unknown";
    }
}

// A deliberately rigid one-line format keeps the developer command channel
// dependency-free and fail-closed. The launcher supplies a unique per-run
// path, while process ID, weapon index and revision prevent stale application.
inline LiveColliderAlignmentCommandParseResult
ParseLiveColliderAlignmentCommand(
    const char* text,
    LiveColliderAlignmentCommand& command) noexcept {
    command = {};
    if (text == nullptr || text[0] == '\0') {
        return LiveColliderAlignmentCommandParseResult::Missing;
    }
    unsigned int version = 0U;
    unsigned long long revision = 0ULL;
    unsigned int processId = 0U;
    int weaponIndex = -1;
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float rotationX = 0.0F;
    float rotationY = 0.0F;
    float rotationZ = 0.0F;
    float length = 0.0F;
    float radius = 0.0F;
    unsigned int reversed = 0U;
    int consumed = 0;
#if defined(_MSC_VER)
    const int parsed = ::sscanf_s(
#else
    const int parsed = std::sscanf(
#endif
        text,
        "version=%u revision=%llu process_id=%u weapon_index=%d "
        "position_x=%f position_y=%f position_z=%f "
        "rotation_x=%f rotation_y=%f rotation_z=%f "
        "length=%f radius=%f reversed=%u %n",
        &version, &revision, &processId, &weaponIndex,
        &positionX, &positionY, &positionZ,
        &rotationX, &rotationY, &rotationZ,
        &length, &radius, &reversed, &consumed);
    if (parsed != 13 || consumed <= 0) {
        return LiveColliderAlignmentCommandParseResult::Malformed;
    }
    for (const char* remaining = text + consumed;
         *remaining != '\0'; ++remaining) {
        if (*remaining != ' ' && *remaining != '\t' &&
            *remaining != '\r' && *remaining != '\n') {
            return LiveColliderAlignmentCommandParseResult::Malformed;
        }
    }
    command.version = static_cast<std::uint32_t>(version);
    command.revision = static_cast<std::uint64_t>(revision);
    command.processId = static_cast<std::uint32_t>(processId);
    command.weaponIndex = static_cast<std::int32_t>(weaponIndex);
    command.settings.positionOffsetUnits =
        {positionX, positionY, positionZ};
    command.settings.rotationOffsetDegrees =
        {rotationX, rotationY, rotationZ};
    command.settings.lengthUnits = length;
    command.settings.radiusUnits = radius;
    command.settings.reversed = reversed != 0U;
    if (command.version != kLiveColliderAlignmentCommandVersion ||
        command.revision == 0U || command.processId == 0U ||
        command.weaponIndex < 0 || reversed > 1U ||
        !ToolMenuColliderSettingsAreValid(command.settings)) {
        command = {};
        return LiveColliderAlignmentCommandParseResult::InvalidValue;
    }
    return LiveColliderAlignmentCommandParseResult::Ok;
}

inline bool LiveColliderAlignmentCommandMatchesTarget(
    const LiveColliderAlignmentCommand& command,
    std::uint32_t processId,
    std::int32_t weaponIndex) noexcept {
    return processId != 0U && weaponIndex >= 0 &&
        command.processId == processId &&
        command.weaponIndex == weaponIndex;
}

inline ToolMenuColliderSettings ToolMenuColliderSettingsFromProfile(
    const PhysicalMeleeProfile& profile) noexcept {
    ToolMenuColliderSettings settings{};
    if (!PhysicalMeleeProfileIsValid(profile)) {
        return settings;
    }
    settings.positionOffsetUnits = profile.localBaseOffsetUnits;
    const fearvr::TrackingVector delta = PhysicalMeleeSubtract(
        profile.localTipOffsetUnits, profile.localBaseOffsetUnits);
    settings.lengthUnits = PhysicalMeleeLength(delta);
    settings.radiusUnits = profile.radiusUnits;
    if (std::isfinite(settings.lengthUnits) &&
        settings.lengthUnits >= 5.0F) {
        const fearvr::TrackingVector direction = PhysicalMeleeScale(
            delta, 1.0F / settings.lengthUnits);
        constexpr float kRadiansToDegrees =
            180.0F / 3.14159265358979323846F;
        settings.rotationOffsetDegrees.x =
            -std::asin(std::clamp(direction.y, -1.0F, 1.0F)) *
            kRadiansToDegrees;
        settings.rotationOffsetDegrees.y =
            std::atan2(direction.x, direction.z) * kRadiansToDegrees;
    }
    return settings;
}

inline void ApplyToolMenuColliderSettings(
    const ToolMenuColliderSettings& settings,
    PhysicalMeleeProfile& profile) noexcept {
    if (!PhysicalMeleeProfileIsValid(profile) ||
        !ToolMenuColliderSettingsAreValid(settings)) {
        return;
    }
    profile.localBaseOffsetUnits = settings.positionOffsetUnits;
    const float signedLength =
        settings.reversed ? -settings.lengthUnits : settings.lengthUnits;
    profile.localTipOffsetUnits = PhysicalMeleeAdd(
        settings.positionOffsetUnits,
        fearvr::Rotate(
            PhysicalMeleeLocalRotationFromDegrees(
                settings.rotationOffsetDegrees),
            {0.0F, 0.0F, signedLength}));
    profile.radiusUnits = settings.radiusUnits;
}

// Reprojects a second controller-local capsule onto the exact weighted grip
// pose represented by an existing physical-melee frame. This lets attack and
// block records share tracking/weighting while retaining independent geometry.
// Only the current pose is reconstructed because Retail owns collision sweep
// history; this helper supplies the native capsule transform and debug shape.
inline bool ResolveToolMenuColliderFrameAtCurrentPose(
    const PhysicalMeleeFrame& sourceFrame,
    const PhysicalMeleeProfile& sourceProfile,
    const ToolMenuColliderSettings& colliderSettings,
    PhysicalMeleeFrame& colliderFrame) noexcept {
    colliderFrame = {};
    if (!sourceFrame.poseValid ||
        !fearvr::IsFinite(sourceFrame.currentBaseUnits) ||
        !fearvr::IsFinite(sourceFrame.currentRotation) ||
        !PhysicalMeleeProfileIsValid(sourceProfile) ||
        !ToolMenuColliderSettingsAreValid(colliderSettings)) {
        return false;
    }
    const fearvr::TrackingQuaternion rotation =
        fearvr::Normalize(sourceFrame.currentRotation);
    const PhysicalMeleePose gripPose{
        PhysicalMeleeSubtract(
            sourceFrame.currentBaseUnits,
            fearvr::Rotate(
                rotation, sourceProfile.localBaseOffsetUnits)),
        rotation};
    if (!PhysicalMeleePoseIsValid(gripPose)) {
        return false;
    }
    PhysicalMeleeProfile colliderProfile = sourceProfile;
    ApplyToolMenuColliderSettings(
        colliderSettings, colliderProfile);
    colliderFrame.currentRotation = rotation;
    colliderFrame.currentBaseUnits = PhysicalMeleeEndpoint(
        gripPose, colliderProfile.localBaseOffsetUnits);
    colliderFrame.currentTipUnits = PhysicalMeleeEndpoint(
        gripPose, colliderProfile.localTipOffsetUnits);
    colliderFrame.radiusUnits = colliderProfile.radiusUnits;
    colliderFrame.poseValid =
        fearvr::IsFinite(colliderFrame.currentBaseUnits) &&
        fearvr::IsFinite(colliderFrame.currentTipUnits) &&
        std::isfinite(colliderFrame.radiusUnits) &&
        colliderFrame.radiusUnits > 0.0F;
    return colliderFrame.poseValid;
}

// Per-weapon correction applied to the dominant-hand socket target after the
// weighted VR weapon pose has been resolved. Position is expressed in the
// weapon/controller target's local LithTech units; rotation is an X/Y/Z local
// correction in degrees. Zero is deliberately the safe proof-build behavior.
struct ToolMenuRightHandIkSettings {
    fearvr::TrackingVector positionOffsetUnits{};
    fearvr::TrackingVector rotationOffsetDegrees{};
};

constexpr float kToolMenuRightHandIkMaximumPositionOffsetUnits = 100.0F;

inline bool ToolMenuRightHandIkSettingsAreValid(
    const ToolMenuRightHandIkSettings& settings) noexcept {
    return fearvr::IsFinite(settings.positionOffsetUnits) &&
        fearvr::IsFinite(settings.rotationOffsetDegrees) &&
        std::fabs(settings.positionOffsetUnits.x) <=
            kToolMenuRightHandIkMaximumPositionOffsetUnits &&
        std::fabs(settings.positionOffsetUnits.y) <=
            kToolMenuRightHandIkMaximumPositionOffsetUnits &&
        std::fabs(settings.positionOffsetUnits.z) <=
            kToolMenuRightHandIkMaximumPositionOffsetUnits &&
        settings.rotationOffsetDegrees.x >= -180.0F &&
        settings.rotationOffsetDegrees.x <= 180.0F &&
        settings.rotationOffsetDegrees.y >= -180.0F &&
        settings.rotationOffsetDegrees.y <= 180.0F &&
        settings.rotationOffsetDegrees.z >= -180.0F &&
        settings.rotationOffsetDegrees.z <= 180.0F;
}

inline PhysicalMeleeRigidTransform ResolveToolMenuRightHandIkTarget(
    const PhysicalMeleeRigidTransform& baseTarget,
    const ToolMenuRightHandIkSettings& settings) noexcept {
    if (!PhysicalMeleeRigidTransformIsValid(baseTarget) ||
        !ToolMenuRightHandIkSettingsAreValid(settings)) {
        return {{}, {0.0F, 0.0F, 0.0F, 0.0F}};
    }
    const fearvr::TrackingQuaternion baseRotation =
        fearvr::Normalize(baseTarget.rotation);
    return {
        PhysicalMeleeAdd(
            baseTarget.positionUnits,
            fearvr::Rotate(
                baseRotation, settings.positionOffsetUnits)),
        fearvr::Normalize(fearvr::Multiply(
            baseRotation,
            PhysicalMeleeLocalRotationFromDegrees(
                settings.rotationOffsetDegrees)))};
}

inline PhysicalMeleeRigidTransform ResolveToolMenuLeftHandIkTarget(
    const PhysicalMeleeRigidTransform& baseTarget,
    const fearvr::ArmIkTuning& tuning,
    float unitsPerMeter = 100.0F) noexcept {
    if (!PhysicalMeleeRigidTransformIsValid(baseTarget) ||
        !std::isfinite(unitsPerMeter) || unitsPerMeter <= 0.0F ||
        unitsPerMeter > 1000.0F) {
        return {{}, {0.0F, 0.0F, 0.0F, 0.0F}};
    }
    const fearvr::ArmIkTuning sanitized =
        fearvr::SanitizeArmIkTuning(tuning);
    const fearvr::TrackingQuaternion baseRotation =
        fearvr::Normalize(baseTarget.rotation);
    const fearvr::TrackingVector localOffsetUnits{
        sanitized.leftHandRightMeters * unitsPerMeter,
        sanitized.leftHandUpMeters * unitsPerMeter,
        sanitized.leftHandForwardMeters * unitsPerMeter};
    const fearvr::TrackingVector localRotationDegrees{
        sanitized.leftHandPitchDegrees,
        sanitized.leftHandYawDegrees,
        sanitized.leftHandRollDegrees};
    return {
        PhysicalMeleeAdd(
            baseTarget.positionUnits,
            fearvr::Rotate(baseRotation, localOffsetUnits)),
        fearvr::Normalize(fearvr::Multiply(
            baseRotation,
            PhysicalMeleeLocalRotationFromDegrees(
                localRotationDegrees)))};
}

inline bool ToolMenuMeleeSettingsAreValid(
    const ToolMenuMeleeSettings& settings) noexcept {
    return std::isfinite(settings.hitSpeedMetersPerSecond) &&
        settings.hitSpeedMetersPerSecond >= 0.25F &&
        settings.hitSpeedMetersPerSecond <= 10.0F &&
        std::isfinite(settings.contactRearmDistanceMeters) &&
        settings.contactRearmDistanceMeters >= 0.02F &&
        settings.contactRearmDistanceMeters <= 1.0F &&
        std::isfinite(settings.swingTriggerSpeedMetersPerSecond) &&
        settings.swingTriggerSpeedMetersPerSecond >= 0.50F &&
        settings.swingTriggerSpeedMetersPerSecond <= 10.0F &&
        std::isfinite(settings.swingRearmSpeedMetersPerSecond) &&
        settings.swingRearmSpeedMetersPerSecond >= 0.0F &&
        settings.swingRearmSpeedMetersPerSecond <
            settings.swingTriggerSpeedMetersPerSecond &&
        settings.swingPulseMilliseconds >= 30U &&
        settings.swingPulseMilliseconds <= 500U &&
        settings.swingCooldownMilliseconds >=
            settings.swingPulseMilliseconds &&
        settings.swingCooldownMilliseconds <= 3000U &&
        std::isfinite(settings.massKilograms) &&
        settings.massKilograms >= 0.5F &&
        settings.massKilograms <= 20.0F &&
        std::isfinite(settings.handlingWeight) &&
        settings.handlingWeight >= 0.10F &&
        settings.handlingWeight <= 4.0F &&
        std::isfinite(settings.positionalFollow) &&
        settings.positionalFollow >= 2.0F &&
        settings.positionalFollow <= 40.0F &&
        std::isfinite(settings.rotationalFollow) &&
        settings.rotationalFollow >= 2.0F &&
        settings.rotationalFollow <= 40.0F &&
        std::isfinite(settings.catchUpStrength) &&
        settings.catchUpStrength >= 0.0F &&
        settings.catchUpStrength <= 4.0F &&
        std::isfinite(settings.dampingRatio) &&
        settings.dampingRatio >= 0.35F &&
        settings.dampingRatio <= 1.0F;
}

inline bool ToolMenuProfileSupportsSwingAttack(
    PhysicalMeleeProfileId id) noexcept {
    return id == PhysicalMeleeProfileId::Pipe ||
        id == PhysicalMeleeProfileId::Crowbar ||
        id == PhysicalMeleeProfileId::FireAxe ||
        id == PhysicalMeleeProfileId::Plank ||
        id == PhysicalMeleeProfileId::OneHandedDebris;
}

inline const char* ToolMenuWeaponProfileLabel(
    PhysicalMeleeProfileId id) noexcept {
    switch (id) {
    case PhysicalMeleeProfileId::Pipe:
        return "PIPE";
    case PhysicalMeleeProfileId::Crowbar:
        return "CROWBAR";
    case PhysicalMeleeProfileId::FireAxe:
        return "FIRE AXE";
    case PhysicalMeleeProfileId::Plank:
        return "PLANK";
    case PhysicalMeleeProfileId::OneHandedDebris:
        return "ONE-HANDED";
    case PhysicalMeleeProfileId::GenericOneHanded:
        return "UNMAPPED WEAPON";
    default:
        return "INVALID PROFILE";
    }
}

inline ToolMenuMeleeSettings ToolMenuMeleeSettingsFromProfile(
    const PhysicalMeleeProfile& profile) noexcept {
    ToolMenuMeleeSettings settings{};
    if (!PhysicalMeleeProfileIsValid(profile)) {
        return settings;
    }
    settings.requireSwingForContactDamage =
        profile.requireSwingForContactDamage;
    settings.hitSpeedMetersPerSecond =
        profile.minimumImpactSpeedMetersPerSecond;
    settings.contactRearmDistanceMeters =
        profile.contactRearmSeparationMeters;
    settings.swingAttackEnabled = profile.swingAttackEnabled &&
        ToolMenuProfileSupportsSwingAttack(profile.id);
    settings.swingTriggerSpeedMetersPerSecond =
        profile.swingAttackTriggerSpeedMetersPerSecond;
    settings.swingRearmSpeedMetersPerSecond =
        profile.swingAttackRearmSpeedMetersPerSecond;
    settings.swingPulseMilliseconds =
        profile.swingAttackPulseMilliseconds;
    settings.swingCooldownMilliseconds =
        profile.swingAttackCooldownMilliseconds;
    settings.massKilograms = profile.massKilograms;
    settings.handlingWeight = profile.handlingWeight;
    settings.positionalFollow = profile.positionalFollow;
    settings.rotationalFollow = profile.rotationalFollow;
    settings.catchUpStrength = profile.catchUpStrength;
    settings.dampingRatio = profile.dampingRatio;
    return settings;
}

inline void ApplyToolMenuMeleeSettings(
    const ToolMenuMeleeSettings& settings,
    PhysicalMeleeProfile& profile) noexcept {
    if (!PhysicalMeleeProfileIsValid(profile) ||
        !ToolMenuMeleeSettingsAreValid(settings)) {
        return;
    }
    profile.requireSwingForContactDamage =
        settings.requireSwingForContactDamage;
    profile.minimumImpactSpeedMetersPerSecond =
        settings.hitSpeedMetersPerSecond;
    profile.contactRearmSeparationMeters =
        settings.contactRearmDistanceMeters;
    profile.swingAttackEnabled = settings.swingAttackEnabled &&
        ToolMenuProfileSupportsSwingAttack(profile.id);
    profile.swingAttackTriggerSpeedMetersPerSecond =
        settings.swingTriggerSpeedMetersPerSecond;
    profile.swingAttackRearmSpeedMetersPerSecond =
        settings.swingRearmSpeedMetersPerSecond;
    profile.swingAttackPulseMilliseconds =
        settings.swingPulseMilliseconds;
    profile.swingAttackCooldownMilliseconds =
        settings.swingCooldownMilliseconds;
    profile.massKilograms = settings.massKilograms;
    profile.handlingWeight = settings.handlingWeight;
    profile.positionalFollow = settings.positionalFollow;
    profile.rotationalFollow = settings.rotationalFollow;
    profile.catchUpStrength = settings.catchUpStrength;
    profile.dampingRatio = settings.dampingRatio;
}

// Tool settings are keyed only by Retail's stable weapon index. Runtime
// object/model pointers are deliberately excluded so dropping and reacquiring
// the same weapon keeps its tuning. The fixed-size LRU registry is bounded and
// contains no allocations in a render or input hook.
struct ToolMenuWeaponSettingsSlot {
    std::int32_t weaponIndex{-1};
    PhysicalMeleeProfileId profileId{
        PhysicalMeleeProfileId::GenericOneHanded};
    ToolMenuMeleeSettings settings{};
    PhysicalMeleeBlockPoseSettings blockPoseSettings{};
    ToolMenuBlockTimingSettings blockTimingSettings{};
    ToolMenuColliderSettings colliderSettings{};
    ToolMenuColliderSettings blockColliderSettings{};
    ToolMenuRightHandIkSettings rightHandIkSettings{};
    std::uint64_t lastUsed{0U};
    bool occupied{false};
    bool persistentLoadAttempted{false};
    bool blockPosePersistentLoadAttempted{false};
    bool blockTimingPersistentLoadAttempted{false};
    bool colliderPersistentLoadAttempted{false};
    bool blockColliderPersistentLoadAttempted{false};
    bool blockColliderUsesAttackFallback{true};
    bool rightHandIkPersistentLoadAttempted{false};
};

constexpr std::size_t kToolMenuWeaponSettingsSlotCount = 64U;

struct ToolMenuWeaponSettingsRegistry {
    std::array<
        ToolMenuWeaponSettingsSlot,
        kToolMenuWeaponSettingsSlotCount> slots{};
    std::uint64_t useSequence{0U};
};

inline ToolMenuWeaponSettingsSlot* FindToolMenuWeaponSettingsSlot(
    ToolMenuWeaponSettingsRegistry& registry,
    std::int32_t weaponIndex) noexcept {
    if (weaponIndex < 0) {
        return nullptr;
    }
    for (ToolMenuWeaponSettingsSlot& slot : registry.slots) {
        if (slot.occupied && slot.weaponIndex == weaponIndex) {
            return &slot;
        }
    }
    return nullptr;
}

inline ToolMenuWeaponSettingsSlot* ResolveToolMenuWeaponSettingsSlot(
    ToolMenuWeaponSettingsRegistry& registry,
    std::int32_t weaponIndex,
    const PhysicalMeleeProfile& baseProfile) noexcept {
    if (weaponIndex < 0 || !PhysicalMeleeProfileIsValid(baseProfile)) {
        return nullptr;
    }
    if (ToolMenuWeaponSettingsSlot* slot =
            FindToolMenuWeaponSettingsSlot(registry, weaponIndex)) {
        if (slot->profileId != baseProfile.id) {
            slot->profileId = baseProfile.id;
            slot->settings = ToolMenuMeleeSettingsFromProfile(baseProfile);
            slot->blockPoseSettings = {};
            slot->blockTimingSettings = {};
            slot->colliderSettings =
                ToolMenuColliderSettingsFromProfile(baseProfile);
            slot->blockColliderSettings = slot->colliderSettings;
            slot->rightHandIkSettings = {};
            slot->persistentLoadAttempted = false;
            slot->blockPosePersistentLoadAttempted = false;
            slot->blockTimingPersistentLoadAttempted = false;
            slot->colliderPersistentLoadAttempted = false;
            slot->blockColliderPersistentLoadAttempted = false;
            slot->blockColliderUsesAttackFallback = true;
            slot->rightHandIkPersistentLoadAttempted = false;
        }
        slot->lastUsed = ++registry.useSequence;
        return slot;
    }

    ToolMenuWeaponSettingsSlot* replacement = nullptr;
    for (ToolMenuWeaponSettingsSlot& slot : registry.slots) {
        if (!slot.occupied) {
            replacement = &slot;
            break;
        }
        if (replacement == nullptr ||
            slot.lastUsed < replacement->lastUsed) {
            replacement = &slot;
        }
    }
    if (replacement == nullptr) {
        return nullptr;
    }
    *replacement = {};
    replacement->weaponIndex = weaponIndex;
    replacement->profileId = baseProfile.id;
    replacement->settings = ToolMenuMeleeSettingsFromProfile(baseProfile);
    replacement->colliderSettings =
        ToolMenuColliderSettingsFromProfile(baseProfile);
    replacement->blockColliderSettings =
        replacement->colliderSettings;
    replacement->lastUsed = ++registry.useSequence;
    replacement->occupied = true;
    return replacement;
}

struct ToolMenuMeleeTelemetry {
    float contactSpeedMetersPerSecond{0.0F};
    float contactReleaseSpeedMetersPerSecond{0.0F};
    float swingSpeedMetersPerSecond{0.0F};
    std::uint32_t triggerCount{0U};
    std::uint32_t contactCallbackCount{0U};
    std::uint32_t damageDispatchCount{0U};
    std::uint32_t contactReleaseSampleCount{0U};
    std::int32_t weaponIndex{-1};
    char weaponName[kRetailWeaponNameCapacity]{};
    char weaponAnimationProperty[
        kRetailWeaponAnimationPropertyCapacity]{};
    RetailWeaponPoseFamily weaponPoseFamily{
        RetailWeaponPoseFamily::Unknown};
    bool weaponNameResolved{false};
    bool weaponAnimationPropertyResolved{false};
    bool contactTrackingFresh{false};
    bool contactFastEnough{false};
    bool contactLatched{false};
    bool contactRearmTravelReady{false};
    bool trackingFresh{false};
    bool wallProxyEnabled{false};
    bool visualProxyEnabled{false};
    bool contactDamageEnabled{false};
    bool colliderDebugEnabled{false};
    bool collisionBodyLive{false};
    bool twoHandedEnabled{false};
    bool secondaryGripAttached{false};
    float secondaryGripDistanceMeters{0.0F};
    float secondaryGripAnchorErrorMeters{0.0F};
    float blockPosePositionErrorMeters{0.0F};
    float blockPoseAngleErrorDegrees{0.0F};
    float lastRetailBlockWindowMilliseconds{0.0F};
    float lastAppliedBlockWindowMilliseconds{0.0F};
    std::uint32_t blockPoseActivationCount{0U};
    bool blockPoseTrackingFresh{false};
    bool blockPoseActive{false};
    bool blockCollisionBodyLive{false};
    bool blockWindowOverrideApplied{false};
};

inline bool ToolMenuToggleChordDown(
    const FearVrInputState& input,
    bool sampleFresh) noexcept {
    return fearvr::IsInputStateUsable(input, sampleFresh) &&
        (input.activeHands &
         (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT)) ==
            (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT) &&
        std::isfinite(input.squeeze[FEARVR_HAND_LEFT]) &&
        std::isfinite(input.squeeze[FEARVR_HAND_RIGHT]) &&
        input.squeeze[FEARVR_HAND_LEFT] >= 0.75F &&
        input.squeeze[FEARVR_HAND_RIGHT] >= 0.75F &&
        (input.buttons & FEARVR_IB_LEFT_SECONDARY) != 0U;
}

constexpr bool ShouldActivateToolMenuShortcut(
    bool menuOpen,
    bool shortcutEnabled,
    bool shortcutRequested) noexcept {
    return shortcutRequested && (menuOpen || shortcutEnabled);
}

constexpr bool ShouldCaptureToolMenuShortcutInput(
    bool menuOpen,
    bool releaseCapture,
    bool shortcutEnabled,
    bool toggleChordDown) noexcept {
    return menuOpen || releaseCapture ||
        (shortcutEnabled && toggleChordDown);
}

constexpr std::size_t kToolMenuMaximumTriangleVertices =
    FEARVR_OVERLAY_TRIANGLE_MAX_INPUT_VERTICES;

constexpr float kToolMenuDefaultScale = 0.62F;
constexpr float kToolMenuDefaultDistanceMeters = 1.50F;
constexpr float kToolMenuDefaultVerticalOffsetNdc = -0.08F;

struct ToolMenuPanelPlacement {
    float scale{kToolMenuDefaultScale};
    float distanceMeters{kToolMenuDefaultDistanceMeters};
    float verticalOffsetNdc{kToolMenuDefaultVerticalOffsetNdc};
};

struct ToolMenuPanelTransform {
    float scale{1.0F};
    float horizontalOffsetNdc{0.0F};
    float verticalOffsetNdc{0.0F};
    bool valid{false};
};

inline ToolMenuPanelTransform ResolveToolMenuPanelTransform(
    std::uint32_t eye,
    float interpupillaryDistanceMeters,
    float horizontalFovRadians,
    const ToolMenuPanelPlacement& placement = {}) noexcept {
    ToolMenuPanelTransform transform{};
    if (eye >= FEARVR_EYE_COUNT ||
        !std::isfinite(interpupillaryDistanceMeters) ||
        interpupillaryDistanceMeters < 0.03F ||
        interpupillaryDistanceMeters > 0.10F ||
        !std::isfinite(horizontalFovRadians) ||
        horizontalFovRadians <= 0.50F ||
        horizontalFovRadians >= 2.80F ||
        !std::isfinite(placement.scale) ||
        placement.scale < 0.35F || placement.scale > 1.0F ||
        !std::isfinite(placement.distanceMeters) ||
        placement.distanceMeters < 0.50F ||
        placement.distanceMeters > 5.0F ||
        !std::isfinite(placement.verticalOffsetNdc) ||
        std::fabs(placement.verticalOffsetNdc) > 0.50F) {
        return transform;
    }
    const float tangent = std::tan(horizontalFovRadians * 0.5F);
    if (!std::isfinite(tangent) || tangent <= 0.0F) {
        return transform;
    }
    const float convergenceOffset =
        interpupillaryDistanceMeters * 0.5F /
        (placement.distanceMeters * tangent);
    if (!std::isfinite(convergenceOffset) ||
        convergenceOffset < 0.0F || convergenceOffset > 0.25F) {
        return transform;
    }
    transform.scale = placement.scale;
    // Crossed disparity: the left-eye image moves right and the right-eye
    // image moves left, converging the head-relative panel at the requested
    // comfortable distance instead of optical infinity.
    transform.horizontalOffsetNdc =
        eye == FEARVR_EYE_LEFT
            ? convergenceOffset
            : -convergenceOffset;
    transform.verticalOffsetNdc = placement.verticalOffsetNdc;
    transform.valid = true;
    return transform;
}

struct ToolMenuOverlay {
    std::array<
        FearVrOverlayLineVertex,
        kToolMenuMaximumTriangleVertices> vertices{};
    std::size_t count{0U};
    bool overflowed{false};
};

inline bool ApplyToolMenuPanelTransform(
    ToolMenuOverlay& overlay,
    const ToolMenuPanelTransform& transform) noexcept {
    if (!transform.valid || overlay.overflowed ||
        overlay.count > overlay.vertices.size()) {
        return false;
    }
    for (std::size_t index = 0; index < overlay.count; ++index) {
        FearVrOverlayLineVertex& vertex = overlay.vertices[index];
        vertex.ndcX = vertex.ndcX * transform.scale +
            transform.horizontalOffsetNdc;
        vertex.ndcY = vertex.ndcY * transform.scale +
            transform.verticalOffsetNdc;
        if (!std::isfinite(vertex.ndcX) ||
            !std::isfinite(vertex.ndcY) ||
            std::fabs(vertex.ndcX) > 1.0F ||
            std::fabs(vertex.ndcY) > 1.0F) {
            overlay.overflowed = true;
            return false;
        }
    }
    return true;
}

inline bool AddToolMenuTriangle(
    ToolMenuOverlay& overlay,
    float x0, float y0,
    float x1, float y1,
    float x2, float y2,
    std::uint32_t argb) noexcept {
    if (overlay.count + 3U > overlay.vertices.size() ||
        !std::isfinite(x0) || !std::isfinite(y0) ||
        !std::isfinite(x1) || !std::isfinite(y1) ||
        !std::isfinite(x2) || !std::isfinite(y2)) {
        overlay.overflowed = true;
        return false;
    }
    overlay.vertices[overlay.count++] = {x0, y0, argb};
    overlay.vertices[overlay.count++] = {x1, y1, argb};
    overlay.vertices[overlay.count++] = {x2, y2, argb};
    return true;
}

inline bool AddToolMenuRectangle(
    ToolMenuOverlay& overlay,
    float left, float top,
    float right, float bottom,
    std::uint32_t argb) noexcept {
    if (!(left < right) || !(bottom < top)) {
        overlay.overflowed = true;
        return false;
    }
    return AddToolMenuTriangle(
               overlay, left, top, right, top,
               right, bottom, argb) &&
        AddToolMenuTriangle(
            overlay, left, top, right, bottom,
            left, bottom, argb);
}

inline std::uint8_t ToolMenuGlyphRow(
    char character,
    std::size_t row) noexcept {
    if (row >= 7U) {
        return 0U;
    }
    if (character >= 'a' && character <= 'z') {
        character = static_cast<char>(character - 'a' + 'A');
    }
    static constexpr std::uint8_t letters[26][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
        {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
        {17,17,17,17,21,27,17}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}};
    static constexpr std::uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}};
    if (character >= 'A' && character <= 'Z') {
        return letters[static_cast<std::size_t>(character - 'A')][row];
    }
    if (character >= '0' && character <= '9') {
        return digits[static_cast<std::size_t>(character - '0')][row];
    }
    switch (character) {
    case '-': {
        static constexpr std::uint8_t glyph[7] = {0,0,0,14,0,0,0};
        return glyph[row];
    }
    case '.': {
        static constexpr std::uint8_t glyph[7] = {0,0,0,0,0,6,6};
        return glyph[row];
    }
    case ':': {
        static constexpr std::uint8_t glyph[7] = {0,6,6,0,6,6,0};
        return glyph[row];
    }
    case '/': {
        static constexpr std::uint8_t glyph[7] = {1,2,2,4,8,8,16};
        return glyph[row];
    }
    case '+': {
        static constexpr std::uint8_t glyph[7] = {0,4,4,31,4,4,0};
        return glyph[row];
    }
    case '%': {
        static constexpr std::uint8_t glyph[7] = {25,25,2,4,8,19,19};
        return glyph[row];
    }
    case '_':
        return row == 6U ? 31U : 0U;
    case '[':
        return row == 0U || row == 6U ? 14U : 8U;
    case ']':
        return row == 0U || row == 6U ? 14U : 2U;
    case '(':
        return row == 0U || row == 6U ? 2U : 4U;
    case ')':
        return row == 0U || row == 6U ? 8U : 4U;
    case '=':
        return row == 2U || row == 4U ? 31U : 0U;
    case '<': {
        static constexpr std::uint8_t glyph[7] = {1,2,4,8,4,2,1};
        return glyph[row];
    }
    case '>': {
        static constexpr std::uint8_t glyph[7] = {16,8,4,2,4,8,16};
        return glyph[row];
    }
    default:
        return 0U;
    }
}

inline bool AddToolMenuText(
    ToolMenuOverlay& overlay,
    float x, float y,
    float pixelWidth, float pixelHeight,
    const char* text,
    std::uint32_t argb) noexcept {
    if (text == nullptr || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(pixelWidth) || !std::isfinite(pixelHeight) ||
        pixelWidth <= 0.0F || pixelHeight <= 0.0F) {
        overlay.overflowed = true;
        return false;
    }
    const float originX = x;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor == '\n') {
            x = originX;
            y -= pixelHeight * 9.0F;
            continue;
        }
        for (std::size_t row = 0; row < 7U; ++row) {
            const std::uint8_t bits = ToolMenuGlyphRow(*cursor, row);
            std::size_t column = 0U;
            while (column < 5U) {
                const std::uint8_t mask = static_cast<std::uint8_t>(
                    1U << (4U - column));
                if ((bits & mask) == 0U) {
                    ++column;
                    continue;
                }
                const std::size_t runStart = column;
                do {
                    ++column;
                } while (column < 5U &&
                    (bits & static_cast<std::uint8_t>(
                        1U << (4U - column))) != 0U);
                const float left = x +
                    static_cast<float>(runStart) * pixelWidth;
                const float right = x +
                    static_cast<float>(column) * pixelWidth;
                const float top = y -
                    static_cast<float>(row) * pixelHeight;
                const float bottom = top - pixelHeight * 0.82F;
                if (!AddToolMenuRectangle(
                        overlay, left, top, right, bottom, argb)) {
                    return false;
                }
            }
        }
        x += pixelWidth * 6.0F;
    }
    return !overlay.overflowed;
}

} // namespace condemnedvr
