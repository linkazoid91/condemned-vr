#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "weapon_settings_store.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <iterator>

namespace condemnedvr {
namespace {

constexpr unsigned int kWeaponSettingsFormatVersion = 2U;
constexpr unsigned int kBlockPoseSettingsFormatVersion = 1U;
constexpr unsigned int kBlockTimingSettingsFormatVersion = 1U;
constexpr unsigned int kColliderSettingsFormatVersion = 1U;
constexpr unsigned int kEmptyRightHandAlignmentFormatVersion = 1U;
constexpr unsigned int kGripSettingsFormatVersion = 1U;
constexpr unsigned int kMagazineSocketSettingsFormatVersion = 1U;
constexpr unsigned int kRightHandIkSettingsFormatVersion = 1U;
constexpr unsigned int kArmIkTuningFormatVersion = 2U;
constexpr unsigned int kDebugDrawSettingsFormatVersion = 2U;
constexpr unsigned int kLegacyDebugDrawSettingsFormatVersion = 1U;
constexpr unsigned int kToolMenuShortcutSettingsFormatVersion = 1U;
constexpr unsigned int kPlayerColliderSettingsFormatVersion = 1U;
constexpr wchar_t kSettingsPathOverride[] =
    L"CONDEMNEDVR_SETTINGS_PATH";
constexpr wchar_t kDefaultSettingsPathOverride[] =
    L"CONDEMNEDVR_DEFAULT_SETTINGS_PATH";
constexpr wchar_t kSettingsDirectoryName[] = L"CondemnedVR";
constexpr wchar_t kSettingsFileName[] = L"weapon-settings.ini";
constexpr wchar_t kDefaultSettingsFileName[] =
    L"condemnedvr-defaults.ini";

bool ResolveModuleSiblingSettingsPath(
    const wchar_t* fileName,
    wchar_t (&path)[MAX_PATH]) noexcept {
    if (fileName == nullptr || fileName[0] == L'\0') {
        return false;
    }
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(
                &ResolveModuleSiblingSettingsPath),
            &module)) {
        return false;
    }
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0U || length >= MAX_PATH) {
        return false;
    }
    wchar_t* const separator = wcsrchr(path, L'\\');
    if (separator == nullptr) {
        return false;
    }
    *(separator + 1) = L'\0';
    return wcscat_s(path, fileName) == 0;
}

bool ResolveWeaponSettingsPath(
    wchar_t (&path)[MAX_PATH]) noexcept {
    const DWORD overrideLength = GetEnvironmentVariableW(
        kSettingsPathOverride, path, MAX_PATH);
    if (overrideLength > 0U) {
        return overrideLength < MAX_PATH;
    }

    wchar_t localAppData[MAX_PATH]{};
    const DWORD localLength = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (localLength == 0U || localLength >= MAX_PATH) {
        return ResolveModuleSiblingSettingsPath(
            kSettingsFileName, path);
    }
    wchar_t directory[MAX_PATH]{};
    const int directoryLength = swprintf_s(
        directory, L"%s\\%s", localAppData,
        kSettingsDirectoryName);
    if (directoryLength <= 0 || directoryLength >= MAX_PATH) {
        return false;
    }
    if (!CreateDirectoryW(directory, nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    const int pathLength = swprintf_s(
        path, L"%s\\%s", directory, kSettingsFileName);
    return pathLength > 0 && pathLength < MAX_PATH;
}

bool ResolvePackagedDefaultSettingsPath(
    wchar_t (&path)[MAX_PATH]) noexcept {
    const DWORD overrideLength = GetEnvironmentVariableW(
        kDefaultSettingsPathOverride, path, MAX_PATH);
    if (overrideLength > 0U) {
        return overrideLength < MAX_PATH;
    }
    return ResolveModuleSiblingSettingsPath(
        kDefaultSettingsFileName, path);
}

WeaponSettingsStoreResult ReadSettingsValue(
    const wchar_t* section,
    const wchar_t* key,
    wchar_t* value,
    std::size_t valueCapacity) noexcept {
    if (section == nullptr || section[0] == L'\0' ||
        key == nullptr || key[0] == L'\0' ||
        value == nullptr || valueCapacity < 2U ||
        valueCapacity > UINT32_MAX) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }

    const auto ReadFromPath =
        [&](const wchar_t* path) noexcept {
            value[0] = L'\0';
            const DWORD length = GetPrivateProfileStringW(
                section, key, L"", value,
                static_cast<DWORD>(valueCapacity), path);
            if (length == 0U) {
                return WeaponSettingsStoreResult::NotFound;
            }
            if (length >= valueCapacity - 1U) {
                value[0] = L'\0';
                return WeaponSettingsStoreResult::ReadFailed;
            }
            return WeaponSettingsStoreResult::Ok;
        };

    bool pathResolved = false;
    wchar_t playerPath[MAX_PATH]{};
    if (ResolveWeaponSettingsPath(playerPath)) {
        pathResolved = true;
        const WeaponSettingsStoreResult playerResult =
            ReadFromPath(playerPath);
        if (playerResult != WeaponSettingsStoreResult::NotFound) {
            return playerResult;
        }
    }

    wchar_t defaultPath[MAX_PATH]{};
    if (ResolvePackagedDefaultSettingsPath(defaultPath)) {
        pathResolved = true;
        if (playerPath[0] == L'\0' ||
            _wcsicmp(playerPath, defaultPath) != 0) {
            const WeaponSettingsStoreResult defaultResult =
                ReadFromPath(defaultPath);
            if (defaultResult != WeaponSettingsStoreResult::NotFound) {
                return defaultResult;
            }
        }
    }

    value[0] = L'\0';
    return pathResolved
        ? WeaponSettingsStoreResult::NotFound
        : WeaponSettingsStoreResult::PathUnavailable;
}

bool FormatWeaponSection(
    std::int32_t weaponIndex,
    wchar_t (&section)[32]) noexcept {
    return weaponIndex >= 0 &&
        swprintf_s(
            section, L"weapon_%ld",
            static_cast<long>(weaponIndex)) > 0;
}

bool CopyMagazineSocketWeaponNameToWide(
    const char* source,
    wchar_t (&destination)[
        kMagazineSocketWeaponNameCapacity]) noexcept {
    destination[0] = L'\0';
    if (!MagazineSocketWeaponNameIsValid(source)) {
        return false;
    }
    std::size_t index = 0U;
    for (; index + 1U < std::size(destination) &&
           source[index] != '\0'; ++index) {
        destination[index] =
            static_cast<unsigned char>(source[index]);
    }
    destination[index] = L'\0';
    return source[index] == '\0';
}

bool MagazineSocketWeaponNameMatches(
    const wchar_t* stored,
    const char* expected) noexcept {
    if (stored == nullptr ||
        !MagazineSocketWeaponNameIsValid(expected)) {
        return false;
    }
    std::size_t index = 0U;
    for (; index < kMagazineSocketWeaponNameCapacity; ++index) {
        const wchar_t left = stored[index];
        const unsigned char right =
            static_cast<unsigned char>(expected[index]);
        if (left != static_cast<wchar_t>(right)) {
            return false;
        }
        if (left == L'\0') {
            return true;
        }
    }
    return false;
}

bool WeaponGripSettingsAreValid(
    const WeaponGripSettings& settings) noexcept {
    constexpr float kMaximumPositionUnits = 300.0F;
    constexpr float kMinimumSecondaryLengthUnits = 5.0F;
    constexpr float kMaximumSecondaryLengthUnits = 300.0F;
    constexpr float kMinimumGrabRadiusMeters = 0.05F;
    constexpr float kMaximumGrabRadiusMeters = 0.50F;
    const float secondaryLengthSquared =
        settings.secondaryGripOffsetUnits.x *
            settings.secondaryGripOffsetUnits.x +
        settings.secondaryGripOffsetUnits.y *
            settings.secondaryGripOffsetUnits.y +
        settings.secondaryGripOffsetUnits.z *
            settings.secondaryGripOffsetUnits.z;
    return fearvr::IsFinite(settings.positionUnits) &&
        fearvr::IsFinite(settings.localRotationDegrees) &&
        fearvr::IsFinite(settings.secondaryGripOffsetUnits) &&
        std::isfinite(settings.secondaryGripGrabRadiusMeters) &&
        std::isfinite(secondaryLengthSquared) &&
        std::fabs(settings.positionUnits.x) <= kMaximumPositionUnits &&
        std::fabs(settings.positionUnits.y) <= kMaximumPositionUnits &&
        std::fabs(settings.positionUnits.z) <= kMaximumPositionUnits &&
        settings.localRotationDegrees.x >= -180.0F &&
        settings.localRotationDegrees.x <= 180.0F &&
        settings.localRotationDegrees.y >= -180.0F &&
        settings.localRotationDegrees.y <= 180.0F &&
        settings.localRotationDegrees.z >= -180.0F &&
        settings.localRotationDegrees.z <= 180.0F &&
        secondaryLengthSquared <=
            kMaximumSecondaryLengthUnits *
                kMaximumSecondaryLengthUnits &&
        (!settings.secondaryGripEnabled ||
         secondaryLengthSquared >=
             kMinimumSecondaryLengthUnits *
                 kMinimumSecondaryLengthUnits) &&
        settings.secondaryGripGrabRadiusMeters >=
            kMinimumGrabRadiusMeters &&
        settings.secondaryGripGrabRadiusMeters <=
            kMaximumGrabRadiusMeters;
}

bool CanInheritPipeOneHandedSettings(
    WeaponSettingsStoreResult result,
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId) noexcept {
    return (result == WeaponSettingsStoreResult::NotFound ||
            result == WeaponSettingsStoreResult::ProfileMismatch) &&
        ShouldInheritPipeOneHandedSettings(
            weaponIndex, expectedProfileId);
}

} // namespace

