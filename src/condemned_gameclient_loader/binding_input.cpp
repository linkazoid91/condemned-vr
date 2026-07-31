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
#include "condemned_controller_input.h"
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
using GetExtremalCommandValueFunction =
    float(__thiscall*)(const void*, std::uint32_t);
using GetInputStateFunction = BOOL(__cdecl*)(FearVrInputState*);
using SubmitHapticRequestFunction =
    BOOL(__cdecl*)(const FearVrHapticRequest*);
using SetMenuActiveFunction = void(__cdecl*)(BOOL);
using ClientShellUpdateFunction = void(__thiscall*)(void*);
using ClientShellKeyUpFunction = void(__thiscall*)(void*, int);
using ClientShellKeyDownFunction =
    void(__thiscall*)(void*, int, int);
using ClientShellGetInterfaceManagerFunction =
    void*(__thiscall*)(void*);

constexpr std::uintptr_t kGetBindingValueRva = 0x000095F0U;
constexpr std::uintptr_t kGetExtremalCommandValueRva = 0x00009900U;
constexpr std::uintptr_t kClientShellVtableRva = 0x0013E714U;
constexpr std::uintptr_t kClientShellKeyUpRva = 0x0004AD90U;
constexpr std::uintptr_t kClientShellKeyDownRva = 0x0004CC00U;
constexpr std::uintptr_t kClientShellUpdateRva = 0x00051150U;
constexpr std::uintptr_t kClientShellGetInterfaceManagerRva =
    0x0004A5E0U;
constexpr std::uintptr_t kInterfaceManagerVtableRva = 0x00142594U;
constexpr std::uintptr_t kInterfaceManagerSingletonRva = 0x0016F388U;
constexpr std::uintptr_t kClientShellKeyUpStateReadRva = 0x0004ADB5U;
constexpr std::size_t kClientShellKeyUpSlot = 16U;
constexpr std::size_t kClientShellKeyDownSlot = 17U;
constexpr std::size_t kClientShellUpdateSlot = 3U;
constexpr std::size_t kClientShellGetInterfaceManagerSlot = 30U;
constexpr std::size_t kInterfaceManagerStateOffset = 8U;
constexpr ULONGLONG kInputFreshnessMilliseconds = 250;
constexpr LONG kUnknownRetailGameState = -1;
constexpr LONG kUnpublishedRetailGameState = -2;

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
constexpr unsigned char kGetExtremalCommandValuePrefix[] = {
    0x51, 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x77, 0x04,
    0x3B, 0x77, 0x08, 0xC7, 0x44, 0x24, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x74, 0x4E, 0x53, 0x8B, 0x5C,
    0x24, 0x14};
constexpr unsigned char kGetExtremalCommandValueLoop[] = {
    0x39, 0x5E, 0x08, 0x75, 0x33, 0x8A, 0x47, 0x4C,
    0x84, 0xC0, 0x75, 0x05, 0xD9, 0x46, 0x0C};
constexpr unsigned char kGetExtremalBindingCall[] = {
    0x56, 0x8B, 0xCF, 0xE8, 0xB7, 0xFC, 0xFF, 0xFF};
constexpr unsigned char kGetExtremalBindingStride[] = {
    0x8B, 0x47, 0x08, 0x83, 0xC6, 0x3C, 0x3B, 0xF0,
    0x75, 0xBE};
constexpr unsigned char kGetExtremalCommandValueTail[] = {
    0xD9, 0x44, 0x24, 0x08, 0x5F, 0x5E, 0x59, 0xC2,
    0x04, 0x00};
constexpr unsigned char kClientShellKeyUpPrefix[] = {
    0x83, 0xEC, 0x28, 0x55, 0x8B, 0x6C, 0x24, 0x30,
    0x83, 0xFD, 0x77, 0x57, 0x8B, 0xF9, 0x0F, 0x84};
constexpr unsigned char kClientShellKeyDownPrefix[] = {
    0x81, 0xEC, 0x08, 0x02, 0x00, 0x00, 0x56, 0x57,
    0x8B, 0xBC, 0x24, 0x14, 0x02, 0x00, 0x00, 0x81,
    0xFF, 0xFF, 0x00, 0x00, 0x00};
constexpr unsigned char kClientShellUpdatePrefix[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC,
    0x1C};
constexpr unsigned char kClientShellUpdateBody[] = {
    0x55, 0x56, 0x57, 0x8B, 0xF1, 0xFF, 0x50, 0x4C,
    0xDC, 0x9E, 0x10, 0x4F, 0x00, 0x00, 0xDF, 0xE0,
    0xF6, 0xC4, 0x41, 0x75, 0x26};
constexpr unsigned char kClientShellBindingUpdateSequence[] = {
    0xE8, 0x42, 0x98, 0xFB, 0xFF, 0x8B, 0xC8, 0xE8,
    0xBB, 0x99, 0xFB, 0xFF};
constexpr unsigned char kClientShellGetInterfaceManagerBody[] = {
    0x8D, 0x41, 0x08, 0xC3};
constexpr unsigned char kClientShellKeyUpStateReadTail[] = {
    0x56, 0x8B, 0x70, 0x08};
