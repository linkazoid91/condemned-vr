#pragma once

#include <cstdint>

namespace condemnedvr {

// CScreenOptions::OnCommand in the verified Retail GameClient.dll maps these
// commands to the stock child screens. Command 0x3A is the sole gap in its
// contiguous 0x37..0x3B dispatch table and falls through to CBaseScreen.
constexpr std::uint32_t kRetailOptionsDisplayCommand = 0x37U;
constexpr std::uint32_t kRetailOptionsAudioCommand = 0x38U;
constexpr std::uint32_t kRetailOptionsControlsCommand = 0x39U;
constexpr std::uint32_t kRetailOptionsVrSettingsCommand = 0x3AU;
constexpr std::uint32_t kRetailOptionsPerformanceCommand = 0x3BU;

constexpr int kRetailScreenDisplay = 22;
constexpr int kRetailScreenAudio = 23;
// Retail constructs and registers this otherwise-unlinked PC Game Options
// screen between Audio and Performance. The experimental VR Settings probe
// routes to it only for bounded capture/lifecycle evidence; live use of its
// dormant controls is not accepted as the final settings architecture.
constexpr int kRetailScreenGameOptions = 24;
constexpr int kRetailScreenPerformance = 25;
constexpr int kRetailScreenControls = 29;
constexpr int kNoRetailOptionsTargetScreen = -1;

// These commands are private to the isolated VR Settings host. They do not
// overlap CBaseScreen's navigation commands (1, 2, and 7), the stock Options
// dispatch range (0x37..0x3B), or dormant CScreenGame's command 0x41.
constexpr std::uint32_t kRetailVrSettingsDisplayCommand = 0x70U;
constexpr std::uint32_t kRetailVrSettingsFeaturesCommand = 0x71U;
constexpr std::uint32_t kRetailVrSettingsComfortCommand = 0x72U;
constexpr std::uint32_t kRetailVrSettingsDeveloperCommand = 0x73U;

enum class RetailVrSettingsCategory : std::uint8_t {
    None = 0,
    Display,
    VrFeatures,
    Comfort,
    DeveloperTools,
};

enum class RetailVrSettingsAction : std::uint8_t {
    None = 0,
    OpenDisplay,
    OpenVrFeatures,
    OpenComfort,
    ToggleDeveloperToolsShortcut,
};

constexpr int RetailOptionsTargetScreen(
    std::uint32_t command) noexcept {
    switch (command) {
    case kRetailOptionsDisplayCommand:
        return kRetailScreenDisplay;
    case kRetailOptionsAudioCommand:
        return kRetailScreenAudio;
    case kRetailOptionsControlsCommand:
        return kRetailScreenControls;
    case kRetailOptionsPerformanceCommand:
        return kRetailScreenPerformance;
    default:
        return kNoRetailOptionsTargetScreen;
    }
}

constexpr bool IsRetailVrSettingsCommand(
    std::uint32_t command) noexcept {
    return command == kRetailOptionsVrSettingsCommand &&
        RetailOptionsTargetScreen(command) ==
            kNoRetailOptionsTargetScreen;
}

constexpr int RetailVrSettingsTargetScreen(
    std::uint32_t command) noexcept {
    return IsRetailVrSettingsCommand(command)
        ? kRetailScreenGameOptions
        : kNoRetailOptionsTargetScreen;
}

constexpr RetailVrSettingsCategory RetailVrSettingsCategoryForCommand(
    std::uint32_t command) noexcept {
    switch (command) {
    case kRetailVrSettingsDisplayCommand:
        return RetailVrSettingsCategory::Display;
    case kRetailVrSettingsFeaturesCommand:
        return RetailVrSettingsCategory::VrFeatures;
    case kRetailVrSettingsComfortCommand:
        return RetailVrSettingsCategory::Comfort;
    case kRetailVrSettingsDeveloperCommand:
        return RetailVrSettingsCategory::DeveloperTools;
    default:
        return RetailVrSettingsCategory::None;
    }
}

constexpr bool IsRetailVrSettingsCategoryCommand(
    std::uint32_t command) noexcept {
    return RetailVrSettingsCategoryForCommand(command) !=
        RetailVrSettingsCategory::None;
}

constexpr RetailVrSettingsAction RetailVrSettingsActionForCommand(
    std::uint32_t command) noexcept {
    switch (command) {
    case kRetailVrSettingsDisplayCommand:
        return RetailVrSettingsAction::OpenDisplay;
    case kRetailVrSettingsFeaturesCommand:
        return RetailVrSettingsAction::OpenVrFeatures;
    case kRetailVrSettingsComfortCommand:
        return RetailVrSettingsAction::OpenComfort;
    case kRetailVrSettingsDeveloperCommand:
        return RetailVrSettingsAction::ToggleDeveloperToolsShortcut;
    default:
        return RetailVrSettingsAction::None;
    }
}

constexpr bool ShouldSuppressRetailVrSettingsCategoryCommand(
    std::uint32_t command,
    bool pageOpenedDuringSameVrAcceptEdge) noexcept {
    return pageOpenedDuringSameVrAcceptEdge &&
        IsRetailVrSettingsCategoryCommand(command);
}

} // namespace condemnedvr
