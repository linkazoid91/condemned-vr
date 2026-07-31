#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "binding_input.h"
#include "module_identity.h"
#include "renderer_probe.h"

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

    char line[512]{};
    const int length = sprintf_s(
        line,
        "{\"event\":\"%s\",\"detail\":\"%s\"}\r\n",
        event == nullptr ? "" : event,
        detail == nullptr ? "" : detail);
    if (length > 0) {
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
    function(masterDatabase);

    if (CommandLineContains(L"-condemnedvr-m4-locomotion")) {
        condemnedvr::InstallBindingLocomotionHook(
            g_original, g_bridge, AppendLoaderEvent);
    }
    if (CommandLineContains(L"-condemnedvr-m3-probe")) {
        condemnedvr::ProbeRendererInterfaces(
            masterDatabase, AppendLoaderEvent);
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
        HMODULE const diagnosticBridge =
            stereoDiagnostic || doubleRenderDiagnostic ||
                eyeOffsetDiagnostic || continuousStereoTuning
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
            continuousStereoTuning);
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
