#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <MinHook.h>

#include "binding_input.h"
#include "condemned_locomotion.h"
#include "protocol.h"

namespace condemnedvr {
namespace {

struct RetailBinding {
    std::uint32_t device;
    std::uint32_t object;
    std::uint32_t command;
    float defaultValue;
    float offset;
    float scale;
    float deadzoneMin;
    float deadzoneMax;
    float deadzoneValue;
    float commandMin;
    float commandMax;
    float condemnedState[4];
};
static_assert(
    sizeof(RetailBinding) == 60,
    "Condemned CBindMgr binding layout changed.");

using GetBindingValueFunction =
    float(__thiscall*)(const void*, const RetailBinding*);
using GetInputStateFunction = BOOL(__cdecl*)(FearVrInputState*);

constexpr std::uintptr_t kGetBindingValueRva = 0x000095F0U;
constexpr ULONGLONG kInputFreshnessMilliseconds = 250;

constexpr unsigned char kGetBindingValuePrefix[] = {
    0x51, 0x56, 0x8B, 0x74, 0x24, 0x0C, 0x8B,
    0x06, 0x83, 0xF8, 0xFF, 0x74, 0x3A};
constexpr unsigned char kIsDeviceReadySequence[] = {
    0x8B, 0x11, 0x57, 0x8D, 0x7C, 0x24,
    0x10, 0x57, 0x50, 0xFF, 0x52, 0x18};
constexpr unsigned char kGetDeviceObjectValueSequence[] = {
    0x8B, 0x01, 0x8D, 0x54, 0x24, 0x04, 0x52, 0x8B,
    0x56, 0x04, 0x52, 0x8B, 0x16, 0x52, 0xFF, 0x50, 0x2C};
constexpr unsigned char kDefaultReturnSequence[] = {
    0xD9, 0x46, 0x0C, 0x5E, 0x59, 0xC2, 0x04, 0x00};

SRWLOCK g_bindingLock = SRWLOCK_INIT;
GetBindingValueFunction g_originalGetBindingValue = nullptr;
GetInputStateFunction g_getInputState = nullptr;
RendererProbeLogFunction g_log = nullptr;
void* g_hookTarget = nullptr;
std::uint64_t g_lastSampleId = 0;
ULONGLONG g_lastSampleTick = 0;
std::uint32_t g_lastDirectionMask = 0;

bool ProcessOwnsForegroundWindow() noexcept {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return false;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId();
}

bool SampleIsFresh(
    std::uint64_t sampleId, ULONGLONG now) noexcept {
    AcquireSRWLockExclusive(&g_bindingLock);
    if (sampleId != 0 && sampleId != g_lastSampleId) {
        g_lastSampleId = sampleId;
        g_lastSampleTick = now;
    }
    const bool fresh = g_lastSampleTick != 0 &&
        now - g_lastSampleTick <= kInputFreshnessMilliseconds;
    ReleaseSRWLockExclusive(&g_bindingLock);
    return fresh;
}

std::uint32_t DirectionMask(
    const LocomotionDirections& directions) noexcept {
    return (directions.forward ? 0x1U : 0U) |
           (directions.backward ? 0x2U : 0U) |
           (directions.left ? 0x4U : 0U) |
           (directions.right ? 0x8U : 0U);
}

void ReportDirectionTransition(std::uint32_t mask) noexcept {
    AcquireSRWLockExclusive(&g_bindingLock);
    if (mask == g_lastDirectionMask) {
        ReleaseSRWLockExclusive(&g_bindingLock);
        return;
    }
    g_lastDirectionMask = mask;
    ReleaseSRWLockExclusive(&g_bindingLock);

    if (g_log != nullptr) {
        char detail[128]{};
        std::snprintf(
            detail, sizeof(detail),
            "directions=0x%X path=retail_binding_value "
            "direct_command_writes=0 system_input=0",
            mask);
        g_log("m4_binding_locomotion_applied", detail);
    }
}

float ActiveBindingValue(const RetailBinding& binding) noexcept {
    if (!std::isfinite(binding.commandMin) ||
        !std::isfinite(binding.commandMax) ||
        binding.commandMin > binding.commandMax) {
        return 1.0F;
    }
    return std::clamp(1.0F, binding.commandMin, binding.commandMax);
}

bool DirectionActive(
    std::uint32_t command,
    const LocomotionDirections& directions) noexcept {
    switch (command) {
    case 0:
        return directions.forward;
    case 1:
        return directions.backward;
    case 3:
        return directions.left;
    case 4:
        return directions.right;
    default:
        return false;
    }
}

float __fastcall HookGetBindingValue(
    const void* bindManager,
    void* ignoredEdx,
    const RetailBinding* binding) {
    (void)ignoredEdx;
    const float original =
        g_originalGetBindingValue(bindManager, binding);
    if (binding == nullptr || binding->command > 4U ||
        binding->command == 2U || g_getInputState == nullptr) {
        return original;
    }

    FearVrInputState input{};
    const bool received = g_getInputState(&input) != FALSE;
    const ULONGLONG now = GetTickCount64();
    const bool usable = received &&
        SampleIsFresh(input.sampleId, now) &&
        ProcessOwnsForegroundWindow();
    const LocomotionDirections directions =
        ResolveLocomotionDirections(input, usable);
    ReportDirectionTransition(DirectionMask(directions));
    if (!DirectionActive(binding->command, directions)) {
        return original;
    }
    return ActiveBindingValue(*binding);
}

bool TargetMatches(const unsigned char* target) noexcept {
    if (target == nullptr) {
        return false;
    }
    __try {
        return std::memcmp(
                   target, kGetBindingValuePrefix,
                   sizeof(kGetBindingValuePrefix)) == 0 &&
               std::memcmp(
                   target + 0x13, kIsDeviceReadySequence,
                   sizeof(kIsDeviceReadySequence)) == 0 &&
               std::memcmp(
                   target + 0x32, kGetDeviceObjectValueSequence,
                   sizeof(kGetDeviceObjectValueSequence)) == 0 &&
               std::memcmp(
                   target + 0x47, kDefaultReturnSequence,
                   sizeof(kDefaultReturnSequence)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

bool InstallBindingLocomotionHook(
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept {
    AcquireSRWLockExclusive(&g_bindingLock);
    if (g_hookTarget != nullptr) {
        ReleaseSRWLockExclusive(&g_bindingLock);
        return true;
    }
    ReleaseSRWLockExclusive(&g_bindingLock);

    if (gameClientModule == nullptr || bridgeModule == nullptr ||
        log == nullptr) {
        return false;
    }
    const auto getInputState = reinterpret_cast<GetInputStateFunction>(
        GetProcAddress(bridgeModule, "CondemnedVr_GetInputState"));
    auto* const target =
        reinterpret_cast<unsigned char*>(gameClientModule) +
        kGetBindingValueRva;
    if (getInputState == nullptr || !TargetMatches(target)) {
        log(
            "m4_binding_locomotion_rejected",
            getInputState == nullptr
                ? "controller_transport_export_missing"
                : "GameOrig_rva_000095f0_signature_mismatch");
        return false;
    }

    const MH_STATUS initialize = MH_Initialize();
    if (initialize != MH_OK &&
        initialize != MH_ERROR_ALREADY_INITIALIZED) {
        log(
            "m4_binding_locomotion_rejected",
            MH_StatusToString(initialize));
        return false;
    }

    MH_STATUS status = MH_CreateHook(
        target, reinterpret_cast<void*>(&HookGetBindingValue),
        reinterpret_cast<void**>(&g_originalGetBindingValue));
    if (status == MH_OK) {
        status = MH_EnableHook(target);
    }
    if (status != MH_OK) {
        MH_RemoveHook(target);
        log(
            "m4_binding_locomotion_rejected",
            MH_StatusToString(status));
        g_originalGetBindingValue = nullptr;
        return false;
    }

    AcquireSRWLockExclusive(&g_bindingLock);
    g_getInputState = getInputState;
    g_log = log;
    g_hookTarget = target;
    ReleaseSRWLockExclusive(&g_bindingLock);
    log(
        "m4_binding_locomotion_armed",
        "target=GameOrig+0x000095F0 commands=0,1,3,4 "
        "binding_size=60 direct_command_writes=0 system_input=0");
    return true;
}

} // namespace condemnedvr
