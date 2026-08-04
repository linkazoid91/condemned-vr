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
#include <cwchar>
#include <cstring>
#include <intrin.h>

#include <MinHook.h>

#include "binding_input.h"
#include "condemned_controller_input.h"
#include "condemned_locomotion.h"
#include "condemned_menu_input.h"
#include "condemned_physical_melee.h"
#include "head_tracking_math.h"
#include "protocol.h"
#include "weapon_identity_reader.h"

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
struct VectorAbi {
    float x;
    float y;
    float z;
};
static_assert(sizeof(VectorAbi) == 12);
struct QuaternionAbi {
    float x;
    float y;
    float z;
    float w;
};
static_assert(sizeof(QuaternionAbi) == 16);
using GetFireVectorsFunction = bool(__thiscall*)(
    const void*, VectorAbi&, VectorAbi&, VectorAbi&, VectorAbi&);
using MeleeEnableCollisionsFunction = std::uintptr_t(__thiscall*)(
    void*, std::uintptr_t, std::uintptr_t, std::uintptr_t,
    std::uintptr_t, std::uintptr_t);
using MeleeUpdateCollisionFunction = void(__thiscall*)(void*, void*);
using BuildRigidTransformFunction = void*(__thiscall*)(
    void*, const VectorAbi*, const QuaternionAbi*);
using MeleeImpactDispatchFunction = std::uintptr_t(__thiscall*)(
    void*, std::uintptr_t, std::uintptr_t, std::uintptr_t,
    std::uintptr_t, std::uintptr_t, std::uintptr_t,
    std::uintptr_t, std::uintptr_t, std::uintptr_t);
using SetMenuActiveFunction = void(__cdecl*)(BOOL);
using ClientShellUpdateFunction = void(__thiscall*)(void*);
using ClientShellKeyUpFunction = void(__thiscall*)(void*, int);
using ClientShellKeyDownFunction =
    void(__thiscall*)(void*, int, int);
using ClientShellGetInterfaceManagerFunction =
    void*(__thiscall*)(void*);

constexpr std::uintptr_t kGetBindingValueRva = 0x000095F0U;
constexpr std::uintptr_t kGetExtremalCommandValueRva = 0x00009900U;
constexpr std::uintptr_t kGetFireVectorsRva = 0x0002AF70U;
constexpr std::uintptr_t kMeleeEnableCollisionsRva = 0x0001FD00U;
constexpr std::uintptr_t kMeleeUpdateCollisionRva = 0x0001FC00U;
constexpr std::uintptr_t kBuildRigidTransformRva = 0x0000F690U;
constexpr std::uintptr_t kMeleeBuildRigidTransformReturnRva =
    0x0001FCDEU;
constexpr std::uintptr_t kMeleeImpactDispatchRva = 0x0001F270U;
constexpr std::uintptr_t kMeleeImpactDispatchReturnRva =
    0x0001FBC8U;
constexpr std::uintptr_t kMeleeCollisionCallbackRva = 0x0001F830U;
constexpr std::uintptr_t kMeleeImpactDispatchCallRva = 0x0001FBC3U;
constexpr std::uintptr_t kMeleeCollisionLimitTextRva = 0x0013A6B8U;
constexpr std::uintptr_t kMeleeClientGlobalRva = 0x00168EECU;
// Verified local-player weapon lifecycle. CClientWeaponMgr::GetCurrentWeapon
// at +0x2F910 returns m_pCurrentWeapon (+0x0C) only when its index (+0x08)
// is valid. CClientWeapon::SetWeaponTransform at +0x255F0 reads the primary
// engine-owned model HOBJECT from the LTObjRef field at +0x1C.
constexpr std::uintptr_t kWeaponManagerGlobalRva = 0x00168EBCU;
constexpr std::uintptr_t kGetCurrentWeaponRva = 0x0002F910U;
constexpr std::uintptr_t kSetWeaponTransformRva = 0x000255F0U;
constexpr std::size_t kCurrentWeaponIndexOffset = 0x08U;
constexpr std::size_t kCurrentWeaponOffset = 0x0CU;
constexpr std::size_t kRightWeaponModelObjectOffset = 0x1CU;
constexpr std::uintptr_t kRetailGameImageSize = 0x00194000U;
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
constexpr unsigned char kGetFireVectorsPrefix[] = {
    0x83, 0xEC, 0x58};
constexpr unsigned char kGetFireVectorsStackInit[] = {
    0x53, 0x55, 0xC7, 0x44, 0x24, 0x30,
    0x00, 0x00, 0x00, 0x00};
constexpr unsigned char kGetFireVectorsCameraProbe[] = {
    0x8B, 0xE9, 0x8B, 0x48, 0x28,
    0x8B, 0x81, 0x18, 0x01, 0x00, 0x00,
    0x85, 0xC0, 0x56, 0x57};
constexpr unsigned char kMeleeEnableCollisionsPrefix[] = {
    0x81, 0xEC, 0x6C, 0x01, 0x00, 0x00, 0xA1};
constexpr unsigned char kMeleeEnableCollisionsBodyPrefix[] = {
    0x8B, 0x50, 0x10, 0x53, 0x55, 0x8B, 0xE9};
constexpr unsigned char kMeleeCollisionLimitTextReferencePrefix[] = {
    0x8B, 0x94, 0x24, 0x88, 0x01, 0x00, 0x00, 0x52, 0x68};
constexpr unsigned char kMeleeUpdateCollisionPrefix[] = {
    0x83, 0xEC, 0x3C, 0x56, 0x8B, 0x74, 0x24, 0x44,
    0x8B, 0x46, 0x40, 0x85, 0xC0, 0x57, 0x8B, 0xF9};
constexpr unsigned char kMeleeUpdateCollisionNodeQuery[] = {
    0x8B, 0x46, 0x38, 0x8B, 0x0D};
constexpr unsigned char kMeleeUpdateCollisionSetTransform[] = {
    0x50, 0x8B, 0xCF, 0xFF, 0x93, 0xC4, 0x00, 0x00, 0x00};
constexpr unsigned char kBuildRigidTransformPrefix[] = {
    0x8B, 0xC1, 0xC7, 0x40, 0x18, 0x00, 0x00, 0x80,
    0x3F, 0x33, 0xC9, 0x89, 0x48, 0x0C, 0x89, 0x48,
    0x10, 0x89, 0x48, 0x14};
constexpr unsigned char kBuildRigidTransformTail[] = {
    0x8B, 0x49, 0x0C, 0x89, 0x48, 0x18, 0xC2, 0x08, 0x00};
constexpr unsigned char kMeleeImpactDispatchPrefix[] = {
    0x8B, 0x44, 0x24, 0x0C, 0x83, 0xEC, 0x4C, 0x83,
    0xF8, 0x10, 0x53, 0x56, 0x8B, 0xD9, 0x75, 0x50};
constexpr unsigned char kMeleeImpactDispatchCallsitePrefix[] = {
    0x8B, 0x46, 0x24, 0x8B, 0x56, 0x38, 0x50, 0x33,
    0xC9, 0x8A, 0x4E, 0x28, 0x57, 0x8D, 0x44, 0x24,
    0x44, 0x51, 0x52};
constexpr unsigned char kGetCurrentWeaponBody[] = {
    0x83, 0x79, 0x08, 0xFF, 0x75, 0x03, 0x33,
    0xC0, 0xC3, 0x8B, 0x41, 0x0C, 0xC3};
constexpr unsigned char kSetWeaponTransformPrefix[] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x1C, 0x85,
    0xC0, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x74, 0x0D};
constexpr unsigned char kSetWeaponTransformSecondModel[] = {
    0x8B, 0x86, 0xEC, 0x00, 0x00, 0x00,
    0x85, 0xC0, 0x74, 0x0D};
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
GetFireVectorsFunction g_originalGetFireVectors = nullptr;
MeleeEnableCollisionsFunction g_originalMeleeEnableCollisions = nullptr;
MeleeUpdateCollisionFunction g_originalMeleeUpdateCollision = nullptr;
BuildRigidTransformFunction g_originalBuildRigidTransform = nullptr;
MeleeImpactDispatchFunction g_originalMeleeImpactDispatch = nullptr;
GetInputStateFunction g_getInputState = nullptr;
SubmitHapticRequestFunction g_submitHapticRequest = nullptr;
SetMenuActiveFunction g_setMenuActive = nullptr;
ClientShellUpdateFunction g_originalClientShellUpdate = nullptr;
ClientShellKeyUpFunction g_clientShellKeyUp = nullptr;
ClientShellKeyDownFunction g_clientShellKeyDown = nullptr;
RendererProbeLogFunction g_log = nullptr;
void* g_bindingValueHookTarget = nullptr;
void* g_turningHookTarget = nullptr;
void* g_fireVectorsHookTarget = nullptr;
void* g_meleeEnableCollisionsHookTarget = nullptr;
void* g_meleeUpdateCollisionHookTarget = nullptr;
void* g_buildRigidTransformHookTarget = nullptr;
void* g_meleeImpactDispatchHookTarget = nullptr;
void* g_menuHookTarget = nullptr;
void* g_clientShell = nullptr;
void* g_interfaceManager = nullptr;
volatile LONG g_menuUpdateObserved = 0;
volatile LONG g_lastPublishedRetailGameState =
    kUnpublishedRetailGameState;
volatile LONG g_menuRenderPublishFailed = 0;
volatile LONG g_menuControlsEnabled = 0;
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
volatile LONG g_headAimInputEnabled = 0;
volatile LONG g_mouseLookSuppressionLogged = 0;
volatile LONG g_controllerFireAimLogged = 0;
volatile LONG g_aimPathProbeEnabled = 0;
volatile LONG g_aimPathFireVectorCalls = 0;
volatile LONG g_aimPathMeleeCalls = 0;
volatile LONG g_aimPathMeleeUpdateCalls = 0;
volatile LONG g_aimPathMeleeTransformCalls = 0;
volatile LONG g_aimPathMeleeImpactCalls = 0;
volatile LONG g_controllerMeleeAimEnabled = 0;
volatile LONG g_controllerMeleeAimLogged = 0;
SRWLOCK g_physicalMeleeLock = SRWLOCK_INIT;
PhysicalMeleeKinematicsState g_physicalMeleeState{};
PhysicalMeleeKinematicsState g_physicalMeleeSwingKinematicsState{};
PhysicalMeleeFrame g_physicalMeleeFrame{};
PhysicalMeleeProfile g_physicalMeleeProfile{};
std::int32_t g_physicalMeleeProfileWeaponIndex = -1;
RetailWeaponIdentitySnapshot g_equippedWeaponIdentity{};
PhysicalMeleeContactState g_physicalMeleeContactState{};
PhysicalMeleeSwingAttackState g_physicalMeleeSwingAttackState{};
std::uint64_t g_physicalMeleeSampleId = 0;
ULONGLONG g_physicalMeleeSampleTick = 0;
std::uint64_t g_physicalMeleeSwingSampleId = 0;
ULONGLONG g_physicalMeleeSwingSampleTick = 0;
float g_physicalMeleeSwingSpeedMetersPerSecond = 0.0F;
volatile LONG g_physicalMeleeProbeEnabled = 0;
volatile LONG g_physicalMeleeSampleCalls = 0;
volatile LONG g_physicalMeleeDamageQualified = 0;
volatile LONG g_physicalMeleeSwingAttackTriggered = 0;
volatile LONG g_physicalMeleeWallProxyEnabled = 0;
volatile LONG g_physicalMeleeVisualProxyEnabled = 0;
volatile LONG g_physicalMeleeWallProxyAppliedLogged = 0;
volatile LONG g_physicalMeleeContactAccepted = 0;
volatile LONG g_physicalMeleeContactRearmed = 0;
volatile LONG g_weaponCatalogProbeState = 0;
unsigned char* g_gameClientBase = nullptr;
MenuToggleLatch g_menuToggleLatch;
MenuNavigationState g_menuNavigationState;