constexpr unsigned char kClientShellKeyUpPlayingCompare[] = {
    0x83, 0xFE, 0x01};
constexpr unsigned char kClientShellKeyUpScreenCompare[] = {
    0x83, 0xFE, 0x06};
constexpr unsigned char kClientShellKeyUpMenuCompare[] = {
    0x83, 0xFE, 0x05};

SRWLOCK g_bindingLock = SRWLOCK_INIT;
GetBindingValueFunction g_originalGetBindingValue = nullptr;
GetExtremalCommandValueFunction g_originalGetExtremalCommandValue = nullptr;
GetInputStateFunction g_getInputState = nullptr;
SubmitHapticRequestFunction g_submitHapticRequest = nullptr;
SetMenuActiveFunction g_setMenuActive = nullptr;
ClientShellUpdateFunction g_originalClientShellUpdate = nullptr;
ClientShellKeyUpFunction g_clientShellKeyUp = nullptr;
ClientShellKeyDownFunction g_clientShellKeyDown = nullptr;
RendererProbeLogFunction g_log = nullptr;
void* g_bindingValueHookTarget = nullptr;
void* g_turningHookTarget = nullptr;
void* g_menuHookTarget = nullptr;
void* g_clientShell = nullptr;
void* g_interfaceManager = nullptr;
volatile LONG g_menuUpdateObserved = 0;
volatile LONG g_lastPublishedRetailGameState =
    kUnpublishedRetailGameState;
volatile LONG g_menuRenderPublishFailed = 0;
std::uint64_t g_lastSampleId = 0;
ULONGLONG g_lastSampleTick = 0;
std::uint32_t g_lastDirectionMask = 0;
int g_lastTurnDirection = 0;
volatile LONG g_locomotionEnabled = 0;
volatile LONG g_interactionEnabled = 0;
volatile LONG g_coreActionsEnabled = 0;
volatile LONG g_lastInteractionActive = 0;
volatile LONG g_lastCoreActionActive[7]{};
alignas(8) volatile LONG64 g_hapticRequestId = 0;
volatile LONG g_hapticsEnabled = 0;
volatile LONG g_hapticFailureReported = 0;
MenuToggleLatch g_menuToggleLatch;

int ReadRetailGameState(void* interfaceManager) noexcept;

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

bool ReadUsableControllerInput(
    FearVrInputState& input) noexcept {
    input = {};
    if (g_getInputState == nullptr ||
        g_getInputState(&input) == FALSE) {
        return false;
    }
    return SampleIsFresh(input.sampleId, GetTickCount64()) &&
        ProcessOwnsForegroundWindow();
}

struct InterfaceArrayAbi {
    std::uint32_t count;
    std::uint32_t capacity;
    void** items;
};

struct InterfaceDatabaseAbi {
    void** vtable;
    void* trackedPointers;
    InterfaceArrayAbi* interfaces;
};

struct InterfaceNameManagerAbi {
    void** vtable;
    const char* name;
    std::int32_t version;
    void* implementations;
    void* holders;
    void* currentInterface;
};

static_assert(sizeof(InterfaceArrayAbi) == 12);
static_assert(sizeof(InterfaceDatabaseAbi) == 12);
static_assert(sizeof(InterfaceNameManagerAbi) == 24);