WeaponSettingsStoreResult LoadWeaponToolSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuMeleeSettings& settings) noexcept {
    if (weaponIndex < 0) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t section[32]{};
    if (!FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[512]{};
    const WeaponSettingsStoreResult readResult =
        ReadSettingsValue(
            section, L"settings", value, std::size(value));
    if (readResult != WeaponSettingsStoreResult::Ok) {
        return readResult;
    }

    unsigned int version = 0U;
    unsigned int profileId = 0U;
    unsigned int swingEnabled = 0U;
    unsigned int requireSwing = 1U;
    unsigned long pulseMilliseconds = 0UL;
    unsigned long cooldownMilliseconds = 0UL;
    ToolMenuMeleeSettings loaded{};
    const int fields = swscanf_s(
        value,
        L"%u,%u,%u,%f,%f,%lu,%lu,%f,%f,%f,%f,%f,%f,%u,%f,%f",
        &version, &profileId, &swingEnabled,
        &loaded.swingTriggerSpeedMetersPerSecond,
        &loaded.swingRearmSpeedMetersPerSecond,
        &pulseMilliseconds, &cooldownMilliseconds,
        &loaded.massKilograms, &loaded.handlingWeight,
        &loaded.positionalFollow, &loaded.rotationalFollow,
        &loaded.catchUpStrength, &loaded.dampingRatio,
        &requireSwing,
        &loaded.hitSpeedMetersPerSecond,
        &loaded.contactRearmDistanceMeters);
    const bool versionOne = version == 1U && fields == 13;
    const bool versionTwo =
        version == kWeaponSettingsFormatVersion && fields == 16;
    if ((!versionOne && !versionTwo) || swingEnabled > 1U ||
        pulseMilliseconds > UINT32_MAX ||
        cooldownMilliseconds > UINT32_MAX ||
        (versionTwo && requireSwing > 1U)) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    if (profileId != static_cast<unsigned int>(expectedProfileId)) {
        return WeaponSettingsStoreResult::ProfileMismatch;
    }
    loaded.requireSwingForContactDamage =
        versionTwo ? requireSwing != 0U : true;
    loaded.swingAttackEnabled = swingEnabled != 0U;
    loaded.swingPulseMilliseconds =
        static_cast<std::uint32_t>(pulseMilliseconds);
    loaded.swingCooldownMilliseconds =
        static_cast<std::uint32_t>(cooldownMilliseconds);
    if (!ToolMenuMeleeSettingsAreValid(loaded)) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    settings = loaded;
    return WeaponSettingsStoreResult::Ok;
}

WeaponSettingsStoreResult
LoadWeaponToolSettingsWithPipeOneHandedFallback(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuMeleeSettings& settings,
    bool& inheritedPipeBaseline) noexcept {
    inheritedPipeBaseline = false;
    const WeaponSettingsStoreResult localResult =
        LoadWeaponToolSettings(
            weaponIndex, expectedProfileId, settings);
    if (!CanInheritPipeOneHandedSettings(
            localResult, weaponIndex, expectedProfileId)) {
        return localResult;
    }
    const WeaponSettingsStoreResult pipeResult =
        LoadWeaponToolSettings(
            kCondemnedPipeLeverWeaponIndex,
            PhysicalMeleeProfileId::Pipe, settings);
    if (pipeResult == WeaponSettingsStoreResult::Ok) {
        inheritedPipeBaseline = true;
        return WeaponSettingsStoreResult::Ok;
    }
    return localResult;
}

WeaponSettingsStoreResult SaveWeaponToolSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const ToolMenuMeleeSettings& settings) noexcept {
    if (weaponIndex < 0 ||
        !ToolMenuMeleeSettingsAreValid(settings)) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t path[MAX_PATH]{};
    wchar_t section[32]{};
    if (!ResolveWeaponSettingsPath(path) ||
        !FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[512]{};
    const int length = swprintf_s(
        value,
        L"%u,%u,%u,%.9g,%.9g,%lu,%lu,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u,%.9g,%.9g",
        kWeaponSettingsFormatVersion,
        static_cast<unsigned int>(profileId),
        settings.swingAttackEnabled ? 1U : 0U,
        static_cast<double>(
            settings.swingTriggerSpeedMetersPerSecond),
        static_cast<double>(
            settings.swingRearmSpeedMetersPerSecond),
        static_cast<unsigned long>(settings.swingPulseMilliseconds),
        static_cast<unsigned long>(settings.swingCooldownMilliseconds),
        static_cast<double>(settings.massKilograms),
        static_cast<double>(settings.handlingWeight),
        static_cast<double>(settings.positionalFollow),
        static_cast<double>(settings.rotationalFollow),
        static_cast<double>(settings.catchUpStrength),
        static_cast<double>(settings.dampingRatio),
        settings.requireSwingForContactDamage ? 1U : 0U,
        static_cast<double>(settings.hitSpeedMetersPerSecond),
        static_cast<double>(settings.contactRearmDistanceMeters));
    if (length <= 0 ||
        static_cast<std::size_t>(length) >= std::size(value)) {
        return WeaponSettingsStoreResult::WriteFailed;
    }
    return WritePrivateProfileStringW(
               section, L"settings", value, path)
        ? WeaponSettingsStoreResult::Ok
        : WeaponSettingsStoreResult::WriteFailed;
}

WeaponSettingsStoreResult LoadWeaponBlockPoseSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    PhysicalMeleeBlockPoseSettings& settings) noexcept {
    if (weaponIndex < 0) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t section[32]{};
    if (!FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[384]{};
    const WeaponSettingsStoreResult readResult = ReadSettingsValue(
        section, L"block_pose", value, std::size(value));
    if (readResult != WeaponSettingsStoreResult::Ok) {
        return readResult;
    }

    unsigned int version = 0U;
    unsigned int profileId = 0U;
    unsigned int enabled = 0U;
    unsigned int captured = 0U;
    PhysicalMeleeBlockPoseSettings loaded{};
    const int fields = swscanf_s(
        value,
        L"%u,%u,%u,%u,%f,%f,%f,%f,%f,%f,%f,%f,%f",
        &version, &profileId, &enabled, &captured,
        &loaded.headRelativePositionMeters.x,
        &loaded.headRelativePositionMeters.y,
        &loaded.headRelativePositionMeters.z,
        &loaded.headRelativeRotation.x,
        &loaded.headRelativeRotation.y,
        &loaded.headRelativeRotation.z,
        &loaded.headRelativeRotation.w,
        &loaded.positionToleranceMeters,
        &loaded.angleToleranceDegrees);
    if (fields != 13 ||
        version != kBlockPoseSettingsFormatVersion ||
        enabled > 1U || captured > 1U) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    if (profileId != static_cast<unsigned int>(expectedProfileId)) {
        return WeaponSettingsStoreResult::ProfileMismatch;
    }
    loaded.enabled = enabled != 0U;
    loaded.captured = captured != 0U;
    if (!PhysicalMeleeBlockQuaternionIsValid(
            loaded.headRelativeRotation)) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    loaded.headRelativeRotation = fearvr::Normalize(
        loaded.headRelativeRotation);
    if (!PhysicalMeleeBlockPoseSettingsAreValid(loaded)) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    settings = loaded;
    return WeaponSettingsStoreResult::Ok;
}

WeaponSettingsStoreResult
LoadWeaponBlockPoseSettingsWithPipeOneHandedFallback(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    PhysicalMeleeBlockPoseSettings& settings,
    bool& inheritedPipeBaseline) noexcept {
    inheritedPipeBaseline = false;
    const WeaponSettingsStoreResult localResult =
        LoadWeaponBlockPoseSettings(
            weaponIndex, expectedProfileId, settings);
    if (!CanInheritPipeOneHandedSettings(
            localResult, weaponIndex, expectedProfileId)) {
        return localResult;
    }
    const WeaponSettingsStoreResult pipeResult =
        LoadWeaponBlockPoseSettings(
            kCondemnedPipeLeverWeaponIndex,
            PhysicalMeleeProfileId::Pipe, settings);
    if (pipeResult == WeaponSettingsStoreResult::Ok) {
        inheritedPipeBaseline = true;
        return WeaponSettingsStoreResult::Ok;
    }
    return localResult;
}

WeaponSettingsStoreResult SaveWeaponBlockPoseSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const PhysicalMeleeBlockPoseSettings& settings) noexcept {
    if (weaponIndex < 0 ||
        !PhysicalMeleeBlockPoseSettingsAreValid(settings)) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t path[MAX_PATH]{};
    wchar_t section[32]{};
    if (!ResolveWeaponSettingsPath(path) ||
        !FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    const fearvr::TrackingQuaternion rotation = fearvr::Normalize(
        settings.headRelativeRotation);
    wchar_t value[384]{};
    const int length = swprintf_s(
        value,
        L"%u,%u,%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g",
        kBlockPoseSettingsFormatVersion,
        static_cast<unsigned int>(profileId),
        settings.enabled ? 1U : 0U,
        settings.captured ? 1U : 0U,
        static_cast<double>(settings.headRelativePositionMeters.x),
        static_cast<double>(settings.headRelativePositionMeters.y),
        static_cast<double>(settings.headRelativePositionMeters.z),
        static_cast<double>(rotation.x),
        static_cast<double>(rotation.y),
        static_cast<double>(rotation.z),
        static_cast<double>(rotation.w),
        static_cast<double>(settings.positionToleranceMeters),
        static_cast<double>(settings.angleToleranceDegrees));
    if (length <= 0 ||
        static_cast<std::size_t>(length) >= std::size(value)) {
        return WeaponSettingsStoreResult::WriteFailed;
    }
    return WritePrivateProfileStringW(
               section, L"block_pose", value, path)
        ? WeaponSettingsStoreResult::Ok
        : WeaponSettingsStoreResult::WriteFailed;
}

WeaponSettingsStoreResult LoadWeaponBlockTimingSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuBlockTimingSettings& settings) noexcept {
    if (weaponIndex < 0) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t section[32]{};
    if (!FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[96]{};
    const WeaponSettingsStoreResult readResult =
        ReadSettingsValue(
            section, L"block_timing", value, std::size(value));
    if (readResult != WeaponSettingsStoreResult::Ok) {
        return readResult;
    }

    unsigned int version = 0U;
    unsigned int profileId = 0U;
    unsigned int overrideEnabled = 0U;
    unsigned int collisionWindowMilliseconds = 0U;
    const int fields = swscanf_s(
        value, L"%u,%u,%u,%u",
        &version, &profileId, &overrideEnabled,
        &collisionWindowMilliseconds);
    if (fields != 4 ||
        version != kBlockTimingSettingsFormatVersion ||
        overrideEnabled > 1U) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    if (profileId != static_cast<unsigned int>(expectedProfileId)) {
        return WeaponSettingsStoreResult::ProfileMismatch;
    }
    ToolMenuBlockTimingSettings loaded{};
    loaded.overrideEnabled = overrideEnabled != 0U;
    loaded.collisionWindowMilliseconds =
        collisionWindowMilliseconds;
    if (!ToolMenuBlockTimingSettingsAreValid(loaded)) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    settings = loaded;
    return WeaponSettingsStoreResult::Ok;
}

WeaponSettingsStoreResult SaveWeaponBlockTimingSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const ToolMenuBlockTimingSettings& settings) noexcept {
    if (weaponIndex < 0 ||
        !ToolMenuBlockTimingSettingsAreValid(settings)) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t path[MAX_PATH]{};
    wchar_t section[32]{};
    if (!ResolveWeaponSettingsPath(path) ||
        !FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[96]{};
    const int length = swprintf_s(
        value, L"%u,%u,%u,%u",
        kBlockTimingSettingsFormatVersion,
        static_cast<unsigned int>(profileId),
        settings.overrideEnabled ? 1U : 0U,
        settings.collisionWindowMilliseconds);
    if (length <= 0 ||
        static_cast<std::size_t>(length) >= std::size(value)) {
        return WeaponSettingsStoreResult::WriteFailed;
    }
    return WritePrivateProfileStringW(
               section, L"block_timing", value, path)
        ? WeaponSettingsStoreResult::Ok
        : WeaponSettingsStoreResult::WriteFailed;
}

WeaponSettingsStoreResult LoadWeaponColliderSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuColliderSettings& settings) noexcept {
    if (weaponIndex < 0) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t section[32]{};
    if (!FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[256]{};
    const WeaponSettingsStoreResult readResult =
        ReadSettingsValue(
            section, L"collider", value, std::size(value));
    if (readResult != WeaponSettingsStoreResult::Ok) {
        return readResult;
    }

    unsigned int version = 0U;
    unsigned int profileId = 0U;
    unsigned int reversed = 0U;
    ToolMenuColliderSettings loaded{};
    const int fields = swscanf_s(
        value, L"%u,%u,%f,%f,%f,%f,%f,%f,%f,%f,%u",
        &version, &profileId,
        &loaded.positionOffsetUnits.x,
        &loaded.positionOffsetUnits.y,
        &loaded.positionOffsetUnits.z,
        &loaded.rotationOffsetDegrees.x,
        &loaded.rotationOffsetDegrees.y,
        &loaded.rotationOffsetDegrees.z,
        &loaded.lengthUnits, &loaded.radiusUnits, &reversed);
    if (fields != 11 ||
        version != kColliderSettingsFormatVersion ||
        reversed > 1U) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    if (profileId != static_cast<unsigned int>(expectedProfileId)) {
        return WeaponSettingsStoreResult::ProfileMismatch;
    }
    loaded.reversed = reversed != 0U;
    if (!ToolMenuColliderSettingsAreValid(loaded)) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    settings = loaded;
    return WeaponSettingsStoreResult::Ok;
}

WeaponSettingsStoreResult
LoadWeaponColliderSettingsWithPipeOneHandedFallback(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuColliderSettings& settings,
    bool& inheritedPipeBaseline) noexcept {
    inheritedPipeBaseline = false;
    const WeaponSettingsStoreResult localResult =
        LoadWeaponColliderSettings(
            weaponIndex, expectedProfileId, settings);
    if (!CanInheritPipeOneHandedSettings(
            localResult, weaponIndex, expectedProfileId)) {
        return localResult;
    }
    const WeaponSettingsStoreResult pipeResult =
        LoadWeaponColliderSettings(
            kCondemnedPipeLeverWeaponIndex,
            PhysicalMeleeProfileId::Pipe, settings);
    if (pipeResult == WeaponSettingsStoreResult::Ok) {
        inheritedPipeBaseline = true;
        return WeaponSettingsStoreResult::Ok;
    }
    return localResult;
}

WeaponSettingsStoreResult SaveWeaponColliderSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const ToolMenuColliderSettings& settings) noexcept {
    if (weaponIndex < 0 ||
        !ToolMenuColliderSettingsAreValid(settings)) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t path[MAX_PATH]{};
    wchar_t section[32]{};
    if (!ResolveWeaponSettingsPath(path) ||
        !FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[256]{};
    const int length = swprintf_s(
        value,
        L"%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u",
        kColliderSettingsFormatVersion,
        static_cast<unsigned int>(profileId),
        static_cast<double>(settings.positionOffsetUnits.x),
        static_cast<double>(settings.positionOffsetUnits.y),
        static_cast<double>(settings.positionOffsetUnits.z),
        static_cast<double>(settings.rotationOffsetDegrees.x),
        static_cast<double>(settings.rotationOffsetDegrees.y),
        static_cast<double>(settings.rotationOffsetDegrees.z),
        static_cast<double>(settings.lengthUnits),
        static_cast<double>(settings.radiusUnits),
        settings.reversed ? 1U : 0U);
    if (length <= 0 ||
        static_cast<std::size_t>(length) >= std::size(value)) {
        return WeaponSettingsStoreResult::WriteFailed;
    }
    return WritePrivateProfileStringW(
               section, L"collider", value, path)
        ? WeaponSettingsStoreResult::Ok
        : WeaponSettingsStoreResult::WriteFailed;
}

WeaponSettingsStoreResult LoadWeaponBlockColliderSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuColliderSettings& settings) noexcept {
    if (weaponIndex < 0) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t section[32]{};
    if (!FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[256]{};
    const WeaponSettingsStoreResult readResult =
        ReadSettingsValue(
            section, L"block_collider", value, std::size(value));
    if (readResult != WeaponSettingsStoreResult::Ok) {
        return readResult;
    }

    unsigned int version = 0U;
    unsigned int profileId = 0U;
    unsigned int reversed = 0U;
    ToolMenuColliderSettings loaded{};
    const int fields = swscanf_s(
        value, L"%u,%u,%f,%f,%f,%f,%f,%f,%f,%f,%u",
        &version, &profileId,
        &loaded.positionOffsetUnits.x,
        &loaded.positionOffsetUnits.y,
        &loaded.positionOffsetUnits.z,
        &loaded.rotationOffsetDegrees.x,
        &loaded.rotationOffsetDegrees.y,
        &loaded.rotationOffsetDegrees.z,
        &loaded.lengthUnits, &loaded.radiusUnits, &reversed);
    if (fields != 11 ||
        version != kColliderSettingsFormatVersion ||
        reversed > 1U) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    if (profileId != static_cast<unsigned int>(expectedProfileId)) {
        return WeaponSettingsStoreResult::ProfileMismatch;
    }
    loaded.reversed = reversed != 0U;
    if (!ToolMenuColliderSettingsAreValid(loaded)) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    settings = loaded;
    return WeaponSettingsStoreResult::Ok;
}

WeaponSettingsStoreResult SaveWeaponBlockColliderSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const ToolMenuColliderSettings& settings) noexcept {
    if (weaponIndex < 0 ||
        !ToolMenuColliderSettingsAreValid(settings)) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t path[MAX_PATH]{};
    wchar_t section[32]{};
    if (!ResolveWeaponSettingsPath(path) ||
        !FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[256]{};
    const int length = swprintf_s(
        value,
        L"%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u",
        kColliderSettingsFormatVersion,
        static_cast<unsigned int>(profileId),
        static_cast<double>(settings.positionOffsetUnits.x),
        static_cast<double>(settings.positionOffsetUnits.y),
        static_cast<double>(settings.positionOffsetUnits.z),
        static_cast<double>(settings.rotationOffsetDegrees.x),
        static_cast<double>(settings.rotationOffsetDegrees.y),
        static_cast<double>(settings.rotationOffsetDegrees.z),
        static_cast<double>(settings.lengthUnits),
        static_cast<double>(settings.radiusUnits),
        settings.reversed ? 1U : 0U);
    if (length <= 0 ||
        static_cast<std::size_t>(length) >= std::size(value)) {
        return WeaponSettingsStoreResult::WriteFailed;
    }
    return WritePrivateProfileStringW(
               section, L"block_collider", value, path)
        ? WeaponSettingsStoreResult::Ok
        : WeaponSettingsStoreResult::WriteFailed;
}

WeaponSettingsStoreResult LoadWeaponGripSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    WeaponGripSettings& settings) noexcept {
    if (weaponIndex < 0) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t section[32]{};
    if (!FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[320]{};
    const WeaponSettingsStoreResult readResult =
        ReadSettingsValue(
            section, L"grip", value, std::size(value));
    if (readResult != WeaponSettingsStoreResult::Ok) {
        return readResult;
    }

    unsigned int version = 0U;
    unsigned int profileId = 0U;
    unsigned int secondaryEnabled = 0U;
    WeaponGripSettings loaded{};
    const int fields = swscanf_s(
        value,
        L"%u,%u,%f,%f,%f,%f,%f,%f,%u,%f,%f,%f,%f",
        &version, &profileId,
        &loaded.positionUnits.x,
        &loaded.positionUnits.y,
        &loaded.positionUnits.z,
        &loaded.localRotationDegrees.x,
        &loaded.localRotationDegrees.y,
        &loaded.localRotationDegrees.z,
        &secondaryEnabled,
        &loaded.secondaryGripOffsetUnits.x,
        &loaded.secondaryGripOffsetUnits.y,
        &loaded.secondaryGripOffsetUnits.z,
        &loaded.secondaryGripGrabRadiusMeters);
    if (fields != 13 || version != kGripSettingsFormatVersion ||
        secondaryEnabled > 1U) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    if (profileId != static_cast<unsigned int>(expectedProfileId)) {
        return WeaponSettingsStoreResult::ProfileMismatch;
    }
    loaded.secondaryGripEnabled = secondaryEnabled != 0U;
    if (!WeaponGripSettingsAreValid(loaded)) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    settings = loaded;
    return WeaponSettingsStoreResult::Ok;
}

WeaponSettingsStoreResult
LoadWeaponGripSettingsWithPipeOneHandedFallback(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    WeaponGripSettings& settings,
    bool& inheritedPipeBaseline) noexcept {
    inheritedPipeBaseline = false;
    const WeaponSettingsStoreResult localResult =
        LoadWeaponGripSettings(
            weaponIndex, expectedProfileId, settings);
    if (!CanInheritPipeOneHandedSettings(
            localResult, weaponIndex, expectedProfileId)) {
        return localResult;
    }
    const WeaponSettingsStoreResult pipeResult =
        LoadWeaponGripSettings(
            kCondemnedPipeLeverWeaponIndex,
            PhysicalMeleeProfileId::Pipe, settings);
    if (pipeResult == WeaponSettingsStoreResult::Ok) {
        inheritedPipeBaseline = true;
        return WeaponSettingsStoreResult::Ok;
    }
    return localResult;
}

WeaponSettingsStoreResult SaveWeaponGripSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const WeaponGripSettings& settings) noexcept {
    if (weaponIndex < 0 || !WeaponGripSettingsAreValid(settings)) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t path[MAX_PATH]{};
    wchar_t section[32]{};
    if (!ResolveWeaponSettingsPath(path) ||
        !FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[320]{};
    const int length = swprintf_s(
        value,
        L"%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u,%.9g,%.9g,%.9g,%.9g",
        kGripSettingsFormatVersion,
        static_cast<unsigned int>(profileId),
        static_cast<double>(settings.positionUnits.x),
        static_cast<double>(settings.positionUnits.y),
        static_cast<double>(settings.positionUnits.z),
        static_cast<double>(settings.localRotationDegrees.x),
        static_cast<double>(settings.localRotationDegrees.y),
        static_cast<double>(settings.localRotationDegrees.z),
        settings.secondaryGripEnabled ? 1U : 0U,
        static_cast<double>(settings.secondaryGripOffsetUnits.x),
        static_cast<double>(settings.secondaryGripOffsetUnits.y),
        static_cast<double>(settings.secondaryGripOffsetUnits.z),
        static_cast<double>(
            settings.secondaryGripGrabRadiusMeters));
    if (length <= 0 ||
        static_cast<std::size_t>(length) >= std::size(value)) {
        return WeaponSettingsStoreResult::WriteFailed;
    }
    return WritePrivateProfileStringW(section, L"grip", value, path)
        ? WeaponSettingsStoreResult::Ok
        : WeaponSettingsStoreResult::WriteFailed;
}

WeaponSettingsStoreResult LoadMagazineInsertionSocketSettings(
    std::int32_t weaponIndex,
    const char* expectedWeaponName,
    MagazineInsertionSocketSettings& settings) noexcept {
    if (weaponIndex < 0 ||
        !MagazineSocketWeaponNameIsValid(expectedWeaponName)) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t section[32]{};
    if (!FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[512]{};
    const WeaponSettingsStoreResult readResult =
        ReadSettingsValue(
            section, L"magazine_socket", value,
            std::size(value));
    if (readResult != WeaponSettingsStoreResult::Ok) {
        return readResult;
    }

    unsigned int version = 0U;
    unsigned int configured = 0U;
    wchar_t storedWeaponName[
        kMagazineSocketWeaponNameCapacity]{};
    wchar_t trailing[2]{};
    MagazineInsertionSocketSettings loaded{};
    const int fields = swscanf_s(
        value,
        L"%u,%63[^,],%u,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f %1s",
        &version,
        storedWeaponName,
        static_cast<unsigned int>(
            std::size(storedWeaponName)),
        &configured,
        &loaded.positionUnits.x,
        &loaded.positionUnits.y,
        &loaded.positionUnits.z,
        &loaded.rotationDegrees.x,
        &loaded.rotationDegrees.y,
        &loaded.rotationDegrees.z,
        &loaded.halfExtentsUnits.x,
        &loaded.halfExtentsUnits.y,
        &loaded.halfExtentsUnits.z,
        &loaded.approachLengthUnits,
        &loaded.snapDistanceUnits,
        &loaded.snapAngleDegrees,
        trailing,
        static_cast<unsigned int>(std::size(trailing)));
    if (fields != 15 ||
        version != kMagazineSocketSettingsFormatVersion ||
        configured > 1U) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    if (!MagazineSocketWeaponNameMatches(
            storedWeaponName, expectedWeaponName)) {
        return WeaponSettingsStoreResult::ProfileMismatch;
    }
    loaded.configured = configured != 0U;
    if (!MagazineInsertionSocketSettingsAreValid(loaded)) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    settings = loaded;
    return WeaponSettingsStoreResult::Ok;
}

WeaponSettingsStoreResult SaveMagazineInsertionSocketSettings(
    std::int32_t weaponIndex,
    const char* weaponName,
    const MagazineInsertionSocketSettings& settings) noexcept {
    if (weaponIndex < 0 ||
        !MagazineSocketWeaponNameIsValid(weaponName) ||
        !MagazineInsertionSocketSettingsAreValid(settings)) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t path[MAX_PATH]{};
    wchar_t section[32]{};
    wchar_t storedWeaponName[
        kMagazineSocketWeaponNameCapacity]{};
    if (!ResolveWeaponSettingsPath(path) ||
        !FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    if (!CopyMagazineSocketWeaponNameToWide(
            weaponName, storedWeaponName)) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t value[512]{};
    const int length = swprintf_s(
        value,
        L"%u,%s,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
        L"%.9g,%.9g,%.9g,%.9g,%.9g,%.9g",
        kMagazineSocketSettingsFormatVersion,
        storedWeaponName,
        settings.configured ? 1U : 0U,
        static_cast<double>(settings.positionUnits.x),
        static_cast<double>(settings.positionUnits.y),
        static_cast<double>(settings.positionUnits.z),
        static_cast<double>(settings.rotationDegrees.x),
        static_cast<double>(settings.rotationDegrees.y),
        static_cast<double>(settings.rotationDegrees.z),
        static_cast<double>(settings.halfExtentsUnits.x),
        static_cast<double>(settings.halfExtentsUnits.y),
        static_cast<double>(settings.halfExtentsUnits.z),
        static_cast<double>(settings.approachLengthUnits),
        static_cast<double>(settings.snapDistanceUnits),
        static_cast<double>(settings.snapAngleDegrees));
    if (length <= 0 ||
        static_cast<std::size_t>(length) >= std::size(value)) {
        return WeaponSettingsStoreResult::WriteFailed;
    }
    return WritePrivateProfileStringW(
               section, L"magazine_socket", value, path)
        ? WeaponSettingsStoreResult::Ok
        : WeaponSettingsStoreResult::WriteFailed;
}

WeaponSettingsStoreResult LoadWeaponRightHandIkSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuRightHandIkSettings& settings) noexcept {
    if (weaponIndex < 0) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t section[32]{};
    if (!FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[256]{};
    const WeaponSettingsStoreResult readResult =
        ReadSettingsValue(
            section, L"right_hand_ik", value, std::size(value));
    if (readResult != WeaponSettingsStoreResult::Ok) {
        return readResult;
    }

    unsigned int version = 0U;
    unsigned int profileId = 0U;
    ToolMenuRightHandIkSettings loaded{};
    const int fields = swscanf_s(
        value, L"%u,%u,%f,%f,%f,%f,%f,%f",
        &version, &profileId,
        &loaded.positionOffsetUnits.x,
        &loaded.positionOffsetUnits.y,
        &loaded.positionOffsetUnits.z,
        &loaded.rotationOffsetDegrees.x,
        &loaded.rotationOffsetDegrees.y,
        &loaded.rotationOffsetDegrees.z);
    if (fields != 8 ||
        version != kRightHandIkSettingsFormatVersion) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    if (profileId != static_cast<unsigned int>(expectedProfileId)) {
        return WeaponSettingsStoreResult::ProfileMismatch;
    }
    if (!ToolMenuRightHandIkSettingsAreValid(loaded)) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    settings = loaded;
    return WeaponSettingsStoreResult::Ok;
}

WeaponSettingsStoreResult
LoadWeaponRightHandIkSettingsWithPipeOneHandedFallback(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId expectedProfileId,
    ToolMenuRightHandIkSettings& settings,
    bool& inheritedPipeBaseline) noexcept {
    inheritedPipeBaseline = false;
    const WeaponSettingsStoreResult localResult =
        LoadWeaponRightHandIkSettings(
            weaponIndex, expectedProfileId, settings);
    if (!CanInheritPipeOneHandedSettings(
            localResult, weaponIndex, expectedProfileId)) {
        return localResult;
    }
    const WeaponSettingsStoreResult pipeResult =
        LoadWeaponRightHandIkSettings(
            kCondemnedPipeLeverWeaponIndex,
            PhysicalMeleeProfileId::Pipe, settings);
    if (pipeResult == WeaponSettingsStoreResult::Ok) {
        inheritedPipeBaseline = true;
        return WeaponSettingsStoreResult::Ok;
    }
    return localResult;
}

WeaponSettingsStoreResult SaveWeaponRightHandIkSettings(
    std::int32_t weaponIndex,
    PhysicalMeleeProfileId profileId,
    const ToolMenuRightHandIkSettings& settings) noexcept {
    if (weaponIndex < 0 ||
        !ToolMenuRightHandIkSettingsAreValid(settings)) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t path[MAX_PATH]{};
    wchar_t section[32]{};
    if (!ResolveWeaponSettingsPath(path) ||
        !FormatWeaponSection(weaponIndex, section)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[256]{};
    const int length = swprintf_s(
        value, L"%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g",
        kRightHandIkSettingsFormatVersion,
        static_cast<unsigned int>(profileId),
        static_cast<double>(settings.positionOffsetUnits.x),
        static_cast<double>(settings.positionOffsetUnits.y),
        static_cast<double>(settings.positionOffsetUnits.z),
        static_cast<double>(settings.rotationOffsetDegrees.x),
        static_cast<double>(settings.rotationOffsetDegrees.y),
        static_cast<double>(settings.rotationOffsetDegrees.z));
    if (length <= 0 ||
        static_cast<std::size_t>(length) >= std::size(value)) {
        return WeaponSettingsStoreResult::WriteFailed;
    }
    return WritePrivateProfileStringW(
               section, L"right_hand_ik", value, path)
        ? WeaponSettingsStoreResult::Ok
        : WeaponSettingsStoreResult::WriteFailed;
}

WeaponSettingsStoreResult LoadEmptyRightHandAlignmentSettings(
    EmptyRightHandAlignmentSettings& settings) noexcept {
    wchar_t value[256]{};
    const WeaponSettingsStoreResult readResult =
        ReadSettingsValue(
            L"arm_ik", L"empty_right_hand",
            value, std::size(value));
    if (readResult != WeaponSettingsStoreResult::Ok) {
        return readResult;
    }

    unsigned int version = 0U;
    EmptyRightHandAlignmentSettings loaded{};
    const int fields = swscanf_s(
        value, L"%u,%f,%f,%f,%f,%f,%f,%f",
        &version,
        &loaded.localPositionOffsetUnits.x,
        &loaded.localPositionOffsetUnits.y,
        &loaded.localPositionOffsetUnits.z,
        &loaded.localRotationOffset.x,
        &loaded.localRotationOffset.y,
        &loaded.localRotationOffset.z,
        &loaded.localRotationOffset.w);
    if (fields != 8 ||
        version != kEmptyRightHandAlignmentFormatVersion ||
        !EmptyRightHandAlignmentSettingsAreValid(loaded)) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    loaded.localRotationOffset =
        fearvr::Normalize(loaded.localRotationOffset);
    settings = loaded;
    return WeaponSettingsStoreResult::Ok;
}

WeaponSettingsStoreResult SaveEmptyRightHandAlignmentSettings(
    const EmptyRightHandAlignmentSettings& settings) noexcept {
    if (!EmptyRightHandAlignmentSettingsAreValid(settings)) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t path[MAX_PATH]{};
    if (!ResolveWeaponSettingsPath(path)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    const fearvr::TrackingQuaternion rotation =
        fearvr::Normalize(settings.localRotationOffset);
    wchar_t value[256]{};
    const int length = swprintf_s(
        value, L"%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g",
        kEmptyRightHandAlignmentFormatVersion,
        static_cast<double>(settings.localPositionOffsetUnits.x),
        static_cast<double>(settings.localPositionOffsetUnits.y),
        static_cast<double>(settings.localPositionOffsetUnits.z),
        static_cast<double>(rotation.x),
        static_cast<double>(rotation.y),
        static_cast<double>(rotation.z),
        static_cast<double>(rotation.w));
    if (length <= 0 ||
        static_cast<std::size_t>(length) >= std::size(value)) {
        return WeaponSettingsStoreResult::WriteFailed;
    }
    return WritePrivateProfileStringW(
               L"arm_ik", L"empty_right_hand", value, path)
        ? WeaponSettingsStoreResult::Ok
        : WeaponSettingsStoreResult::WriteFailed;
}

WeaponSettingsStoreResult LoadDebugDrawSettings(
    ToolMenuDebugDrawSettings& settings) noexcept {
    wchar_t value[64]{};
    const WeaponSettingsStoreResult readResult =
        ReadSettingsValue(
            L"debug", L"draw", value, std::size(value));
    if (readResult != WeaponSettingsStoreResult::Ok) {
        return readResult;
    }

    unsigned int version = 0U;
    unsigned int colliderVisible = 0U;
    unsigned int blockColliderVisible = 0U;
    unsigned int controllerVisible = 0U;
    const int fields = swscanf_s(
        value, L"%u,%u,%u,%u",
        &version, &colliderVisible, &blockColliderVisible,
        &controllerVisible);
    const bool legacy =
        version == kLegacyDebugDrawSettingsFormatVersion &&
        fields == 3;
    const bool current =
        version == kDebugDrawSettingsFormatVersion &&
        fields == 4;
    if ((!legacy && !current) || colliderVisible > 1U ||
        blockColliderVisible > 1U || controllerVisible > 1U) {
        return WeaponSettingsStoreResult::ParseFailed;
    }

    settings.colliderVisible = colliderVisible != 0U;
    settings.blockColliderVisible =
        current && blockColliderVisible != 0U;
    settings.controllerVisible = legacy
        ? blockColliderVisible != 0U
        : controllerVisible != 0U;
    return WeaponSettingsStoreResult::Ok;
}

WeaponSettingsStoreResult SaveDebugDrawSettings(
    const ToolMenuDebugDrawSettings& settings) noexcept {
    wchar_t path[MAX_PATH]{};
    if (!ResolveWeaponSettingsPath(path)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[64]{};
    const int length = swprintf_s(
        value, L"%u,%u,%u,%u",
        kDebugDrawSettingsFormatVersion,
        settings.colliderVisible ? 1U : 0U,
        settings.blockColliderVisible ? 1U : 0U,
        settings.controllerVisible ? 1U : 0U);
    if (length <= 0 ||
        static_cast<std::size_t>(length) >= std::size(value)) {
        return WeaponSettingsStoreResult::WriteFailed;
    }
    return WritePrivateProfileStringW(
               L"debug", L"draw", value, path)
        ? WeaponSettingsStoreResult::Ok
        : WeaponSettingsStoreResult::WriteFailed;
}

WeaponSettingsStoreResult LoadToolMenuShortcutEnabled(
    bool& enabled) noexcept {
    wchar_t value[32]{};
    const WeaponSettingsStoreResult readResult =
        ReadSettingsValue(
            L"developer", L"tool_menu_shortcut",
            value, std::size(value));
    if (readResult != WeaponSettingsStoreResult::Ok) {
        return readResult;
    }

    unsigned int version = 0U;
    unsigned int storedEnabled = 0U;
    const int fields = swscanf_s(
        value, L"%u,%u", &version, &storedEnabled);
    if (fields != 2 ||
        version != kToolMenuShortcutSettingsFormatVersion ||
        storedEnabled > 1U) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    enabled = storedEnabled != 0U;
    return WeaponSettingsStoreResult::Ok;
}

WeaponSettingsStoreResult SaveToolMenuShortcutEnabled(
    bool enabled) noexcept {
    wchar_t path[MAX_PATH]{};
    if (!ResolveWeaponSettingsPath(path)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[32]{};
    const int length = swprintf_s(
        value, L"%u,%u",
        kToolMenuShortcutSettingsFormatVersion,
        enabled ? 1U : 0U);
    if (length <= 0 ||
        static_cast<std::size_t>(length) >= std::size(value)) {
        return WeaponSettingsStoreResult::WriteFailed;
    }
    return WritePrivateProfileStringW(
               L"developer", L"tool_menu_shortcut", value, path)
        ? WeaponSettingsStoreResult::Ok
        : WeaponSettingsStoreResult::WriteFailed;
}

WeaponSettingsStoreResult LoadPlayerColliderSettings(
    PlayerColliderSettings& settings) noexcept {
    wchar_t value[64]{};
    const WeaponSettingsStoreResult readResult =
        ReadSettingsValue(
            L"player_collision", L"width_scale",
            value, std::size(value));
    if (readResult != WeaponSettingsStoreResult::Ok) {
        return readResult;
    }

    unsigned int version = 0U;
    PlayerColliderSettings loaded = settings;
    const int fields = swscanf_s(
        value, L"%u,%f", &version, &loaded.widthScale);
    if (fields != 2 ||
        version != kPlayerColliderSettingsFormatVersion ||
        !PlayerColliderSettingsAreValid(loaded)) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    settings = loaded;
    return WeaponSettingsStoreResult::Ok;
}

WeaponSettingsStoreResult SavePlayerColliderSettings(
    const PlayerColliderSettings& settings) noexcept {
    if (!PlayerColliderSettingsAreValid(settings)) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    wchar_t path[MAX_PATH]{};
    if (!ResolveWeaponSettingsPath(path)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[64]{};
    const int length = swprintf_s(
        value, L"%u,%.9g",
        kPlayerColliderSettingsFormatVersion,
        static_cast<double>(settings.widthScale));
    if (length <= 0 ||
        static_cast<std::size_t>(length) >= std::size(value)) {
        return WeaponSettingsStoreResult::WriteFailed;
    }
    return WritePrivateProfileStringW(
               L"player_collision", L"width_scale",
               value, path)
        ? WeaponSettingsStoreResult::Ok
        : WeaponSettingsStoreResult::WriteFailed;
}

WeaponSettingsStoreResult LoadArmIkTuning(
    fearvr::ArmIkTuning& tuning) noexcept {
    wchar_t value[192]{};
    const WeaponSettingsStoreResult readResult =
        ReadSettingsValue(
            L"arm_ik", L"elbow", value, std::size(value));
    if (readResult != WeaponSettingsStoreResult::Ok) {
        return readResult;
    }

    unsigned int version = 0U;
    unsigned int continuity = 0U;
    fearvr::ArmIkTuning loaded = tuning;
    const int fields = swscanf_s(
        value, L"%u,%f,%f,%f,%u,%f,%f,%f,%f,%f,%f",
        &version, &loaded.elbowOutward, &loaded.elbowDown,
        &loaded.elbowBack, &continuity,
        &loaded.leftHandRightMeters, &loaded.leftHandUpMeters,
        &loaded.leftHandForwardMeters,
        &loaded.leftHandPitchDegrees, &loaded.leftHandYawDegrees,
        &loaded.leftHandRollDegrees);
    const bool versionOne = version == 1U && fields == 5;
    const bool versionTwo =
        version == kArmIkTuningFormatVersion && fields == 11;
    if ((!versionOne && !versionTwo) ||
        continuity > 1U || !std::isfinite(loaded.elbowOutward) ||
        !std::isfinite(loaded.elbowDown) ||
        !std::isfinite(loaded.elbowBack) ||
        loaded.elbowOutward < 0.20F || loaded.elbowOutward > 2.0F ||
        loaded.elbowDown < 0.0F || loaded.elbowDown > 1.5F ||
        loaded.elbowBack < -1.0F || loaded.elbowBack > 1.0F ||
        !std::isfinite(loaded.leftHandRightMeters) ||
        !std::isfinite(loaded.leftHandUpMeters) ||
        !std::isfinite(loaded.leftHandForwardMeters) ||
        !std::isfinite(loaded.leftHandPitchDegrees) ||
        !std::isfinite(loaded.leftHandYawDegrees) ||
        !std::isfinite(loaded.leftHandRollDegrees) ||
        loaded.leftHandRightMeters < -0.20F ||
        loaded.leftHandRightMeters > 0.20F ||
        loaded.leftHandUpMeters < -0.20F ||
        loaded.leftHandUpMeters > 0.20F ||
        loaded.leftHandForwardMeters < -0.20F ||
        loaded.leftHandForwardMeters > 0.20F ||
        loaded.leftHandPitchDegrees < -180.0F ||
        loaded.leftHandPitchDegrees > 180.0F ||
        loaded.leftHandYawDegrees < -180.0F ||
        loaded.leftHandYawDegrees > 180.0F ||
        loaded.leftHandRollDegrees < -180.0F ||
        loaded.leftHandRollDegrees > 180.0F) {
        return WeaponSettingsStoreResult::ParseFailed;
    }
    loaded.preserveElbowContinuity = continuity != 0U;
    tuning = loaded;
    return WeaponSettingsStoreResult::Ok;
}

WeaponSettingsStoreResult SaveArmIkTuning(
    const fearvr::ArmIkTuning& tuning) noexcept {
    const fearvr::ArmIkTuning sanitized =
        fearvr::SanitizeArmIkTuning(tuning);
    wchar_t path[MAX_PATH]{};
    if (!ResolveWeaponSettingsPath(path)) {
        return WeaponSettingsStoreResult::PathUnavailable;
    }
    wchar_t value[192]{};
    const int length = swprintf_s(
        value,
        L"%u,%.9g,%.9g,%.9g,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g",
        kArmIkTuningFormatVersion,
        static_cast<double>(sanitized.elbowOutward),
        static_cast<double>(sanitized.elbowDown),
        static_cast<double>(sanitized.elbowBack),
        sanitized.preserveElbowContinuity ? 1U : 0U,
        static_cast<double>(sanitized.leftHandRightMeters),
        static_cast<double>(sanitized.leftHandUpMeters),
        static_cast<double>(sanitized.leftHandForwardMeters),
        static_cast<double>(sanitized.leftHandPitchDegrees),
        static_cast<double>(sanitized.leftHandYawDegrees),
        static_cast<double>(sanitized.leftHandRollDegrees));
    if (length <= 0 ||
        static_cast<std::size_t>(length) >= std::size(value)) {
        return WeaponSettingsStoreResult::WriteFailed;
    }
    return WritePrivateProfileStringW(
               L"arm_ik", L"elbow", value, path)
        ? WeaponSettingsStoreResult::Ok
        : WeaponSettingsStoreResult::WriteFailed;
}

const char* WeaponSettingsStoreResultName(
    WeaponSettingsStoreResult result) noexcept {
    switch (result) {
    case WeaponSettingsStoreResult::Ok:
        return "ok";
    case WeaponSettingsStoreResult::InvalidArgument:
        return "invalid_argument";
    case WeaponSettingsStoreResult::PathUnavailable:
        return "path_unavailable";
    case WeaponSettingsStoreResult::NotFound:
        return "not_found";
    case WeaponSettingsStoreResult::ReadFailed:
        return "read_failed";
    case WeaponSettingsStoreResult::ParseFailed:
        return "parse_failed";
    case WeaponSettingsStoreResult::ProfileMismatch:
        return "profile_mismatch";
    case WeaponSettingsStoreResult::WriteFailed:
        return "write_failed";
    default:
        return "unknown";
    }
}

} // namespace condemnedvr
