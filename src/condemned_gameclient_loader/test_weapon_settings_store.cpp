#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cmath>
#include <cstdio>

#include "weapon_settings_store.h"

namespace {

int Fail(const char* message) {
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

bool Near(float left, float right) {
    return std::fabs(left - right) <= 1.0e-5F;
}

} // namespace

int main() {
    using namespace condemnedvr;

    wchar_t temporaryDirectory[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, temporaryDirectory) == 0U) {
        return Fail("temporary settings path unavailable");
    }
    wchar_t path[MAX_PATH]{};
    if (swprintf_s(
            path, L"%scondemnedvr-weapon-settings-test-%lu.ini",
            temporaryDirectory,
            static_cast<unsigned long>(GetCurrentProcessId())) <= 0) {
        return Fail("temporary settings path formatting failed");
    }
    DeleteFileW(path);
    if (!SetEnvironmentVariableW(
            L"CONDEMNEDVR_SETTINGS_PATH", path)) {
        return Fail("settings path override failed");
    }

    ToolMenuMeleeSettings expected{};
    expected.swingAttackEnabled = false;
    expected.requireSwingForContactDamage = false;
    expected.hitSpeedMetersPerSecond = 2.75F;
    expected.contactRearmDistanceMeters = 0.22F;
    expected.swingTriggerSpeedMetersPerSecond = 4.25F;
    expected.swingRearmSpeedMetersPerSecond = 1.0F;
    expected.swingPulseMilliseconds = 130U;
    expected.swingCooldownMilliseconds = 650U;
    expected.massKilograms = 2.25F;
    expected.handlingWeight = 2.75F;
    expected.positionalFollow = 10.0F;
    expected.rotationalFollow = 8.0F;
    expected.catchUpStrength = 0.8F;
    expected.dampingRatio = 0.65F;
    if (SaveWeaponToolSettings(
            32, PhysicalMeleeProfileId::Pipe, expected) !=
        WeaponSettingsStoreResult::Ok) {
        return Fail("valid weapon settings must save");
    }

    ToolMenuMeleeSettings loaded{};
    if (LoadWeaponToolSettings(
            32, PhysicalMeleeProfileId::Pipe, loaded) !=
            WeaponSettingsStoreResult::Ok ||
        loaded.swingAttackEnabled ||
        loaded.requireSwingForContactDamage ||
        !Near(loaded.hitSpeedMetersPerSecond, 2.75F) ||
        !Near(loaded.contactRearmDistanceMeters, 0.22F) ||
        !Near(loaded.swingTriggerSpeedMetersPerSecond, 4.25F) ||
        loaded.swingPulseMilliseconds != 130U ||
        !Near(loaded.handlingWeight, 2.75F) ||
        !Near(loaded.dampingRatio, 0.65F)) {
        return Fail("saved weapon settings must round-trip exactly");
    }
    if (LoadWeaponToolSettings(
            32, PhysicalMeleeProfileId::Plank, loaded) !=
        WeaponSettingsStoreResult::ProfileMismatch) {
        return Fail("a changed profile identity must reject stale settings");
    }

    if (!WritePrivateProfileStringW(
            L"weapon_17", L"settings",
            L"1,3,1,3,0.75,100,450,4.5,4,10,8,0.8,0.55", path)) {
        return Fail("legacy weapon settings fixture must be writable");
    }
    ToolMenuMeleeSettings legacy{};
    if (LoadWeaponToolSettings(
            17, PhysicalMeleeProfileId::FireAxe, legacy) !=
            WeaponSettingsStoreResult::Ok ||
        !legacy.swingAttackEnabled ||
        !legacy.requireSwingForContactDamage ||
        !Near(legacy.hitSpeedMetersPerSecond, 1.25F) ||
        !Near(legacy.contactRearmDistanceMeters, 0.12F) ||
        !Near(legacy.massKilograms, 4.5F)) {
        return Fail(
            "version-one settings must load with safe hit defaults");
    }


