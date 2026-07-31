#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdio>

int wmain(int argumentCount, wchar_t** arguments) {
    if (argumentCount != 2) {
        std::fputs("Expected the bridge DLL path.\n", stderr);
        return 2;
    }
    const HMODULE bridge = LoadLibraryExW(
        arguments[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (bridge == nullptr) {
        std::fprintf(stderr, "Bridge load failed: %lu\n", GetLastError());
        return 1;
    }
    using InstallFunction = BOOL(__cdecl*)();
    const auto install = reinterpret_cast<InstallFunction>(
        GetProcAddress(bridge, "CondemnedVr_InstallD3D9Hooks"));
    if (install == nullptr) {
        std::fputs("Bridge install export is missing.\n", stderr);
        FreeLibrary(bridge);
        return 1;
    }
    if (GetProcAddress(
            bridge, "CondemnedVr_SetMenuActive") == nullptr) {
        std::fputs("Bridge menu-state export is missing.\n", stderr);
        FreeLibrary(bridge);
        return 1;
    }
    if (GetProcAddress(
            bridge, "CondemnedVr_SubmitHapticRequest") == nullptr) {
        std::fputs("Bridge haptic export is missing.\n", stderr);
        FreeLibrary(bridge);
        return 1;
    }
    if (GetProcAddress(
            bridge, "CondemnedVr_WaitForNewRenderRequest") == nullptr) {
        std::fputs("Bridge frame-pacing export is missing.\n", stderr);
        FreeLibrary(bridge);
        return 1;
    }
    const BOOL installed = install();
    FreeLibrary(bridge);
    if (installed) {
        std::fputs("Bridge accepted a non-Condemned test executable.\n", stderr);
        return 1;
    }
    std::puts("Condemned bridge version guard test passed.");
    return 0;
}
