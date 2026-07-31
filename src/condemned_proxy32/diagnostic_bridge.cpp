#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d9.h>

#include <MinHook.h>

#include "module_identity.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {

using ResetFunction = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using PresentFunction = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);

INIT_ONCE g_installOnce = INIT_ONCE_STATIC_INIT;
BOOL g_installResult = FALSE;
ResetFunction g_originalReset = nullptr;
PresentFunction g_originalPresent = nullptr;
SRWLOCK g_logLock = SRWLOCK_INIT;
volatile LONG g_presentCount = 0;
volatile LONG g_presentDetailsLogged = 0;
LARGE_INTEGER g_frequency{};
LARGE_INTEGER g_windowStart{};

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

void AppendLine(const char* line) noexcept {
    if (line == nullptr) {
        return;
    }
    wchar_t path[MAX_PATH]{};
    if (!ModuleSiblingPath(L"condemnedvr-d3d9.log", path)) {
        return;
    }

    AcquireSRWLockExclusive(&g_logLock);
    const HANDLE file = CreateFileW(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        const std::size_t length = std::strlen(line);
        DWORD written = 0;
        if (length <= MAXDWORD) {
            WriteFile(
                file,
                line,
                static_cast<DWORD>(length),
                &written,
                nullptr);
            static constexpr char newline[] = "\r\n";
            WriteFile(
                file,
                newline,
                static_cast<DWORD>(sizeof(newline) - 1),
                &written,
                nullptr);
        }
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&g_logLock);
}

void LogEvent(const char* event, const char* detail) noexcept {
    char line[512]{};
    sprintf_s(
        line,
        "{\"event\":\"%s\",\"detail\":\"%s\"}",
        event == nullptr ? "" : event,
        detail == nullptr ? "" : detail);
    AppendLine(line);
}

void LogPresentDetails(IDirect3DDevice9* device) noexcept {
    if (device == nullptr ||
        InterlockedCompareExchange(&g_presentDetailsLogged, 1, 0) != 0) {
        return;
    }

    D3DDEVICE_CREATION_PARAMETERS creation{};
    const HRESULT creationResult = device->GetCreationParameters(&creation);

    D3DPRESENT_PARAMETERS presentation{};
    HRESULT presentationResult = E_FAIL;
    IDirect3DSwapChain9* swapChain = nullptr;
    if (SUCCEEDED(device->GetSwapChain(0, &swapChain)) &&
        swapChain != nullptr) {
        presentationResult = swapChain->GetPresentParameters(&presentation);
        swapChain->Release();
    }

    D3DSURFACE_DESC backBuffer{};
    HRESULT backBufferResult = E_FAIL;
    IDirect3DSurface9* surface = nullptr;
    if (SUCCEEDED(device->GetBackBuffer(
            0, 0, D3DBACKBUFFER_TYPE_MONO, &surface)) &&
        surface != nullptr) {
        backBufferResult = surface->GetDesc(&backBuffer);
        surface->Release();
    }

    char line[1536]{};
    sprintf_s(
        line,
        "{\"event\":\"present_observed\","
        "\"creation_hr\":\"0x%08lX\",\"adapter\":%u,"
        "\"device_type\":%u,\"focus_window\":\"%p\","
        "\"behavior_flags\":%lu,"
        "\"present_hr\":\"0x%08lX\",\"windowed\":%s,"
        "\"device_window\":\"%p\",\"backbuffer_width\":%u,"
        "\"backbuffer_height\":%u,\"backbuffer_format\":%u,"
        "\"backbuffer_count\":%u,\"swap_effect\":%u,"
        "\"multisample_type\":%u,\"multisample_quality\":%lu,"
        "\"presentation_interval\":%u,"
        "\"surface_hr\":\"0x%08lX\",\"surface_width\":%u,"
        "\"surface_height\":%u,\"surface_format\":%u,"
        "\"surface_multisample_type\":%u,"
        "\"surface_multisample_quality\":%lu}",
        static_cast<unsigned long>(creationResult),
        creation.AdapterOrdinal,
        static_cast<unsigned>(creation.DeviceType),
        creation.hFocusWindow,
        creation.BehaviorFlags,
        static_cast<unsigned long>(presentationResult),
        presentation.Windowed ? "true" : "false",
        presentation.hDeviceWindow,
        presentation.BackBufferWidth,
        presentation.BackBufferHeight,
        static_cast<unsigned>(presentation.BackBufferFormat),
        presentation.BackBufferCount,
        static_cast<unsigned>(presentation.SwapEffect),
        static_cast<unsigned>(presentation.MultiSampleType),
        presentation.MultiSampleQuality,
        presentation.PresentationInterval,
        static_cast<unsigned long>(backBufferResult),
        backBuffer.Width,
        backBuffer.Height,
        static_cast<unsigned>(backBuffer.Format),
        static_cast<unsigned>(backBuffer.MultiSampleType),
        backBuffer.MultiSampleQuality);
    AppendLine(line);
}