    ToolMenuColliderSettings expectedCollider{};
    expectedCollider.positionOffsetUnits = {1.0F, -2.0F, 3.0F};
    expectedCollider.rotationOffsetDegrees = {10.0F, -20.0F, 30.0F};
    expectedCollider.lengthUnits = 84.5F;
    expectedCollider.radiusUnits = 5.5F;
    expectedCollider.reversed = true;
    if (SaveWeaponColliderSettings(
            32, PhysicalMeleeProfileId::Pipe, expectedCollider) !=
        WeaponSettingsStoreResult::Ok) {
        return Fail("valid collider settings must save");
    }
    ToolMenuColliderSettings loadedCollider{};
    if (LoadWeaponColliderSettings(
            32, PhysicalMeleeProfileId::Pipe, loadedCollider) !=
            WeaponSettingsStoreResult::Ok ||
        !Near(loadedCollider.positionOffsetUnits.x, 1.0F) ||
        !Near(loadedCollider.positionOffsetUnits.y, -2.0F) ||
        !Near(loadedCollider.positionOffsetUnits.z, 3.0F) ||
        !Near(loadedCollider.rotationOffsetDegrees.x, 10.0F) ||
        !Near(loadedCollider.rotationOffsetDegrees.y, -20.0F) ||
        !Near(loadedCollider.rotationOffsetDegrees.z, 30.0F) ||
        !Near(loadedCollider.lengthUnits, 84.5F) ||
        !Near(loadedCollider.radiusUnits, 5.5F) ||
        !loadedCollider.reversed) {
        return Fail("collider settings must round-trip exactly");
    }
    if (LoadWeaponColliderSettings(
            32, PhysicalMeleeProfileId::Plank, loadedCollider) !=
        WeaponSettingsStoreResult::ProfileMismatch) {
        return Fail(
            "collider settings must reject a changed profile identity");
    }

    WeaponGripSettings expectedGrip{};
    expectedGrip.positionUnits = {2.0F, 2.5F, -4.0F};
    expectedGrip.localRotationDegrees = {-25.0F, 10.0F, -5.0F};
    expectedGrip.secondaryGripOffsetUnits = {0.0F, 46.0F, 3.0F};
    expectedGrip.secondaryGripGrabRadiusMeters = 0.18F;
    expectedGrip.secondaryGripEnabled = true;
    if (SaveWeaponGripSettings(
            32, PhysicalMeleeProfileId::Pipe, expectedGrip) !=
        WeaponSettingsStoreResult::Ok) {
        return Fail("valid grip settings must save");
    }
    WeaponGripSettings loadedGrip{};
    if (LoadWeaponGripSettings(
            32, PhysicalMeleeProfileId::Pipe, loadedGrip) !=
            WeaponSettingsStoreResult::Ok ||
        !Near(loadedGrip.positionUnits.x, 2.0F) ||
        !Near(loadedGrip.positionUnits.y, 2.5F) ||
        !Near(loadedGrip.positionUnits.z, -4.0F) ||
        !Near(loadedGrip.localRotationDegrees.x, -25.0F) ||
        !Near(loadedGrip.localRotationDegrees.y, 10.0F) ||
        !Near(loadedGrip.localRotationDegrees.z, -5.0F) ||
        !Near(loadedGrip.secondaryGripOffsetUnits.y, 46.0F) ||
        !Near(loadedGrip.secondaryGripOffsetUnits.z, 3.0F) ||
        !Near(loadedGrip.secondaryGripGrabRadiusMeters, 0.18F) ||
        !loadedGrip.secondaryGripEnabled) {
        return Fail("grip settings must round-trip exactly");
    }
    if (LoadWeaponGripSettings(
            32, PhysicalMeleeProfileId::Plank, loadedGrip) !=
        WeaponSettingsStoreResult::ProfileMismatch) {
        return Fail(
            "grip settings must reject a changed profile identity");
    }
    expectedGrip.positionUnits.x = 300.01F;
    if (SaveWeaponGripSettings(
            32, PhysicalMeleeProfileId::Pipe, expectedGrip) !=
        WeaponSettingsStoreResult::InvalidArgument) {
        return Fail("out-of-range grip settings must not save");
    }
    if (!WritePrivateProfileStringW(
            L"weapon_11", L"grip",
            L"1,1,0,0,0,0,0,0,2,0,45,0,0.15", path)) {
        return Fail("invalid grip settings fixture must be writable");
    }
    if (LoadWeaponGripSettings(
            11, PhysicalMeleeProfileId::Plank, loadedGrip) !=
        WeaponSettingsStoreResult::ParseFailed) {
        return Fail("invalid grip settings must fail closed");
    }