int ReadRetailGameState(void* interfaceManager) noexcept;

bool CommandLineContains(const wchar_t* option) noexcept {
    const wchar_t* const commandLine = GetCommandLineW();
    return commandLine != nullptr && option != nullptr &&
        std::wcsstr(commandLine, option) != nullptr;
}

void TryLogRetailWeaponCatalog() noexcept {
    if (!CommandLineContains(
            L"-condemnedvr-m5-weapon-catalog-probe") ||
        g_log == nullptr ||
        InterlockedCompareExchange(
            &g_weaponCatalogProbeState, 1, 0) != 0) {
        return;
    }
    RetailWeaponIdentityCatalog catalog{};
    const RetailWeaponIdentityReadResult result =
        ReadRetailWeaponIdentityCatalog(catalog);
    char summary[160]{};
    std::snprintf(
        summary, sizeof(summary), "result=%s count=%u",
        RetailWeaponIdentityReadResultName(result), catalog.count);
    g_log("m5_weapon_catalog_probe", summary);
    if (result != RetailWeaponIdentityReadResult::Ok) {
        InterlockedExchange(&g_weaponCatalogProbeState, 0);
        return;
    }
    for (std::uint32_t index = 0U; index < catalog.count; ++index) {
        const RetailWeaponIdentitySnapshot& entry =
            catalog.entries[index];
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld weapon_name=%s animation_property=%s "
            "pose_family=%s",
            static_cast<long>(entry.playerWeaponIndex),
            entry.recordName,
            entry.animationPropertyResolved
                ? entry.animationProperty : "UNKNOWN",
            RetailWeaponPoseFamilyLabel(entry.poseFamily));
        g_log("m5_weapon_catalog_entry", detail);
    }
    InterlockedExchange(&g_weaponCatalogProbeState, 2);
}

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

bool WeaponGripCalibrationCapturesInput(
    const FearVrInputState& input,
    bool sampleFresh) noexcept {
    return VrToolMenuCapturesControllerInput(input, sampleFresh) ||
        (WeaponGripCalibrationAcceptsControllerInput() &&
         ResolveWeaponGripCalibrationControls(
             input, sampleFresh).captured);
}

void FormatGameClientStack(
    char* output, std::size_t outputSize) noexcept {
    if (output == nullptr || outputSize == 0) {
        return;
    }
    output[0] = '\0';
    void* frames[16]{};
    const USHORT count = CaptureStackBackTrace(
        0, static_cast<DWORD>(
            sizeof(frames) / sizeof(frames[0])), frames, nullptr);
    std::size_t used = 0;
    for (USHORT index = 0; index < count; ++index) {
        auto* const address = static_cast<unsigned char*>(frames[index]);
        if (g_gameClientBase == nullptr ||
            address < g_gameClientBase ||
            address >= g_gameClientBase + kRetailGameImageSize) {
            continue;
        }
        const auto rva = static_cast<unsigned long>(
            address - g_gameClientBase);
        const int written = std::snprintf(
            output + used, outputSize - used,
            used == 0 ? "0x%08lX" : ",0x%08lX", rva);
        if (written <= 0 ||
            static_cast<std::size_t>(written) >= outputSize - used) {
            output[outputSize - 1] = '\0';
            break;
        }
        used += static_cast<std::size_t>(written);
    }
    if (used == 0) {
        std::snprintf(output, outputSize, "none");
    }
}

bool ReadControllerForward(VectorAbi& forward) noexcept {
    float rotation[4]{};
    if (!ReadTrackedControllerAimRotation(rotation)) {
        return false;
    }
    const fearvr::TrackingQuaternion controller = fearvr::Normalize({
        rotation[0], rotation[1], rotation[2], rotation[3]});
    if (!fearvr::IsFinite(controller)) {
        return false;
    }
    const fearvr::TrackingVector value =
        fearvr::Rotate(controller, {0.0F, 0.0F, 1.0F});
    if (!fearvr::IsFinite(value)) {
        return false;
    }
    forward = {value.x, value.y, value.z};
    return true;
}

bool ReadControllerSwingPose(
    fearvr::TrackingVector& gripPositionMeters,
    fearvr::TrackingQuaternion& aimRotation,
    std::uint64_t& sampleId,
    std::uint64_t& timestampNs) noexcept {
    gripPositionMeters = {};
    aimRotation = {};
    sampleId = 0;
    timestampNs = 0;
    if (g_getInputState == nullptr) {
        return false;
    }
    FearVrInputState input{};
    if (g_getInputState(&input) == FALSE ||
        !fearvr::IsInputStateUsable(input, true) ||
        (input.activeHands & FEARVR_HAND_MASK_RIGHT) == 0 ||
        (input.gripPoseValidHands & FEARVR_HAND_MASK_RIGHT) == 0 ||
        (input.aimPoseValidHands & FEARVR_HAND_MASK_RIGHT) == 0 ||
        input.sampleId == 0 || input.predictedDisplayTimeNs == 0 ||
        !fearvr::IsValidPose(
            input.handGripPose[FEARVR_HAND_RIGHT]) ||
        !fearvr::IsValidPose(
            input.handAimPose[FEARVR_HAND_RIGHT])) {
        return false;
    }

    // Tracking-space motion excludes Retail camera translation and turning,
    // so walking or snap-turning cannot masquerade as a hand swing. The
    // OpenXR-to-LithTech conversion is orthonormal and preserves speed.
    gripPositionMeters = fearvr::OpenXrToLithTech(
        fearvr::PosePosition(
            input.handGripPose[FEARVR_HAND_RIGHT]));
    aimRotation = fearvr::OpenXrToLithTech(
        fearvr::PoseRotation(
            input.handAimPose[FEARVR_HAND_RIGHT]));
    ResolvePhysicalMeleeTrackedTwoHandPose(
        input, gripPositionMeters, aimRotation);
    if (!fearvr::IsFinite(gripPositionMeters) ||
        !fearvr::IsFinite(aimRotation)) {
        gripPositionMeters = {};
        aimRotation = {};
        return false;
    }
    sampleId = input.sampleId;
    timestampNs = input.predictedDisplayTimeNs;
    return true;
}

bool ReadVectorCandidate(
    std::uintptr_t address,
    VectorAbi& value) noexcept {
    value = {};
    if (address == 0) {
        return false;
    }
    __try {
        std::memcpy(
            &value, reinterpret_cast<const void*>(address),
            sizeof(value));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        value = {};
        return false;
    }
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

const char* PhysicalMeleeResetReasonName(
    PhysicalMeleeResetReason reason) noexcept {
    switch (reason) {
    case PhysicalMeleeResetReason::None:
        return "none";
    case PhysicalMeleeResetReason::FirstPose:
        return "first_pose";
    case PhysicalMeleeResetReason::TrackingLost:
        return "tracking_lost";
    case PhysicalMeleeResetReason::TrackingReacquired:
        return "tracking_reacquired";
    case PhysicalMeleeResetReason::InvalidPose:
        return "invalid_pose";
    case PhysicalMeleeResetReason::InvalidProfile:
        return "invalid_profile";
    case PhysicalMeleeResetReason::NonPositiveTime:
        return "non_positive_time";
    case PhysicalMeleeResetReason::InsufficientSampleInterval:
        return "insufficient_sample_interval";
    case PhysicalMeleeResetReason::ExcessiveSampleGap:
        return "excessive_sample_gap";
    case PhysicalMeleeResetReason::ExcessiveTravel:
        return "excessive_travel";
    default:
        return "unknown";
    }
}

const char* PhysicalMeleeContactReasonName(
    PhysicalMeleeContactReason reason) noexcept {
    switch (reason) {
    case PhysicalMeleeContactReason::None:
        return "none";
    case PhysicalMeleeContactReason::Accepted:
        return "accepted";
    case PhysicalMeleeContactReason::InvalidProfile:
        return "invalid_profile";
    case PhysicalMeleeContactReason::MissingTarget:
        return "missing_target";
    case PhysicalMeleeContactReason::InvalidContact:
        return "invalid_contact";
    case PhysicalMeleeContactReason::InvalidFrame:
        return "invalid_frame";
    case PhysicalMeleeContactReason::BelowNormalSpeed:
        return "below_normal_speed";
    case PhysicalMeleeContactReason::BelowNormalEnergy:
        return "below_normal_energy";
    case PhysicalMeleeContactReason::ContactLatched:
        return "contact_latched";
    default:
        return "unknown";
    }
}

bool CopyLatestPhysicalMeleeFrame(
    PhysicalMeleeFrame& frame,
    std::uint64_t& sampleId) noexcept {
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_physicalMeleeLock);
    frame = g_physicalMeleeFrame;
    sampleId = g_physicalMeleeSampleId;
    const bool available = sampleId != 0 && frame.poseValid &&
        g_physicalMeleeSampleTick != 0 &&
        now - g_physicalMeleeSampleTick <=
            kInputFreshnessMilliseconds;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    return available && ProcessOwnsForegroundWindow();
}

bool ReadPhysicalMeleeSwingAttackActive(
    bool inputEligible) noexcept {
    const ULONGLONG now = GetTickCount64();
    const bool probeEnabled = InterlockedCompareExchange(
        &g_physicalMeleeProbeEnabled, 0, 0) != 0;
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    const bool sampleFresh = g_physicalMeleeSampleId != 0 &&
        g_physicalMeleeSampleTick != 0 &&
        now - g_physicalMeleeSampleTick <=
            kInputFreshnessMilliseconds &&
        g_physicalMeleeSwingSampleId != 0 &&
        g_physicalMeleeSwingSampleTick != 0 &&
        now - g_physicalMeleeSwingSampleTick <=
            kInputFreshnessMilliseconds;
    if (!inputEligible || !probeEnabled || !sampleFresh ||
        !g_physicalMeleeProfile.swingAttackEnabled) {
        ResetPhysicalMeleeSwingAttack(
            g_physicalMeleeSwingAttackState);
        ReleaseSRWLockExclusive(&g_physicalMeleeLock);
        return false;
    }
    const bool active = PhysicalMeleeSwingAttackPulseIsActive(
        g_physicalMeleeSwingAttackState,
        static_cast<std::uint64_t>(now));
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
    return active;
}

