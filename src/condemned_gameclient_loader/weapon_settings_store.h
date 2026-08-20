#pragma once

#include <cstdint>

#include "arm_ik.h"
#include "condemned_interaction_authoring.h"
#include "condemned_tool_menu.h"
#include "condemned_right_hand_ik.h"

namespace condemnedvr {

enum class WeaponSettingsStoreResult : std::uint8_t {
    Ok,
    InvalidArgument,
    PathUnavailable,
    NotFound,
    ReadFailed,
    ParseFailed,
    ProfileMismatch,
    WriteFailed
};

// Per-weapon visual alignment. The primary position is the absolute local
// grip point sampled from the Retail weapon model plus any live adjustment;
// rotation remains a local Euler correction over the authored base rotation.
// Secondary-grip values share the record because the GRIP and 2-HAND tabs
// edit the same runtime calibration slot.
struct WeaponGripSettings {
    fearvr::TrackingVector positionUnits{};
    fearvr::TrackingVector localRotationDegrees{};
    fearvr::TrackingVector secondaryGripOffsetUnits{};
    float secondaryGripGrabRadiusMeters{0.15F};
    bool secondaryGripEnabled{false};
};

// Persists the editable Melee/Weapon tabs by stable Retail player-weapon
// index. Runtime pointers and calibration objects are intentionally excluded.
// Reads prefer the writable LocalAppData record, then the project-authored
// condemnedvr-defaults.ini beside GameClient.dll. Writes always target the
// per-player record, so release defaults are never modified. A malformed or
// profile-mismatched player record fails closed instead of being hidden by a
// packaged value. CONDEMNEDVR_SETTINGS_PATH and
// CONDEMNEDVR_DEFAULT_SETTINGS_PATH override those paths for isolated tests
// and portable developer launches.
WeaponSettingsStoreResult LoadWeaponToolSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuMeleeSettings& settings) noexcept;

// Mapped one-handed weapons without a valid per-index record may temporarily
// read the accepted Pipe record. These fallback loads never write or copy the
// record; the first ordinary save for the equipped index shadows only that
// setting family. Malformed data and ineligible/mismatched identities remain
// fail-closed.
WeaponSettingsStoreResult
LoadWeaponToolSettingsWithPipeOneHandedFallback(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuMeleeSettings& settings,
    bool& inheritedPipeBaseline) noexcept;

WeaponSettingsStoreResult SaveWeaponToolSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const ToolMenuMeleeSettings& settings) noexcept;

// A captured guard is stored independently from hit tuning. The pose is
// head-yaw-relative and therefore portable across world movement/turning;
// missing mapped one-handed records may inherit the accepted Pipe baseline.
WeaponSettingsStoreResult LoadWeaponBlockPoseSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    PhysicalMeleeBlockPoseSettings& settings) noexcept;

WeaponSettingsStoreResult
LoadWeaponBlockPoseSettingsWithPipeOneHandedFallback(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    PhysicalMeleeBlockPoseSettings& settings,
    bool& inheritedPipeBaseline) noexcept;

WeaponSettingsStoreResult SaveWeaponBlockPoseSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const PhysicalMeleeBlockPoseSettings& settings) noexcept;

// Optional per-weapon override of Retail's finite native block window. The
// default record keeps the override disabled, preserving Retail timing.
WeaponSettingsStoreResult LoadWeaponBlockTimingSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuBlockTimingSettings& settings) noexcept;

WeaponSettingsStoreResult SaveWeaponBlockTimingSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const ToolMenuBlockTimingSettings& settings) noexcept;

// Stored independently so collider geometry can evolve without invalidating
// existing melee handling or hand-alignment tuning.
WeaponSettingsStoreResult LoadWeaponColliderSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuColliderSettings& settings) noexcept;

WeaponSettingsStoreResult
LoadWeaponColliderSettingsWithPipeOneHandedFallback(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuColliderSettings& settings,
    bool& inheritedPipeBaseline) noexcept;

WeaponSettingsStoreResult SaveWeaponColliderSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const ToolMenuColliderSettings& settings) noexcept;

// Block geometry is stored separately from the attack capsule. A missing
// record is intentionally reported as NotFound so the runtime can follow the
// weapon's current attack collider until the player edits block geometry.
WeaponSettingsStoreResult LoadWeaponBlockColliderSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuColliderSettings& settings) noexcept;