    ToolMenuRightHandIkSettings expectedHandIk{};
    expectedHandIk.positionOffsetUnits = {1.25F, -2.5F, 3.75F};
    expectedHandIk.rotationOffsetDegrees = {15.0F, -30.0F, 45.0F};
    if (SaveWeaponRightHandIkSettings(
            32, PhysicalMeleeProfileId::Pipe, expectedHandIk) !=
        WeaponSettingsStoreResult::Ok) {
        return Fail("valid right-hand IK settings must save");
    }
    ToolMenuRightHandIkSettings loadedHandIk{};
    if (LoadWeaponRightHandIkSettings(
            32, PhysicalMeleeProfileId::Pipe, loadedHandIk) !=
            WeaponSettingsStoreResult::Ok ||
        !Near(loadedHandIk.positionOffsetUnits.x, 1.25F) ||
        !Near(loadedHandIk.positionOffsetUnits.y, -2.5F) ||
        !Near(loadedHandIk.positionOffsetUnits.z, 3.75F) ||
        !Near(loadedHandIk.rotationOffsetDegrees.x, 15.0F) ||
        !Near(loadedHandIk.rotationOffsetDegrees.y, -30.0F) ||
        !Near(loadedHandIk.rotationOffsetDegrees.z, 45.0F)) {
        return Fail("right-hand IK settings must round-trip exactly");
    }
    if (LoadWeaponRightHandIkSettings(
            32, PhysicalMeleeProfileId::Plank, loadedHandIk) !=
        WeaponSettingsStoreResult::ProfileMismatch) {
        return Fail(
            "right-hand IK settings must reject a changed profile identity");
    }

