#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "condemned_weapon_identity.h"

namespace condemnedvr {

enum class RetailWeaponIdentityReadResult {
    Ok,
    InvalidIndex,
    DatabaseModuleUnavailable,
    DatabaseExportUnavailable,
    DatabaseManagerUnavailable,
    DatabaseInterfaceMismatch,
    DatabaseUnavailable,
    GlobalCategoryUnavailable,
    GlobalRecordUnavailable,
    PlayerWeaponsUnavailable,
    WeaponRecordUnavailable,
    WeaponNameUnavailable,
    WeaponDataUnavailable,
    AnimationPropertyUnavailable,
    AccessViolation
};

struct RetailWeaponIdentitySnapshot {
    std::int32_t playerWeaponIndex{-1};
    char recordName[kRetailWeaponNameCapacity]{};
    char animationProperty[
        kRetailWeaponAnimationPropertyCapacity]{};
    RetailWeaponPoseFamily poseFamily{
        RetailWeaponPoseFamily::Unknown};
    bool nameResolved{false};
    bool animationPropertyResolved{false};
};

constexpr std::size_t kRetailWeaponCatalogCapacity = 256U;

struct RetailWeaponIdentityCatalog {
    std::array<
        RetailWeaponIdentitySnapshot,
        kRetailWeaponCatalogCapacity> entries{};
    std::uint32_t count{0U};
};

// Resolves only already-loaded Retail database data. It opens an additional
// read reference to Database\Dark.Gamdb00p for the duration of the lookup and
// releases it after copying the strings into caller-owned storage.
RetailWeaponIdentityReadResult ReadRetailWeaponIdentity(
    std::int32_t playerWeaponIndex,
    RetailWeaponIdentitySnapshot& snapshot) noexcept;

// Opt-in discovery helper for bringing a new Retail weapon into the measured
// VR catalog. It is never used by the per-frame path.
RetailWeaponIdentityReadResult ReadRetailWeaponIdentityCatalog(
    RetailWeaponIdentityCatalog& catalog) noexcept;

const char* RetailWeaponIdentityReadResultName(
    RetailWeaponIdentityReadResult result) noexcept;

} // namespace condemnedvr