HRESULT STDMETHODCALLTYPE HookReset(
    IDirect3DDevice9* device,
    D3DPRESENT_PARAMETERS* parameters) {
    if (g_originalReset == nullptr) {
        return D3DERR_INVALIDCALL;
    }

    const HRESULT result = g_originalReset(device, parameters);
    InterlockedExchange(&g_presentDetailsLogged, 0);

    char line[768]{};
    if (parameters != nullptr) {
        sprintf_s(
            line,
            "{\"event\":\"reset\",\"result\":\"0x%08lX\","
            "\"windowed\":%s,\"backbuffer_width\":%u,"
            "\"backbuffer_height\":%u,\"backbuffer_format\":%u,"
            "\"swap_effect\":%u,\"presentation_interval\":%u}",
            static_cast<unsigned long>(result),
            parameters->Windowed ? "true" : "false",
            parameters->BackBufferWidth,
            parameters->BackBufferHeight,
            static_cast<unsigned>(parameters->BackBufferFormat),
            static_cast<unsigned>(parameters->SwapEffect),
            parameters->PresentationInterval);
    } else {
        sprintf_s(
            line,
            "{\"event\":\"reset\",\"result\":\"0x%08lX\","
            "\"parameters\":null}",
            static_cast<unsigned long>(result));
    }
    AppendLine(line);
    return result;
}

HRESULT STDMETHODCALLTYPE HookPresent(
    IDirect3DDevice9* device,
    const RECT* source,
    const RECT* destination,
    HWND overrideWindow,
    const RGNDATA* dirtyRegion) {
    if (g_originalPresent == nullptr) {
        return D3DERR_INVALIDCALL;
    }

    (void)source;
    (void)destination;
    (void)overrideWindow;
    (void)dirtyRegion;
    LogPresentDetails(device);

    const LONG count = InterlockedIncrement(&g_presentCount);
    if (count == 1) {
        QueryPerformanceCounter(&g_windowStart);
    } else if (count % 300 == 0 && g_frequency.QuadPart > 0) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const double seconds = static_cast<double>(
            now.QuadPart - g_windowStart.QuadPart) /
            static_cast<double>(g_frequency.QuadPart);
        const double framesPerSecond = seconds > 0.0 ? 299.0 / seconds : 0.0;
        char line[384]{};
        sprintf_s(
            line,
            "{\"event\":\"present_window\",\"count\":%ld,"
            "\"seconds\":%.6f,\"fps\":%.3f}",
            count,
            seconds,
            framesPerSecond);
        AppendLine(line);
        g_windowStart = now;
    }

    return g_originalPresent(
        device, source, destination, overrideWindow, dirtyRegion);
}