    constexpr std::int32_t inheritedWeaponIndex = 29;
    const PhysicalMeleeProfile inheritedProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(
            inheritedWeaponIndex);
    if (inheritedProfile.id !=
            PhysicalMeleeProfileId::OneHandedDebris) {
        return Fail("one-handed fallback fixture must be mapped");
    }
    bool inheritedPipeBaseline = false;
    ToolMenuMeleeSettings inheritedMelee{};
    if (LoadWeaponToolSettingsWithPipeOneHandedFallback(
            inheritedWeaponIndex, inheritedProfile.id,
            inheritedMelee, inheritedPipeBaseline) !=
            WeaponSettingsStoreResult::Ok ||
        !inheritedPipeBaseline ||
        inheritedMelee.swingAttackEnabled ||
        inheritedMelee.requireSwingForContactDamage ||
        !Near(inheritedMelee.hitSpeedMetersPerSecond, 2.75F) ||
        !Near(inheritedMelee.contactRearmDistanceMeters, 0.22F) ||
        !Near(inheritedMelee.handlingWeight, 2.75F)) {
        return Fail(
            "new one-handed melee settings must inherit the saved pipe record");
    }
    ToolMenuColliderSettings inheritedCollider{};
    if (LoadWeaponColliderSettingsWithPipeOneHandedFallback(
            inheritedWeaponIndex, inheritedProfile.id,
            inheritedCollider, inheritedPipeBaseline) !=
            WeaponSettingsStoreResult::Ok ||
        !inheritedPipeBaseline ||
        !Near(inheritedCollider.positionOffsetUnits.x, 1.0F) ||
        !Near(inheritedCollider.lengthUnits, 84.5F) ||
        !Near(inheritedCollider.radiusUnits, 5.5F) ||
        !inheritedCollider.reversed) {
        return Fail(
            "new one-handed collider settings must inherit the saved pipe record");
    }
    WeaponGripSettings inheritedGrip{};
    if (LoadWeaponGripSettingsWithPipeOneHandedFallback(
            inheritedWeaponIndex, inheritedProfile.id,
            inheritedGrip, inheritedPipeBaseline) !=
            WeaponSettingsStoreResult::Ok ||
        !inheritedPipeBaseline ||
        !Near(inheritedGrip.positionUnits.x, 2.0F) ||
        !Near(inheritedGrip.positionUnits.y, 2.5F) ||
        !Near(inheritedGrip.localRotationDegrees.x, -25.0F) ||
        !Near(inheritedGrip.secondaryGripGrabRadiusMeters, 0.18F)) {
        return Fail(
            "new one-handed grip settings must inherit the saved pipe record");
    }
    ToolMenuRightHandIkSettings inheritedHandIk{};
    if (LoadWeaponRightHandIkSettingsWithPipeOneHandedFallback(
            inheritedWeaponIndex, inheritedProfile.id,
            inheritedHandIk, inheritedPipeBaseline) !=
            WeaponSettingsStoreResult::Ok ||
        !inheritedPipeBaseline ||
        !Near(inheritedHandIk.positionOffsetUnits.x, 1.25F) ||
        !Near(inheritedHandIk.rotationOffsetDegrees.y, -30.0F)) {
        return Fail(
            "new one-handed hand IK must inherit the saved pipe record");
    }

    inheritedPipeBaseline = true;
    if (LoadWeaponToolSettingsWithPipeOneHandedFallback(
            inheritedWeaponIndex, PhysicalMeleeProfileId::Plank,
            inheritedMelee, inheritedPipeBaseline) !=
            WeaponSettingsStoreResult::NotFound ||
        inheritedPipeBaseline) {
        return Fail(
            "a mismatched target profile must not borrow pipe settings");
    }
    if (LoadWeaponToolSettingsWithPipeOneHandedFallback(
            7, PhysicalMeleeProfileId::GenericOneHanded,
            inheritedMelee, inheritedPipeBaseline) !=
            WeaponSettingsStoreResult::NotFound ||
        inheritedPipeBaseline) {
        return Fail(
            "ordinary firearms must not borrow one-handed melee settings");
    }

    ToolMenuMeleeSettings localOneHanded = expected;
    localOneHanded.hitSpeedMetersPerSecond = 6.5F;
    localOneHanded.handlingWeight = 3.25F;
    if (SaveWeaponToolSettings(
            inheritedWeaponIndex, inheritedProfile.id,
            localOneHanded) != WeaponSettingsStoreResult::Ok ||
        LoadWeaponToolSettingsWithPipeOneHandedFallback(
            inheritedWeaponIndex, inheritedProfile.id,
            inheritedMelee, inheritedPipeBaseline) !=
            WeaponSettingsStoreResult::Ok ||
        inheritedPipeBaseline ||
        !Near(inheritedMelee.hitSpeedMetersPerSecond, 6.5F) ||
        !Near(inheritedMelee.handlingWeight, 3.25F)) {
        return Fail(
            "a weapon-specific save must replace only that inherited record");
    }
    ToolMenuMeleeSettings unchangedPipe{};
    if (LoadWeaponToolSettings(
            kCondemnedPipeLeverWeaponIndex,
            PhysicalMeleeProfileId::Pipe, unchangedPipe) !=
            WeaponSettingsStoreResult::Ok ||
        !Near(unchangedPipe.hitSpeedMetersPerSecond, 2.75F) ||
        !Near(unchangedPipe.handlingWeight, 2.75F)) {
        return Fail(
            "editing an inherited weapon must leave the pipe record unchanged");
    }
    if (SaveWeaponToolSettings(
            30, PhysicalMeleeProfileId::GenericOneHanded,
            localOneHanded) != WeaponSettingsStoreResult::Ok ||
        LoadWeaponToolSettingsWithPipeOneHandedFallback(
            30, PhysicalMeleeProfileId::OneHandedDebris,
            inheritedMelee, inheritedPipeBaseline) !=
            WeaponSettingsStoreResult::Ok ||
        !inheritedPipeBaseline ||
        !Near(inheritedMelee.hitSpeedMetersPerSecond, 2.75F)) {
        return Fail(
            "a stale generic profile record must migrate through the pipe fallback");
    }

