#pragma once

#include <cstdint>

#include "arm_ik.h"
#include "condemned_tool_menu.h"

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
// CONDEMNEDVR_SETTINGS_PATH can override the normal LocalAppData path for
// isolated tests and portable developer launches.
WeaponSettingsStoreResult LoadWeaponToolSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuMeleeSettings& settings) noexcept;

WeaponSettingsStoreResult SaveWeaponToolSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const ToolMenuMeleeSettings& settings) noexcept;

// Stored independently so collider geometry can evolve without invalidating
// existing melee handling or hand-alignment tuning.
WeaponSettingsStoreResult LoadWeaponColliderSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuColliderSettings& settings) noexcept;

WeaponSettingsStoreResult SaveWeaponColliderSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const ToolMenuColliderSettings& settings) noexcept;

// Stored independently from collision and handling so visual alignment can
// evolve without invalidating accepted melee tuning.
WeaponSettingsStoreResult LoadWeaponGripSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    WeaponGripSettings& settings) noexcept;

WeaponSettingsStoreResult SaveWeaponGripSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const WeaponGripSettings& settings) noexcept;

// Stored independently from the Melee/Weapon record so existing user tuning
// remains backward-compatible while hand alignment can evolve on its own.
WeaponSettingsStoreResult LoadWeaponRightHandIkSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuRightHandIkSettings& settings) noexcept;

WeaponSettingsStoreResult SaveWeaponRightHandIkSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const ToolMenuRightHandIkSettings& settings) noexcept;

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
