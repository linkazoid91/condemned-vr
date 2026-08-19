#include <cstdio>
#include <initializer_list>

#include "condemned_retail_menu.h"

namespace {

int Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

} // namespace

int main() {
    using condemnedvr::RetailOptionsTargetScreen;

    if (RetailOptionsTargetScreen(
            condemnedvr::kRetailOptionsDisplayCommand) !=
            condemnedvr::kRetailScreenDisplay ||
        RetailOptionsTargetScreen(
            condemnedvr::kRetailOptionsAudioCommand) !=
            condemnedvr::kRetailScreenAudio ||
        RetailOptionsTargetScreen(
            condemnedvr::kRetailOptionsControlsCommand) !=
            condemnedvr::kRetailScreenControls ||
        RetailOptionsTargetScreen(
            condemnedvr::kRetailOptionsPerformanceCommand) !=
            condemnedvr::kRetailScreenPerformance) {
        return Fail("stock Options commands must retain their Retail screens");
    }

    if (!condemnedvr::IsRetailVrSettingsCommand(
            condemnedvr::kRetailOptionsVrSettingsCommand) ||
        RetailOptionsTargetScreen(
            condemnedvr::kRetailOptionsVrSettingsCommand) !=
            condemnedvr::kNoRetailOptionsTargetScreen ||
        condemnedvr::RetailVrSettingsTargetScreen(
            condemnedvr::kRetailOptionsVrSettingsCommand) !=
            condemnedvr::kRetailScreenGameOptions) {
        return Fail("VR Settings must remain limited to the unused 0x3A gap");
    }

    struct CategoryCase {
        std::uint32_t command;
        condemnedvr::RetailVrSettingsCategory category;
        condemnedvr::RetailVrSettingsAction action;
    };
    constexpr CategoryCase kCategories[] = {
        {condemnedvr::kRetailVrSettingsDisplayCommand,
         condemnedvr::RetailVrSettingsCategory::Display,
         condemnedvr::RetailVrSettingsAction::OpenDisplay},
        {condemnedvr::kRetailVrSettingsFeaturesCommand,
         condemnedvr::RetailVrSettingsCategory::VrFeatures,
         condemnedvr::RetailVrSettingsAction::OpenVrFeatures},
        {condemnedvr::kRetailVrSettingsComfortCommand,
         condemnedvr::RetailVrSettingsCategory::Comfort,
         condemnedvr::RetailVrSettingsAction::OpenComfort},
        {condemnedvr::kRetailVrSettingsDeveloperCommand,
         condemnedvr::RetailVrSettingsCategory::DeveloperTools,
         condemnedvr::RetailVrSettingsAction::ToggleDeveloperToolsShortcut},
    };
    for (const CategoryCase& category : kCategories) {
        if (!condemnedvr::IsRetailVrSettingsCategoryCommand(
                category.command) ||
            condemnedvr::RetailVrSettingsCategoryForCommand(
                category.command) != category.category ||
            condemnedvr::RetailVrSettingsActionForCommand(
                category.command) != category.action) {
            return Fail(
                "each VR Settings row needs one private command and action");
        }
    }

    for (const std::uint32_t collision : {
             1U, 2U, 7U, 0x37U, 0x38U, 0x39U, 0x3AU, 0x3BU, 0x41U}) {
        if (condemnedvr::IsRetailVrSettingsCategoryCommand(collision)) {
            return Fail("VR category commands must not collide with Retail navigation");
        }
    }

    for (std::uint32_t command = 0U; command < 0x100U; ++command) {
        if (condemnedvr::IsRetailVrSettingsCommand(command) !=
            (command ==
             condemnedvr::kRetailOptionsVrSettingsCommand) ||
            condemnedvr::RetailVrSettingsTargetScreen(command) !=
                (command == condemnedvr::kRetailOptionsVrSettingsCommand
                     ? condemnedvr::kRetailScreenGameOptions
                     : condemnedvr::kNoRetailOptionsTargetScreen)) {
            return Fail("no other command may enter the VR Settings path");
        }
    }

    std::uint32_t categoryCommandCount = 0U;
    for (std::uint32_t command = 0U; command < 0x100U; ++command) {
        if (condemnedvr::IsRetailVrSettingsCategoryCommand(command)) {
            ++categoryCommandCount;
        } else if (condemnedvr::RetailVrSettingsCategoryForCommand(command) !=
                   condemnedvr::RetailVrSettingsCategory::None) {
            return Fail("unrecognized VR commands must map to no category");
        }
    }
    if (categoryCommandCount != 4U) {
        return Fail("exactly four VR Settings category commands are allowed");
    }

    if (!condemnedvr::ShouldSuppressRetailVrSettingsCategoryCommand(
            condemnedvr::kRetailVrSettingsDisplayCommand, true) ||
        condemnedvr::ShouldSuppressRetailVrSettingsCategoryCommand(
            condemnedvr::kRetailVrSettingsDisplayCommand, false) ||
        condemnedvr::ShouldSuppressRetailVrSettingsCategoryCommand(
            condemnedvr::kRetailOptionsVrSettingsCommand, true)) {
        return Fail(
            "only a category activated by the same VR entry edge is suppressed");
    }

    std::puts("Condemned Retail-menu mapping tests passed.");
    return 0;
}
