#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>

int wmain(int argumentCount, wchar_t** arguments) {
    if (argumentCount != 2) {
        std::fputs("Expected the loader DLL path.\n", stderr);
        return 2;
    }

    const HMODULE loader = LoadLibraryExW(
        arguments[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (loader == nullptr) {
        std::fprintf(
            stderr, "LoadLibraryExW failed: %lu\n", GetLastError());
        return 1;
    }

    using GetBuildNumberFunction = unsigned long(__cdecl*)();
    using SetMasterDatabaseFunction = void(__cdecl*)(void*);
    const auto getBuildNumber =
        reinterpret_cast<GetBuildNumberFunction>(
            GetProcAddress(loader, "GetBuildNumber"));
    const auto setMasterDatabase =
        reinterpret_cast<SetMasterDatabaseFunction>(
            GetProcAddress(loader, "SetMasterDatabase"));
    if (getBuildNumber == nullptr || setMasterDatabase == nullptr) {
        std::fputs("Required loader export is missing.\n", stderr);
        FreeLibrary(loader);
        return 1;
    }

    const unsigned long buildNumber = getBuildNumber();
    setMasterDatabase(nullptr);
    FreeLibrary(loader);
    if (buildNumber != 0UL) {
        std::fprintf(
            stderr,
            "Fail-closed build number should be zero, got %lu.\n",
            buildNumber);
        return 1;
    }

    std::puts("Condemned loader fail-closed test passed.");
    return 0;
}
