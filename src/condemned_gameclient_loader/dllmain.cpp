#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "arm_ik_discovery.h"
#include "arm_ik_integration.h"
#include "binding_input.h"
#include "loader_event_format.h"
#include "module_identity.h"
#include "renderer_probe.h"
#include "retail_menu_integration.h"

#include <cstdio>
#include <cwchar>

extern "C" int CondemnedVrGameClientCompatData = 0;

namespace {

INIT_ONCE g_originalOnce = INIT_ONCE_STATIC_INIT;
INIT_ONCE g_bridgeOnce = INIT_ONCE_STATIC_INIT;
HMODULE g_original = nullptr;
HMODULE g_bridge = nullptr;
FARPROC g_getBuildNumber = nullptr;
FARPROC g_setMasterDatabase = nullptr;

bool CommandLineContains(const wchar_t* option) noexcept {
    const wchar_t* const commandLine = GetCommandLineW();
    return commandLine != nullptr && option != nullptr &&
        std::wcsstr(commandLine, option) != nullptr;
}

bool ModuleSiblingPath(
    const wchar_t* fileName,
    wchar_t (&path)[MAX_PATH]) noexcept {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ModuleSiblingPath),
            &self)) {
        return false;
    }
    const DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return false;
    }
    wchar_t* separator = wcsrchr(path, L'\\');
    if (separator == nullptr) {
        return false;
    }
    *(separator + 1) = L'\0';
    return wcscat_s(path, fileName) == 0;
}

