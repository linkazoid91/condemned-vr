#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <MinHook.h>

#include "condemned_retail_menu.h"
#include "retail_menu_integration.h"

namespace condemnedvr {

#if defined(_M_IX86)
namespace {

struct RetailTextControlConfigAbi {
    std::uint32_t words[13];
};
static_assert(
    sizeof(RetailTextControlConfigAbi) == 0x34,
    "Retail CLTGUI text-control configuration ABI changed.");

using CreateLocalizedTextControlFunction = void*(__thiscall*)(
    void*,
    const char*,
    RetailTextControlConfigAbi,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t);
using CreateWideTextControlFunction = void*(__thiscall*)(
    void*,
    const wchar_t*,
    RetailTextControlConfigAbi,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t);
using AddControlFunction =
    std::uint16_t(__thiscall*)(void*, void*);
using OptionsOnCommandFunction = std::uint32_t(__thiscall*)(
    void*, std::uint32_t, std::uint32_t, std::uint32_t);
using SetWideTitleFunction = bool(__thiscall*)(
    void*, const wchar_t*);
using SetWideTextFunction = void(__thiscall*)(
    void*, const wchar_t*);
using SwitchScreenFunction = void(__thiscall*)(
    void*, std::uint32_t);
using ScreenBuildFunction = bool(__thiscall*)(void*);
using ScreenOnFocusFunction = void(__thiscall*)(void*, bool);
using FinalizeScreenFunction = void(__thiscall*)(
    void*, bool, bool, bool);

constexpr std::uintptr_t kRetailImageSize = 0x00194000U;
constexpr std::uintptr_t kCreateWideTextControlRva = 0x00006340U;
constexpr std::uintptr_t kCreateLocalizedTextControlRva = 0x00007160U;
constexpr std::uintptr_t kSetWideTitleRva = 0x00005E70U;
constexpr std::uintptr_t kSetLocalizedTitleRva = 0x00005F40U;
constexpr std::uintptr_t kBaseScreenOnCommandRva = 0x00005BC0U;
constexpr std::uintptr_t kBaseScreenOnFocusRva = 0x00008370U;
constexpr std::uintptr_t kAddControlRva = 0x00009410U;
constexpr std::uintptr_t kFinalizeScreenRva = 0x000094C0U;
constexpr std::uintptr_t kBaseScreenBuildRva = 0x00009540U;
constexpr std::uintptr_t kBaseScreenVtableRva = 0x00138E14U;
constexpr std::uintptr_t kOptionsOnCommandRva = 0x000CD470U;
constexpr std::uintptr_t kOptionsDestructorRva = 0x000CD4F0U;
constexpr std::uintptr_t kOptionsBuildRva = 0x000CD620U;
constexpr std::uintptr_t kOptionsVtableRva = 0x0014814CU;
constexpr std::uintptr_t kStringEditInterfaceRva = 0x00172EF8U;
constexpr std::uintptr_t kStringEditModuleRva = 0x00172EFCU;
constexpr std::uintptr_t kOptionsHelpKeyRva = 0x00147CE8U;
constexpr std::uintptr_t kPerformanceLabelKeyRva = 0x001481D0U;
constexpr std::uintptr_t kPerformanceHelpKeyRva = 0x001481E0U;
constexpr std::uintptr_t kPerformanceRowRva = 0x000CDCBEU;
constexpr std::uintptr_t kPerformanceCommandWriteRva = 0x000CDCCFU;
constexpr std::uintptr_t kPerformanceHelpWriteRva = 0x000CDCD7U;
constexpr std::uintptr_t kPerformanceLabelPushRva = 0x000CDCE1U;
constexpr std::uintptr_t kPerformanceFactoryCallRva = 0x000CDCE8U;
constexpr std::uintptr_t kOptionsCommandTableRva = 0x000CD4D4U;
constexpr std::uintptr_t kScreenFactoryTableRva = 0x000CCBC4U;
constexpr std::uintptr_t kGameOptionsFactoryBranchRva = 0x000CCB2BU;
constexpr std::uintptr_t kGameOptionsFactoryRva = 0x000CBA30U;
constexpr std::uintptr_t kGameOptionsConstructorRva = 0x000C5CB0U;
constexpr std::uintptr_t kGameOptionsDestructorRva = 0x000C5D50U;
constexpr std::uintptr_t kGameOptionsOnCommandRva = 0x000C5D20U;
constexpr std::uintptr_t kGameOptionsOnFocusRva = 0x000C5E70U;
constexpr std::uintptr_t kGameOptionsBuildRva = 0x000C6080U;
constexpr std::uintptr_t kGameOptionsVtableRva = 0x0014732CU;
constexpr std::uintptr_t kGameOptionsTitleKeyRva = 0x00147630U;
constexpr std::uintptr_t kGameOptionsBuildTitlePushRva = 0x000C608AU;
constexpr std::uintptr_t kGameOptionsBuildTitleCallRva = 0x000C6091U;
constexpr std::uintptr_t kGameOptionsBaseOnFocusCallRva = 0x000C5F9EU;
constexpr std::uintptr_t kGameOptionsBaseBuildCallRva = 0x000C6B2DU;
constexpr std::uintptr_t kGameOptionsFinalizeSequenceRva = 0x000C6B6EU;
constexpr std::uintptr_t kTextControlSetWideTextRva = 0x0012BA40U;
constexpr std::uintptr_t kEmbeddedWideTextSetterRva = 0x001330A0U;
constexpr std::uintptr_t kTextControlVtableRva = 0x00150AE0U;
constexpr std::size_t kTextControlSetWideTextSlot =
    0xD8U / sizeof(void*);
constexpr std::size_t kBaseScreenManagerOffset = 0x0CU;
constexpr std::size_t kScreenManagerSwitchSlot =
    0x40U / sizeof(void*);
constexpr std::uintptr_t kOptionsCommandTargets[] = {
    0x000CD483U,
    0x000CD495U,
    0x000CD4B9U,
    0x000CD4CBU,
    0x000CD4A7U,
};
constexpr wchar_t kVrSettingsLabel[] = L"VR Settings";
constexpr wchar_t kDisplayLabel[] = L"Display";
constexpr wchar_t kVrFeaturesLabel[] = L"VR Features";
constexpr wchar_t kComfortLabel[] = L"Comfort";
constexpr wchar_t kDeveloperToolsEnabledLabel[] =
    L"Developer Tools: On";
constexpr wchar_t kDeveloperToolsDisabledLabel[] =
    L"Developer Tools: Off";
constexpr std::size_t kDeveloperToolsRowIndex = 3U;

struct PageRowSpec {
    const wchar_t* label;
    std::uint32_t command;
};

constexpr PageRowSpec kPageRows[] = {
    {kDisplayLabel, kRetailVrSettingsDisplayCommand},
    {kVrFeaturesLabel, kRetailVrSettingsFeaturesCommand},
    {kComfortLabel, kRetailVrSettingsComfortCommand},
    {kDeveloperToolsDisabledLabel, kRetailVrSettingsDeveloperCommand},
};

constexpr unsigned char kCreateLocalizedPrefix[] = {
    0x53, 0x8B, 0xD9, 0x8B, 0x0D};
constexpr unsigned char kCreateLocalizedBody[] = {
    0x85, 0xC9, 0x56, 0x57, 0x74, 0x27, 0x8B, 0x15};
constexpr unsigned char kCreateWidePrefix[] = {
    0x83, 0xEC, 0x30, 0x56, 0x57, 0x68, 0xDC,
    0x00, 0x00, 0x00, 0x8B, 0xF1, 0xE8};
constexpr unsigned char kAddControlPrefix[] = {
    0x83, 0xEC, 0x08, 0x53, 0x55, 0x56, 0x8B, 0xF1,
    0x57, 0x8D, 0x44, 0x24, 0x1C, 0x8D, 0xBE, 0xC4,
    0x00, 0x00, 0x00};
constexpr unsigned char kOptionsOnCommandPrefix[] = {
    0x8B, 0x54, 0x24, 0x04, 0x8D, 0x42, 0xC9, 0x83,
    0xF8, 0x04, 0x77, 0x4F, 0xFF, 0x24, 0x85};
constexpr unsigned char kPerformanceRowPrefix[] = {
    0x53, 0x53, 0x53, 0x83, 0xEC, 0x34, 0x8B, 0xFC,
    0xB9, 0x0D, 0x00, 0x00, 0x00, 0x8D, 0x74, 0x24,
    0x54};
constexpr unsigned char kSetWideTitlePrefix[] = {
    0x83, 0xEC, 0x30, 0x56, 0x8B, 0xF1, 0x8B, 0x86,
    0xB8, 0x00, 0x00, 0x00};
constexpr unsigned char kSetLocalizedTitlePrefix[] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x0D};
constexpr unsigned char kGameOptionsConstructorPrefix[] = {
    0x53, 0x56, 0x8B, 0xF1, 0xE8};
constexpr unsigned char kGameOptionsFactoryPrefix[] = {
    0x68, 0xD0, 0x01, 0x00, 0x00, 0xE8};
constexpr unsigned char kBaseScreenOnCommandPrefix[] = {
    0x8B, 0x44, 0x24, 0x04, 0x48, 0x74, 0x37, 0x48,
    0x74, 0x1C, 0x83, 0xE8, 0x05, 0x74, 0x05};
constexpr unsigned char kBaseScreenOnFocusPrefix[] = {
    0x8A, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x08, 0x84,
    0xC0, 0x56, 0x8B, 0xF1, 0x57};
constexpr unsigned char kFinalizeScreenPrefix[] = {
    0x53, 0x8A, 0x5C, 0x24, 0x08, 0x84, 0xDB, 0x56,
    0x8B, 0xF1, 0x74, 0x56};
constexpr unsigned char kBaseScreenBuildPrefix[] = {
    0x55, 0x56, 0x57, 0x6A, 0x00, 0x8B, 0xF1, 0x6A,
    0x00, 0xC6, 0x46, 0x05, 0x01};
constexpr unsigned char kGameOptionsOnCommandPrefix[] = {
    0x8B, 0x44, 0x24, 0x04, 0x83, 0xF8, 0x41, 0x74,
    0x09, 0x89, 0x44, 0x24, 0x04, 0xE9};
constexpr unsigned char kGameOptionsOnFocusPrefix[] = {
    0x53, 0x8B, 0x5C, 0x24, 0x08, 0x84, 0xDB, 0x56,
    0x57, 0x8B, 0x3D};
constexpr unsigned char kGameOptionsBuildPrefix[] = {
    0x81, 0xEC, 0x8C, 0x01, 0x00, 0x00, 0x53, 0x55,
    0x56, 0x57};
constexpr unsigned char kGameOptionsFinalizeSequencePrefix[] = {
    0x53, 0x6A, 0x01, 0x6A, 0x01, 0x8B, 0xCD, 0xE8};
constexpr unsigned char kTextControlSetWideTextPrefix[] = {
    0x83, 0xC1, 0x54, 0xE9, 0x58, 0x76, 0x00, 0x00};
constexpr unsigned char kEmbeddedWideTextSetterPrefix[] = {
    0x53, 0x8B, 0x5C, 0x24, 0x08, 0x85, 0xDB, 0x56, 0x8B, 0xF1};

SRWLOCK g_installLock = SRWLOCK_INIT;
unsigned char* g_gameClientBase = nullptr;
void* g_createLocalizedHookTarget = nullptr;
void* g_optionsOnCommandHookTarget = nullptr;
void* g_gameOptionsBuildHookTarget = nullptr;
void* g_gameOptionsOnFocusHookTarget = nullptr;
void* g_gameOptionsOnCommandHookTarget = nullptr;
CreateLocalizedTextControlFunction g_originalCreateLocalized = nullptr;
CreateWideTextControlFunction g_createWide = nullptr;
AddControlFunction g_addControl = nullptr;
OptionsOnCommandFunction g_originalOptionsOnCommand = nullptr;
ScreenBuildFunction g_rejectedOriginalGameOptionsBuild = nullptr;
ScreenOnFocusFunction g_rejectedOriginalGameOptionsOnFocus = nullptr;
OptionsOnCommandFunction g_rejectedOriginalGameOptionsOnCommand = nullptr;
SetWideTitleFunction g_setWideTitle = nullptr;
SetWideTextFunction g_setWideText = nullptr;
ScreenBuildFunction g_baseScreenBuild = nullptr;
ScreenOnFocusFunction g_baseScreenOnFocus = nullptr;
OptionsOnCommandFunction g_baseScreenOnCommand = nullptr;
FinalizeScreenFunction g_finalizeScreen = nullptr;
RendererProbeLogFunction g_log = nullptr;
volatile LONG g_rowInjectionState = 0;
volatile LONG g_selectionCount = 0;
volatile LONG g_stockDescriptorState = 0;
RetailTextControlConfigAbi g_stockTextConfig{};
std::uint32_t g_stockTrailing[3]{};
void* volatile g_pageBuildScreen = nullptr;
void* volatile g_pageControls[
    sizeof(kPageRows) / sizeof(kPageRows[0])]{};
volatile LONG g_pageBuildState = 0;
volatile LONG g_pageFocusCount = 0;
volatile LONG g_categorySelectionCount = 0;
volatile LONG g_menuKeyEdgeVirtualKey = 0;
volatile LONG g_suppressCategoryUntilEntryEdgeEnd = 0;
volatile LONG g_suppressedEntryCategoryCount = 0;

std::uint32_t ReadU32(const unsigned char* address) noexcept {
    std::uint32_t value = 0U;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

std::uintptr_t RelativeCallTarget(
    const unsigned char* instruction) noexcept {
    std::int32_t displacement = 0;
    std::memcpy(
        &displacement, instruction + 1, sizeof(displacement));
    return reinterpret_cast<std::uintptr_t>(instruction + 5) +
        displacement;
}

std::uint32_t ModuleAddress32(
    const unsigned char* base,
    std::uintptr_t rva) noexcept {
    return static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(base + rva));
}

bool IsExpectedOptionsScreen(void* screen) noexcept {
    if (screen == nullptr || g_gameClientBase == nullptr) {
        return false;
    }
    __try {
        return *static_cast<void***>(screen) ==
            reinterpret_cast<void**>(
                g_gameClientBase + kOptionsVtableRva);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsExpectedGameOptionsScreen(void* screen) noexcept {
    if (screen == nullptr || g_gameClientBase == nullptr) {
        return false;
    }
    __try {
        return *static_cast<void***>(screen) ==
            reinterpret_cast<void**>(
                g_gameClientBase + kGameOptionsVtableRva);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsExpectedTextControl(void* control) noexcept {
    if (control == nullptr || g_gameClientBase == nullptr) {
        return false;
    }
    __try {
        return *static_cast<void***>(control) ==
            reinterpret_cast<void**>(
                g_gameClientBase + kTextControlVtableRva);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

const wchar_t* DeveloperToolsLabel(bool enabled) noexcept {
    return enabled
        ? kDeveloperToolsEnabledLabel
        : kDeveloperToolsDisabledLabel;
}

bool SetExpectedTextControlLabel(
    void* control,
    const wchar_t* label) noexcept {
    if (label == nullptr || g_setWideText == nullptr ||
        !IsExpectedTextControl(control)) {
        return false;
    }
    __try {
        g_setWideText(control, label);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool RefreshDeveloperToolsLabel(
    bool& settingReady,
    bool& enabled) noexcept {
    settingReady = ReadVrToolMenuShortcutEnabled(enabled);
    void* const control = InterlockedCompareExchangePointer(
        &g_pageControls[kDeveloperToolsRowIndex], nullptr, nullptr);
    return control != nullptr &&
        SetExpectedTextControlLabel(
            control,
            DeveloperToolsLabel(settingReady && enabled));
}

bool ExpectedImageAndMenuTargetsMatch(
    HMODULE gameClientModule) noexcept {
    if (gameClientModule == nullptr) {
        return false;
    }
    auto* const base =
        reinterpret_cast<unsigned char*>(gameClientModule);
    __try {
        const auto* const dos =
            reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
            dos->e_lfanew <= 0 ||
            dos->e_lfanew > 0x1000) {
            return false;
        }
        const auto* const nt =
            reinterpret_cast<const IMAGE_NT_HEADERS32*>(
                base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            nt->OptionalHeader.SizeOfImage != kRetailImageSize) {
            return false;
        }

        void** const optionsVtable =
            reinterpret_cast<void**>(base + kOptionsVtableRva);
        if (optionsVtable[0] != base + kOptionsDestructorRva ||
            optionsVtable[1] != base + kOptionsOnCommandRva ||
            optionsVtable[2] != base + 0x00007680U ||
            optionsVtable[3] != base + 0x000091A0U ||
            optionsVtable[4] != base + kOptionsBuildRva) {
            return false;
        }

        const auto* const localized =
            base + kCreateLocalizedTextControlRva;
        const auto* const wide = base + kCreateWideTextControlRva;
        const auto* const setWideTitle = base + kSetWideTitleRva;
        const auto* const setLocalizedTitle =
            base + kSetLocalizedTitleRva;
        const auto* const addControl = base + kAddControlRva;
        const auto* const onCommand = base + kOptionsOnCommandRva;
        const auto* const baseScreenOnCommand =
            base + kBaseScreenOnCommandRva;
        const auto* const baseScreenOnFocus =
            base + kBaseScreenOnFocusRva;
        const auto* const finalizeScreen =
            base + kFinalizeScreenRva;
        const auto* const baseScreenBuild =
            base + kBaseScreenBuildRva;
        const auto* const setWideText =
            base + kTextControlSetWideTextRva;
        const auto* const embeddedWideTextSetter =
            base + kEmbeddedWideTextSetterRva;
        void** const textControlVtable =
            reinterpret_cast<void**>(
                base + kTextControlVtableRva);
        if (std::memcmp(
                localized, kCreateLocalizedPrefix,
                sizeof(kCreateLocalizedPrefix)) != 0 ||
            ReadU32(localized + 5) !=
                ModuleAddress32(base, kStringEditInterfaceRva) ||
            std::memcmp(
                localized + 9, kCreateLocalizedBody,
                sizeof(kCreateLocalizedBody)) != 0 ||
            ReadU32(localized + 0x11) !=
                ModuleAddress32(base, kStringEditModuleRva) ||
            std::memcmp(
                wide, kCreateWidePrefix,
                sizeof(kCreateWidePrefix)) != 0 ||
            std::memcmp(
                setWideTitle, kSetWideTitlePrefix,
                sizeof(kSetWideTitlePrefix)) != 0 ||
            std::memcmp(
                setLocalizedTitle, kSetLocalizedTitlePrefix,
                sizeof(kSetLocalizedTitlePrefix)) != 0 ||
            ReadU32(setLocalizedTitle + 5) !=
                ModuleAddress32(base, kStringEditInterfaceRva) ||
            setLocalizedTitle[0x0D] != 0x8B ||
            setLocalizedTitle[0x0E] != 0x15 ||
            ReadU32(setLocalizedTitle + 0x0F) !=
                ModuleAddress32(base, kStringEditModuleRva) ||
            std::memcmp(
                addControl, kAddControlPrefix,
                sizeof(kAddControlPrefix)) != 0 ||
            std::memcmp(
                onCommand, kOptionsOnCommandPrefix,
                sizeof(kOptionsOnCommandPrefix)) != 0 ||
            ReadU32(onCommand + sizeof(kOptionsOnCommandPrefix)) !=
                ModuleAddress32(base, kOptionsCommandTableRva) ||
            std::memcmp(
                baseScreenOnCommand, kBaseScreenOnCommandPrefix,
                sizeof(kBaseScreenOnCommandPrefix)) != 0 ||
            std::memcmp(
                baseScreenOnFocus, kBaseScreenOnFocusPrefix,
                sizeof(kBaseScreenOnFocusPrefix)) != 0 ||
            std::memcmp(
                finalizeScreen, kFinalizeScreenPrefix,
                sizeof(kFinalizeScreenPrefix)) != 0 ||
            std::memcmp(
                baseScreenBuild, kBaseScreenBuildPrefix,
                sizeof(kBaseScreenBuildPrefix)) != 0 ||
            std::memcmp(
                setWideText, kTextControlSetWideTextPrefix,
                sizeof(kTextControlSetWideTextPrefix)) != 0 ||
            RelativeCallTarget(setWideText + 3) !=
                reinterpret_cast<std::uintptr_t>(
                    embeddedWideTextSetter) ||
            std::memcmp(
                embeddedWideTextSetter,
                kEmbeddedWideTextSetterPrefix,
                sizeof(kEmbeddedWideTextSetterPrefix)) != 0 ||
            textControlVtable[kTextControlSetWideTextSlot] !=
                setWideText) {
            return false;
        }

        const auto* const commandTable =
            base + kOptionsCommandTableRva;
        for (std::size_t index = 0;
             index < sizeof(kOptionsCommandTargets) /
                 sizeof(kOptionsCommandTargets[0]);
             ++index) {
            if (ReadU32(commandTable + index * sizeof(std::uint32_t)) !=
                ModuleAddress32(base, kOptionsCommandTargets[index])) {
                return false;
            }
        }

        void** const gameOptionsVtable =
            reinterpret_cast<void**>(
                base + kGameOptionsVtableRva);
        void** const baseScreenVtable =
            reinterpret_cast<void**>(
                base + kBaseScreenVtableRva);
        if (gameOptionsVtable[0] !=
                base + kGameOptionsDestructorRva ||
            gameOptionsVtable[1] !=
                base + kGameOptionsOnCommandRva ||
            gameOptionsVtable[2] != base + 0x00007680U ||
            gameOptionsVtable[3] != base + 0x000091A0U ||
            gameOptionsVtable[4] !=
                base + kGameOptionsBuildRva ||
            gameOptionsVtable[8] !=
                base + kGameOptionsOnFocusRva ||
            baseScreenVtable[1] !=
                base + kBaseScreenOnCommandRva ||
            baseScreenVtable[4] !=
                base + kBaseScreenBuildRva ||
            baseScreenVtable[8] !=
                base + kBaseScreenOnFocusRva) {
            return false;
        }
        // Static comparison shows that destructor, OnCommand, Build, and
        // OnFocus are the only CScreenGame overrides. The isolated host hooks
        // the latter three; every other lifecycle callback must stay byte-for-
        // byte inherited from CBaseScreen.
        for (std::size_t index = 2U; index <= 32U; ++index) {
            if (index != 4U && index != 8U &&
                gameOptionsVtable[index] != baseScreenVtable[index]) {
                return false;
            }
        }

        const auto* const gameFactoryEntry =
            base + kScreenFactoryTableRva +
            (kRetailScreenGameOptions - 1) *
                sizeof(std::uint32_t);
        const auto* const gameFactoryBranch =
            base + kGameOptionsFactoryBranchRva;
        const auto* const gameFactory =
            base + kGameOptionsFactoryRva;
        const auto* const gameConstructor =
            base + kGameOptionsConstructorRva;
        const auto* const gameBuildTitlePush =
            base + kGameOptionsBuildTitlePushRva;
        const auto* const gameBuildTitleCall =
            base + kGameOptionsBuildTitleCallRva;
        const auto* const gameOnCommand =
            base + kGameOptionsOnCommandRva;
        const auto* const gameOnFocus =
            base + kGameOptionsOnFocusRva;
        const auto* const gameBuild =
            base + kGameOptionsBuildRva;
        const auto* const baseOnFocusCall =
            base + kGameOptionsBaseOnFocusCallRva;
        const auto* const baseBuildCall =
            base + kGameOptionsBaseBuildCallRva;
        const auto* const finalizeSequence =
            base + kGameOptionsFinalizeSequenceRva;
        if (ReadU32(gameFactoryEntry) !=
                ModuleAddress32(
                    base, kGameOptionsFactoryBranchRva) ||
            gameFactoryBranch[0] != 0xE8 ||
            RelativeCallTarget(gameFactoryBranch) !=
                reinterpret_cast<std::uintptr_t>(gameFactory) ||
            std::memcmp(
                gameFactory, kGameOptionsFactoryPrefix,
                sizeof(kGameOptionsFactoryPrefix)) != 0 ||
            gameFactory[0x13] != 0xE9 ||
            RelativeCallTarget(gameFactory + 0x13) !=
                reinterpret_cast<std::uintptr_t>(
                    gameConstructor) ||
            std::memcmp(
                gameConstructor,
                kGameOptionsConstructorPrefix,
                sizeof(kGameOptionsConstructorPrefix)) != 0 ||
            gameConstructor[0x0B] != 0xC7 ||
            gameConstructor[0x0C] != 0x06 ||
            ReadU32(gameConstructor + 0x0D) !=
                ModuleAddress32(
                    base, kGameOptionsVtableRva) ||
            gameBuildTitlePush[0] != 0x68 ||
            ReadU32(gameBuildTitlePush + 1) !=
                ModuleAddress32(
                    base, kGameOptionsTitleKeyRva) ||
            gameBuildTitleCall[0] != 0xE8 ||
            RelativeCallTarget(gameBuildTitleCall) !=
                reinterpret_cast<std::uintptr_t>(
                    setLocalizedTitle) ||
            std::memcmp(
                gameOnCommand, kGameOptionsOnCommandPrefix,
                sizeof(kGameOptionsOnCommandPrefix)) != 0 ||
            RelativeCallTarget(gameOnCommand + 0x0D) !=
                reinterpret_cast<std::uintptr_t>(
                    baseScreenOnCommand) ||
            std::memcmp(
                gameOnFocus, kGameOptionsOnFocusPrefix,
                sizeof(kGameOptionsOnFocusPrefix)) != 0 ||
            baseOnFocusCall[0] != 0xE8 ||
            RelativeCallTarget(baseOnFocusCall) !=
                reinterpret_cast<std::uintptr_t>(
                    baseScreenOnFocus) ||
            std::memcmp(
                gameBuild, kGameOptionsBuildPrefix,
                sizeof(kGameOptionsBuildPrefix)) != 0 ||
            baseBuildCall[0] != 0xE8 ||
            RelativeCallTarget(baseBuildCall) !=
                reinterpret_cast<std::uintptr_t>(
                    baseScreenBuild) ||
            std::memcmp(
                finalizeSequence,
                kGameOptionsFinalizeSequencePrefix,
                sizeof(kGameOptionsFinalizeSequencePrefix)) != 0 ||
            RelativeCallTarget(finalizeSequence + 7) !=
                reinterpret_cast<std::uintptr_t>(
                    finalizeScreen) ||
            std::strcmp(
                reinterpret_cast<const char*>(
                    base + kGameOptionsTitleKeyRva),
                "IDS_TITLE_GAME_OPTIONS") != 0) {
            return false;
        }

        const auto* const performanceRow =
            base + kPerformanceRowRva;
        const auto* const commandWrite =
            base + kPerformanceCommandWriteRva;
        const auto* const helpWrite =
            base + kPerformanceHelpWriteRva;
        const auto* const labelPush =
            base + kPerformanceLabelPushRva;
        const auto* const factoryCall =
            base + kPerformanceFactoryCallRva;
        constexpr unsigned char kCommandWritePrefix[] = {
            0xC7, 0x44, 0x24, 0x54};
        constexpr unsigned char kHelpWritePrefix[] = {
            0xC7, 0x44, 0x24, 0x58};
        if (std::memcmp(
                performanceRow, kPerformanceRowPrefix,
                sizeof(kPerformanceRowPrefix)) != 0 ||
            std::memcmp(
                commandWrite, kCommandWritePrefix,
                sizeof(kCommandWritePrefix)) != 0 ||
            ReadU32(commandWrite + sizeof(kCommandWritePrefix)) !=
                kRetailOptionsPerformanceCommand ||
            std::memcmp(
                helpWrite, kHelpWritePrefix,
                sizeof(kHelpWritePrefix)) != 0 ||
            ReadU32(helpWrite + sizeof(kHelpWritePrefix)) !=
                ModuleAddress32(base, kPerformanceHelpKeyRva) ||
            labelPush[0] != 0x68 ||
            ReadU32(labelPush + 1) !=
                ModuleAddress32(base, kPerformanceLabelKeyRva) ||
            factoryCall[0] != 0xE8 ||
            RelativeCallTarget(factoryCall) !=
                reinterpret_cast<std::uintptr_t>(
                    base + kCreateLocalizedTextControlRva) ||
            std::strcmp(
                reinterpret_cast<const char*>(
                    base + kOptionsHelpKeyRva),
                "IDS_HELP_OPTIONS") != 0 ||
            std::strcmp(
                reinterpret_cast<const char*>(
                    base + kPerformanceLabelKeyRva),
                "IDS_PERFORMANCE") != 0 ||
            std::strcmp(
                reinterpret_cast<const char*>(
                    base + kPerformanceHelpKeyRva),
                "IDS_HELP_PERFORMANCE") != 0) {
            return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void CaptureStockTextDescriptor(
    const RetailTextControlConfigAbi& config,
    std::uint32_t firstTrailing,
    std::uint32_t secondTrailing,
    std::uint32_t thirdTrailing) noexcept {
    if (InterlockedCompareExchange(
            &g_stockDescriptorState, 1, 0) != 0) {
        return;
    }
    g_stockTextConfig = config;
    g_stockTrailing[0] = firstTrailing;
    g_stockTrailing[1] = secondTrailing;
    g_stockTrailing[2] = thirdTrailing;
    InterlockedExchange(&g_stockDescriptorState, 2);
}

void InjectVrSettingsRow(
    void* screen,
    const RetailTextControlConfigAbi& stockConfig,
    std::uint32_t firstTrailing,
    std::uint32_t secondTrailing,
    std::uint32_t thirdTrailing) noexcept {
    if (!IsExpectedOptionsScreen(screen) ||
        InterlockedCompareExchange(
            &g_rowInjectionState, 1, 0) != 0) {
        return;
    }

    RetailTextControlConfigAbi vrConfig = stockConfig;
    vrConfig.words[0] = kRetailOptionsVrSettingsCommand;
    vrConfig.words[1] = ModuleAddress32(
        g_gameClientBase, kOptionsHelpKeyRva);

    void* control = nullptr;
    std::uint16_t index = 0xFFFFU;
    __try {
        control = g_createWide(
            screen, kVrSettingsLabel, vrConfig,
            firstTrailing, secondTrailing, thirdTrailing);
        if (control != nullptr) {
            index = g_addControl(screen, control);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        control = nullptr;
        index = 0xFFFFU;
    }

    if (control == nullptr || index == 0xFFFFU) {
        // AddControl is the ownership handoff. If it faults or returns its
        // sentinel, ownership is no longer provable; leave the object alone
        // and fail the probe instead of risking a dangling Retail entry.
        InterlockedExchange(&g_rowInjectionState, -1);
        if (g_log != nullptr) {
            g_log(
                "m6_retail_vr_settings_row_failed",
                "stage=native_create_or_add "
                "ownership_after_add=unknown");
        }
        return;
    }

    InterlockedExchange(&g_rowInjectionState, 2);
    if (g_log != nullptr) {
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "screen=CScreenOptions label=VR_Settings "
            "command=0x%02X control_index=%u "
            "factory=CBaseScreen_wide_text "
            "layout_descriptor=cloned_from_performance "
            "native_add_control=1",
            static_cast<unsigned int>(
                kRetailOptionsVrSettingsCommand),
            static_cast<unsigned int>(index));
        g_log("m6_retail_vr_settings_row_added", detail);
    }
}

void* __fastcall HookCreateLocalizedTextControl(
    void* screen,
    void*,
    const char* labelKey,
    RetailTextControlConfigAbi config,
    std::uint32_t firstTrailing,
    std::uint32_t secondTrailing,
    std::uint32_t thirdTrailing) {
    if (IsExpectedOptionsScreen(screen) &&
        labelKey ==
            reinterpret_cast<const char*>(
                g_gameClientBase + kPerformanceLabelKeyRva) &&
        config.words[0] == kRetailOptionsPerformanceCommand &&
        config.words[1] ==
            ModuleAddress32(
                g_gameClientBase, kPerformanceHelpKeyRva)) {
        CaptureStockTextDescriptor(
            config, firstTrailing, secondTrailing, thirdTrailing);
        // Text placement is resolved at construction time. Add VR Settings
        // first so Retail creates Performance at the following native row.
        InjectVrSettingsRow(
            screen, config,
            firstTrailing, secondTrailing, thirdTrailing);
    }
    return g_originalCreateLocalized(
        screen, labelKey, config,
        firstTrailing, secondTrailing, thirdTrailing);
}

const char* RetailCategoryName(
    RetailVrSettingsCategory category) noexcept {
    switch (category) {
    case RetailVrSettingsCategory::Display:
        return "Display";
    case RetailVrSettingsCategory::VrFeatures:
        return "VR_Features";
    case RetailVrSettingsCategory::Comfort:
        return "Comfort";
    case RetailVrSettingsCategory::DeveloperTools:
        return "Developer_Tools";
    default:
        return "None";
    }
}

bool RejectPageBuild(
    const char* reason,
    int rowIndex = -1) noexcept {
    InterlockedExchange(&g_pageBuildState, -1);
    if (g_log != nullptr) {
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "screen=CScreenGame screen_id=24 "
            "host=isolated_base_screen reason=%s row=%d "
            "original_build_called=0",
            reason != nullptr ? reason : "unknown",
            rowIndex);
        g_log("m6_retail_vr_settings_page_build_failed", detail);
    }
    return false;
}

bool __fastcall HookGameOptionsBuild(
    void* screen,
    void*) {
    if (!IsExpectedGameOptionsScreen(screen)) {
        return RejectPageBuild("unexpected_screen_object");
    }

    void* const priorScreen = InterlockedCompareExchangePointer(
        &g_pageBuildScreen, screen, nullptr);
    const LONG priorState = InterlockedCompareExchange(
        &g_pageBuildState, 0, 0);
    if (priorScreen == screen && priorState == 2) {
        return true;
    }
    if (priorScreen != nullptr && priorScreen != screen) {
        return RejectPageBuild("unexpected_second_screen_object");
    }
    if (InterlockedCompareExchange(
            &g_pageBuildState, 1, 0) != 0) {
        return false;
    }
    if (InterlockedCompareExchange(
            &g_stockDescriptorState, 2, 2) != 2) {
        return RejectPageBuild("stock_descriptor_unavailable");
    }

    const RetailTextControlConfigAbi stockConfig =
        g_stockTextConfig;
    const std::uint32_t firstTrailing = g_stockTrailing[0];
    const std::uint32_t secondTrailing = g_stockTrailing[1];
    const std::uint32_t thirdTrailing = g_stockTrailing[2];
    bool developerToolsEnabled = false;
    const bool developerToolsSettingReady =
        ReadVrToolMenuShortcutEnabled(developerToolsEnabled);
    std::uint16_t indices[
        sizeof(kPageRows) / sizeof(kPageRows[0])]{};
    for (std::size_t index = 0;
         index < sizeof(indices) / sizeof(indices[0]);
         ++index) {
        indices[index] = 0xFFFFU;
    }

    const char* failure = nullptr;
    int failedRow = -1;
    __try {
        if (!g_setWideTitle(screen, kVrSettingsLabel)) {
            failure = "wide_title_failed";
        }
        for (std::size_t row = 0;
             failure == nullptr &&
             row < sizeof(kPageRows) / sizeof(kPageRows[0]);
             ++row) {
            RetailTextControlConfigAbi config = stockConfig;
            config.words[0] = kPageRows[row].command;
            config.words[1] = ModuleAddress32(
                g_gameClientBase, kOptionsHelpKeyRva);
            const wchar_t* const label =
                row == kDeveloperToolsRowIndex
                    ? DeveloperToolsLabel(
                          developerToolsSettingReady &&
                          developerToolsEnabled)
                    : kPageRows[row].label;
            void* const control = g_createWide(
                screen, label, config,
                firstTrailing, secondTrailing, thirdTrailing);
            if (control == nullptr) {
                failure = "wide_row_factory_failed";
                failedRow = static_cast<int>(row);
                break;
            }
            indices[row] = g_addControl(screen, control);
            if (indices[row] == 0xFFFFU) {
                failure = "native_add_control_failed";
                failedRow = static_cast<int>(row);
                break;
            }
            if (!IsExpectedTextControl(control)) {
                failure = "unexpected_text_control_vtable";
                failedRow = static_cast<int>(row);
                break;
            }
            InterlockedExchangePointer(
                &g_pageControls[row], control);
        }
        if (failure == nullptr && !g_baseScreenBuild(screen)) {
            failure = "base_build_failed";
        }
        if (failure == nullptr) {
            // This is the exact successful tail used by the dormant Retail
            // build: FinalizeScreen(this, true, true, false).
            g_finalizeScreen(screen, true, true, false);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        failure = "seh_exception";
    }

    if (failure != nullptr) {
        // AddControl owns every successfully inserted control. Do not attempt
        // manual cleanup when a later step fails; the base destructor owns the
        // vector and is the only evidence-backed release path.
        return RejectPageBuild(failure, failedRow);
    }

    InterlockedExchange(&g_pageBuildState, 2);
    if (g_log != nullptr) {
        g_log(
            "m6_retail_vr_settings_page_title_applied",
            "screen=CScreenGame screen_id=24 title=VR_Settings "
            "renderer=Retail_wide_title host=isolated_base_screen");
        char detail[384]{};
        std::snprintf(
            detail, sizeof(detail),
            "screen=CScreenGame screen_id=24 "
            "host=isolated_base_screen rows=4 "
            "categories=Display,VR_Features,Comfort "
            "developer_tools=toggle_tool_menu_shortcut "
            "developer_tools_enabled=%u settings_ready=%u "
            "control_indices=%u,%u,%u,%u "
            "base_build=1 finalizer=1 original_build_called=0 "
            "original_focus_called=0 original_command_called=0",
            developerToolsEnabled ? 1U : 0U,
            developerToolsSettingReady ? 1U : 0U,
            static_cast<unsigned int>(indices[0]),
            static_cast<unsigned int>(indices[1]),
            static_cast<unsigned int>(indices[2]),
            static_cast<unsigned int>(indices[3]));
        g_log("m6_retail_vr_settings_page_built", detail);
    }
    return true;
}

void __fastcall HookGameOptionsOnFocus(
    void* screen,
    void*,
    bool focused) {
    if (!IsExpectedGameOptionsScreen(screen) ||
        InterlockedCompareExchange(
            &g_pageBuildState, 2, 2) != 2) {
        if (g_log != nullptr) {
            g_log(
                "m6_retail_vr_settings_page_focus_rejected",
                "reason=unexpected_screen_or_unbuilt_host "
                "original_focus_called=0");
        }
        return;
    }

    bool completed = false;
    bool developerToolsSettingReady = false;
    bool developerToolsEnabled = false;
    bool developerToolsLabelRefreshed = false;
    __try {
        g_baseScreenOnFocus(screen, focused);
        completed = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        completed = false;
    }
    if (focused) {
        developerToolsLabelRefreshed = RefreshDeveloperToolsLabel(
            developerToolsSettingReady,
            developerToolsEnabled);
    }
    const LONG count = InterlockedIncrement(&g_pageFocusCount);
    if (g_log != nullptr) {
        char detail[320]{};
        std::snprintf(
            detail, sizeof(detail),
            "screen=CScreenGame screen_id=24 active=%u "
            "focus_count=%ld base_focus_completed=%u "
            "developer_tools_enabled=%u settings_ready=%u "
            "label_refresh_attempted=%u label_refreshed=%u "
            "original_focus_called=0",
            focused ? 1U : 0U,
            static_cast<long>(count),
            completed ? 1U : 0U,
            developerToolsEnabled ? 1U : 0U,
            developerToolsSettingReady ? 1U : 0U,
            focused ? 1U : 0U,
            developerToolsLabelRefreshed ? 1U : 0U);
        g_log(
            completed
                ? "m6_retail_vr_settings_page_focus"
                : "m6_retail_vr_settings_page_focus_failed",
            detail);
    }
}

std::uint32_t __fastcall HookGameOptionsOnCommand(
    void* screen,
    void*,
    std::uint32_t command,
    std::uint32_t firstParameter,
    std::uint32_t secondParameter) {
    if (!IsExpectedGameOptionsScreen(screen)) {
        return 0U;
    }

    const RetailVrSettingsCategory category =
        RetailVrSettingsCategoryForCommand(command);
    if (category != RetailVrSettingsCategory::None) {
        const bool suppressEntryEdge =
            ShouldSuppressRetailVrSettingsCategoryCommand(
                command,
                InterlockedCompareExchange(
                    &g_suppressCategoryUntilEntryEdgeEnd,
                    0, 0) != 0);
        if (suppressEntryEdge) {
            const LONG suppression = InterlockedIncrement(
                &g_suppressedEntryCategoryCount);
            if (g_log != nullptr) {
                char detail[288]{};
                std::snprintf(
                    detail, sizeof(detail),
                    "screen=CScreenGame screen_id=24 command=0x%02X "
                    "category=%s suppression=%ld handled=1 "
                    "reason=same_vr_enter_edge_as_page_entry "
                    "settings_mutated=0",
                    static_cast<unsigned int>(command),
                    RetailCategoryName(category),
                    static_cast<long>(suppression));
                g_log(
                    "m6_retail_vr_settings_entry_category_suppressed",
                    detail);
            }
            return 1U;
        }
        const RetailVrSettingsAction action =
            RetailVrSettingsActionForCommand(command);
        const LONG selection =
            InterlockedIncrement(&g_categorySelectionCount);
        if (action ==
            RetailVrSettingsAction::ToggleDeveloperToolsShortcut) {
            bool previousEnabled = false;
            const bool settingReady =
                ReadVrToolMenuShortcutEnabled(previousEnabled);
            const bool requestedEnabled = !previousEnabled;
            const bool saved = settingReady &&
                SetVrToolMenuShortcutEnabled(requestedEnabled);
            bool currentEnabled = previousEnabled;
            bool currentSettingReady = false;
            bool labelRefreshed = false;
            if (saved) {
                labelRefreshed = RefreshDeveloperToolsLabel(
                    currentSettingReady, currentEnabled);
            }
            if (g_log != nullptr) {
                char detail[352]{};
                std::snprintf(
                    detail, sizeof(detail),
                    "screen=CScreenGame screen_id=24 command=0x%02X "
                    "selection=%ld parameter1=0x%08X parameter2=0x%08X "
                    "handled=1 behavior=toggle_tool_menu_shortcut "
                    "settings_ready=%u previous=%u requested=%u "
                    "saved=%u current=%u label_refreshed=%u",
                    static_cast<unsigned int>(command),
                    static_cast<long>(selection),
                    static_cast<unsigned int>(firstParameter),
                    static_cast<unsigned int>(secondParameter),
                    settingReady ? 1U : 0U,
                    previousEnabled ? 1U : 0U,
                    requestedEnabled ? 1U : 0U,
                    saved ? 1U : 0U,
                    currentEnabled ? 1U : 0U,
                    labelRefreshed ? 1U : 0U);
                g_log(
                    saved && labelRefreshed
                        ? "m6_retail_vr_settings_developer_tools_changed"
                        : "m6_retail_vr_settings_developer_tools_failed",
                    detail);
            }
            return 1U;
        }
        if (g_log != nullptr) {
            char detail[288]{};
            std::snprintf(
                detail, sizeof(detail),
                "screen=CScreenGame screen_id=24 command=0x%02X "
                "category=%s selection=%ld parameter1=0x%08X "
                "parameter2=0x%08X handled=1 "
                "behavior=category_placeholder_no_state_mutation",
                static_cast<unsigned int>(command),
                RetailCategoryName(category),
                static_cast<long>(selection),
                static_cast<unsigned int>(firstParameter),
                static_cast<unsigned int>(secondParameter));
            g_log("m6_retail_vr_settings_category_selected", detail);
        }
        return 1U;
    }

    std::uint32_t result = 0U;
    bool completed = false;
    __try {
        result = g_baseScreenOnCommand(
            screen, command, firstParameter, secondParameter);
        completed = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        completed = false;
    }
    if (g_log != nullptr &&
        (command == 1U || command == 2U || command == 7U ||
         !completed)) {
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "screen=CScreenGame screen_id=24 command=0x%02X "
            "parameter1=0x%08X parameter2=0x%08X "
            "base_completed=%u handled=%u original_command_called=0",
            static_cast<unsigned int>(command),
            static_cast<unsigned int>(firstParameter),
            static_cast<unsigned int>(secondParameter),
            completed ? 1U : 0U,
            static_cast<unsigned int>(result));
        g_log(
            completed
                ? "m6_retail_vr_settings_page_base_command"
                : "m6_retail_vr_settings_page_command_failed",
            detail);
    }
    return completed ? result : 0U;
}

bool OpenRetailVrSettingsScreen(void* screen) noexcept {
    if (!IsExpectedOptionsScreen(screen)) {
        return false;
    }
    __try {
        auto* const manager = *reinterpret_cast<void**>(
            static_cast<unsigned char*>(screen) +
            kBaseScreenManagerOffset);
        if (manager == nullptr) {
            return false;
        }
        void** const managerVtable =
            *static_cast<void***>(manager);
        if (managerVtable == nullptr) {
            return false;
        }
        void* const switchTarget =
            managerVtable[kScreenManagerSwitchSlot];
        MEMORY_BASIC_INFORMATION memory{};
        if (switchTarget == nullptr ||
            VirtualQuery(
                switchTarget, &memory, sizeof(memory)) !=
                sizeof(memory) ||
            memory.AllocationBase != g_gameClientBase ||
            memory.State != MEM_COMMIT ||
            memory.Type != MEM_IMAGE ||
            (memory.Protect & PAGE_GUARD) != 0U) {
            return false;
        }
        const DWORD protection = memory.Protect & 0xFFU;
        if (protection != PAGE_EXECUTE &&
            protection != PAGE_EXECUTE_READ &&
            protection != PAGE_EXECUTE_READWRITE &&
            protection != PAGE_EXECUTE_WRITECOPY) {
            return false;
        }
        reinterpret_cast<SwitchScreenFunction>(switchTarget)(
            manager,
            static_cast<std::uint32_t>(
                RetailVrSettingsTargetScreen(
                    kRetailOptionsVrSettingsCommand)));
        // The verified Retail manager builds the target synchronously during
        // this call. Treat a missing/failed isolated build as a rejected
        // transition instead of claiming that the page opened.
        return InterlockedCompareExchange(
                   &g_pageBuildState, 2, 2) == 2;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::uint32_t __fastcall HookOptionsOnCommand(
    void* screen,
    void*,
    std::uint32_t command,
    std::uint32_t firstParameter,
    std::uint32_t secondParameter) {
    if (IsExpectedOptionsScreen(screen) &&
        IsRetailVrSettingsCommand(command)) {
        const LONG selection =
            InterlockedIncrement(&g_selectionCount);
        if (InterlockedCompareExchange(
                &g_menuKeyEdgeVirtualKey, 0, 0) == VK_RETURN) {
            // Retail processes the injected key-down/key-up synchronously. A
            // page switch can expose its selected first row before that same
            // Enter edge returns. Suppress only that page-entry edge; the
            // bracketing input path clears this flag after native KeyUp.
            InterlockedExchange(
                &g_suppressCategoryUntilEntryEdgeEnd, 1);
        }
        const bool opened =
            OpenRetailVrSettingsScreen(screen);
        if (g_log != nullptr) {
            char detail[256]{};
            std::snprintf(
                detail, sizeof(detail),
                "screen=CScreenOptions command=0x%02X "
                "selection=%ld parameter1=0x%08X "
                "parameter2=0x%08X "
                "handled=%u behavior=isolated_base_screen_host "
                "target_screen=24 screen_change=%s",
                static_cast<unsigned int>(command),
                static_cast<long>(selection),
                static_cast<unsigned int>(firstParameter),
                static_cast<unsigned int>(secondParameter),
                opened ? 1U : 0U,
                opened ? "requested" : "rejected");
            g_log(
                "m6_retail_vr_settings_selected", detail);
        }
        if (opened) {
            return 1U;
        }
        return g_originalOptionsOnCommand(
            screen, command, firstParameter, secondParameter);
    }
    return g_originalOptionsOnCommand(
        screen, command, firstParameter, secondParameter);
}

void ResetHookState() noexcept {
    g_gameClientBase = nullptr;
    g_createLocalizedHookTarget = nullptr;
    g_optionsOnCommandHookTarget = nullptr;
    g_gameOptionsBuildHookTarget = nullptr;
    g_gameOptionsOnFocusHookTarget = nullptr;
    g_gameOptionsOnCommandHookTarget = nullptr;
    g_originalCreateLocalized = nullptr;
    g_createWide = nullptr;
    g_addControl = nullptr;
    g_originalOptionsOnCommand = nullptr;
    g_rejectedOriginalGameOptionsBuild = nullptr;
    g_rejectedOriginalGameOptionsOnFocus = nullptr;
    g_rejectedOriginalGameOptionsOnCommand = nullptr;
    g_setWideTitle = nullptr;
    g_setWideText = nullptr;
    g_baseScreenBuild = nullptr;
    g_baseScreenOnFocus = nullptr;
    g_baseScreenOnCommand = nullptr;
    g_finalizeScreen = nullptr;
    g_log = nullptr;
    g_stockTextConfig = {};
    g_stockTrailing[0] = 0U;
    g_stockTrailing[1] = 0U;
    g_stockTrailing[2] = 0U;
    InterlockedExchangePointer(&g_pageBuildScreen, nullptr);
    for (void* volatile& control : g_pageControls) {
        InterlockedExchangePointer(&control, nullptr);
    }
    InterlockedExchange(&g_rowInjectionState, 0);
    InterlockedExchange(&g_selectionCount, 0);
    InterlockedExchange(&g_stockDescriptorState, 0);
    InterlockedExchange(&g_pageBuildState, 0);
    InterlockedExchange(&g_pageFocusCount, 0);
    InterlockedExchange(&g_categorySelectionCount, 0);
    InterlockedExchange(&g_menuKeyEdgeVirtualKey, 0);
    InterlockedExchange(
        &g_suppressCategoryUntilEntryEdgeEnd, 0);
    InterlockedExchange(&g_suppressedEntryCategoryCount, 0);
}

} // namespace
#endif

void BeginRetailVrSettingsMenuKeyEdge(
    int virtualKey) noexcept {
#if defined(_M_IX86)
    InterlockedExchange(
        &g_menuKeyEdgeVirtualKey,
        static_cast<LONG>(virtualKey));
#else
    (void)virtualKey;
#endif
}

void EndRetailVrSettingsMenuKeyEdge(
    int virtualKey) noexcept {
#if defined(_M_IX86)
    InterlockedCompareExchange(
        &g_menuKeyEdgeVirtualKey,
        0,
        static_cast<LONG>(virtualKey));
    if (virtualKey == VK_RETURN) {
        InterlockedExchange(
            &g_suppressCategoryUntilEntryEdgeEnd, 0);
    }
#else
    (void)virtualKey;
#endif
}

bool InstallRetailVrSettingsMenuProbe(
    HMODULE gameClientModule,
    RendererProbeLogFunction log) noexcept {
#if !defined(_M_IX86)
    (void)gameClientModule;
    if (log != nullptr) {
        log(
            "m6_retail_vr_settings_rejected",
            "reason=x86_gameclient_required");
    }
    return false;
#else
    AcquireSRWLockExclusive(&g_installLock);
    if (g_createLocalizedHookTarget != nullptr &&
        g_optionsOnCommandHookTarget != nullptr &&
        g_gameOptionsBuildHookTarget != nullptr &&
        g_gameOptionsOnFocusHookTarget != nullptr &&
        g_gameOptionsOnCommandHookTarget != nullptr) {
        ReleaseSRWLockExclusive(&g_installLock);
        return true;
    }

    if (gameClientModule == nullptr || log == nullptr) {
        ReleaseSRWLockExclusive(&g_installLock);
        return false;
    }
    if (!ExpectedImageAndMenuTargetsMatch(gameClientModule)) {
        ReleaseSRWLockExclusive(&g_installLock);
        log(
            "m6_retail_vr_settings_rejected",
            "reason=GameOrig_1.0.314.0_menu_target_mismatch");
        return false;
    }

    const MH_STATUS initialize = MH_Initialize();
    if (initialize != MH_OK &&
        initialize != MH_ERROR_ALREADY_INITIALIZED) {
        ReleaseSRWLockExclusive(&g_installLock);
        log(
            "m6_retail_vr_settings_rejected",
            MH_StatusToString(initialize));
        return false;
    }

    auto* const base =
        reinterpret_cast<unsigned char*>(gameClientModule);
    void* const localizedTarget =
        base + kCreateLocalizedTextControlRva;
    void* const optionsOnCommandTarget =
        base + kOptionsOnCommandRva;
    void* const gameBuildTarget =
        base + kGameOptionsBuildRva;
    void* const gameOnFocusTarget =
        base + kGameOptionsOnFocusRva;
    void* const gameOnCommandTarget =
        base + kGameOptionsOnCommandRva;
    g_gameClientBase = base;
    g_createWide =
        reinterpret_cast<CreateWideTextControlFunction>(
            base + kCreateWideTextControlRva);
    g_addControl = reinterpret_cast<AddControlFunction>(
        base + kAddControlRva);
    g_setWideTitle = reinterpret_cast<SetWideTitleFunction>(
        base + kSetWideTitleRva);
    g_setWideText = reinterpret_cast<SetWideTextFunction>(
        base + kTextControlSetWideTextRva);
    g_baseScreenBuild = reinterpret_cast<ScreenBuildFunction>(
        base + kBaseScreenBuildRva);
    g_baseScreenOnFocus =
        reinterpret_cast<ScreenOnFocusFunction>(
            base + kBaseScreenOnFocusRva);
    g_baseScreenOnCommand =
        reinterpret_cast<OptionsOnCommandFunction>(
            base + kBaseScreenOnCommandRva);
    g_finalizeScreen = reinterpret_cast<FinalizeScreenFunction>(
        base + kFinalizeScreenRva);
    g_log = log;
    g_stockTextConfig = {};
    g_stockTrailing[0] = 0U;
    g_stockTrailing[1] = 0U;
    g_stockTrailing[2] = 0U;
    InterlockedExchangePointer(&g_pageBuildScreen, nullptr);
    for (void* volatile& control : g_pageControls) {
        InterlockedExchangePointer(&control, nullptr);
    }
    InterlockedExchange(&g_rowInjectionState, 0);
    InterlockedExchange(&g_selectionCount, 0);
    InterlockedExchange(&g_stockDescriptorState, 0);
    InterlockedExchange(&g_pageBuildState, 0);
    InterlockedExchange(&g_pageFocusCount, 0);
    InterlockedExchange(&g_categorySelectionCount, 0);
    InterlockedExchange(&g_menuKeyEdgeVirtualKey, 0);
    InterlockedExchange(
        &g_suppressCategoryUntilEntryEdgeEnd, 0);
    InterlockedExchange(&g_suppressedEntryCategoryCount, 0);

    MH_STATUS status = MH_CreateHook(
        localizedTarget,
        reinterpret_cast<void*>(
            &HookCreateLocalizedTextControl),
        reinterpret_cast<void**>(
            &g_originalCreateLocalized));
    if (status == MH_OK) {
        status = MH_CreateHook(
            gameBuildTarget,
            reinterpret_cast<void*>(&HookGameOptionsBuild),
            reinterpret_cast<void**>(
                &g_rejectedOriginalGameOptionsBuild));
    }
    if (status == MH_OK) {
        status = MH_CreateHook(
            gameOnFocusTarget,
            reinterpret_cast<void*>(&HookGameOptionsOnFocus),
            reinterpret_cast<void**>(
                &g_rejectedOriginalGameOptionsOnFocus));
    }
    if (status == MH_OK) {
        status = MH_CreateHook(
            gameOnCommandTarget,
            reinterpret_cast<void*>(&HookGameOptionsOnCommand),
            reinterpret_cast<void**>(
                &g_rejectedOriginalGameOptionsOnCommand));
    }
    if (status == MH_OK) {
        status = MH_CreateHook(
            optionsOnCommandTarget,
            reinterpret_cast<void*>(&HookOptionsOnCommand),
            reinterpret_cast<void**>(
                &g_originalOptionsOnCommand));
    }
    if (status == MH_OK) {
        status = MH_EnableHook(localizedTarget);
    }
    if (status == MH_OK) {
        status = MH_EnableHook(gameBuildTarget);
    }
    if (status == MH_OK) {
        status = MH_EnableHook(gameOnFocusTarget);
    }
    if (status == MH_OK) {
        status = MH_EnableHook(gameOnCommandTarget);
    }
    if (status == MH_OK) {
        // Route command 0x3A only after every target-screen lifecycle hook is
        // active, so no partial install can enter the rejected Retail path.
        status = MH_EnableHook(optionsOnCommandTarget);
    }
    if (status != MH_OK) {
        MH_DisableHook(optionsOnCommandTarget);
        MH_DisableHook(gameOnCommandTarget);
        MH_DisableHook(gameOnFocusTarget);
        MH_DisableHook(gameBuildTarget);
        MH_DisableHook(localizedTarget);
        MH_RemoveHook(optionsOnCommandTarget);
        MH_RemoveHook(gameOnCommandTarget);
        MH_RemoveHook(gameOnFocusTarget);
        MH_RemoveHook(gameBuildTarget);
        MH_RemoveHook(localizedTarget);
        ResetHookState();
        ReleaseSRWLockExclusive(&g_installLock);
        log(
            "m6_retail_vr_settings_rejected",
            MH_StatusToString(status));
        return false;
    }

    g_createLocalizedHookTarget = localizedTarget;
    g_optionsOnCommandHookTarget = optionsOnCommandTarget;
    g_gameOptionsBuildHookTarget = gameBuildTarget;
    g_gameOptionsOnFocusHookTarget = gameOnFocusTarget;
    g_gameOptionsOnCommandHookTarget = gameOnCommandTarget;
    ReleaseSRWLockExclusive(&g_installLock);
    log(
        "m6_retail_vr_settings_armed",
        "screen=CScreenOptions native_factory=CLTGUITextCtrl "
        "insertion_anchor=IDS_PERFORMANCE "
        "command=0x3A help=IDS_HELP_OPTIONS "
        "target_guards=image,full_inherited_vtable,commands,factory,signatures "
        "selection_behavior=isolated_base_screen_host_on_screen_24 "
        "page_title=VR_Settings developer_tools_packaged_default=enabled "
        "categories=Display,VR_Features,Comfort "
        "developer_tools=toggle_tool_menu_shortcut "
        "developer_tools_labels=On,Off "
        "original_build_focus_command=bypassed "
        "same_vr_entry_accept_category_suppressed=1 "
        "isolated_host_prior_live_evidence=1 "
        "developer_tools_toggle_live_gate=pending");
    return true;
#endif
}

} // namespace condemnedvr
