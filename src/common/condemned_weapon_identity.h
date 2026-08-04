#pragma once

#include <cstddef>

namespace condemnedvr {

constexpr std::size_t kRetailWeaponNameCapacity = 64U;
constexpr std::size_t kRetailWeaponAnimationPropertyCapacity = 64U;

// This is the family expressed by Retail's AnimationProperty database value,
// not a VR handling decision. Weapon-specific poses remain distinct so they
// can be measured before being assigned to reusable one/two-hand VR profiles.
enum class RetailWeaponPoseFamily {
    Unknown,
    OneHandedDebris,
    TwoHandedDebris,
    WeaponSpecific,
    Other
};

inline bool WeaponIdentityAsciiEqualsIgnoreCase(
    const char* left,
    const char* right) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (;;) {
        unsigned char leftCharacter =
            static_cast<unsigned char>(*left++);
        unsigned char rightCharacter =
            static_cast<unsigned char>(*right++);
        if (leftCharacter >= 'a' && leftCharacter <= 'z') {
            leftCharacter = static_cast<unsigned char>(
                leftCharacter - 'a' + 'A');
        }
        if (rightCharacter >= 'a' && rightCharacter <= 'z') {
            rightCharacter = static_cast<unsigned char>(
                rightCharacter - 'a' + 'A');
        }
        if (leftCharacter != rightCharacter) {
            return false;
        }
        if (leftCharacter == 0U) {
            return true;
        }
    }
}

inline RetailWeaponPoseFamily ClassifyRetailWeaponAnimationProperty(
    const char* animationProperty) noexcept {
    if (animationProperty == nullptr || animationProperty[0] == '\0') {
        return RetailWeaponPoseFamily::Unknown;
    }
    if (WeaponIdentityAsciiEqualsIgnoreCase(
            animationProperty, "WEAP_1HandedDebris")) {
        return RetailWeaponPoseFamily::OneHandedDebris;
    }
    if (WeaponIdentityAsciiEqualsIgnoreCase(
            animationProperty, "WEAP_2HandedDebris")) {
        return RetailWeaponPoseFamily::TwoHandedDebris;
    }
    if (WeaponIdentityAsciiEqualsIgnoreCase(
            animationProperty, "WEAP_Crowbar") ||
        WeaponIdentityAsciiEqualsIgnoreCase(
            animationProperty, "WEAP_FireAxe") ||
        WeaponIdentityAsciiEqualsIgnoreCase(
            animationProperty, "WEAP_Shovel") ||
        WeaponIdentityAsciiEqualsIgnoreCase(
            animationProperty, "WEAP_SledgeHammer") ||
        WeaponIdentityAsciiEqualsIgnoreCase(
            animationProperty, "WEAP_FireExtinguisher")) {
        return RetailWeaponPoseFamily::WeaponSpecific;
    }
    return RetailWeaponPoseFamily::Other;
}

inline const char* RetailWeaponPoseFamilyLabel(
    RetailWeaponPoseFamily family) noexcept {
    switch (family) {
    case RetailWeaponPoseFamily::OneHandedDebris:
        return "1-HANDED DEBRIS";
    case RetailWeaponPoseFamily::TwoHandedDebris:
        return "2-HANDED DEBRIS";
    case RetailWeaponPoseFamily::WeaponSpecific:
        return "WEAPON-SPECIFIC";
    case RetailWeaponPoseFamily::Other:
        return "OTHER";
    default:
        return "UNKNOWN";
    }
}

} // namespace condemnedvr