    fearvr::ArmIkTuning expectedArmIk{};
    expectedArmIk.elbowOutward = 1.35F;
    expectedArmIk.elbowDown = 0.60F;
    expectedArmIk.elbowBack = -0.25F;
    expectedArmIk.preserveElbowContinuity = false;
    expectedArmIk.leftHandRightMeters = 0.025F;
    expectedArmIk.leftHandUpMeters = -0.015F;
    expectedArmIk.leftHandForwardMeters = 0.040F;
    expectedArmIk.leftHandPitchDegrees = 15.0F;
    expectedArmIk.leftHandYawDegrees = -25.0F;
    expectedArmIk.leftHandRollDegrees = 35.0F;
    if (SaveArmIkTuning(expectedArmIk) !=
        WeaponSettingsStoreResult::Ok) {
        return Fail("valid global elbow IK tuning must save");
    }
    fearvr::ArmIkTuning loadedArmIk{};
    if (LoadArmIkTuning(loadedArmIk) !=
            WeaponSettingsStoreResult::Ok ||
        !Near(loadedArmIk.elbowOutward, 1.35F) ||
        !Near(loadedArmIk.elbowDown, 0.60F) ||
        !Near(loadedArmIk.elbowBack, -0.25F) ||
        loadedArmIk.preserveElbowContinuity ||
        !Near(loadedArmIk.leftHandRightMeters, 0.025F) ||
        !Near(loadedArmIk.leftHandUpMeters, -0.015F) ||
        !Near(loadedArmIk.leftHandForwardMeters, 0.040F) ||
        !Near(loadedArmIk.leftHandPitchDegrees, 15.0F) ||
        !Near(loadedArmIk.leftHandYawDegrees, -25.0F) ||
        !Near(loadedArmIk.leftHandRollDegrees, 35.0F)) {
        return Fail("global elbow IK tuning must round-trip exactly");
    }

    if (!WritePrivateProfileStringW(
            L"arm_ik", L"elbow", L"1,1.2,0.3,-0.1,1", path)) {
        return Fail("legacy elbow IK fixture must be writable");
    }
    fearvr::ArmIkTuning legacyArmIk{};
    if (LoadArmIkTuning(legacyArmIk) !=
            WeaponSettingsStoreResult::Ok ||
        !Near(legacyArmIk.elbowOutward, 1.2F) ||
        !Near(legacyArmIk.elbowDown, 0.3F) ||
        !Near(legacyArmIk.elbowBack, -0.1F) ||
        !legacyArmIk.preserveElbowContinuity ||
        !Near(legacyArmIk.leftHandRightMeters, 0.0F) ||
        !Near(legacyArmIk.leftHandPitchDegrees, 0.0F)) {
        return Fail(
            "version-one elbow records must load with safe left-hand defaults");
    }

    DeleteFileW(path);
    SetEnvironmentVariableW(L"CONDEMNEDVR_SETTINGS_PATH", nullptr);
    return 0;
}
