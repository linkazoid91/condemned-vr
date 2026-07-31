#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "bridge.h"
#include "module_identity.h"

extern "C" __declspec(dllexport) BOOL __cdecl
CondemnedVr_InstallD3D9Hooks() {
    wchar_t executablePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(
        nullptr, executablePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return FALSE;
    }

    if (condemnedvr::VerifyCondemnedExecutable(executablePath) !=
        condemnedvr::ModuleIdentityResult::ok) {
        return FALSE;
    }

    fearvr::ApplyEngineFixes();
    return fearvr::InstallLateD3D9Hooks();
}