BOOL CALLBACK InstallOnce(PINIT_ONCE, PVOID, PVOID*) noexcept {
    wchar_t executablePath[MAX_PATH]{};
    const DWORD pathLength = GetModuleFileNameW(
        nullptr, executablePath, MAX_PATH);
    if (pathLength == 0 || pathLength >= MAX_PATH) {
        LogEvent("bridge_rejected", "executable_path_failed");
        return TRUE;
    }
    const condemnedvr::ModuleIdentityResult identity =
        condemnedvr::VerifyCondemnedExecutable(executablePath);
    if (identity != condemnedvr::ModuleIdentityResult::ok) {
        LogEvent(
            "bridge_rejected",
            condemnedvr::ModuleIdentityResultName(identity));
        return TRUE;
    }

    const HMODULE systemD3D9 = LoadLibraryExW(
        L"d3d9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (systemD3D9 == nullptr) {
        LogEvent("bridge_rejected", "system_d3d9_load_failed");
        return TRUE;
    }
    using Direct3DCreate9Function = IDirect3D9*(WINAPI*)(UINT);
    const auto createDirect3D = reinterpret_cast<Direct3DCreate9Function>(
        GetProcAddress(systemD3D9, "Direct3DCreate9"));
    if (createDirect3D == nullptr) {
        LogEvent("bridge_rejected", "direct3dcreate9_missing");
        return TRUE;
    }

    const HWND window = CreateWindowExW(
        0,
        L"STATIC",
        L"CondemnedVrD3D9Probe",
        WS_OVERLAPPED,
        0,
        0,
        32,
        32,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (window == nullptr) {
        LogEvent("bridge_rejected", "probe_window_failed");
        return TRUE;
    }

    IDirect3D9* direct3D = createDirect3D(D3D_SDK_VERSION);
    if (direct3D == nullptr) {
        DestroyWindow(window);
        LogEvent("bridge_rejected", "probe_d3d9_failed");
        return TRUE;
    }

    D3DPRESENT_PARAMETERS parameters{};
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = window;
    parameters.BackBufferWidth = 32;
    parameters.BackBufferHeight = 32;
    parameters.BackBufferFormat = D3DFMT_UNKNOWN;

    IDirect3DDevice9* device = nullptr;
    const HRESULT createResult = direct3D->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        window,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
        &parameters,
        &device);
    if (FAILED(createResult) || device == nullptr) {
        direct3D->Release();
        DestroyWindow(window);
        LogEvent("bridge_rejected", "probe_device_failed");
        return TRUE;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    void* const resetTarget = vtable[16];
    void* const presentTarget = vtable[17];
    const MH_STATUS initialize = MH_Initialize();
    if (initialize != MH_OK &&
        initialize != MH_ERROR_ALREADY_INITIALIZED) {
        device->Release();
        direct3D->Release();
        DestroyWindow(window);
        LogEvent("bridge_rejected", "minhook_initialize_failed");
        return TRUE;
    }

    MH_STATUS status = MH_CreateHook(
        resetTarget,
        reinterpret_cast<void*>(&HookReset),
        reinterpret_cast<void**>(&g_originalReset));
    if (status == MH_OK) {
        status = MH_CreateHook(
            presentTarget,
            reinterpret_cast<void*>(&HookPresent),
            reinterpret_cast<void**>(&g_originalPresent));
    }
    if (status == MH_OK) {
        status = MH_QueueEnableHook(resetTarget);
    }
    if (status == MH_OK) {
        status = MH_QueueEnableHook(presentTarget);
    }
    if (status == MH_OK) {
        status = MH_ApplyQueued();
    }

    device->Release();
    direct3D->Release();
    DestroyWindow(window);
    if (status != MH_OK) {
        g_originalReset = nullptr;
        g_originalPresent = nullptr;
        LogEvent("bridge_rejected", "d3d9_hook_failed");
        return TRUE;
    }

    QueryPerformanceFrequency(&g_frequency);
    QueryPerformanceCounter(&g_windowStart);
    char line[384]{};
    sprintf_s(
        line,
        "{\"event\":\"hooks_installed\",\"reset\":\"%p\","
        "\"present\":\"%p\",\"capture_enabled\":false}",
        resetTarget,
        presentTarget);
    AppendLine(line);
    g_installResult = TRUE;
    return TRUE;
}

} // namespace

extern "C" __declspec(dllexport) BOOL __cdecl
CondemnedVr_InstallD3D9Hooks() {
    if (!InitOnceExecuteOnce(
            &g_installOnce, InstallOnce, nullptr, nullptr)) {
        return FALSE;
    }
    return g_installResult;
}