void AppendLoaderEvent(const char* event, const char* detail) noexcept {
    wchar_t logPath[MAX_PATH]{};
    if (!ModuleSiblingPath(L"condemnedvr-loader.log", logPath)) {
        return;
    }
    const HANDLE file = CreateFileW(
        logPath,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    char line[condemnedvr::kLoaderEventLineCapacity]{};
    const std::size_t length = condemnedvr::FormatLoaderEventLine(
        line, sizeof(line), event, detail);
    if (length > 0U) {
        DWORD bytesWritten = 0;
        WriteFile(
            file,
            line,
            static_cast<DWORD>(length),
            &bytesWritten,
            nullptr);
    }
    CloseHandle(file);
}

BOOL CALLBACK LoadBridge(
    PINIT_ONCE once,
    PVOID parameter,
    PVOID* context) noexcept {
    (void)once;
    (void)parameter;
    (void)context;

    wchar_t path[MAX_PATH]{};
    if (!ModuleSiblingPath(L"condemnedvr-d3d9.dll", path)) {
        AppendLoaderEvent("bridge_rejected", "sibling_path_failed");
        return TRUE;
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        return TRUE;
    }

    g_bridge = LoadLibraryExW(
        path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (g_bridge == nullptr) {
        AppendLoaderEvent("bridge_rejected", "load_library_failed");
        return TRUE;
    }

    using InstallFunction = BOOL(__cdecl*)();
    const auto install = reinterpret_cast<InstallFunction>(
        GetProcAddress(g_bridge, "CondemnedVr_InstallD3D9Hooks"));
    if (install == nullptr || install() == FALSE) {
        AppendLoaderEvent("bridge_rejected", "hook_install_failed");
        FreeLibrary(g_bridge);
        g_bridge = nullptr;
        return TRUE;
    }

    AppendLoaderEvent("bridge_loaded", "verified_d3d9_transport");
    return TRUE;
}

void EnsureBridge() noexcept {
    InitOnceExecuteOnce(&g_bridgeOnce, LoadBridge, nullptr, nullptr);
}

BOOL CALLBACK LoadOriginal(
    PINIT_ONCE once,
    PVOID parameter,
    PVOID* context) noexcept {
    (void)once;
    (void)parameter;
    (void)context;

    wchar_t path[MAX_PATH]{};
    if (!ModuleSiblingPath(L"GameOrig.dll", path)) {
        AppendLoaderEvent("original_rejected", "sibling_path_failed");
        return TRUE;
    }

    const condemnedvr::ModuleIdentityResult identity =
        condemnedvr::VerifyCondemnedGameClient(path);
    if (identity != condemnedvr::ModuleIdentityResult::ok) {
        AppendLoaderEvent(
            "original_rejected",
            condemnedvr::ModuleIdentityResultName(identity));
        return TRUE;
    }

    g_original = LoadLibraryExW(
        path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (g_original == nullptr) {
        AppendLoaderEvent("original_rejected", "load_library_failed");
        return TRUE;
    }

    g_getBuildNumber = GetProcAddress(g_original, "GetBuildNumber");
    g_setMasterDatabase = GetProcAddress(g_original, "SetMasterDatabase");
    if (g_getBuildNumber == nullptr || g_setMasterDatabase == nullptr) {
        AppendLoaderEvent("original_rejected", "required_export_missing");
        FreeLibrary(g_original);
        g_original = nullptr;
        g_getBuildNumber = nullptr;
        g_setMasterDatabase = nullptr;
        return TRUE;
    }

    AppendLoaderEvent("original_loaded", "verified_1.0.314.0");
    return TRUE;
}

HMODULE OriginalModule() noexcept {
    InitOnceExecuteOnce(&g_originalOnce, LoadOriginal, nullptr, nullptr);
    return g_original;
}

} // namespace

extern "C" unsigned long GetBuildNumber() {
    EnsureBridge();
    using Function = unsigned long(__cdecl*)();
    if (OriginalModule() == nullptr || g_getBuildNumber == nullptr) {
        return 0UL;
    }
    const auto function = reinterpret_cast<Function>(g_getBuildNumber);
    return function();
}

extern "C" void SetMasterDatabase(void* masterDatabase) {
    EnsureBridge();
    using Function = void(__cdecl*)(void*);
    if (OriginalModule() == nullptr || g_setMasterDatabase == nullptr) {
        return;
    }
    const auto function = reinterpret_cast<Function>(g_setMasterDatabase);
    if (CommandLineContains(
            L"-condemnedvr-m6-retail-vr-settings")) {
        // CScreenOptions can be constructed during Retail's database setup,
        // so this opt-in hook must arm before forwarding that lifecycle call.
        condemnedvr::InstallRetailVrSettingsMenuProbe(
            g_original, AppendLoaderEvent);
    }
    function(masterDatabase);

    if (CommandLineContains(L"-condemnedvr-m4-locomotion")) {
        condemnedvr::InstallBindingLocomotionHook(
            g_original, g_bridge, AppendLoaderEvent);
    }
    if (CommandLineContains(L"-condemnedvr-m4-interaction")) {
        condemnedvr::InstallBindingInteractionHook(
            masterDatabase, g_original, g_bridge,
            AppendLoaderEvent);
    }
    if (CommandLineContains(L"-condemnedvr-m4-core-actions")) {
        condemnedvr::InstallBindingCoreActionsHook(
            masterDatabase, g_original, g_bridge,
            AppendLoaderEvent,
            CommandLineContains(
                L"-condemnedvr-m5-forensic-memory-probe"));
    }
    if (CommandLineContains(L"-condemnedvr-m4-haptics")) {
        condemnedvr::InstallControllerHaptics(
            g_bridge, AppendLoaderEvent);
    }
    if (CommandLineContains(L"-condemnedvr-m4-turning")) {
        condemnedvr::InstallBindingTurningHook(
            g_original, g_bridge, AppendLoaderEvent);
    }
    if (CommandLineContains(L"-condemnedvr-m5-head-aim")) {
        condemnedvr::InstallHeadAimHooks(
            masterDatabase, g_original,
            AppendLoaderEvent,
            CommandLineContains(
                L"-condemnedvr-m5-aim-path-probe"),
            CommandLineContains(
                L"-condemnedvr-m5-controller-melee-aim"),
            CommandLineContains(
                L"-condemnedvr-m5-physical-melee-probe"),
            CommandLineContains(
                L"-condemnedvr-m5-physical-melee-wall-proxy"),
            CommandLineContains(
                L"-condemnedvr-m5-physical-melee-collider-debug"),
            CommandLineContains(
                L"-condemnedvr-m5-physical-melee-contact-damage"),
            CommandLineContains(
                L"-condemnedvr-m5-physical-melee-visual-proxy"),
            CommandLineContains(
                L"-condemnedvr-m5-weapon-grip-calibration"),
            CommandLineContains(
                L"-condemnedvr-m5-two-handed-melee"));
    }
    const bool armIkRightArm = CommandLineContains(
        L"-condemnedvr-arm-ik-right-arm");
    const bool slideNodeControlTest = CommandLineContains(
        L"-condemnedvr-m5-slide-control-test");
    const bool armIkRightHandProof = CommandLineContains(
        L"-condemnedvr-arm-ik-right-hand-proof");
    const bool menuControls = CommandLineContains(
        L"-condemnedvr-m6-menu-controls");
    const bool headBobDiagnostic = CommandLineContains(
        L"-condemnedvr-m5-headbob-diagnostic");
    const bool postProfileHeadBobZero = CommandLineContains(
        L"-condemnedvr-m5-retail-headbob-post-profile-zero");
    const bool postProfileHeadBobOne = CommandLineContains(
        L"-condemnedvr-m5-retail-headbob-post-profile-one");
    const bool postProfileHeadBobConflict =
        postProfileHeadBobZero && postProfileHeadBobOne;
    const bool postProfileHeadBob =
        (postProfileHeadBobZero || postProfileHeadBobOne) &&
        !postProfileHeadBobConflict;
    const int postProfileHeadBobCommandValue =
        postProfileHeadBobOne ? 1 : 0;
    bool armIkLifecycleObserverReady = false;
    if (postProfileHeadBobConflict) {
        AppendLoaderEvent(
            "m5_retail_headbob_post_profile_rejected",
            "reason=conflicting_post_profile_flags");
    }
    if (CommandLineContains(L"-condemnedvr-m4-menu") ||
        menuControls || headBobDiagnostic || postProfileHeadBob) {
        armIkLifecycleObserverReady =
            condemnedvr::InstallMenuToggleHook(
                masterDatabase, g_original, g_bridge,
                AppendLoaderEvent, menuControls,
                headBobDiagnostic, postProfileHeadBob,
                postProfileHeadBobCommandValue);
    }
    if (CommandLineContains(L"-condemnedvr-m3-probe")) {
        condemnedvr::ProbeRendererInterfaces(
            masterDatabase, AppendLoaderEvent);
    }
    if (CommandLineContains(L"-condemnedvr-arm-ik-discovery")) {
        condemnedvr::InstallArmIkDiscovery(
            masterDatabase, g_original, AppendLoaderEvent);
    }
    if (CommandLineContains(
            L"-condemnedvr-m5-weapon-model-discovery")) {
        condemnedvr::InstallWeaponModelDiscovery(
            masterDatabase, g_original, AppendLoaderEvent);
    }
    if (armIkRightArm) {
        if (armIkLifecycleObserverReady) {
            condemnedvr::InstallArmIkRightArm(
                masterDatabase, g_original, AppendLoaderEvent);
        } else {
            AppendLoaderEvent(
                "arm_ik_right_arm_rejected",
                "reason=retail_game_state_lifecycle_observer_not_armed "
                "requires=-condemnedvr-m4-menu");
        }
    } else if (armIkRightHandProof) {
        if (armIkLifecycleObserverReady) {
            condemnedvr::InstallArmIkRightHandProof(
                masterDatabase, g_original, AppendLoaderEvent);
        } else {
            AppendLoaderEvent(
                "arm_ik_right_hand_proof_rejected",
                "reason=retail_game_state_lifecycle_observer_not_armed "
                "requires=-condemnedvr-m4-menu");
        }
    }
    if (slideNodeControlTest) {
        if (!armIkRightArm ||
            !condemnedvr::SetSlideNodeControlTestEnabled(true)) {
            AppendLoaderEvent(
                "m5_slide_node_control_rejected",
                "reason=requires_verified_full_arm_control "
                "requires=-condemnedvr-arm-ik-right-arm");
        }
    }
    if (CommandLineContains(L"-condemnedvr-m3-pass-through")) {
        const bool stereoDiagnostic = CommandLineContains(
            L"-condemnedvr-m3-stereo-diagnostic");
        const bool doubleRenderDiagnostic = CommandLineContains(
            L"-condemnedvr-m3-double-render-diagnostic");
        const bool eyeOffsetDiagnostic = CommandLineContains(
            L"-condemnedvr-m3-eye-offset-diagnostic");
        const bool continuousStereoTuning = CommandLineContains(
            L"-condemnedvr-m3-stereo-tuning");
        const bool controllerRecenter = CommandLineContains(
            L"-condemnedvr-m4-recenter");
        const bool headAim = CommandLineContains(
            L"-condemnedvr-m5-head-aim");
        HMODULE const diagnosticBridge =
            stereoDiagnostic || doubleRenderDiagnostic ||
                eyeOffsetDiagnostic || continuousStereoTuning ||
                controllerRecenter || headAim
            ? g_bridge
            : nullptr;
        condemnedvr::InstallRendererPassThroughProbe(
            masterDatabase,
            AppendLoaderEvent,
            diagnosticBridge,
            doubleRenderDiagnostic,
            CommandLineContains(L"-condemnedvr-m3-camera-read-probe"),
            eyeOffsetDiagnostic,
            CommandLineContains(
                L"-condemnedvr-m3-reverse-eye-offset-diagnostic"),
            CommandLineContains(
                L"-condemnedvr-m3-zero-eye-offset-diagnostic"),
            continuousStereoTuning,
            controllerRecenter,
            headAim);
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