WeaponSettingsStoreResult SaveWeaponBlockColliderSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const ToolMenuColliderSettings& settings) noexcept;

// Stored independently from collision and handling so visual alignment can
// evolve without invalidating accepted melee tuning.
WeaponSettingsStoreResult LoadWeaponGripSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    WeaponGripSettings& settings) noexcept;

WeaponSettingsStoreResult
LoadWeaponGripSettingsWithPipeOneHandedFallback(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    WeaponGripSettings& settings,
    bool& inheritedPipeBaseline) noexcept;

WeaponSettingsStoreResult SaveWeaponGripSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const WeaponGripSettings& settings) noexcept;

// Phase-1 interaction authoring is keyed by both the stable Retail catalog
// index and the exact resolved catalog name. Unlike melee profile settings it
// never inherits another weapon's record: an absent or mismatched name remains
// unconfigured until explicitly captured for that exact held model.
WeaponSettingsStoreResult LoadMagazineInsertionSocketSettings(
    std::int32_t weaponIndex,
    const char* expectedWeaponName,
    MagazineInsertionSocketSettings& settings) noexcept;

WeaponSettingsStoreResult SaveMagazineInsertionSocketSettings(
    std::int32_t weaponIndex,
    const char* weaponName,
    const MagazineInsertionSocketSettings& settings) noexcept;

// Stored independently from the Melee/Weapon record so existing user tuning
// remains backward-compatible while hand alignment can evolve on its own.
WeaponSettingsStoreResult LoadWeaponRightHandIkSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuRightHandIkSettings& settings) noexcept;

WeaponSettingsStoreResult
LoadWeaponRightHandIkSettingsWithPipeOneHandedFallback(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuRightHandIkSettings& settings,
    bool& inheritedPipeBaseline) noexcept;

WeaponSettingsStoreResult SaveWeaponRightHandIkSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const ToolMenuRightHandIkSettings& settings) noexcept;

// Empty-hand alignment is global and controller-local. It must never be
// persisted under Retail's Unarmed weapon index because no held model owns
// that pose. Player values override the packaged identity/default record.
WeaponSettingsStoreResult LoadEmptyRightHandAlignmentSettings(
    EmptyRightHandAlignmentSettings& settings) noexcept;

WeaponSettingsStoreResult SaveEmptyRightHandAlignmentSettings(
    const EmptyRightHandAlignmentSettings& settings) noexcept;

// Debug visualization is a global user preference rather than weapon tuning.
// A missing preference uses the release-safe ToolMenuDebugDrawSettings
// defaults (all hidden); a successful menu change is saved immediately.
WeaponSettingsStoreResult LoadDebugDrawSettings(
    ToolMenuDebugDrawSettings& settings) noexcept;

WeaponSettingsStoreResult SaveDebugDrawSettings(
    const ToolMenuDebugDrawSettings& settings) noexcept;

// Developer Tools controls only whether the VR tool-menu shortcuts may open
// the overlay. The value is global, versioned, and persisted independently
// from diagnostic draw visibility and per-weapon calibration.
WeaponSettingsStoreResult LoadToolMenuShortcutEnabled(
    bool& enabled) noexcept;

WeaponSettingsStoreResult SaveToolMenuShortcutEnabled(
    bool enabled) noexcept;

// Player collision width is global locomotion tuning, not weapon geometry.
// Versioned values default to exact Retail width (1.0); malformed player
// overrides fail closed and never silently fall through to packaged data.
WeaponSettingsStoreResult LoadPlayerColliderSettings(
    PlayerColliderSettings& settings) noexcept;

WeaponSettingsStoreResult SavePlayerColliderSettings(
    const PlayerColliderSettings& settings) noexcept;

// Arm anatomy is global rather than weapon-specific. It lives in the same
// portable INI, but under its own section so changing weapons cannot move the
// elbow pole or continuity behavior.
WeaponSettingsStoreResult LoadArmIkTuning(
    fearvr::ArmIkTuning& tuning) noexcept;

WeaponSettingsStoreResult SaveArmIkTuning(
    const fearvr::ArmIkTuning& tuning) noexcept;

const char* WeaponSettingsStoreResultName(
    WeaponSettingsStoreResult result) noexcept;

} // namespace condemnedvr