void* FindCurrentInterface(
    void* masterDatabase,
    const char* name,
    std::int32_t version) noexcept {
    __try {
        auto* const database =
            static_cast<InterfaceDatabaseAbi*>(masterDatabase);
        InterfaceArrayAbi* const array = database->interfaces;
        if (array == nullptr || array->count > array->capacity ||
            array->count > 4096U || array->items == nullptr) {
            return nullptr;
        }
        for (std::uint32_t index = 0; index < array->count; ++index) {
            auto* const manager =
                static_cast<InterfaceNameManagerAbi*>(
                    array->items[index]);
            if (manager != nullptr && manager->name != nullptr &&
                manager->version == version &&
                std::strcmp(manager->name, name) == 0) {
                return manager->currentInterface;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return nullptr;
}

bool IsExecutableModuleAddress(
    const void* address,
    HMODULE module) noexcept {
    if (address == nullptr || module == nullptr) {
        return false;
    }
    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(
            address, &information, sizeof(information)) !=
        sizeof(information)) {
        return false;
    }
    DWORD protection = information.Protect &
        ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
    const bool executable =
        protection == PAGE_EXECUTE ||
        protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
    return executable && information.AllocationBase == module;
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

void ReportTurnTransition(
    const TurningValue& turning,
    float retailValue,
    float outputValue) noexcept {
    const bool applied = turning.active &&
        std::isfinite(turning.value) &&
        std::isfinite(retailValue) &&
        std::fabs(turning.value) > std::fabs(retailValue);
    const int direction = !applied
        ? 0
        : turning.value < 0.0F ? -1 : 1;
    AcquireSRWLockExclusive(&g_bindingLock);
    if (direction == g_lastTurnDirection) {
        ReleaseSRWLockExclusive(&g_bindingLock);
        return;
    }
    g_lastTurnDirection = direction;
    ReleaseSRWLockExclusive(&g_bindingLock);

    if (g_log != nullptr) {
        char detail[192]{};
        std::snprintf(
            detail, sizeof(detail),
            "command=23 applied=%u direction=%d vr_value=%.3f "
            "retail_value=%.3f "
            "output_value=%.3f path=retail_extremal_value "
            "direct_command_writes=0 system_input=0",
            applied ? 1U : 0U, direction, turning.value,
            retailValue, outputValue);
        g_log("m4_binding_turning_applied", detail);
    }
}

void RequestCoreActionHaptic(
    std::uint32_t command,
    bool controllerApplied) noexcept {
    if (!controllerApplied || g_submitHapticRequest == nullptr ||
        InterlockedCompareExchange(&g_hapticsEnabled, 0, 0) == 0) {
        return;
    }
    const CoreActionHapticPulse pulse =
        ResolveCoreActionHapticPulse(command);
    if (!pulse.active) {
        return;
    }

    FearVrHapticRequest request{};
    request.requestId = static_cast<std::uint64_t>(
        InterlockedIncrement64(&g_hapticRequestId));
    request.durationNs = pulse.durationNs;
    request.amplitude = pulse.amplitude;
    request.frequency = 0.0F;
    request.handMask = pulse.handMask;
    request.flags = FEARVR_HF_VALID;
    BOOL submitted = FALSE;
    __try {
        submitted = g_submitHapticRequest(&request);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        submitted = FALSE;
    }
    if (submitted == FALSE) {
        if (InterlockedCompareExchange(
                &g_hapticFailureReported, 1, 0) == 0 &&
            g_log != nullptr) {
            g_log(
                "m4_controller_haptic_failed",
                "CondemnedVr_SubmitHapticRequest_failed");
        }
        return;
    }
    if (g_log != nullptr) {
        char detail[192]{};
        std::snprintf(
            detail, sizeof(detail),
            "command=%u request_id=%llu hand_mask=0x%X "
            "duration_ns=%llu amplitude=%.2f "
            "source=vr_binding_rising_edge",
            command,
            static_cast<unsigned long long>(request.requestId),
            request.handMask,
            static_cast<unsigned long long>(request.durationNs),
            request.amplitude);
        g_log("m4_controller_haptic_requested", detail);
    }
}

void ReportInteractionTransition(
    const ActivateValue& activate,
    float retailValue,
    float outputValue,
    int retailGameState) noexcept {
    const LONG active = activate.active ? 1 : 0;
    if (InterlockedExchange(
            &g_lastInteractionActive, active) == active) {
        return;
    }
    const bool controllerApplied = activate.active &&
        std::isfinite(activate.value) && std::isfinite(retailValue) &&
        std::fabs(activate.value) > std::fabs(retailValue);
    RequestCoreActionHaptic(
        kCondemnedActivateCommand, controllerApplied);
    if (g_log != nullptr) {
        char detail[224]{};
        std::snprintf(
            detail, sizeof(detail),
            "command=87 controller_active=%ld retail_value=%.3f "
            "output_value=%.3f game_state=%d "
            "button=right_squeeze path=retail_binding_value "
            "direct_command_writes=0 system_input=0",
            active, retailValue, outputValue, retailGameState);
        g_log("m4_binding_interaction_applied", detail);
    }
}

void ReportCoreActionTransition(
    std::uint32_t command,
    const CoreActionValue& action,
    float retailValue,
    float outputValue,
    int retailGameState) noexcept {
    const int index = CondemnedCoreActionIndex(command);
    if (index < 0) {
        return;
    }
    const LONG active = action.active ? 1 : 0;
    if (InterlockedExchange(
            &g_lastCoreActionActive[index], active) == active) {
        return;
    }
    const bool controllerApplied = action.active &&
        std::isfinite(action.value) && std::isfinite(retailValue) &&
        std::fabs(action.value) > std::fabs(retailValue);
    RequestCoreActionHaptic(command, controllerApplied);
    if (g_log != nullptr) {
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "command=%u controller_active=%ld retail_value=%.3f "
            "output_value=%.3f game_state=%d control=%s "
            "path=retail_binding_value direct_command_writes=0 "
            "system_input=0",
            command, active, retailValue, outputValue,
            retailGameState,
            CondemnedCoreActionControlName(command));
        g_log("m4_binding_core_action_applied", detail);
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
    if (binding == nullptr || g_getInputState == nullptr) {
        return original;
    }

    const bool locomotionCommand =
        binding->command <= 4U && binding->command != 2U;
    if (locomotionCommand && InterlockedCompareExchange(
            &g_locomotionEnabled, 0, 0) != 0) {
        FearVrInputState input{};
        const bool usable = ReadUsableControllerInput(input);
        const LocomotionDirections directions =
            ResolveLocomotionDirections(input, usable);
        ReportDirectionTransition(DirectionMask(directions));
        if (DirectionActive(binding->command, directions)) {
            return ActiveBindingValue(*binding);
        }
    }

    if (binding->command == kCondemnedActivateCommand &&
        InterlockedCompareExchange(
            &g_interactionEnabled, 0, 0) != 0) {
        FearVrInputState input{};
        const int retailGameState =
            ReadRetailGameState(g_interfaceManager);
        const bool usable = ReadUsableControllerInput(input) &&
            retailGameState == kCondemnedGameStatePlaying;
        const ActivateValue activate =
            ResolveActivateValue(input, usable);
        const float outputValue =
            MergeActivateWithRetail(original, activate);
        ReportInteractionTransition(
            activate, original, outputValue, retailGameState);
        return outputValue;
    }

    if (CondemnedCoreActionIndex(binding->command) >= 0 &&
        InterlockedCompareExchange(
            &g_coreActionsEnabled, 0, 0) != 0) {
        FearVrInputState input{};
        const int retailGameState =
            ReadRetailGameState(g_interfaceManager);
        const bool usable = ReadUsableControllerInput(input) &&
            retailGameState == kCondemnedGameStatePlaying;
        const CoreActionValue action = ResolveCoreActionValue(
            input, usable, binding->command);
        const float outputValue = MergeCoreActionWithRetail(
            original, action);
        ReportCoreActionTransition(
            binding->command, action, original, outputValue,
            retailGameState);
        return outputValue;
    }

    return original;
}

float __fastcall HookGetExtremalCommandValue(
    const void* bindManager,
    void* ignoredEdx,
    std::uint32_t command) {
    (void)ignoredEdx;
    const float retailValue =
        g_originalGetExtremalCommandValue(bindManager, command);
    if (command != kCondemnedYawAccelCommand ||
        g_getInputState == nullptr) {
        return retailValue;
    }

    FearVrInputState input{};
    const bool usable = ReadUsableControllerInput(input);
    const TurningValue turning = ResolveTurningValue(input, usable);
    const float outputValue = MergeTurningWithRetail(
        retailValue, turning);
    ReportTurnTransition(turning, retailValue, outputValue);
    return outputValue;
}

const char* RetailGameStateName(int state) noexcept {
    switch (state) {
    case kCondemnedGameStateUndefined:
        return "undefined";
    case kCondemnedGameStatePlaying:
        return "playing";
    case kCondemnedGameStateExiting:
        return "exiting";
    case kCondemnedGameStateLoading:
        return "loading";
    case kCondemnedGameStateSplash:
        return "splash";
    case kCondemnedGameStateMenu:
        return "menu";
    case kCondemnedGameStateScreen:
        return "screen";
    case kCondemnedGameStatePaused:
        return "paused";
    case kCondemnedGameStateDemo:
        return "demo";
    case kCondemnedGameStateMovie:
        return "movie";
    default:
        return "unknown";
    }
}

int ReadRetailGameState(void* interfaceManager) noexcept {
    if (interfaceManager == nullptr) {
        return kUnknownRetailGameState;
    }
    __try {
        const int state = *reinterpret_cast<const int*>(
            static_cast<const unsigned char*>(interfaceManager) +
            kInterfaceManagerStateOffset);
        return IsKnownCondemnedGameState(state)
            ? state
            : kUnknownRetailGameState;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return kUnknownRetailGameState;
    }
}

int PublishMenuRenderState() noexcept {
    const int state = ReadRetailGameState(g_interfaceManager);
    const LONG publishedState = IsKnownCondemnedGameState(state)
        ? static_cast<LONG>(state)
        : kUnknownRetailGameState;
    const LONG previous = InterlockedExchange(
        &g_lastPublishedRetailGameState, publishedState);
    if (previous == publishedState) {
        return state;
    }

    const bool flatPanel = CondemnedGameStateUsesFlatPanel(state);
    bool published = false;
    if (g_setMenuActive != nullptr) {
        __try {
            g_setMenuActive(flatPanel ? TRUE : FALSE);
            published = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            published = false;
        }
    }
    if (!published) {
        InterlockedExchange(
            &g_lastPublishedRetailGameState,
            kUnpublishedRetailGameState);
        if (InterlockedCompareExchange(
                &g_menuRenderPublishFailed, 1, 0) == 0 &&
            g_log != nullptr) {
            g_log(
                "m4_menu_render_state_failed",
                "CondemnedVr_SetMenuActive_callback_failed");
        }
        return state;
    }

    if (g_log != nullptr) {
        char detail[192]{};
        std::snprintf(
            detail, sizeof(detail),
            "state=%d state_name=%s state_known=%u playing=%u "
            "flat_panel=%u source=CInterfaceMgr_plus_0x08",
            state, RetailGameStateName(state),
            IsKnownCondemnedGameState(state) ? 1U : 0U,
            state == kCondemnedGameStatePlaying ? 1U : 0U,
            flatPanel ? 1U : 0U);
        g_log("m4_menu_render_state", detail);
    }
    return state;
}

void PollMenuToggle(
    void* clientShell,
    int retailGameState) noexcept {
    FearVrInputState input{};
    const bool usable = ReadUsableControllerInput(input) &&
        CondemnedGameStateAllowsMenuToggle(retailGameState);
    if (!ConsumeMenuTogglePress(
            g_menuToggleLatch, input, usable)) {
        return;
    }

    __try {
        g_clientShellKeyDown(clientShell, VK_ESCAPE, 1);
        g_clientShellKeyUp(clientShell, VK_ESCAPE);
        if (g_log != nullptr) {
            g_log(
                "m4_menu_toggle_dispatched",
                "button=left_secondary path=IClientShell_v4_escape_edge "
                "result=escape_edge_dispatched direct_command_writes=0 "
                "system_input=0");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_log != nullptr) {
            g_log(
                "m4_menu_toggle_failed",
                "IClientShell_v4_escape_callback_exception");
        }
    }
}

void __fastcall HookClientShellUpdate(
    void* clientShell,
    void* ignoredEdx) {
    (void)ignoredEdx;
    if (clientShell == g_clientShell &&
        g_clientShellKeyDown != nullptr &&
        g_clientShellKeyUp != nullptr) {
        if (InterlockedCompareExchange(
                &g_menuUpdateObserved, 1, 0) == 0 &&
            g_log != nullptr) {
            g_log(
                "m4_menu_update_hook_called",
                "interface=IClientShell.Default.v4 update_slot=3 "
                "poll_before_retail=1 render_state_pre_post=1");
        }
        const int stateBeforeInput = PublishMenuRenderState();
        PollMenuToggle(clientShell, stateBeforeInput);
        PublishMenuRenderState();
    }
    g_originalClientShellUpdate(clientShell);
    if (clientShell == g_clientShell) {
        PublishMenuRenderState();
    }
}

bool LocomotionTargetMatches(const unsigned char* target) noexcept {
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

bool TurningTargetMatches(const unsigned char* target) noexcept {
    if (target == nullptr) {
        return false;
    }
    __try {
        return std::memcmp(
                   target, kGetExtremalCommandValuePrefix,
                   sizeof(kGetExtremalCommandValuePrefix)) == 0 &&
               std::memcmp(
                   target + 0x20, kGetExtremalCommandValueLoop,
                   sizeof(kGetExtremalCommandValueLoop)) == 0 &&
               std::memcmp(
                   target + 0x31, kGetExtremalBindingCall,
                   sizeof(kGetExtremalBindingCall)) == 0 &&
               std::memcmp(
                   target + 0x58, kGetExtremalBindingStride,
                   sizeof(kGetExtremalBindingStride)) == 0 &&
               std::memcmp(
                   target + 0x63, kGetExtremalCommandValueTail,
                   sizeof(kGetExtremalCommandValueTail)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MenuTargetsMatch(
    HMODULE gameClientModule,
    void* clientShell) noexcept {
    if (gameClientModule == nullptr || clientShell == nullptr) {
        return false;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    void** vtable = nullptr;
    __try {
        vtable = *static_cast<void***>(clientShell);
        if (vtable != reinterpret_cast<void**>(
                base + kClientShellVtableRva) ||
            vtable[kClientShellKeyUpSlot] !=
                base + kClientShellKeyUpRva ||
            vtable[kClientShellKeyDownSlot] !=
                base + kClientShellKeyDownRva ||
            vtable[kClientShellUpdateSlot] !=
                base + kClientShellUpdateRva ||
            vtable[kClientShellGetInterfaceManagerSlot] !=
                base + kClientShellGetInterfaceManagerRva) {
            return false;
        }
        auto* const stateRead =
            base + kClientShellKeyUpStateReadRva;
        std::uint32_t singletonOperand = 0;
        std::memcpy(
            &singletonOperand, stateRead + 1,
            sizeof(singletonOperand));
        const auto expectedSingletonOperand =
            static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(
                    base + kInterfaceManagerSingletonRva));
        return IsExecutableModuleAddress(
                   vtable[kClientShellKeyUpSlot], gameClientModule) &&
               IsExecutableModuleAddress(
                   vtable[kClientShellKeyDownSlot], gameClientModule) &&
               IsExecutableModuleAddress(
                   vtable[kClientShellUpdateSlot], gameClientModule) &&
               IsExecutableModuleAddress(
                   vtable[kClientShellGetInterfaceManagerSlot],
                   gameClientModule) &&
               std::memcmp(
                   vtable[kClientShellKeyUpSlot],
                   kClientShellKeyUpPrefix,
                   sizeof(kClientShellKeyUpPrefix)) == 0 &&
               std::memcmp(
                   vtable[kClientShellKeyDownSlot],
                   kClientShellKeyDownPrefix,
                   sizeof(kClientShellKeyDownPrefix)) == 0 &&
               std::memcmp(
                   vtable[kClientShellUpdateSlot],
                   kClientShellUpdatePrefix,
                   sizeof(kClientShellUpdatePrefix)) == 0 &&
               std::memcmp(
                   static_cast<unsigned char*>(
                       vtable[kClientShellUpdateSlot]) + 0x0E,
                   kClientShellUpdateBody,
                   sizeof(kClientShellUpdateBody)) == 0 &&
               std::memcmp(
                   static_cast<unsigned char*>(
                       vtable[kClientShellUpdateSlot]) + 0x49,
                   kClientShellBindingUpdateSequence,
                   sizeof(kClientShellBindingUpdateSequence)) == 0 &&
               std::memcmp(
                   vtable[kClientShellGetInterfaceManagerSlot],
                   kClientShellGetInterfaceManagerBody,
                   sizeof(kClientShellGetInterfaceManagerBody)) == 0 &&
               stateRead[0] == 0xA1 &&
               singletonOperand == expectedSingletonOperand &&
               std::memcmp(
                   stateRead + 5,
                   kClientShellKeyUpStateReadTail,
                   sizeof(kClientShellKeyUpStateReadTail)) == 0 &&
               std::memcmp(
                   stateRead + 0x0F,
                   kClientShellKeyUpPlayingCompare,
                   sizeof(kClientShellKeyUpPlayingCompare)) == 0 &&
               std::memcmp(
                   stateRead + 0x14,
                   kClientShellKeyUpScreenCompare,
                   sizeof(kClientShellKeyUpScreenCompare)) == 0 &&
               std::memcmp(
                   stateRead + 0x19,
                   kClientShellKeyUpMenuCompare,
                   sizeof(kClientShellKeyUpMenuCompare)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* ResolveVerifiedInterfaceManager(
    HMODULE gameClientModule,
    void* clientShell) noexcept {
    if (gameClientModule == nullptr || clientShell == nullptr) {
        return nullptr;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    __try {
        void** const vtable = *static_cast<void***>(clientShell);
        const auto getInterfaceManager =
            reinterpret_cast<ClientShellGetInterfaceManagerFunction>(
                vtable[kClientShellGetInterfaceManagerSlot]);
        void* const interfaceManager =
            getInterfaceManager(clientShell);
        if (interfaceManager !=
                static_cast<unsigned char*>(clientShell) + 8 ||
            *static_cast<void***>(interfaceManager) !=
                reinterpret_cast<void**>(
                    base + kInterfaceManagerVtableRva) ||
            *reinterpret_cast<void**>(
                base + kInterfaceManagerSingletonRva) !=
                interfaceManager) {
            return nullptr;
        }
        return interfaceManager;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool EnsureBindingValueHook(
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log,
    const char* rejectionEvent) noexcept {
    if (gameClientModule == nullptr || bridgeModule == nullptr ||
        log == nullptr || rejectionEvent == nullptr) {
        return false;
    }
    const auto getInputState = reinterpret_cast<GetInputStateFunction>(
        GetProcAddress(bridgeModule, "CondemnedVr_GetInputState"));
    auto* const target =
        reinterpret_cast<unsigned char*>(gameClientModule) +
        kGetBindingValueRva;
    if (getInputState == nullptr) {
        log(
            rejectionEvent,
            "controller_transport_export_missing");
        return false;
    }

    AcquireSRWLockExclusive(&g_bindingLock);
    const bool alreadyInstalled =
        g_bindingValueHookTarget == target &&
        g_originalGetBindingValue != nullptr;
    ReleaseSRWLockExclusive(&g_bindingLock);
    if (alreadyInstalled) {
        g_getInputState = getInputState;
        g_log = log;
        return true;
    }
    if (!LocomotionTargetMatches(target)) {
        log(
            rejectionEvent,
            "GameOrig_rva_000095f0_signature_mismatch");
        return false;
    }

    const MH_STATUS initialize = MH_Initialize();
    if (initialize != MH_OK &&
        initialize != MH_ERROR_ALREADY_INITIALIZED) {
        log(rejectionEvent, MH_StatusToString(initialize));
        return false;
    }

    g_getInputState = getInputState;
    g_log = log;
    MH_STATUS status = MH_CreateHook(
        target, reinterpret_cast<void*>(&HookGetBindingValue),
        reinterpret_cast<void**>(&g_originalGetBindingValue));
    if (status == MH_OK) {
        status = MH_EnableHook(target);
    }
    if (status != MH_OK) {
        MH_RemoveHook(target);
        log(rejectionEvent, MH_StatusToString(status));
        g_originalGetBindingValue = nullptr;
        return false;
    }

    AcquireSRWLockExclusive(&g_bindingLock);
    g_bindingValueHookTarget = target;
    ReleaseSRWLockExclusive(&g_bindingLock);
    return true;
}

} // namespace

bool InstallBindingLocomotionHook(
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept {
    if (!EnsureBindingValueHook(
            gameClientModule, bridgeModule, log,
            "m4_binding_locomotion_rejected")) {
        return false;
    }
    InterlockedExchange(&g_locomotionEnabled, 1);
    log(
        "m4_binding_locomotion_armed",
        "target=GameOrig+0x000095F0 commands=0,1,3,4 "
        "binding_size=60 direct_command_writes=0 system_input=0");
    return true;
}

bool InstallBindingInteractionHook(
    void* masterDatabase,
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept {
    if (masterDatabase == nullptr || gameClientModule == nullptr ||
        bridgeModule == nullptr || log == nullptr) {
        return false;
    }
    void* const clientShell = FindCurrentInterface(
        masterDatabase, "IClientShell.Default", 4);
    if (clientShell == nullptr ||
        !MenuTargetsMatch(gameClientModule, clientShell)) {
        log(
            "m4_binding_interaction_rejected",
            clientShell == nullptr
                ? "IClientShell_Default_v4_missing"
                : "IClientShell_Default_v4_state_guard_mismatch");
        return false;
    }
    void* const interfaceManager = ResolveVerifiedInterfaceManager(
        gameClientModule, clientShell);
    if (interfaceManager == nullptr) {
        log(
            "m4_binding_interaction_rejected",
            "CInterfaceMgr_state_source_mismatch");
        return false;
    }
    if (!EnsureBindingValueHook(
            gameClientModule, bridgeModule, log,
            "m4_binding_interaction_rejected")) {
        return false;
    }

    g_interfaceManager = interfaceManager;
    InterlockedExchange(&g_interactionEnabled, 1);
    log(
        "m4_binding_interaction_armed",
        "target=GameOrig+0x000095F0 command=87 "
        "button=right_squeeze threshold=0.65 "
        "state=playing path=retail_binding_value "
        "binding_size=60 direct_command_writes=0 system_input=0");
    return true;
}

bool InstallBindingCoreActionsHook(
    void* masterDatabase,
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept {
    if (masterDatabase == nullptr || gameClientModule == nullptr ||
        bridgeModule == nullptr || log == nullptr) {
        return false;
    }
    void* const clientShell = FindCurrentInterface(
        masterDatabase, "IClientShell.Default", 4);
    if (clientShell == nullptr ||
        !MenuTargetsMatch(gameClientModule, clientShell)) {
        log(
            "m4_binding_core_actions_rejected",
            clientShell == nullptr
                ? "IClientShell_Default_v4_missing"
                : "IClientShell_Default_v4_state_guard_mismatch");
        return false;
    }
    void* const interfaceManager = ResolveVerifiedInterfaceManager(
        gameClientModule, clientShell);
    if (interfaceManager == nullptr) {
        log(
            "m4_binding_core_actions_rejected",
            "CInterfaceMgr_state_source_mismatch");
        return false;
    }
    if (!EnsureBindingValueHook(
            gameClientModule, bridgeModule, log,
            "m4_binding_core_actions_rejected")) {
        return false;
    }

    g_interfaceManager = interfaceManager;
    for (auto& state : g_lastCoreActionActive) {
        InterlockedExchange(&state, 0);
    }
    InterlockedExchange(&g_coreActionsEnabled, 1);
    log(
        "m4_binding_core_actions_armed",
        "target=GameOrig+0x000095F0 "
        "commands=16,17,28,60,61,62,114 "
        "controls=left_squeeze,right_trigger,left_trigger,"
        "right_primary,right_secondary,left_stick,"
        "left_primary state=playing path=retail_binding_value "
        "binding_size=60 direct_command_writes=0 system_input=0");
    return true;
}

bool InstallControllerHaptics(
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept {
    if (bridgeModule == nullptr || log == nullptr) {
        return false;
    }
    if (InterlockedCompareExchange(
            &g_coreActionsEnabled, 0, 0) == 0 &&
        InterlockedCompareExchange(
            &g_interactionEnabled, 0, 0) == 0) {
        log(
            "m4_controller_haptics_rejected",
            "core_action_or_interaction_gate_required");
        return false;
    }
    const auto submit =
        reinterpret_cast<SubmitHapticRequestFunction>(
            GetProcAddress(
                bridgeModule,
                "CondemnedVr_SubmitHapticRequest"));
    if (submit == nullptr) {
        log(
            "m4_controller_haptics_rejected",
            "haptic_transport_export_missing");
        return false;
    }

    g_submitHapticRequest = submit;
    InterlockedExchange64(&g_hapticRequestId, 0);
    InterlockedExchange(&g_hapticFailureReported, 0);
    InterlockedExchange(&g_hapticsEnabled, 1);
    log(
        "m4_controller_haptics_armed",
        "commands=17,28,87 edge=rising "
        "pulses=fire_right_35ms_0.25,block_left_25ms_0.18,"
        "activate_right_20ms_0.15 transport=openxr_haptic "
        "weapon_event_haptics=0");
    return true;
}

bool InstallBindingTurningHook(
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept {
    AcquireSRWLockExclusive(&g_bindingLock);
    if (g_turningHookTarget != nullptr) {
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
        kGetExtremalCommandValueRva;
    if (getInputState == nullptr || !TurningTargetMatches(target)) {
        log(
            "m4_binding_turning_rejected",
            getInputState == nullptr
                ? "controller_transport_export_missing"
                : "GameOrig_rva_00009900_signature_mismatch");
        return false;
    }

    const MH_STATUS initialize = MH_Initialize();
    if (initialize != MH_OK &&
        initialize != MH_ERROR_ALREADY_INITIALIZED) {
        log(
            "m4_binding_turning_rejected",
            MH_StatusToString(initialize));
        return false;
    }

    g_getInputState = getInputState;
    g_log = log;
    MH_STATUS status = MH_CreateHook(
        target,
        reinterpret_cast<void*>(&HookGetExtremalCommandValue),
        reinterpret_cast<void**>(
            &g_originalGetExtremalCommandValue));
    if (status == MH_OK) {
        status = MH_EnableHook(target);
    }
    if (status != MH_OK) {
        MH_RemoveHook(target);
        log(
            "m4_binding_turning_rejected",
            MH_StatusToString(status));
        g_originalGetExtremalCommandValue = nullptr;
        return false;
    }

    AcquireSRWLockExclusive(&g_bindingLock);
    g_turningHookTarget = target;
    ReleaseSRWLockExclusive(&g_bindingLock);
    log(
        "m4_binding_turning_armed",
        "target=GameOrig+0x00009900 command=23 "
        "path=retail_extremal_value deadzone=0.22 "
        "direct_command_writes=0 system_input=0");
    return true;
}

bool InstallMenuToggleHook(
    void* masterDatabase,
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log) noexcept {
    AcquireSRWLockExclusive(&g_bindingLock);
    if (g_menuHookTarget != nullptr) {
        ReleaseSRWLockExclusive(&g_bindingLock);
        return true;
    }
    ReleaseSRWLockExclusive(&g_bindingLock);

    if (masterDatabase == nullptr || gameClientModule == nullptr ||
        bridgeModule == nullptr || log == nullptr) {
        return false;
    }
    const auto getInputState = reinterpret_cast<GetInputStateFunction>(
        GetProcAddress(bridgeModule, "CondemnedVr_GetInputState"));
    const auto setMenuActive = reinterpret_cast<SetMenuActiveFunction>(
        GetProcAddress(bridgeModule, "CondemnedVr_SetMenuActive"));
    void* const clientShell = FindCurrentInterface(
        masterDatabase, "IClientShell.Default", 4);
    if (getInputState == nullptr || setMenuActive == nullptr ||
        clientShell == nullptr ||
        !MenuTargetsMatch(gameClientModule, clientShell)) {
        const char* reason =
            getInputState == nullptr
            ? "controller_transport_export_missing"
            : setMenuActive == nullptr
            ? "menu_render_transport_export_missing"
            : clientShell == nullptr
            ? "IClientShell_Default_v4_missing"
            : "IClientShell_Default_v4_target_mismatch";
        log("m4_menu_toggle_rejected", reason);
        return false;
    }
    void* const interfaceManager = ResolveVerifiedInterfaceManager(
        gameClientModule, clientShell);
    if (interfaceManager == nullptr ||
        !IsKnownCondemnedGameState(
            ReadRetailGameState(interfaceManager))) {
        log(
            "m4_menu_toggle_rejected",
            "CInterfaceMgr_state_layout_mismatch");
        return false;
    }

    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    void** const vtable = *static_cast<void***>(clientShell);
    void* const target = vtable[kClientShellUpdateSlot];
    const auto keyUp = reinterpret_cast<ClientShellKeyUpFunction>(
        base + kClientShellKeyUpRva);
    const auto keyDown = reinterpret_cast<ClientShellKeyDownFunction>(
        base + kClientShellKeyDownRva);

    const MH_STATUS initialize = MH_Initialize();
    if (initialize != MH_OK &&
        initialize != MH_ERROR_ALREADY_INITIALIZED) {
        log(
            "m4_menu_toggle_rejected",
            MH_StatusToString(initialize));
        return false;
    }

    g_getInputState = getInputState;
    g_setMenuActive = setMenuActive;
    g_log = log;
    g_clientShell = clientShell;
    g_interfaceManager = interfaceManager;
    g_clientShellKeyUp = keyUp;
    g_clientShellKeyDown = keyDown;
    g_menuToggleLatch = {};
    InterlockedExchange(&g_menuUpdateObserved, 0);
    InterlockedExchange(
        &g_lastPublishedRetailGameState,
        kUnpublishedRetailGameState);
    InterlockedExchange(&g_menuRenderPublishFailed, 0);
    MH_STATUS status = MH_CreateHook(
        target, reinterpret_cast<void*>(&HookClientShellUpdate),
        reinterpret_cast<void**>(&g_originalClientShellUpdate));
    if (status == MH_OK) {
        status = MH_EnableHook(target);
    }
    if (status != MH_OK) {
        MH_RemoveHook(target);
        log("m4_menu_toggle_rejected", MH_StatusToString(status));
        g_originalClientShellUpdate = nullptr;
        g_clientShellKeyUp = nullptr;
        g_clientShellKeyDown = nullptr;
        g_setMenuActive = nullptr;
        g_interfaceManager = nullptr;
        g_clientShell = nullptr;
        return false;
    }

    AcquireSRWLockExclusive(&g_bindingLock);
    g_menuHookTarget = target;
    ReleaseSRWLockExclusive(&g_bindingLock);
    log(
        "m4_menu_toggle_armed",
        "target=GameOrig+0x00051150 "
        "interface=IClientShell.Default.v4 update_slot=3 "
        "button=left_secondary path=escape_callbacks "
        "state_source=CInterfaceMgr+0x08 flat_panel_nonplaying=1 "
        "escape_states=playing,menu "
        "direct_command_writes=0 system_input=0");
    PublishMenuRenderState();
    return true;
}

} // namespace condemnedvr
