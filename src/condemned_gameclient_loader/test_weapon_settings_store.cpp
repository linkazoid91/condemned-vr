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