bool EvaluatePhysicalMeleeContact(
    std::uintptr_t targetId,
    const VectorAbi& contactPosition,
    const VectorAbi& contactNormal,
    PhysicalMeleeFrame& frame,
    std::uint64_t& sampleId,
    PhysicalMeleeContactQualification& qualification) noexcept {
    const ULONGLONG now = GetTickCount64();
    const bool foreground = ProcessOwnsForegroundWindow();
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    frame = g_physicalMeleeFrame;
    sampleId = g_physicalMeleeSampleId;
    const bool available = foreground && sampleId != 0 &&
        frame.poseValid && g_physicalMeleeSampleTick != 0 &&
        now - g_physicalMeleeSampleTick <=
            kInputFreshnessMilliseconds;
    if (available) {
        qualification = QualifyPhysicalMeleeContact(
            g_physicalMeleeContactState,
            targetId,
            {contactPosition.x, contactPosition.y, contactPosition.z},
            {contactNormal.x, contactNormal.y, contactNormal.z},
            frame, sampleId, g_physicalMeleeProfile);
    } else {
        qualification = {};
        qualification.reason = PhysicalMeleeContactReason::InvalidFrame;
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
    return available;
}

void UpdatePhysicalMeleeProbe() noexcept {
    if (InterlockedCompareExchange(
            &g_physicalMeleeProbeEnabled, 0, 0) == 0) {
        return;
    }

    float position[3]{};
    float rotation[4]{};
    std::uint64_t sampleId = 0;
    std::uint64_t timestampNs = 0;
    const bool fresh = ReadTrackedControllerWorldPose(
        position, rotation, sampleId, timestampNs);
    fearvr::TrackingVector swingGripPositionMeters{};
    fearvr::TrackingQuaternion swingAimRotation{};
    std::uint64_t swingSampleId = 0;
    std::uint64_t swingTimestampNs = 0;
    const bool swingPoseFresh = ReadControllerSwingPose(
        swingGripPositionMeters, swingAimRotation,
        swingSampleId, swingTimestampNs);
    if (!fresh) {
        bool trackingWasActive = false;
        AcquireSRWLockExclusive(&g_physicalMeleeLock);
        trackingWasActive = g_physicalMeleeState.havePose;
        if (trackingWasActive) {
            ResetPhysicalMeleeKinematics(
                g_physicalMeleeState,
                PhysicalMeleeResetReason::TrackingLost);
        }
        ResetPhysicalMeleeKinematics(
            g_physicalMeleeSwingKinematicsState,
            PhysicalMeleeResetReason::TrackingLost);
        ResetPhysicalMeleeContactState(g_physicalMeleeContactState);
        ResetPhysicalMeleeSwingAttack(
            g_physicalMeleeSwingAttackState);
        g_physicalMeleeFrame = {};
        g_physicalMeleeFrame.resetReason =
            PhysicalMeleeResetReason::TrackingLost;
        g_physicalMeleeSampleId = 0;
        g_physicalMeleeSampleTick = 0;
        g_physicalMeleeSwingSampleId = 0;
        g_physicalMeleeSwingSampleTick = 0;
        g_physicalMeleeSwingSpeedMetersPerSecond = 0.0F;
        ReleaseSRWLockExclusive(&g_physicalMeleeLock);
        InterlockedExchange(&g_physicalMeleeDamageQualified, 0);
        if (trackingWasActive && g_log != nullptr) {
            g_log(
                "m5_physical_melee_tracking_lost",
                "history_cleared=1 engine_writes=0");
        }
        return;
    }

    PhysicalMeleeFrame frame{};
    PhysicalMeleeFrame swingFrame{};
    PhysicalMeleeSwingAttackResult swingAttack{};
    PhysicalMeleeProfile sampledProfile{};
    bool newSample = false;
    bool newSwingSample = false;
    bool contactRearmed = false;
    const ULONGLONG now = GetTickCount64();
    const bool swingAttackContext = ProcessOwnsForegroundWindow() &&
        !VrToolMenuIsOpen() &&
        ReadRetailGameState(g_interfaceManager) ==
            kCondemnedGameStatePlaying;
    std::int32_t toolSettingsWeaponIndex = -1;
    AcquireSRWLockShared(&g_physicalMeleeLock);
    toolSettingsWeaponIndex = g_physicalMeleeProfileWeaponIndex;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    const ToolMenuMeleeSettings toolSettings =
        ReadVrToolMenuMeleeSettings(toolSettingsWeaponIndex);
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    if (toolSettingsWeaponIndex ==
        g_physicalMeleeProfileWeaponIndex) {
        ApplyToolMenuMeleeSettings(
            toolSettings, g_physicalMeleeProfile);
    }
    if (sampleId != g_physicalMeleeSampleId) {
        const PhysicalMeleePose pose{
            {position[0], position[1], position[2]},
            {rotation[0], rotation[1], rotation[2], rotation[3]}};
        frame = UpdatePhysicalMeleeKinematics(
            g_physicalMeleeState, pose, true, timestampNs,
            g_physicalMeleeProfile);
        g_physicalMeleeFrame = frame;
        g_physicalMeleeSampleId = sampleId;
        g_physicalMeleeSampleTick = now;
        contactRearmed = UpdatePhysicalMeleeContactSeparation(
            g_physicalMeleeContactState,
            frame.currentTipUnits, frame.poseValid,
            g_physicalMeleeProfile);
        sampledProfile = g_physicalMeleeProfile;
        newSample = true;
    }
    if (swingPoseFresh &&
        swingSampleId != g_physicalMeleeSwingSampleId) {
        const PhysicalMeleePose swingPose{
            PhysicalMeleeScale(
                swingGripPositionMeters,
                g_physicalMeleeProfile.unitsPerMeter),
            swingAimRotation};
        swingFrame = UpdatePhysicalMeleeKinematics(
            g_physicalMeleeSwingKinematicsState,
            swingPose, true, swingTimestampNs,
            g_physicalMeleeProfile);
        g_physicalMeleeSwingSampleId = swingSampleId;
        g_physicalMeleeSwingSampleTick = now;
        g_physicalMeleeSwingSpeedMetersPerSecond =
            swingFrame.sweepValid
                ? swingFrame.impactSpeedMetersPerSecond
                : 0.0F;
        swingAttack = UpdatePhysicalMeleeSwingAttack(
            g_physicalMeleeSwingAttackState, swingFrame,
            static_cast<std::uint64_t>(now),
            swingAttackContext, g_physicalMeleeProfile);
        sampledProfile = g_physicalMeleeProfile;
        newSwingSample = true;
    } else if (!swingPoseFresh) {
        ResetPhysicalMeleeKinematics(
            g_physicalMeleeSwingKinematicsState,
            PhysicalMeleeResetReason::TrackingLost);
        ResetPhysicalMeleeSwingAttack(
            g_physicalMeleeSwingAttackState);
        g_physicalMeleeSwingSampleId = 0;
        g_physicalMeleeSwingSampleTick = 0;
        g_physicalMeleeSwingSpeedMetersPerSecond = 0.0F;
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
    if ((!newSample && !newSwingSample) || g_log == nullptr) {
        return;
    }

    if (swingAttack.triggered) {
        const LONG trigger = InterlockedIncrement(
            &g_physicalMeleeSwingAttackTriggered);
        if (trigger <= 512) {
            char triggerDetail[384]{};
            std::snprintf(
                triggerDetail, sizeof(triggerDetail),
                "trigger=%ld sample_id=%llu profile=%s "
                "speed_mps=%.3f threshold_mps=%.3f "
                "pulse_ms=%u cooldown_ms=%u "
                "motion_space=openxr_tracking "
                "output=retail_fire_command_17",
                trigger,
                static_cast<unsigned long long>(swingSampleId),
                PhysicalMeleeProfileName(sampledProfile.id),
                swingFrame.impactSpeedMetersPerSecond,
                sampledProfile
                    .swingAttackTriggerSpeedMetersPerSecond,
                sampledProfile.swingAttackPulseMilliseconds,
                sampledProfile.swingAttackCooldownMilliseconds);
            g_log(
                "m5_physical_melee_swing_attack_triggered",
                triggerDetail);
        }
    }

    if (!newSample) {
        return;
    }

    if (contactRearmed) {
        const LONG rearm = InterlockedIncrement(
            &g_physicalMeleeContactRearmed);
        if (rearm <= 512) {
            char rearmDetail[256]{};
            std::snprintf(
                rearmDetail, sizeof(rearmDetail),
                "rearm=%ld sample_id=%llu normal_separation_m=0.12 "
                "native_impact_dispatch=blocked",
                rearm,
                static_cast<unsigned long long>(sampleId));
            g_log(
                "m5_physical_melee_contact_rearmed",
                rearmDetail);
        }
    }

    const LONG sampleCall = InterlockedIncrement(
        &g_physicalMeleeSampleCalls);
    const LONG wasDamageQualified = InterlockedExchange(
        &g_physicalMeleeDamageQualified,
        frame.damageQualified ? 1 : 0);
    const bool logSample = sampleCall <= 4 ||
        (frame.damageQualified && wasDamageQualified == 0) ||
        frame.resetReason ==
            PhysicalMeleeResetReason::InsufficientSampleInterval ||
        frame.resetReason ==
            PhysicalMeleeResetReason::ExcessiveSampleGap ||
        frame.resetReason ==
            PhysicalMeleeResetReason::ExcessiveTravel;
    if (!logSample || sampleCall > 512) {
        return;
    }
    char detail[896]{};
    std::snprintf(
        detail, sizeof(detail),
        "sample_call=%ld sample_id=%llu timestamp_ns=%llu "
        "base=(%.3f,%.3f,%.3f) tip=(%.3f,%.3f,%.3f) "
        "sweep_valid=%u sweep_m=%.4f speed_mps=%.3f "
        "energy_j=%.3f damage_qualified=%u reset=%s "
        "profile=%s engine_writes=0",
        sampleCall,
        static_cast<unsigned long long>(sampleId),
        static_cast<unsigned long long>(timestampNs),
        frame.currentBaseUnits.x, frame.currentBaseUnits.y,
        frame.currentBaseUnits.z, frame.currentTipUnits.x,
        frame.currentTipUnits.y, frame.currentTipUnits.z,
        frame.sweepValid ? 1U : 0U,
        frame.sweepDistanceMeters,
        frame.impactSpeedMetersPerSecond,
        frame.impactEnergyJoules,
        frame.damageQualified ? 1U : 0U,
        PhysicalMeleeResetReasonName(frame.resetReason),
        PhysicalMeleeProfileName(sampledProfile.id));
    g_log("m5_physical_melee_sample", detail);
}

void SelectPhysicalMeleeProfileForWeaponIndex(
    std::int32_t weaponIndex) noexcept {
    AcquireSRWLockShared(&g_physicalMeleeLock);
    const bool alreadySelected =
        weaponIndex == g_physicalMeleeProfileWeaponIndex;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    if (alreadySelected) {
        return;
    }
    PhysicalMeleeProfile selected =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    ApplyToolMenuMeleeSettings(
        ReadVrToolMenuMeleeSettings(weaponIndex), selected);
    RetailWeaponIdentitySnapshot identity{};
    const RetailWeaponIdentityReadResult identityResult =
        weaponIndex >= 0
        ? ReadRetailWeaponIdentity(weaponIndex, identity)
        : RetailWeaponIdentityReadResult::InvalidIndex;
    if (identityResult == RetailWeaponIdentityReadResult::Ok) {
        TryLogRetailWeaponCatalog();
    }
    if (!identity.nameResolved) {
        std::snprintf(
            identity.recordName, sizeof(identity.recordName), "%s",
            weaponIndex >= 0
                ? ToolMenuWeaponProfileLabel(selected.id)
                : "NO WEAPON");
    }
    if (!identity.animationPropertyResolved) {
        std::snprintf(
            identity.animationProperty,
            sizeof(identity.animationProperty), "UNKNOWN");
        identity.poseFamily = RetailWeaponPoseFamily::Unknown;
    }
    bool changed = false;
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    if (weaponIndex != g_physicalMeleeProfileWeaponIndex) {
        g_physicalMeleeProfileWeaponIndex = weaponIndex;
        g_physicalMeleeProfile = selected;
        g_equippedWeaponIdentity = identity;
        g_physicalMeleeState = {};
        g_physicalMeleeSwingKinematicsState = {};
        g_physicalMeleeFrame = {};
        g_physicalMeleeContactState = {};
        g_physicalMeleeSwingAttackState = {};
        g_physicalMeleeSampleId = 0;
        g_physicalMeleeSampleTick = 0;
        g_physicalMeleeSwingSampleId = 0;
        g_physicalMeleeSwingSampleTick = 0;
        g_physicalMeleeSwingSpeedMetersPerSecond = 0.0F;
        changed = true;
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
    if (!changed) {
        return;
    }
    InterlockedExchange(&g_physicalMeleeDamageQualified, 0);
    if (g_log != nullptr) {
        char detail[896]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld profile=%s "
            "weapon_name=%s animation_property=%s pose_family=%s "
            "identity_result=%s identity_name_resolved=%u "
            "identity_animation_resolved=%u "
            "mass_kg=%.2f "
            "handling_weight=%.2f positional_follow=%.2f "
            "rotational_follow=%.2f catch_up=%.2f damping_ratio=%.2f "
            "swing_attack=%u swing_trigger_mps=%.2f "
            "swing_rearm_mps=%.2f swing_pulse_ms=%u "
            "swing_cooldown_ms=%u "
            "grip_position=(%.3f,%.3f,%.3f) "
            "grip_rotation=(%.6f,%.6f,%.6f,%.6f) "
            "secondary_grip=%u "
            "secondary_offset=(%.3f,%.3f,%.3f) "
            "secondary_grab_radius_m=%.3f "
            "kinematics_reset=1",
            static_cast<long>(weaponIndex),
            PhysicalMeleeProfileName(selected.id),
            identity.recordName, identity.animationProperty,
            RetailWeaponPoseFamilyLabel(identity.poseFamily),
            RetailWeaponIdentityReadResultName(identityResult),
            identity.nameResolved ? 1U : 0U,
            identity.animationPropertyResolved ? 1U : 0U,
            selected.massKilograms, selected.handlingWeight,
            selected.positionalFollow, selected.rotationalFollow,
            selected.catchUpStrength, selected.dampingRatio,
            selected.swingAttackEnabled ? 1U : 0U,
            selected.swingAttackTriggerSpeedMetersPerSecond,
            selected.swingAttackRearmSpeedMetersPerSecond,
            selected.swingAttackPulseMilliseconds,
            selected.swingAttackCooldownMilliseconds,
            selected.modelLocalGripPositionUnits.x,
            selected.modelLocalGripPositionUnits.y,
            selected.modelLocalGripPositionUnits.z,
            selected.modelLocalGripRotation.x,
            selected.modelLocalGripRotation.y,
            selected.modelLocalGripRotation.z,
            selected.modelLocalGripRotation.w,
            selected.secondaryGripEnabled ? 1U : 0U,
            selected.secondaryGripOffsetUnits.x,
            selected.secondaryGripOffsetUnits.y,
            selected.secondaryGripOffsetUnits.z,
            selected.secondaryGripGrabRadiusMeters);
        g_log("m5_physical_melee_profile_selected", detail);
    }
}

void UpdateEquippedWeaponVisualSource() noexcept {
    if (g_gameClientBase == nullptr) {
        return;
    }
    const bool visualProxyEnabled = InterlockedCompareExchange(
        &g_physicalMeleeVisualProxyEnabled, 0, 0) != 0;
    if (g_interfaceManager != nullptr &&
        ReadRetailGameState(g_interfaceManager) !=
            kCondemnedGameStatePlaying) {
        SelectPhysicalMeleeProfileForWeaponIndex(-1);
        if (visualProxyEnabled) {
            InvalidatePhysicalMeleeVisualProxySource();
        }
        return;
    }

    void* weaponManager = nullptr;
    std::int32_t currentWeaponIndex = -1;
    void* const* currentWeaponReference = nullptr;
    void* currentWeapon = nullptr;
    void* const* modelObjectReference = nullptr;
    void* modelObject = nullptr;
    bool readable = false;
    __try {
        std::memcpy(
            &weaponManager,
            g_gameClientBase + kWeaponManagerGlobalRva,
            sizeof(weaponManager));
        if (weaponManager != nullptr) {
            currentWeaponIndex =
                *reinterpret_cast<const std::int32_t*>(
                    static_cast<unsigned char*>(weaponManager) +
                    kCurrentWeaponIndexOffset);
        }
        if (weaponManager != nullptr && currentWeaponIndex != -1) {
            currentWeaponReference =
                reinterpret_cast<void* const*>(
                    static_cast<unsigned char*>(weaponManager) +
                    kCurrentWeaponOffset);
            std::memcpy(
                &currentWeapon, currentWeaponReference,
                sizeof(currentWeapon));
        }
        if (visualProxyEnabled && currentWeapon != nullptr) {
            modelObjectReference =
                reinterpret_cast<void* const*>(
                    static_cast<unsigned char*>(currentWeapon) +
                    kRightWeaponModelObjectOffset);
            std::memcpy(
                &modelObject, modelObjectReference,
                sizeof(modelObject));
        }
        readable = weaponManager != nullptr &&
            currentWeaponIndex != -1 &&
            currentWeaponReference != nullptr &&
            currentWeapon != nullptr;
        if (visualProxyEnabled) {
            readable = readable &&
                modelObjectReference != nullptr &&
                modelObject != nullptr;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        readable = false;
    }
    if (!readable) {
        SelectPhysicalMeleeProfileForWeaponIndex(-1);
        if (visualProxyEnabled) {
            InvalidatePhysicalMeleeVisualProxySource();
        }
        return;
    }

    SelectPhysicalMeleeProfileForWeaponIndex(currentWeaponIndex);
    if (!visualProxyEnabled) {
        return;
    }

    float localGripPosition[3]{};
    float localGripRotation[4]{};
    AcquireSRWLockShared(&g_physicalMeleeLock);
    localGripPosition[0] =
        g_physicalMeleeProfile.modelLocalGripPositionUnits.x;
    localGripPosition[1] =
        g_physicalMeleeProfile.modelLocalGripPositionUnits.y;
    localGripPosition[2] =
        g_physicalMeleeProfile.modelLocalGripPositionUnits.z;
    localGripRotation[0] =
        g_physicalMeleeProfile.modelLocalGripRotation.x;
    localGripRotation[1] =
        g_physicalMeleeProfile.modelLocalGripRotation.y;
    localGripRotation[2] =
        g_physicalMeleeProfile.modelLocalGripRotation.z;
    localGripRotation[3] =
        g_physicalMeleeProfile.modelLocalGripRotation.w;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    PublishEquippedWeaponVisualProxySource(
        currentWeaponReference, currentWeapon,
        currentWeaponIndex,
        modelObjectReference, modelObject,
        localGripPosition, localGripRotation);
}

bool AimPathCommand(std::uint32_t command) noexcept {
    return command == kCondemnedFireCommand ||
        command == kCondemnedBlockCommand ||
        command == kCondemnedToggleMeleeCommand ||
        command == kCondemnedStunGunCommand;
}

void LogAimPathCommandEdge(
    std::uint32_t command,
    LONG active,
    bool controllerApplied,
    float retailValue,
    float outputValue) noexcept {
    if (InterlockedCompareExchange(
            &g_aimPathProbeEnabled, 0, 0) == 0 ||
        !AimPathCommand(command) || g_log == nullptr) {
        return;
    }
    VectorAbi controllerForward{};
    const bool controllerAim = ReadControllerForward(controllerForward);
    char stack[192]{};
    FormatGameClientStack(stack, sizeof(stack));
    char detail[512]{};
    std::snprintf(
        detail, sizeof(detail),
        "command=%u edge=%s controller_applied=%u "
        "retail_value=%.3f output_value=%.3f "
        "controller_aim_valid=%u controller_forward=(%.4f,%.4f,%.4f) "
        "gameorig_stack_rvas=%s",
        command, active != 0 ? "down" : "up",
        controllerApplied ? 1U : 0U, retailValue, outputValue,
        controllerAim ? 1U : 0U,
        controllerForward.x, controllerForward.y, controllerForward.z,
        stack);
    g_log("m5_aim_path_command_edge", detail);
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
    LogAimPathCommandEdge(
        command, active, controllerApplied, retailValue, outputValue);
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
        const bool calibrationCaptured =
            WeaponGripCalibrationCapturesInput(input, usable);
        const LocomotionDirections directions =
            ResolveLocomotionDirections(
                input, usable && !calibrationCaptured);
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
        const bool calibrationCaptured =
            WeaponGripCalibrationCapturesInput(input, usable);
        const ActivateValue activate =
            ResolveActivateValue(
                input, usable && !calibrationCaptured);
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
        const bool calibrationCaptured =
            WeaponGripCalibrationCapturesInput(input, usable);
        const bool secondaryGripCaptured =
            binding->command == kCondemnedRunCommand &&
            PhysicalMeleeSecondaryGripCapturesInput(input, usable);
        CoreActionValue action = ResolveCoreActionValue(
            input,
            usable && !calibrationCaptured &&
                !secondaryGripCaptured,
            binding->command);
        if (binding->command == kCondemnedFireCommand &&
            ReadPhysicalMeleeSwingAttackActive(
                usable && !calibrationCaptured)) {
            action = {ActiveBindingValue(*binding), true};
        }
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
    UpdatePhysicalMeleeProbe();
    if (command == kCondemnedYawAccelCommand) {
        UpdateEquippedWeaponVisualSource();
    }
    if ((command == kCondemnedPitchCommand ||
         command == kCondemnedYawCommand) &&
        InterlockedCompareExchange(
            &g_headAimInputEnabled, 0, 0) != 0 &&
        TrackedHeadAimIsFresh()) {
        if (retailValue != 0.0F &&
            InterlockedCompareExchange(
                &g_mouseLookSuppressionLogged, 1, 0) == 0 &&
            g_log != nullptr) {
            g_log(
                "m5_vr_mouse_look_suppressed",
                "commands=11,12 replacement=0 "
                "condition=fresh_focused_hmd_look "
                "keyboard_mouse_fallback_on_stale=1");
        }
        return 0.0F;
    }
    if (command != kCondemnedYawAccelCommand ||
        g_getInputState == nullptr) {
        return retailValue;
    }

    FearVrInputState input{};
    const bool usable = ReadUsableControllerInput(input);
    const bool calibrationCaptured =
        WeaponGripCalibrationCapturesInput(input, usable);
    const TurningValue turning = ResolveTurningValue(
        input, usable && !calibrationCaptured);
    const float outputValue = MergeTurningWithRetail(
        retailValue, turning);
    ReportTurnTransition(turning, retailValue, outputValue);
    return outputValue;
}

bool __fastcall HookGetFireVectors(
    const void* weapon,
    void* ignoredEdx,
    VectorAbi& right,
    VectorAbi& up,
    VectorAbi& forward,
    VectorAbi& firePosition) {
    (void)ignoredEdx;
    const bool result = g_originalGetFireVectors(
        weapon, right, up, forward, firePosition);
    if (InterlockedCompareExchange(
            &g_aimPathProbeEnabled, 0, 0) != 0 &&
        g_log != nullptr) {
        const LONG call = InterlockedIncrement(
            &g_aimPathFireVectorCalls);
        if (call <= 512) {
            VectorAbi controllerForward{};
            const bool controllerAim =
                ReadControllerForward(controllerForward);
            char stack[192]{};
            FormatGameClientStack(stack, sizeof(stack));
            char detail[640]{};
            std::snprintf(
                detail, sizeof(detail),
                "call=%ld result=%u weapon=%p "
                "retail_forward=(%.4f,%.4f,%.4f) "
                "retail_fire_position=(%.3f,%.3f,%.3f) "
                "controller_aim_valid=%u "
                "controller_forward=(%.4f,%.4f,%.4f) "
                "gameorig_stack_rvas=%s",
                call, result ? 1U : 0U, weapon,
                forward.x, forward.y, forward.z,
                firePosition.x, firePosition.y, firePosition.z,
                controllerAim ? 1U : 0U,
                controllerForward.x, controllerForward.y,
                controllerForward.z, stack);
            g_log("m5_aim_path_fire_vectors", detail);
        }
    }
    float rotation[4]{};
    if (!result || !ReadTrackedControllerAimRotation(rotation)) {
        return result;
    }
    const fearvr::TrackingQuaternion controller = fearvr::Normalize({
        rotation[0], rotation[1], rotation[2], rotation[3]});
    if (!fearvr::IsFinite(controller)) {
        return result;
    }
    const fearvr::TrackingVector controllerRight =
        fearvr::Rotate(controller, {1.0F, 0.0F, 0.0F});
    const fearvr::TrackingVector controllerUp =
        fearvr::Rotate(controller, {0.0F, 1.0F, 0.0F});
    const fearvr::TrackingVector controllerForward =
        fearvr::Rotate(controller, {0.0F, 0.0F, 1.0F});
    if (!fearvr::IsFinite(controllerRight) ||
        !fearvr::IsFinite(controllerUp) ||
        !fearvr::IsFinite(controllerForward)) {
        return result;
    }
    right = {
        controllerRight.x, controllerRight.y, controllerRight.z};
    up = {controllerUp.x, controllerUp.y, controllerUp.z};
    forward = {
        controllerForward.x,
        controllerForward.y,
        controllerForward.z};
    if (InterlockedCompareExchange(
            &g_controllerFireAimLogged, 1, 0) == 0 &&
        g_log != nullptr) {
        g_log(
            "m5_controller_fire_vectors_active",
            "target=GameOrig+0x0002AF70 "
            "direction=right_controller_world_basis "
            "retail_fire_position_preserved=1 stale_fallback=retail");
    }
    return result;
}

std::uintptr_t __fastcall HookMeleeEnableCollisions(
    void* controller,
    void* ignoredEdx,
    std::uintptr_t argument1,
    std::uintptr_t argument2,
    std::uintptr_t argument3,
    std::uintptr_t argument4,
    std::uintptr_t argument5) {
    (void)ignoredEdx;
    if (InterlockedCompareExchange(
            &g_aimPathProbeEnabled, 0, 0) != 0 &&
        g_log != nullptr) {
        const LONG call = InterlockedIncrement(&g_aimPathMeleeCalls);
        if (call <= 512) {
            VectorAbi controllerForward{};
            const bool controllerAim =
                ReadControllerForward(controllerForward);
            char stack[192]{};
            FormatGameClientStack(stack, sizeof(stack));
            char detail[704]{};
            std::snprintf(
                detail, sizeof(detail),
                "call=%ld controller=%p "
                "args=(0x%08lX,0x%08lX,0x%08lX,0x%08lX,0x%08lX) "
                "controller_aim_valid=%u "
                "controller_forward=(%.4f,%.4f,%.4f) "
                "gameorig_stack_rvas=%s behavior=pass_through",
                call, controller,
                static_cast<unsigned long>(argument1),
                static_cast<unsigned long>(argument2),
                static_cast<unsigned long>(argument3),
                static_cast<unsigned long>(argument4),
                static_cast<unsigned long>(argument5),
                controllerAim ? 1U : 0U,
                controllerForward.x, controllerForward.y,
                controllerForward.z, stack);
            g_log("m5_aim_path_melee_collision_enable", detail);
        }
    }
    return g_originalMeleeEnableCollisions(
        controller, argument1, argument2, argument3, argument4,
        argument5);
}

void __fastcall HookMeleeUpdateCollision(
    void* controller,
    void* ignoredEdx,
    void* record) {
    (void)ignoredEdx;
    g_originalMeleeUpdateCollision(controller, record);
    if (InterlockedCompareExchange(
            &g_aimPathProbeEnabled, 0, 0) == 0 ||
        g_log == nullptr || record == nullptr) {
        return;
    }

    std::uintptr_t sourceObject = 0;
    std::uintptr_t sourceNode = 0;
    std::uintptr_t collisionObject = 0;
    unsigned int attackIndex = 0;
    unsigned int collisionFinished = 0;
    bool readable = false;
    __try {
        auto* const bytes = static_cast<unsigned char*>(record);
        std::memcpy(
            &sourceObject, bytes + 0x38, sizeof(sourceObject));
        std::memcpy(&sourceNode, bytes + 0x3C, sizeof(sourceNode));
        std::memcpy(
            &collisionObject, bytes + 0x40,
            sizeof(collisionObject));
        attackIndex = bytes[0x10];
        collisionFinished = bytes[0x58];
        readable = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        readable = false;
    }
    if (!readable || collisionObject == 0) {
        return;
    }

    const LONG call = InterlockedIncrement(
        &g_aimPathMeleeUpdateCalls);
    if (call > 512) {
        return;
    }
    VectorAbi controllerForward{};
    const bool controllerAim = ReadControllerForward(controllerForward);
    int slot = -1;
    const auto controllerAddress = reinterpret_cast<std::uintptr_t>(
        controller);
    const auto recordAddress = reinterpret_cast<std::uintptr_t>(record);
    if (controllerAddress != 0 && recordAddress >= controllerAddress + 0x18 &&
        recordAddress < controllerAddress + 0xD8) {
        slot = static_cast<int>(
            (recordAddress - (controllerAddress + 0x18)) / 0x60);
    }
    char stack[192]{};
    FormatGameClientStack(stack, sizeof(stack));
    char detail[896]{};
    std::snprintf(
        detail, sizeof(detail),
        "call=%ld slot=%d controller=%p record=%p attack_index=%u "
        "source_object=0x%08lX source_node=0x%08lX "
        "collision_object=0x%08lX collision_finished=%u "
        "controller_aim_valid=%u "
        "controller_forward=(%.4f,%.4f,%.4f) "
        "gameorig_stack_rvas=%s behavior=pass_through",
        call, slot, controller, record, attackIndex,
        static_cast<unsigned long>(sourceObject),
        static_cast<unsigned long>(sourceNode),
        static_cast<unsigned long>(collisionObject),
        collisionFinished,
        controllerAim ? 1U : 0U,
        controllerForward.x, controllerForward.y,
        controllerForward.z, stack);
    g_log("m5_aim_path_melee_collision_update", detail);
}

void* __fastcall HookBuildRigidTransform(
    void* destination,
    void* ignoredEdx,
    const VectorAbi* position,
    const QuaternionAbi* rotation) {
    (void)ignoredEdx;
    const auto* const caller = static_cast<const unsigned char*>(
        _ReturnAddress());
    const bool meleeTransformCall = g_gameClientBase != nullptr &&
        caller == g_gameClientBase +
            kMeleeBuildRigidTransformReturnRva;
    if (!meleeTransformCall || position == nullptr ||
        rotation == nullptr) {
        return g_originalBuildRigidTransform(
            destination, position, rotation);
    }

    VectorAbi retailPosition{};
    QuaternionAbi retailRotation{};
    bool readable = false;
    __try {
        retailPosition = *position;
        retailRotation = *rotation;
        readable = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        readable = false;
    }
    if (!readable ||
        !std::isfinite(retailPosition.x) ||
        !std::isfinite(retailPosition.y) ||
        !std::isfinite(retailPosition.z) ||
        !std::isfinite(retailRotation.x) ||
        !std::isfinite(retailRotation.y) ||
        !std::isfinite(retailRotation.z) ||
        !std::isfinite(retailRotation.w)) {
        return g_originalBuildRigidTransform(
            destination, position, rotation);
    }

    VectorAbi appliedPosition = retailPosition;
    QuaternionAbi appliedRotation = retailRotation;
    bool physicalWallProxyApplied = false;
    std::uint64_t physicalSampleId = 0;
    const bool physicalWallProxyRequested =
        InterlockedCompareExchange(
            &g_physicalMeleeWallProxyEnabled, 0, 0) != 0;
    if (physicalWallProxyRequested) {
        PhysicalMeleeFrame frame{};
        if (CopyLatestPhysicalMeleeFrame(frame, physicalSampleId)) {
            const PhysicalMeleeWallProxyTransform proxy =
                ResolvePhysicalMeleeWallProxyTransform(frame, true);
            if (proxy.active) {
                appliedPosition = {
                    proxy.positionUnits.x,
                    proxy.positionUnits.y,
                    proxy.positionUnits.z};
                appliedRotation = {
                    proxy.rotation.x,
                    proxy.rotation.y,
                    proxy.rotation.z,
                    proxy.rotation.w};
                physicalWallProxyApplied = true;
            }
        }
    }
    bool meleeAimApplied = false;
    if (!physicalWallProxyRequested &&
        InterlockedCompareExchange(
            &g_controllerMeleeAimEnabled, 0, 0) != 0) {
        float pivot[3]{};
        float baseRotation[4]{};
        float controllerRotation[4]{};
        const bool basisFresh = ReadTrackedMeleeAimBasis(
            pivot, baseRotation, controllerRotation);
        const auto resolved =
            ResolveControllerRelativeMeleeTransform(
                {retailPosition.x, retailPosition.y, retailPosition.z},
                {retailRotation.x, retailRotation.y,
                 retailRotation.z, retailRotation.w},
                {pivot[0], pivot[1], pivot[2]},
                {baseRotation[0], baseRotation[1],
                 baseRotation[2], baseRotation[3]},
                {controllerRotation[0], controllerRotation[1],
                 controllerRotation[2], controllerRotation[3]},
                basisFresh);
        if (resolved.active) {
            appliedPosition = {
                resolved.position.x,
                resolved.position.y,
                resolved.position.z};
            appliedRotation = {
                resolved.rotation.x,
                resolved.rotation.y,
                resolved.rotation.z,
                resolved.rotation.w};
            meleeAimApplied = true;
        }
    }

    void* const result = g_originalBuildRigidTransform(
        destination, &appliedPosition, &appliedRotation);
    if (physicalWallProxyApplied &&
        InterlockedCompareExchange(
            &g_physicalMeleeWallProxyAppliedLogged, 1, 0) == 0 &&
        g_log != nullptr) {
        char detail[384]{};
        std::snprintf(
            detail, sizeof(detail),
            "target=GameOrig+0x0000F690 sample_id=%llu "
            "source=controller_weapon_tip native_impact_dispatch=blocked "
            "actor_damage=0 stale_fallback=retail_transform",
            static_cast<unsigned long long>(physicalSampleId));
        g_log("m5_physical_melee_wall_proxy_active", detail);
    }
    if (meleeAimApplied &&
        InterlockedCompareExchange(
            &g_controllerMeleeAimLogged, 1, 0) == 0 &&
        g_log != nullptr) {
        g_log(
            "m5_controller_melee_aim_active",
            "target=GameOrig+0x0000F690 "
            "source=melee_node_transform "
            "operation=controller_delta_about_camera_pivot "
            "retail_swing_timing_and_shape_preserved=1 "
            "stale_fallback=retail");
    }
    if (InterlockedCompareExchange(
            &g_aimPathProbeEnabled, 0, 0) == 0 ||
        g_log == nullptr) {
        return result;
    }
    const LONG call = InterlockedIncrement(
        &g_aimPathMeleeTransformCalls);
    if (call > 512) {
        return result;
    }
    VectorAbi controllerForward{};
    const bool controllerAim = ReadControllerForward(controllerForward);
    char detail[960]{};
    std::snprintf(
        detail, sizeof(detail),
        "call=%ld retail_position=(%.3f,%.3f,%.3f) "
        "applied_position=(%.3f,%.3f,%.3f) "
        "retail_rotation=(%.5f,%.5f,%.5f,%.5f) "
        "applied_rotation=(%.5f,%.5f,%.5f,%.5f) "
        "melee_aim_applied=%u physical_wall_proxy_applied=%u "
        "physical_sample_id=%llu "
        "controller_aim_valid=%u "
        "controller_forward=(%.4f,%.4f,%.4f) "
        "source=melee_node_transform",
        call, retailPosition.x, retailPosition.y, retailPosition.z,
        appliedPosition.x, appliedPosition.y, appliedPosition.z,
        retailRotation.x, retailRotation.y, retailRotation.z,
        retailRotation.w,
        appliedRotation.x, appliedRotation.y, appliedRotation.z,
        appliedRotation.w, meleeAimApplied ? 1U : 0U,
        physicalWallProxyApplied ? 1U : 0U,
        static_cast<unsigned long long>(physicalSampleId),
        controllerAim ? 1U : 0U,
        controllerForward.x, controllerForward.y,
        controllerForward.z);
    g_log("m5_aim_path_melee_physics_transform", detail);
    return result;
}

std::uintptr_t __fastcall HookMeleeImpactDispatch(
    void* impactController,
    void* ignoredEdx,
    std::uintptr_t argument1,
    std::uintptr_t argument2,
    std::uintptr_t argument3,
    std::uintptr_t argument4,
    std::uintptr_t argument5,
    std::uintptr_t argument6,
    std::uintptr_t argument7,
    std::uintptr_t argument8,
    std::uintptr_t argument9) {
    (void)ignoredEdx;
    const auto* const caller = static_cast<const unsigned char*>(
        _ReturnAddress());
    const bool verifiedMeleeCallback = g_gameClientBase != nullptr &&
        caller == g_gameClientBase +
            kMeleeImpactDispatchReturnRva;
    if (!verifiedMeleeCallback ||
        InterlockedCompareExchange(
            &g_aimPathProbeEnabled, 0, 0) == 0) {
        return g_originalMeleeImpactDispatch(
            impactController, argument1, argument2, argument3,
            argument4, argument5, argument6, argument7,
            argument8, argument9);
    }

    VectorAbi contactPosition{};
    VectorAbi contactNormal{};
    const bool contactPositionValid = ReadVectorCandidate(
        argument4, contactPosition);
    const bool contactNormalValid = ReadVectorCandidate(
        argument5, contactNormal);
    VectorAbi controllerForward{};
    const bool controllerAim = ReadControllerForward(controllerForward);
    PhysicalMeleeFrame physicalFrame{};
    std::uint64_t physicalSampleId = 0;
    const bool physicalWallProxyEnabled =
        InterlockedCompareExchange(
            &g_physicalMeleeWallProxyEnabled, 0, 0) != 0;
    PhysicalMeleeContactQualification physicalContact{};
    const bool physicalFrameAvailable =
        InterlockedCompareExchange(
            &g_physicalMeleeProbeEnabled, 0, 0) != 0 &&
        (physicalWallProxyEnabled
             ? EvaluatePhysicalMeleeContact(
                   argument1, contactPosition, contactNormal,
                   physicalFrame, physicalSampleId,
                   physicalContact)
             : CopyLatestPhysicalMeleeFrame(
                   physicalFrame, physicalSampleId));
    if (physicalContact.accepted) {
        InterlockedIncrement(&g_physicalMeleeContactAccepted);
    }
    const bool nativeImpactForwarded =
        ShouldDispatchPhysicalMeleeNativeImpact(
            physicalWallProxyEnabled);
    const std::uintptr_t result = nativeImpactForwarded
        ? g_originalMeleeImpactDispatch(
              impactController, argument1, argument2, argument3,
              argument4, argument5, argument6, argument7,
              argument8, argument9)
        : 0U;
    if (g_log == nullptr) {
        return result;
    }
    const LONG call = InterlockedIncrement(
        &g_aimPathMeleeImpactCalls);
    if (call > 512) {
        return result;
    }
    char detail[1792]{};
    std::snprintf(
        detail, sizeof(detail),
        "call=%ld impact_controller=%p "
        "args=(0x%08lX,0x%08lX,0x%08lX,0x%08lX,0x%08lX,"
        "0x%08lX,0x%08lX,0x%08lX,0x%08lX) "
        "contact_position_valid=%u "
        "contact_position=(%.3f,%.3f,%.3f) "
        "contact_normal_valid=%u "
        "contact_normal=(%.4f,%.4f,%.4f) "
        "controller_aim_valid=%u "
        "controller_forward=(%.4f,%.4f,%.4f) "
        "physical_sample_valid=%u physical_sample_id=%llu "
        "physical_base=(%.3f,%.3f,%.3f) "
        "physical_tip=(%.3f,%.3f,%.3f) "
        "physical_sweep_valid=%u physical_speed_mps=%.3f "
        "physical_energy_j=%.3f physical_damage_qualified=%u "
        "physical_wall_proxy=%u native_impact_forwarded=%u "
        "physical_contact_accepted=%u contact_reason=%s "
        "normal_speed_mps=%.3f normal_energy_j=%.3f "
        "result=0x%08lX source=melee_collision_callback "
        "behavior=%s",
        call, impactController,
        static_cast<unsigned long>(argument1),
        static_cast<unsigned long>(argument2),
        static_cast<unsigned long>(argument3),
        static_cast<unsigned long>(argument4),
        static_cast<unsigned long>(argument5),
        static_cast<unsigned long>(argument6),
        static_cast<unsigned long>(argument7),
        static_cast<unsigned long>(argument8),
        static_cast<unsigned long>(argument9),
        contactPositionValid ? 1U : 0U,
        contactPosition.x, contactPosition.y, contactPosition.z,
        contactNormalValid ? 1U : 0U,
        contactNormal.x, contactNormal.y, contactNormal.z,
        controllerAim ? 1U : 0U,
        controllerForward.x, controllerForward.y,
        controllerForward.z,
        physicalFrameAvailable ? 1U : 0U,
        static_cast<unsigned long long>(physicalSampleId),
        physicalFrame.currentBaseUnits.x,
        physicalFrame.currentBaseUnits.y,
        physicalFrame.currentBaseUnits.z,
        physicalFrame.currentTipUnits.x,
        physicalFrame.currentTipUnits.y,
        physicalFrame.currentTipUnits.z,
        physicalFrame.sweepValid ? 1U : 0U,
        physicalFrame.impactSpeedMetersPerSecond,
        physicalFrame.impactEnergyJoules,
        physicalFrame.damageQualified ? 1U : 0U,
        physicalWallProxyEnabled ? 1U : 0U,
        nativeImpactForwarded ? 1U : 0U,
        physicalContact.accepted ? 1U : 0U,
        PhysicalMeleeContactReasonName(physicalContact.reason),
        physicalContact.normalSpeedMetersPerSecond,
        physicalContact.normalEnergyJoules,
        static_cast<unsigned long>(result),
        nativeImpactForwarded ? "pass_through" : "wall_probe_blocked");
    g_log("m5_aim_path_melee_impact_dispatch", detail);
    return result;
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
    if (previous == kCondemnedGameStatePlaying &&
        publishedState != kCondemnedGameStatePlaying) {
        InvalidatePhysicalMeleeVisualProxySource();
    }
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

bool PollMenuToggle(
    void* clientShell,
    int retailGameState) noexcept {
    FearVrInputState input{};
    const bool usable = ReadUsableControllerInput(input) &&
        CondemnedGameStateAllowsMenuToggle(retailGameState);
    const bool calibrationCaptured =
        WeaponGripCalibrationCapturesInput(input, usable);
    if (!ConsumeMenuTogglePress(
            g_menuToggleLatch, input,
            usable && !calibrationCaptured)) {
        return false;
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
    return true;
}

int MenuNavigationVirtualKey(
    MenuNavigationAction action) noexcept {
    switch (action) {
    case MenuNavigationAction::up:
        return VK_UP;
    case MenuNavigationAction::down:
        return VK_DOWN;
    case MenuNavigationAction::left:
        return VK_LEFT;
    case MenuNavigationAction::right:
        return VK_RIGHT;
    case MenuNavigationAction::accept:
        return VK_RETURN;
    case MenuNavigationAction::back:
        return VK_ESCAPE;
    default:
        return 0;
    }
}

const char* MenuNavigationControlName(
    MenuNavigationAction action) noexcept {
    switch (action) {
    case MenuNavigationAction::up:
    case MenuNavigationAction::down:
    case MenuNavigationAction::left:
    case MenuNavigationAction::right:
        return "left_stick";
    case MenuNavigationAction::accept:
        return "right_primary_or_trigger";
    case MenuNavigationAction::back:
        return "right_secondary";
    default:
        return "none";
    }
}

void PollMenuNavigation(
    void* clientShell,
    int retailGameState) noexcept {
    FearVrInputState input{};
    const bool usable = ReadUsableControllerInput(input);
    const bool calibrationCaptured =
        WeaponGripCalibrationCapturesInput(input, usable);
    const MenuNavigationAction action = UpdateMenuNavigation(
        g_menuNavigationState,
        input,
        usable && !calibrationCaptured,
        CondemnedGameStateAllowsMenuNavigation(retailGameState),
        GetTickCount64());
    const int virtualKey = MenuNavigationVirtualKey(action);
    if (virtualKey == 0) {
        return;
    }

    bool dispatched = false;
    __try {
        g_clientShellKeyDown(clientShell, virtualKey, 1);
        g_clientShellKeyUp(clientShell, virtualKey);
        dispatched = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dispatched = false;
    }
    if (g_log == nullptr) {
        return;
    }

    char detail[256]{};
    std::snprintf(
        detail, sizeof(detail),
        "action=%s key=0x%02X control=%s game_state=%d "
        "path=IClientShell_v4_key_edge direct_command_writes=0 "
        "system_input=0",
        MenuNavigationActionName(action), virtualKey,
        MenuNavigationControlName(action), retailGameState);
    g_log(
        dispatched
            ? "m6_menu_control_dispatched"
            : "m6_menu_control_failed",
        detail);
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
        if (PollMenuToggle(clientShell, stateBeforeInput)) {
            RequireMenuNavigationRelease(g_menuNavigationState);
        } else if (InterlockedCompareExchange(
                       &g_menuControlsEnabled, 0, 0) != 0) {
            PollMenuNavigation(clientShell, stateBeforeInput);
        }
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

bool FireVectorsTargetMatches(const unsigned char* target) noexcept {
    if (target == nullptr) {
        return false;
    }
    __try {
        return std::memcmp(
                   target, kGetFireVectorsPrefix,
                   sizeof(kGetFireVectorsPrefix)) == 0 &&
               std::memcmp(
                   target + 0x08, kGetFireVectorsStackInit,
                   sizeof(kGetFireVectorsStackInit)) == 0 &&
               std::memcmp(
                   target + 0x2A, kGetFireVectorsCameraProbe,
                   sizeof(kGetFireVectorsCameraProbe)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MeleeEnableCollisionsTargetMatches(
    HMODULE gameClientModule,
    const unsigned char* target) noexcept {
    if (gameClientModule == nullptr || target == nullptr) {
        return false;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    __try {
        std::uint32_t clientGlobal = 0;
        std::uint32_t limitText = 0;
        std::memcpy(&clientGlobal, target + 7, sizeof(clientGlobal));
        std::memcpy(&limitText, target + 0x10F, sizeof(limitText));
        return std::memcmp(
                   target, kMeleeEnableCollisionsPrefix,
                   sizeof(kMeleeEnableCollisionsPrefix)) == 0 &&
               clientGlobal == static_cast<std::uint32_t>(
                   reinterpret_cast<std::uintptr_t>(
                       base + kMeleeClientGlobalRva)) &&
               std::memcmp(
                   target + 0x0B, kMeleeEnableCollisionsBodyPrefix,
                   sizeof(kMeleeEnableCollisionsBodyPrefix)) == 0 &&
               std::memcmp(
                   target + 0x106,
                   kMeleeCollisionLimitTextReferencePrefix,
                   sizeof(kMeleeCollisionLimitTextReferencePrefix)) == 0 &&
               limitText == static_cast<std::uint32_t>(
                   reinterpret_cast<std::uintptr_t>(
                       base + kMeleeCollisionLimitTextRva));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MeleeUpdateCollisionTargetMatches(
    const unsigned char* target) noexcept {
    if (target == nullptr) {
        return false;
    }
    __try {
        return std::memcmp(
                   target, kMeleeUpdateCollisionPrefix,
                   sizeof(kMeleeUpdateCollisionPrefix)) == 0 &&
               std::memcmp(
                   target + 0x7B, kMeleeUpdateCollisionNodeQuery,
                   sizeof(kMeleeUpdateCollisionNodeQuery)) == 0 &&
               std::memcmp(
                   target + 0xE2, kMeleeUpdateCollisionSetTransform,
                   sizeof(kMeleeUpdateCollisionSetTransform)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool BuildRigidTransformTargetMatches(
    const unsigned char* target) noexcept {
    if (target == nullptr) {
        return false;
    }
    __try {
        return std::memcmp(
                   target, kBuildRigidTransformPrefix,
                   sizeof(kBuildRigidTransformPrefix)) == 0 &&
               std::memcmp(
                   target + 0x3D, kBuildRigidTransformTail,
                   sizeof(kBuildRigidTransformTail)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MeleeImpactDispatchTargetMatches(
    HMODULE gameClientModule,
    const unsigned char* target) noexcept {
    if (gameClientModule == nullptr || target == nullptr) {
        return false;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    auto* const callbackCallsite =
        base + kMeleeCollisionCallbackRva + 0x36DU;
    auto* const dispatchCall =
        base + kMeleeImpactDispatchCallRva;
    __try {
        std::int32_t relativeTarget = 0;
        std::memcpy(
            &relativeTarget, dispatchCall + 1,
            sizeof(relativeTarget));
        const auto resolvedTarget =
            reinterpret_cast<std::uintptr_t>(dispatchCall + 5) +
            static_cast<std::intptr_t>(relativeTarget);
        return std::memcmp(
                   target, kMeleeImpactDispatchPrefix,
                   sizeof(kMeleeImpactDispatchPrefix)) == 0 &&
               std::memcmp(
                   callbackCallsite,
                   kMeleeImpactDispatchCallsitePrefix,
                   sizeof(kMeleeImpactDispatchCallsitePrefix)) == 0 &&
               dispatchCall[0] == 0xE8 &&
               resolvedTarget ==
                   reinterpret_cast<std::uintptr_t>(target);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool EquippedWeaponLayoutMatches(
    HMODULE gameClientModule) noexcept {
    if (gameClientModule == nullptr) {
        return false;
    }
    auto* const base = reinterpret_cast<unsigned char*>(
        gameClientModule);
    auto* const getCurrentWeapon =
        base + kGetCurrentWeaponRva;
    auto* const setWeaponTransform =
        base + kSetWeaponTransformRva;
    __try {
        return std::memcmp(
                   getCurrentWeapon, kGetCurrentWeaponBody,
                   sizeof(kGetCurrentWeaponBody)) == 0 &&
               std::memcmp(
                   setWeaponTransform, kSetWeaponTransformPrefix,
                   sizeof(kSetWeaponTransformPrefix)) == 0 &&
               std::memcmp(
                   setWeaponTransform + 0x1C,
                   kSetWeaponTransformSecondModel,
                   sizeof(kSetWeaponTransformSecondModel)) == 0;
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

bool InstallHeadAimHooks(
    HMODULE gameClientModule,
    RendererProbeLogFunction log,
    bool aimPathProbe,
    bool controllerMeleeAim,
    bool physicalMeleeProbe,
    bool physicalMeleeWallProxy,
    bool physicalMeleeVisualProxy,
    bool weaponGripCalibration,
    bool twoHandedMelee) noexcept {
    if (gameClientModule == nullptr || log == nullptr) {
        return false;
    }
    if (g_turningHookTarget == nullptr ||
        g_originalGetExtremalCommandValue == nullptr) {
        log(
            "m5_head_aim_rejected",
            "verified_extremal_binding_hook_required");
        return false;
    }
    if (controllerMeleeAim && !aimPathProbe) {
        log(
            "m5_controller_melee_aim_rejected",
            "aim_path_probe_required_for_initial_live_gate");
        return false;
    }
    if (physicalMeleeProbe && !aimPathProbe) {
        log(
            "m5_physical_melee_probe_rejected",
            "aim_path_probe_required_for_native_impact_trace");
        return false;
    }
    if (physicalMeleeWallProxy && !physicalMeleeProbe) {
        log(
            "m5_physical_melee_wall_proxy_rejected",
            "physical_melee_probe_required");
        return false;
    }
    if (physicalMeleeWallProxy && controllerMeleeAim) {
        log(
            "m5_physical_melee_wall_proxy_rejected",
            "controller_melee_aim_conflicts_with_physical_proxy");
        return false;
    }
    if (physicalMeleeVisualProxy && !physicalMeleeWallProxy) {
        log(
            "m5_physical_melee_visual_proxy_rejected",
            "physical_melee_wall_proxy_required");
        return false;
    }
    if (weaponGripCalibration && !physicalMeleeVisualProxy) {
        log(
            "m5_weapon_grip_calibration_rejected",
            "physical_melee_visual_proxy_required");
        return false;
    }
    if (twoHandedMelee && !physicalMeleeVisualProxy) {
        log(
            "m5_two_handed_melee_rejected",
            "physical_melee_visual_proxy_required");
        return false;
    }
    auto* const fireVectors =
        reinterpret_cast<unsigned char*>(gameClientModule) +
        kGetFireVectorsRva;
    auto* const meleeEnableCollisions =
        reinterpret_cast<unsigned char*>(gameClientModule) +
        kMeleeEnableCollisionsRva;
    auto* const meleeUpdateCollision =
        reinterpret_cast<unsigned char*>(gameClientModule) +
        kMeleeUpdateCollisionRva;
    auto* const buildRigidTransform =
        reinterpret_cast<unsigned char*>(gameClientModule) +
        kBuildRigidTransformRva;
    auto* const meleeImpactDispatch =
        reinterpret_cast<unsigned char*>(gameClientModule) +
        kMeleeImpactDispatchRva;
    if (!FireVectorsTargetMatches(fireVectors)) {
        log(
            "m5_head_aim_rejected",
            "GameOrig_rva_0002af70_fire_vector_signature_mismatch");
        return false;
    }
    if (aimPathProbe && !MeleeEnableCollisionsTargetMatches(
            gameClientModule, meleeEnableCollisions)) {
        log(
            "m5_aim_path_rejected",
            "GameOrig_rva_0001fd00_melee_collision_signature_mismatch");
        return false;
    }
    if (aimPathProbe && !MeleeUpdateCollisionTargetMatches(
            meleeUpdateCollision)) {
        log(
            "m5_aim_path_rejected",
            "GameOrig_rva_0001fc00_melee_update_signature_mismatch");
        return false;
    }
    if (aimPathProbe && !BuildRigidTransformTargetMatches(
            buildRigidTransform)) {
        log(
            "m5_aim_path_rejected",
            "GameOrig_rva_0000f690_transform_builder_signature_mismatch");
        return false;
    }
    if (aimPathProbe && !MeleeImpactDispatchTargetMatches(
            gameClientModule, meleeImpactDispatch)) {
        log(
            "m5_aim_path_rejected",
            "GameOrig_rva_0001f270_melee_impact_signature_mismatch");
        return false;
    }
    if (physicalMeleeVisualProxy &&
        !EquippedWeaponLayoutMatches(gameClientModule)) {
        log(
            "m5_physical_melee_visual_proxy_rejected",
            "GameOrig_current_weapon_or_model_layout_mismatch");
        return false;
    }

    const MH_STATUS initialize = MH_Initialize();
    if (initialize != MH_OK &&
        initialize != MH_ERROR_ALREADY_INITIALIZED) {
        log("m5_head_aim_rejected", MH_StatusToString(initialize));
        return false;
    }
    MH_STATUS status = MH_CreateHook(
        fireVectors,
        reinterpret_cast<void*>(&HookGetFireVectors),
        reinterpret_cast<void**>(&g_originalGetFireVectors));
    if (status == MH_OK) {
        status = MH_EnableHook(fireVectors);
    }
    if (status != MH_OK) {
        MH_RemoveHook(fireVectors);
        g_originalGetFireVectors = nullptr;
        log("m5_head_aim_rejected", MH_StatusToString(status));
        return false;
    }
    if (aimPathProbe) {
        status = MH_CreateHook(
            meleeEnableCollisions,
            reinterpret_cast<void*>(&HookMeleeEnableCollisions),
            reinterpret_cast<void**>(
                &g_originalMeleeEnableCollisions));
        if (status == MH_OK) {
            status = MH_EnableHook(meleeEnableCollisions);
        }
        if (status != MH_OK) {
            MH_RemoveHook(meleeEnableCollisions);
            g_originalMeleeEnableCollisions = nullptr;
            MH_DisableHook(fireVectors);
            MH_RemoveHook(fireVectors);
            g_originalGetFireVectors = nullptr;
            log("m5_aim_path_rejected", MH_StatusToString(status));
            return false;
        }
        status = MH_CreateHook(
            meleeUpdateCollision,
            reinterpret_cast<void*>(&HookMeleeUpdateCollision),
            reinterpret_cast<void**>(
                &g_originalMeleeUpdateCollision));
        if (status == MH_OK) {
            status = MH_EnableHook(meleeUpdateCollision);
        }
        if (status != MH_OK) {
            MH_RemoveHook(meleeUpdateCollision);
            g_originalMeleeUpdateCollision = nullptr;
            MH_DisableHook(meleeEnableCollisions);
            MH_RemoveHook(meleeEnableCollisions);
            g_originalMeleeEnableCollisions = nullptr;
            MH_DisableHook(fireVectors);
            MH_RemoveHook(fireVectors);
            g_originalGetFireVectors = nullptr;
            log("m5_aim_path_rejected", MH_StatusToString(status));
            return false;
        }
        status = MH_CreateHook(
            buildRigidTransform,
            reinterpret_cast<void*>(&HookBuildRigidTransform),
            reinterpret_cast<void**>(
                &g_originalBuildRigidTransform));
        if (status == MH_OK) {
            status = MH_EnableHook(buildRigidTransform);
        }
        if (status != MH_OK) {
            MH_RemoveHook(buildRigidTransform);
            g_originalBuildRigidTransform = nullptr;
            MH_DisableHook(meleeUpdateCollision);
            MH_RemoveHook(meleeUpdateCollision);
            g_originalMeleeUpdateCollision = nullptr;
            MH_DisableHook(meleeEnableCollisions);
            MH_RemoveHook(meleeEnableCollisions);
            g_originalMeleeEnableCollisions = nullptr;
            MH_DisableHook(fireVectors);
            MH_RemoveHook(fireVectors);
            g_originalGetFireVectors = nullptr;
            log("m5_aim_path_rejected", MH_StatusToString(status));
            return false;
        }
        status = MH_CreateHook(
            meleeImpactDispatch,
            reinterpret_cast<void*>(&HookMeleeImpactDispatch),
            reinterpret_cast<void**>(
                &g_originalMeleeImpactDispatch));
        if (status == MH_OK) {
            status = MH_EnableHook(meleeImpactDispatch);
        }
        if (status != MH_OK) {
            MH_RemoveHook(meleeImpactDispatch);
            g_originalMeleeImpactDispatch = nullptr;
            MH_DisableHook(buildRigidTransform);
            MH_RemoveHook(buildRigidTransform);
            g_originalBuildRigidTransform = nullptr;
            MH_DisableHook(meleeUpdateCollision);
            MH_RemoveHook(meleeUpdateCollision);
            g_originalMeleeUpdateCollision = nullptr;
            MH_DisableHook(meleeEnableCollisions);
            MH_RemoveHook(meleeEnableCollisions);
            g_originalMeleeEnableCollisions = nullptr;
            MH_DisableHook(fireVectors);
            MH_RemoveHook(fireVectors);
            g_originalGetFireVectors = nullptr;
            log("m5_aim_path_rejected", MH_StatusToString(status));
            return false;
        }
    }

    g_fireVectorsHookTarget = fireVectors;
    g_meleeEnableCollisionsHookTarget = aimPathProbe
        ? meleeEnableCollisions
        : nullptr;
    g_meleeUpdateCollisionHookTarget = aimPathProbe
        ? meleeUpdateCollision
        : nullptr;
    g_buildRigidTransformHookTarget = aimPathProbe
        ? buildRigidTransform
        : nullptr;
    g_meleeImpactDispatchHookTarget = aimPathProbe
        ? meleeImpactDispatch
        : nullptr;
    g_gameClientBase = reinterpret_cast<unsigned char*>(
        gameClientModule);
    g_log = log;
    InterlockedExchange(&g_mouseLookSuppressionLogged, 0);
    InterlockedExchange(&g_controllerFireAimLogged, 0);
    InterlockedExchange(&g_aimPathFireVectorCalls, 0);
    InterlockedExchange(&g_aimPathMeleeCalls, 0);
    InterlockedExchange(&g_aimPathMeleeUpdateCalls, 0);
    InterlockedExchange(&g_aimPathMeleeTransformCalls, 0);
    InterlockedExchange(&g_aimPathMeleeImpactCalls, 0);
    InterlockedExchange(&g_controllerMeleeAimLogged, 0);
    AcquireSRWLockExclusive(&g_physicalMeleeLock);
    g_physicalMeleeState = {};
    g_physicalMeleeSwingKinematicsState = {};
    g_physicalMeleeFrame = {};
    g_physicalMeleeProfile = {};
    g_physicalMeleeProfileWeaponIndex = -1;
    g_equippedWeaponIdentity = {};
    g_physicalMeleeContactState = {};
    g_physicalMeleeSwingAttackState = {};
    g_physicalMeleeSampleId = 0;
    g_physicalMeleeSampleTick = 0;
    g_physicalMeleeSwingSampleId = 0;
    g_physicalMeleeSwingSampleTick = 0;
    g_physicalMeleeSwingSpeedMetersPerSecond = 0.0F;
    ReleaseSRWLockExclusive(&g_physicalMeleeLock);
    InterlockedExchange(&g_physicalMeleeSampleCalls, 0);
    InterlockedExchange(&g_physicalMeleeDamageQualified, 0);
    InterlockedExchange(&g_physicalMeleeSwingAttackTriggered, 0);
    InterlockedExchange(&g_physicalMeleeWallProxyAppliedLogged, 0);
    InterlockedExchange(&g_physicalMeleeContactAccepted, 0);
    InterlockedExchange(&g_physicalMeleeContactRearmed, 0);
    InterlockedExchange(
        &g_physicalMeleeWallProxyEnabled,
        physicalMeleeWallProxy ? 1 : 0);
    InterlockedExchange(
        &g_physicalMeleeVisualProxyEnabled,
        physicalMeleeVisualProxy ? 1 : 0);
    SetPhysicalMeleeVisualProxyEnabled(
        physicalMeleeVisualProxy);
    SetWeaponGripCalibrationEnabled(
        weaponGripCalibration);
    SetTwoHandedMeleeEnabled(twoHandedMelee);
    InterlockedExchange(
        &g_physicalMeleeProbeEnabled,
        physicalMeleeProbe ? 1 : 0);
    InterlockedExchange(
        &g_controllerMeleeAimEnabled,
        controllerMeleeAim ? 1 : 0);
    InterlockedExchange(
        &g_aimPathProbeEnabled, aimPathProbe ? 1 : 0);
    InterlockedExchange(&g_headAimInputEnabled, 1);
    log(
        "m5_head_aim_armed",
        "mouse_commands=11,12 suppression=fresh_hmd_look_only "
        "fire_vectors=GameOrig+0x0002AF70 "
        "direction=right_controller_world_basis fire_position=retail "
        "stale_and_flat_fallback=retail");
    if (aimPathProbe) {
        log(
            "m5_aim_path_probe_armed",
            "behavior=observation_only command_edges=17,28,60,62 "
            "fire_vectors=GameOrig+0x0002AF70 "
            "melee_collision_enable=GameOrig+0x0001FD00 "
            "melee_collision_update=GameOrig+0x0001FC00 "
            "melee_transform_builder=GameOrig+0x0000F690 "
            "melee_impact_dispatch=GameOrig+0x0001F270 "
            "controller_forward_samples=1 gameorig_stack_rvas=1 "
            "event_cap_per_path=512");
    }
    if (controllerMeleeAim) {
        log(
            "m5_controller_melee_aim_armed",
            "target=GameOrig+0x0000F690 "
            "operation=controller_delta_about_camera_pivot "
            "retail_animation_curve_timing_damage_and_collision_rules=1 "
            "freshness_ms=250 stale_and_flat_fallback=retail");
    }
    if (physicalMeleeProbe) {
        log(
            "m5_physical_melee_probe_armed",
            "source=right_controller_weapon_pose "
            "position=openxr_grip rotation=openxr_aim "
            "swing_motion_space=openxr_tracking "
            "retail_locomotion_and_turning_excluded=1 "
            "profile=generic_one_handed_fallback "
            "catalog=pipe,crowbar,fire_axe,plank "
            "2x4_retail_indices=0,1,64,65 "
            "2x4_pose=WEAP_1HandedDebris "
            "pipe_lever_retail_index=32 pipe_lever_mass_kg=1.75 "
            "pipe_lever_handling_weight=1.75 "
            "pipe_lever_pose=WEAP_1HandedDebris "
            "fire_axe_retail_index=17 fire_axe_mass_kg=4.5 "
            "fire_axe_handling_weight=4.0 "
            "mapped_swing_attack=2x4_family,pipe_lever,fire_axe "
            "retail_fire_command=17 "
            "swing_trigger_mps=3.00 swing_rearm_mps=0.75 "
            "swing_pulse_ms=100 swing_cooldown_ms=450 "
            "length_m=0.75 radius_m=0.04 mass_kg=1.5 "
            "sweep=base_and_tip speed_gate_mps=1.25 "
            "energy_gate_j=1.0 native_impact_writes=0");
    }
    if (physicalMeleeWallProxy) {
        log(
            "m5_physical_melee_wall_proxy_armed",
            "target=GameOrig+0x0000F690 "
            "proxy_origin=controller_weapon_tip length_m=0.75 "
            "requires_retail_attack_window=1 collision_writes=1 "
            "contact_gate=normal_speed_and_energy "
            "duplicate_latch=normal_separation_0.12m "
            "native_impact_dispatch=blocked actor_damage=0 "
            "stale_and_background_fallback=retail_transform");
    }
    if (physicalMeleeVisualProxy) {
        log(
            "m5_physical_melee_visual_proxy_armed",
            "source=CClientWeaponMgr_current_weapon_model "
            "manager=GameOrig+0x00168EBC current_weapon_offset=0x0C "
            "model_LTObjRef_offset=0x1C acquisition=gameplay_update "
            "alignment=model_local_grip_to_openxr_right_grip "
            "rotation=openxr_right_aim profile_driven=1 "
            "heavy_profiles=bounded_damped_spring_visible_inertia "
            "attack_required=0 weapon_switch_auto_release=1 "
            "render_override_only=1 exact_transform_restore=1 "
            "placeholder_model=0 native_impact_dispatch=blocked");
    }
    if (weaponGripCalibration) {
        log(
            "m5_weapon_grip_calibration_armed",
            "live_render_update=1 session_cache=per_weapon_index_pointer_model "
            "start_mode=position start_active=1 "
            "controller_capture=both_squeezes "
            "controller_axes=right_stick_xy,left_stick_y_z "
            "controller_buttons=a_position,b_rotation,x_reset,y_snapshot,"
            "left_stick_finer,right_stick_coarser "
            "visual_reference=generic_controller_wireframe "
            "visual_grip_pose=openxr_right_grip "
            "visual_aim_ray=openxr_right_aim "
            "gameplay_input_suppressed_during_capture=1 "
            "keyboard=j_l_x,k_i_y,u_o_z,t_mode,comma_period_step,"
            "r_reset,p_snapshot,f11_pause "
            "position_units=lithtech rotation_axes=model_local_xyz "
            "foreground_only=1 retail_transform_restore=exact");
    }
    if (twoHandedMelee) {
        log(
            "m5_two_handed_melee_armed",
            "dominant_hand=right support_hand=left "
            "attach=left_squeeze_near_profile_handle "
            "attach_threshold=0.65 release_threshold=0.35 "
            "remote_snap_grab=0 authored_weapon_scaling=0 "
            "solver=dominant_anchor_shortest_arc_twist_preserving "
            "weight_filter=existing_bounded_damped_spring "
            "release_momentum_reset=0 tracking_loss_fail_closed=1 "
            "conflicting_left_squeeze_run_action=captured "
            "supported_profile=fire_axe retail_index=17");
    }
    return true;
}

bool InstallMenuToggleHook(
    void* masterDatabase,
    HMODULE gameClientModule,
    HMODULE bridgeModule,
    RendererProbeLogFunction log,
    bool menuControls) noexcept {
    AcquireSRWLockExclusive(&g_bindingLock);
    if (g_menuHookTarget != nullptr) {
        InterlockedExchange(
            &g_menuControlsEnabled, menuControls ? 1 : 0);
        RequireMenuNavigationRelease(g_menuNavigationState);
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
    g_menuNavigationState = {};
    InterlockedExchange(
        &g_menuControlsEnabled, menuControls ? 1 : 0);
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
        InterlockedExchange(&g_menuControlsEnabled, 0);
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
    if (menuControls) {
        log(
            "m6_menu_controls_armed",
            "states=menu,screen left_stick=arrow_keys "
            "right_primary_or_trigger=enter right_secondary=escape "
            "initial_repeat_ms=350 repeat_ms=110 "
            "neutral_on_entry=1 both_hands_required=1 "
            "path=IClientShell_v4_key_edges mouse_keyboard_unchanged=1 "
            "direct_command_writes=0 system_input=0");
    }
    PublishMenuRenderState();
    return true;
}

void ReadPhysicalMeleeToolTelemetry(
    ToolMenuMeleeTelemetry& telemetry) noexcept {
    telemetry = {};
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_physicalMeleeLock);
    telemetry.weaponIndex = g_physicalMeleeProfileWeaponIndex;
    std::memcpy(
        telemetry.weaponName,
        g_equippedWeaponIdentity.recordName,
        sizeof(telemetry.weaponName));
    std::memcpy(
        telemetry.weaponAnimationProperty,
        g_equippedWeaponIdentity.animationProperty,
        sizeof(telemetry.weaponAnimationProperty));
    telemetry.weaponPoseFamily =
        g_equippedWeaponIdentity.poseFamily;
    telemetry.weaponNameResolved =
        g_equippedWeaponIdentity.nameResolved;
    telemetry.weaponAnimationPropertyResolved =
        g_equippedWeaponIdentity.animationPropertyResolved;
    telemetry.trackingFresh =
        g_physicalMeleeSwingSampleId != 0 &&
        g_physicalMeleeSwingSampleTick != 0 &&
        now - g_physicalMeleeSwingSampleTick <=
            kInputFreshnessMilliseconds;
    telemetry.swingSpeedMetersPerSecond = telemetry.trackingFresh
        ? g_physicalMeleeSwingSpeedMetersPerSecond
        : 0.0F;
    ReleaseSRWLockShared(&g_physicalMeleeLock);
    telemetry.triggerCount = static_cast<std::uint32_t>(
        std::max<LONG>(
            0, InterlockedCompareExchange(
                   &g_physicalMeleeSwingAttackTriggered, 0, 0)));
    telemetry.wallProxyEnabled = InterlockedCompareExchange(
        &g_physicalMeleeWallProxyEnabled, 0, 0) != 0;
    telemetry.visualProxyEnabled = InterlockedCompareExchange(
        &g_physicalMeleeVisualProxyEnabled, 0, 0) != 0;
    ReadPhysicalMeleeTwoHandTelemetry(telemetry);
}

} // namespace condemnedvr
