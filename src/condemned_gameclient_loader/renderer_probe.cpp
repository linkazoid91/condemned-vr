#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmsystem.h>

#include "renderer_probe.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include <MinHook.h>

#include "arm_ik_discovery.h"
#include "arm_ik_integration.h"
#include "binding_input.h"
#include "condemned_calibration_gizmo.h"
#include "condemned_interaction_authoring.h"
#include "condemned_physical_melee_collider_gizmo.h"
#include "condemned_controller_input.h"
#include "condemned_physical_melee.h"
#include "condemned_right_hand_ik.h"
#include "condemned_slide_grab.h"
#include "head_tracking_math.h"
#include "protocol.h"
#include "stereo_math.h"
#include "weapon_weight.h"
#include "weapon_settings_store.h"

namespace condemnedvr {
namespace {

static_assert(sizeof(void*) == 4, "The renderer probe is x86-only.");

using RenderCameraFunction = unsigned long(__thiscall*)(void*, void*);
using RenderCameraOverrideFunction =
    unsigned long(__thiscall*)(void*, void*, const char*);
using SubmitStereoDiagnosticFunction = BOOL(__cdecl*)();
using GetRenderRequestFunction = BOOL(__cdecl*)(FearVrRenderRequest*);
using WaitForNewRenderRequestFunction = BOOL(__cdecl*)(
    std::uint64_t, std::uint32_t, FearVrRenderRequest*);
using BeginEyeFunction = void(__cdecl*)(std::uint32_t);
using ClearEyeFunction = BOOL(__cdecl*)(std::uint32_t);
using CaptureEyeFunction = void(__cdecl*)(std::uint32_t);
using EndStereoDiagnosticFrameFunction = BOOL(__cdecl*)(std::uint64_t);
using EndStereoFrameFunction = void(__cdecl*)(std::uint64_t);
using SetFovScalePercentFunction = void(__cdecl*)(std::uint32_t);
using GetInputStateFunction = BOOL(__cdecl*)(FearVrInputState*);
using DrawOverlayLinesFunction = BOOL(__cdecl*)(
    const FearVrOverlayLineVertex*, std::uint32_t);
using DrawOverlayTrianglesFunction = BOOL(__cdecl*)(
    const FearVrOverlayLineVertex*, std::uint32_t);
struct RigidTransformAbi {
    float position[3];
    float rotation[4];
};
struct PhysicalMeleeVisualOverride {
    void* object{nullptr};
    RigidTransformAbi original{};
    bool active{false};
};
using GetRigidTransformFunction =
    unsigned long(__thiscall*)(void*, void*, RigidTransformAbi*);
using SetRigidTransformFunction =
    unsigned long(__thiscall*)(void*, void*, const RigidTransformAbi*);
using GetCameraFovFunction = void(__cdecl*)(void*, float*, float*);
using SetCameraFovFunction = void(__cdecl*)(void*, float, float);

static_assert(sizeof(RigidTransformAbi) == 28);

constexpr std::uintptr_t kRenderCameraForwarderRva = 0x00102650U;
constexpr std::uintptr_t kRenderCameraOverrideRva = 0x00103D20U;
constexpr std::size_t kRenderCameraSlot = 18;
constexpr std::size_t kRenderCameraOverrideSlot = 20;
constexpr unsigned char kRenderCameraForwarder[] = {
    0x8B, 0x54, 0x24, 0x04, 0x8B, 0x01, 0x6A, 0x00,
    0x52, 0xFF, 0x50, 0x50, 0xC2, 0x04, 0x00};
constexpr unsigned char kRenderCameraOverridePrefix[] = {
    0x8B, 0x44, 0x24, 0x08, 0x8B, 0x54, 0x24, 0x04,
    0x50, 0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00, 0x52};
constexpr std::size_t kGetRigidTransformSlot = 21;
constexpr std::uintptr_t kGetRigidTransformRva = 0x0000C750U;
constexpr unsigned char kGetRigidTransformPrefix[] = {
    0x8B, 0x4C, 0x24, 0x04, 0x83, 0xEC, 0x1C, 0x85,
    0xC9, 0x74, 0x63, 0x8B, 0x44, 0x24, 0x24, 0x85};
constexpr std::size_t kSetRigidTransformSlot = 23;
constexpr std::uintptr_t kSetRigidTransformRva = 0x0000B980U;
constexpr unsigned char kSetRigidTransformPrefix[] = {
    0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6, 0x75,
    0x3C, 0x6A, 0x3C, 0xE8, 0x10, 0x2F, 0x07, 0x00};
constexpr std::size_t kGetCameraFovMemberOffset = 0x5CU;
constexpr std::uintptr_t kGetCameraFovRva = 0x0000C660U;
constexpr unsigned char kGetCameraFovPrefix[] = {
    0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0, 0x74, 0x28,
    0x80, 0x78, 0x20, 0x04, 0x75, 0x22, 0x8B, 0x54};
constexpr std::size_t kSetCameraFovMemberOffset = 0x60U;
constexpr std::uintptr_t kSetCameraFovRva = 0x0000D700U;
constexpr unsigned char kSetCameraFovPrefix[] = {
    0x8B, 0x4C, 0x24, 0x04, 0x85, 0xC9, 0x0F, 0x84,
    0x91, 0x00, 0x00, 0x00, 0x80, 0x79, 0x20, 0x04};
constexpr float kCondemnedDefaultFovScale = 1.30F;
constexpr float kMinimumFovScale =
    static_cast<float>(FEARVR_FOV_SCALE_MIN_PERCENT) / 100.0F;
constexpr float kMaximumFovScale =
    static_cast<float>(FEARVR_FOV_SCALE_MAX_PERCENT) / 100.0F;

SRWLOCK g_passThroughLock = SRWLOCK_INIT;
RenderCameraFunction g_originalRenderCamera = nullptr;
RenderCameraOverrideFunction g_renderCameraOverride = nullptr;
RendererProbeLogFunction g_passThroughLog = nullptr;
volatile LONG g_renderCameraCalls = 0;
SubmitStereoDiagnosticFunction g_submitStereoDiagnostic = nullptr;
volatile LONG g_stereoDiagnosticState = 0;
bool g_doubleRenderDiagnostic = false;
GetRenderRequestFunction g_getRenderRequest = nullptr;
WaitForNewRenderRequestFunction g_waitForNewRenderRequest = nullptr;
BeginEyeFunction g_beginEye = nullptr;
ClearEyeFunction g_clearEye = nullptr;
CaptureEyeFunction g_captureEye = nullptr;
EndStereoDiagnosticFrameFunction g_endStereoDiagnosticFrame = nullptr;
EndStereoFrameFunction g_endStereoFrame = nullptr;
SetFovScalePercentFunction g_setFovScalePercent = nullptr;
GetInputStateFunction g_getInputState = nullptr;
DrawOverlayLinesFunction g_drawOverlayLines = nullptr;
DrawOverlayTrianglesFunction g_drawOverlayTriangles = nullptr;
void* g_client = nullptr;
GetRigidTransformFunction g_getRigidTransform = nullptr;
GetRigidTransformFunction g_originalHeadAimGetRigidTransform = nullptr;
void* g_headAimGetRigidTransformTarget = nullptr;
SetRigidTransformFunction g_setRigidTransform = nullptr;
GetCameraFovFunction g_getCameraFov = nullptr;
SetCameraFovFunction g_setCameraFov = nullptr;
bool g_cameraReadProbe = false;
bool g_eyeOffsetDiagnostic = false;
bool g_reverseEyeOffsetDiagnostic = false;
bool g_zeroEyeOffsetDiagnostic = false;
bool g_continuousStereoTuning = false;
bool g_continuousStereoEnabled = true;
bool g_tuningReversePolarity = false;
float g_tuningUnitsPerMeter = 100.0F;
float g_tuningFovScale = kCondemnedDefaultFovScale;
bool g_hmdTranslationEnabled = true;
bool g_controllerRecenterEnabled = false;
bool g_controllerRecenterRequested = false;
bool g_headAimEnabled = false;
bool g_trackingRecenterPending = true;
bool g_trackingRecenterValid = false;
FearVrPose g_trackingRecenter{};
RecenterLatch g_controllerRecenterLatch;
volatile LONG g_continuousRenderActive = 0;
std::uint64_t g_lastStereoRenderRequestId = 0;
volatile LONG g_continuousStereoLogged = 0;
volatile LONG g_cameraReadFailures = 0;
std::uint64_t g_lastInputSampleId = 0;
ULONGLONG g_lastInputSampleTick = 0;
std::uint64_t g_inputSamplesObserved = 0;
std::uint32_t g_lastInputButtons = 0;
std::uint32_t g_lastInputHands = 0;
std::uint32_t g_lastInputFlags = 0;
std::uint32_t g_lastInputPoseMasks = 0;
SRWLOCK g_headAimLock = SRWLOCK_INIT;
void* g_headAimCamera = nullptr;
float g_headAimRotation[4]{};
float g_headAimPosition[3]{};
float g_headAimBaseRotation[4]{};
ULONGLONG g_headAimTick = 0;
float g_controllerAimPosition[3]{};
float g_controllerAimRotation[4]{};
std::uint64_t g_controllerAimSampleId = 0;
std::uint64_t g_controllerAimTimestampNs = 0;
ULONGLONG g_controllerAimTick = 0;
float g_controllerGripPosition[3]{};
float g_controllerGripRotation[4]{};
std::uint64_t g_controllerGripSampleId = 0;
std::uint64_t g_controllerGripTimestampNs = 0;
ULONGLONG g_controllerGripTick = 0;
float g_controllerWeaponPosition[3]{};
float g_controllerWeaponRotation[4]{};
std::uint64_t g_controllerWeaponSampleId = 0;
std::uint64_t g_controllerWeaponTimestampNs = 0;
ULONGLONG g_controllerWeaponTick = 0;
volatile LONG g_headAimCameraReadLogged = 0;
volatile LONG g_controllerAimPublishedLogged = 0;
volatile LONG g_controllerWeaponPublishedLogged = 0;
SRWLOCK g_physicalMeleeVisualLock = SRWLOCK_INIT;
volatile LONG g_physicalMeleeVisualEnabled = 0;
void* const* g_physicalMeleeVisualWeaponReference = nullptr;
void* g_physicalMeleeVisualWeapon = nullptr;
std::int32_t g_physicalMeleeVisualWeaponIndex = -1;
void* const* g_physicalMeleeVisualModelReference = nullptr;
void* g_physicalMeleeVisualModel = nullptr;
fearvr::TrackingVector g_physicalMeleeVisualModelLocalGripPosition{};
fearvr::TrackingQuaternion g_physicalMeleeVisualModelLocalGripRotation{
    0.0F, 0.0F, 0.0F, 1.0F};
fearvr::TrackingVector g_physicalMeleeSecondaryGripOffsetUnits{};
float g_physicalMeleeSecondaryGripGrabRadiusMeters = 0.15F;
bool g_physicalMeleeSecondaryGripProfileEnabled = false;
std::uint64_t g_physicalMeleeVisualSourceGeneration = 0;
RightHandIkTargetSource g_rightHandIkTargetSource =
    RightHandIkTargetSource::Invalid;
std::int32_t g_rightHandIkTargetWeaponIndex = -1;
std::uint64_t g_rightHandIkTargetSourceGeneration = 0;
std::uint64_t g_rightHandIkTargetLastLoggedSampleId = 0;
ULONGLONG g_rightHandIkTargetLastLogTick = 0;
std::uint32_t g_rightHandIkTargetLogCount = 0;
EmptyRightHandAlignmentSettings g_emptyRightHandAlignmentSettings{};
EmptyRightHandAlignmentState g_emptyRightHandAlignmentState{};
EmptyRightHandAlignmentEvent g_emptyRightHandAlignmentLastEvent =
    EmptyRightHandAlignmentEvent::None;
WeaponSettingsStoreResult g_emptyRightHandAlignmentLastSaveResult =
    WeaponSettingsStoreResult::NotFound;
HeldObjectAlignmentState g_heldObjectAlignmentState{};
HeldObjectAlignmentEvent g_heldObjectAlignmentLastEvent =
    HeldObjectAlignmentEvent::None;
WeaponSettingsStoreResult g_heldObjectAlignmentLastGripSaveResult =
    WeaponSettingsStoreResult::NotFound;
WeaponSettingsStoreResult g_heldObjectAlignmentLastHandSaveResult =
    WeaponSettingsStoreResult::NotFound;
WeaponSettingsStoreResult g_heldObjectAlignmentLastColliderSaveResult =
    WeaponSettingsStoreResult::NotFound;
enum class HeldAssemblyControllerAlignmentEvent : std::uint8_t {
    None,
    Applied,
    SourceUnavailable,
    PoseUnavailable,
    SolveRejected,
    ApplyRejected
};
HeldAssemblyControllerAlignmentEvent
    g_heldAssemblyControllerAlignmentLastEvent =
        HeldAssemblyControllerAlignmentEvent::None;
WeaponSettingsStoreResult
    g_heldAssemblyControllerAlignmentLastGripSaveResult =
        WeaponSettingsStoreResult::NotFound;
WeaponSettingsStoreResult
    g_heldAssemblyControllerAlignmentLastHandSaveResult =
        WeaponSettingsStoreResult::NotFound;
WeaponSettingsStoreResult
    g_heldAssemblyControllerAlignmentLastColliderSaveResult =
        WeaponSettingsStoreResult::NotFound;
volatile LONG g_physicalMeleeVisualActiveLogged = 0;
volatile LONG g_physicalMeleeVisualRestoreFailed = 0;
volatile LONG g_weaponGripControllerGizmoActiveLogged = 0;
volatile LONG g_weaponGripControllerGizmoFailureLogged = 0;
volatile LONG g_weaponGripControllerDebugDrawVisible = 0;
volatile LONG g_physicalMeleeColliderPreviewLogged = 0;
volatile LONG g_physicalMeleeColliderLiveLogged = 0;
volatile LONG g_physicalMeleeColliderFailureLogged = 0;
volatile LONG g_physicalMeleeColliderDebugDrawVisible = 0;
volatile LONG g_physicalMeleeBlockColliderPreviewLogged = 0;
volatile LONG g_physicalMeleeBlockColliderLiveLogged = 0;
volatile LONG g_physicalMeleeBlockColliderFailureLogged = 0;
volatile LONG g_physicalMeleeBlockColliderDebugDrawVisible = 0;
volatile LONG g_twoHandedMeleeEnabled = 0;
volatile LONG g_physicalMeleeSecondaryGripAttached = 0;
SRWLOCK g_physicalMeleeSecondaryGripTelemetryLock = SRWLOCK_INIT;
PhysicalMeleeSecondaryGripState g_physicalMeleeSecondaryGripState{};
std::int32_t g_physicalMeleeSecondaryGripWeaponIndex = -1;
std::uint64_t g_physicalMeleeSecondaryGripSourceGeneration = 0;
float g_physicalMeleeSecondaryGripDistanceMeters = 0.0F;
float g_physicalMeleeSecondaryGripAnchorErrorMeters = 0.0F;

enum class WeaponGripCalibrationMode : std::uint8_t {
    Position,
    Rotation
};

struct WeaponGripCalibrationSlot {
    void* weapon{nullptr};
    void* modelObject{nullptr};
    std::int32_t weaponIndex{-1};
    PhysicalMeleeGripCalibration calibration{};
    std::uint64_t lastUsed{0};
    bool occupied{false};
};

constexpr std::size_t kWeaponGripCalibrationSlotCount = 16;
WeaponGripCalibrationSlot
    g_weaponGripCalibrationSlots[kWeaponGripCalibrationSlotCount]{};
std::int32_t g_activeWeaponGripCalibrationSlot = -1;
std::uint64_t g_weaponGripCalibrationUseSequence = 0;
volatile LONG g_weaponGripCalibrationEnabled = 0;
volatile LONG g_weaponGripCalibrationActive = 0;
WeaponGripCalibrationMode g_weaponGripCalibrationMode =
    WeaponGripCalibrationMode::Position;
std::size_t g_weaponGripCalibrationStepIndex = 2;
fearvr::WeaponWeightFilterState g_physicalMeleeWeaponWeightFilter{};
PhysicalMeleeSupportHandOrientationState
    g_physicalMeleeSupportHandOrientation{};
std::int32_t g_physicalMeleeWeightedWeaponIndex = -1;
std::uint64_t g_physicalMeleeWeightedSourceGeneration = 0;
std::uint64_t g_physicalMeleeWeightedSampleId = 0;
fearvr::WeaponWeightPose g_physicalMeleeWeightedLocalPose{
    {}, {0.0F, 0.0F, 0.0F, 1.0F}};
bool g_physicalMeleeWeightedPoseValid = false;
volatile LONG g_physicalMeleeWeightActiveLogged = 0;
SRWLOCK g_toolMenuSettingsLock = SRWLOCK_INIT;
ToolMenuWeaponSettingsRegistry g_toolMenuWeaponSettingsRegistry{};
constexpr wchar_t kLiveColliderCommandPathEnvironment[] =
    L"CONDEMNEDVR_LIVE_COLLIDER_COMMAND_PATH";
constexpr ULONGLONG kLiveColliderCommandPollMilliseconds = 100U;
SRWLOCK g_liveColliderCommandLock = SRWLOCK_INIT;
wchar_t g_liveColliderCommandPath[MAX_PATH]{};
bool g_liveColliderCommandPathResolved = false;
bool g_liveColliderCommandPathAvailable = false;
FILETIME g_liveColliderCommandLastWriteTime{};
ULONGLONG g_liveColliderCommandLastPollTick = 0U;
std::uint64_t g_liveColliderCommandLastRevision = 0U;
std::int32_t g_liveColliderCommandArmedWeaponIndex = -1;

struct MagazineSocketAuthoringRuntimeState {
    MagazineSocketEditorState editor{};
    PhysicalMeleeRigidTransform modelWorld{};
    PhysicalMeleeRigidTransform cursorModelLocal{};
    MagazineSocketSnapPreview preview{};
    WeaponSettingsStoreResult lastLoadResult{
        WeaponSettingsStoreResult::NotFound};
    WeaponSettingsStoreResult lastSaveResult{
        WeaponSettingsStoreResult::NotFound};
    std::int32_t weaponIndex{-1};
    std::uint64_t sourceGeneration{0U};
    std::uint64_t revision{0U};
    ULONGLONG visualTick{0U};
    char weaponName[kMagazineSocketWeaponNameCapacity]{};
    bool identityReady{false};
    bool visualReady{false};
};
SRWLOCK g_magazineSocketAuthoringLock = SRWLOCK_INIT;
MagazineSocketAuthoringRuntimeState
    g_magazineSocketAuthoringState{};
std::uint64_t g_liveMagazineSocketCommandLastRevision = 0U;
volatile LONG g_magazineSocketAuthoringGizmoLogged = 0;
volatile LONG g_magazineSocketAuthoringGizmoFailureLogged = 0;

InteractionAuthoringPrimitive g_authoringPrimitive =
    InteractionAuthoringPrimitive::MagazineInsertSocket;

struct SlideGrabAuthoringRuntimeState {
    SlideGrabEditorState editor{};
    PhysicalMeleeRigidTransform modelWorld{};
    PhysicalMeleeRigidTransform cursorModelLocal{};
    WeaponSettingsStoreResult lastLoadResult{
        WeaponSettingsStoreResult::NotFound};
    WeaponSettingsStoreResult lastSaveResult{
        WeaponSettingsStoreResult::NotFound};
    std::int32_t weaponIndex{-1};
    std::uint64_t sourceGeneration{0U};
    ULONGLONG visualTick{0U};
    char weaponName[kMagazineSocketWeaponNameCapacity]{};
    bool identityReady{false};
    bool visualReady{false};
    bool invalidRecord{false};
};
SRWLOCK g_slideGrabAuthoringLock = SRWLOCK_INIT;
SlideGrabAuthoringRuntimeState g_slideGrabAuthoringState{};
volatile LONG g_slideGrabAuthoringGizmoLogged = 0;
volatile LONG g_slideGrabAuthoringGizmoFailureLogged = 0;
SRWLOCK g_slideGrabRuntimeLock = SRWLOCK_INIT;
SlideGrabStateMachine g_slideGrabStateMachine{};
SlideGrabFrameResult g_slideGrabLastFrame{};
SlideGrabSoundCueState g_slideGrabSoundCueState{};
volatile LONG g_slideGrabCaptureGrip = 0;
volatile LONG g_slideGrabCaptureTrigger = 0;

struct LiveEquippedWeaponVisualSource {
    std::int32_t weaponIndex{-1};
    std::uint64_t sourceGeneration{0};
    void* modelObject{nullptr};
    bool live{false};
};

std::size_t g_rightHandIkCalibrationStepIndex = 2U;
ToolMenuState g_toolMenuState{};
ToolMenuOverlay g_toolMenuOverlay{};
ToolMenuPanelPlacement g_toolMenuPanelPlacement{};
volatile LONG g_toolMenuEnabled = 0;
volatile LONG g_toolMenuShortcutEnabled = 0;
volatile LONG g_toolMenuShortcutSettingsReady = 0;
volatile LONG g_toolMenuOpen = 0;
volatile LONG g_toolMenuReleaseCapture = 0;
volatile LONG g_toolMenuOverlayFailureLogged = 0;

constexpr ULONGLONG kHeadAimFreshnessMilliseconds = 250;

void ResetPhysicalMeleeWeaponWeight(
    fearvr::WeaponWeightResetReason reason) noexcept {
    fearvr::ClearWeaponWeightFilter(
        g_physicalMeleeWeaponWeightFilter, reason);
    g_physicalMeleeWeightedWeaponIndex = -1;
    g_physicalMeleeWeightedSourceGeneration = 0;
    g_physicalMeleeWeightedSampleId = 0;
    g_physicalMeleeWeightedLocalPose = {};
    g_physicalMeleeWeightedPoseValid = false;
    g_physicalMeleeSupportHandOrientation = {};
}

const char* PhysicalMeleeSecondaryGripReleaseReasonName(
    PhysicalMeleeSecondaryGripReleaseReason reason) noexcept {
    switch (reason) {
    case PhysicalMeleeSecondaryGripReleaseReason::None:
        return "none";
    case PhysicalMeleeSecondaryGripReleaseReason::Released:
        return "released";
    case PhysicalMeleeSecondaryGripReleaseReason::TrackingLost:
        return "tracking_lost";
    case PhysicalMeleeSecondaryGripReleaseReason::ContextDisabled:
        return "context_disabled";
    case PhysicalMeleeSecondaryGripReleaseReason::Unsupported:
        return "unsupported";
    case PhysicalMeleeSecondaryGripReleaseReason::ExcessiveStretch:
        return "excessive_stretch";
    case PhysicalMeleeSecondaryGripReleaseReason::InvalidPose:
        return "invalid_pose";
    default:
        return "invalid";
    }
}

void ResetPhysicalMeleeSecondaryGrip(bool requireRelease) noexcept {
    g_physicalMeleeSecondaryGripState = {};
    g_physicalMeleeSecondaryGripState.attachmentArmed =
        !requireRelease;
    g_physicalMeleeSecondaryGripWeaponIndex = -1;
    g_physicalMeleeSecondaryGripSourceGeneration = 0;
    AcquireSRWLockExclusive(
        &g_physicalMeleeSecondaryGripTelemetryLock);
    g_physicalMeleeSecondaryGripDistanceMeters = 0.0F;
    g_physicalMeleeSecondaryGripAnchorErrorMeters = 0.0F;
    ReleaseSRWLockExclusive(
        &g_physicalMeleeSecondaryGripTelemetryLock);
    InterlockedExchange(&g_physicalMeleeSecondaryGripAttached, 0);
}

void InvalidateTrackedHeadAim() noexcept {
    AcquireSRWLockExclusive(&g_headAimLock);
    g_headAimCamera = nullptr;
    g_headAimRotation[0] = 0.0F;
    g_headAimRotation[1] = 0.0F;
    g_headAimRotation[2] = 0.0F;
    g_headAimRotation[3] = 0.0F;
    g_headAimPosition[0] = 0.0F;
    g_headAimPosition[1] = 0.0F;
    g_headAimPosition[2] = 0.0F;
    g_headAimBaseRotation[0] = 0.0F;
    g_headAimBaseRotation[1] = 0.0F;
    g_headAimBaseRotation[2] = 0.0F;
    g_headAimBaseRotation[3] = 0.0F;
    g_headAimTick = 0;
    g_controllerAimPosition[0] = 0.0F;
    g_controllerAimPosition[1] = 0.0F;
    g_controllerAimPosition[2] = 0.0F;
    g_controllerAimRotation[0] = 0.0F;
    g_controllerAimRotation[1] = 0.0F;
    g_controllerAimRotation[2] = 0.0F;
    g_controllerAimRotation[3] = 0.0F;
    g_controllerAimSampleId = 0;
    g_controllerAimTimestampNs = 0;
    g_controllerAimTick = 0;
    g_controllerGripPosition[0] = 0.0F;
    g_controllerGripPosition[1] = 0.0F;
    g_controllerGripPosition[2] = 0.0F;
    g_controllerGripRotation[0] = 0.0F;
    g_controllerGripRotation[1] = 0.0F;
    g_controllerGripRotation[2] = 0.0F;
    g_controllerGripRotation[3] = 0.0F;
    g_controllerGripSampleId = 0;
    g_controllerGripTimestampNs = 0;
    g_controllerGripTick = 0;
    g_controllerWeaponPosition[0] = 0.0F;
    g_controllerWeaponPosition[1] = 0.0F;
    g_controllerWeaponPosition[2] = 0.0F;
    g_controllerWeaponRotation[0] = 0.0F;
    g_controllerWeaponRotation[1] = 0.0F;
    g_controllerWeaponRotation[2] = 0.0F;
    g_controllerWeaponRotation[3] = 0.0F;
    g_controllerWeaponSampleId = 0;
    g_controllerWeaponTimestampNs = 0;
    g_controllerWeaponTick = 0;
    ReleaseSRWLockExclusive(&g_headAimLock);
    ResetPhysicalMeleeWeaponWeight(
        fearvr::WeaponWeightResetReason::trackingLost);
    ResetPhysicalMeleeSecondaryGrip(true);
}

void InvalidateTrackedControllerAim() noexcept {
    AcquireSRWLockExclusive(&g_headAimLock);
    g_controllerAimPosition[0] = 0.0F;
    g_controllerAimPosition[1] = 0.0F;
    g_controllerAimPosition[2] = 0.0F;
    g_controllerAimRotation[0] = 0.0F;
    g_controllerAimRotation[1] = 0.0F;
    g_controllerAimRotation[2] = 0.0F;
    g_controllerAimRotation[3] = 0.0F;
    g_controllerAimSampleId = 0;
    g_controllerAimTimestampNs = 0;
    g_controllerAimTick = 0;
    g_controllerGripPosition[0] = 0.0F;
    g_controllerGripPosition[1] = 0.0F;
    g_controllerGripPosition[2] = 0.0F;
    g_controllerGripRotation[0] = 0.0F;
    g_controllerGripRotation[1] = 0.0F;
    g_controllerGripRotation[2] = 0.0F;
    g_controllerGripRotation[3] = 0.0F;
    g_controllerGripSampleId = 0;
    g_controllerGripTimestampNs = 0;
    g_controllerGripTick = 0;
    g_controllerWeaponPosition[0] = 0.0F;
    g_controllerWeaponPosition[1] = 0.0F;
    g_controllerWeaponPosition[2] = 0.0F;
    g_controllerWeaponRotation[0] = 0.0F;
    g_controllerWeaponRotation[1] = 0.0F;
    g_controllerWeaponRotation[2] = 0.0F;
    g_controllerWeaponRotation[3] = 0.0F;
    g_controllerWeaponSampleId = 0;
    g_controllerWeaponTimestampNs = 0;
    g_controllerWeaponTick = 0;
    ReleaseSRWLockExclusive(&g_headAimLock);
    ResetPhysicalMeleeWeaponWeight(
        fearvr::WeaponWeightResetReason::trackingLost);
    ResetPhysicalMeleeSecondaryGrip(true);
}

void InvalidateTrackedControllerWeaponPose() noexcept {
    AcquireSRWLockExclusive(&g_headAimLock);
    g_controllerWeaponPosition[0] = 0.0F;
    g_controllerWeaponPosition[1] = 0.0F;
    g_controllerWeaponPosition[2] = 0.0F;
    g_controllerWeaponRotation[0] = 0.0F;
    g_controllerWeaponRotation[1] = 0.0F;
    g_controllerWeaponRotation[2] = 0.0F;
    g_controllerWeaponRotation[3] = 0.0F;
    g_controllerWeaponSampleId = 0;
    g_controllerWeaponTimestampNs = 0;
    g_controllerWeaponTick = 0;
    ReleaseSRWLockExclusive(&g_headAimLock);
    ResetPhysicalMeleeWeaponWeight(
        fearvr::WeaponWeightResetReason::trackingLost);
    ResetPhysicalMeleeSecondaryGrip(true);
}

void InvalidateTrackedControllerGripPose() noexcept {
    AcquireSRWLockExclusive(&g_headAimLock);
    g_controllerGripPosition[0] = 0.0F;
    g_controllerGripPosition[1] = 0.0F;
    g_controllerGripPosition[2] = 0.0F;
    g_controllerGripRotation[0] = 0.0F;
    g_controllerGripRotation[1] = 0.0F;
    g_controllerGripRotation[2] = 0.0F;
    g_controllerGripRotation[3] = 0.0F;
    g_controllerGripSampleId = 0;
    g_controllerGripTimestampNs = 0;
    g_controllerGripTick = 0;
    ReleaseSRWLockExclusive(&g_headAimLock);
}

void PublishTrackedHeadAim(
    void* camera,
    const fearvr::TrackingVector& position,
    const fearvr::TrackingQuaternion& baseRotation,
    const fearvr::TrackingQuaternion& rotation) noexcept {
    if (!g_headAimEnabled || camera == nullptr ||
        !fearvr::IsFinite(position) ||
        !fearvr::IsFinite(baseRotation) ||
        !fearvr::IsFinite(rotation)) {
        InvalidateTrackedHeadAim();
        return;
    }
    AcquireSRWLockExclusive(&g_headAimLock);
    g_headAimCamera = camera;
    g_headAimRotation[0] = rotation.x;
    g_headAimRotation[1] = rotation.y;
    g_headAimRotation[2] = rotation.z;
    g_headAimRotation[3] = rotation.w;
    g_headAimPosition[0] = position.x;
    g_headAimPosition[1] = position.y;
    g_headAimPosition[2] = position.z;
    g_headAimBaseRotation[0] = baseRotation.x;
    g_headAimBaseRotation[1] = baseRotation.y;
    g_headAimBaseRotation[2] = baseRotation.z;
    g_headAimBaseRotation[3] = baseRotation.w;
    g_headAimTick = GetTickCount64();
    ReleaseSRWLockExclusive(&g_headAimLock);
}

void PublishTrackedControllerAim(
    const fearvr::TrackingVector& position,
    const fearvr::TrackingQuaternion& rotation,
    std::uint64_t sampleId,
    std::uint64_t timestampNs) noexcept {
    if (!g_headAimEnabled || !fearvr::IsFinite(position) ||
        !fearvr::IsFinite(rotation) || sampleId == 0 ||
        timestampNs == 0) {
        InvalidateTrackedControllerAim();
        return;
    }
    AcquireSRWLockExclusive(&g_headAimLock);
    g_controllerAimPosition[0] = position.x;
    g_controllerAimPosition[1] = position.y;
    g_controllerAimPosition[2] = position.z;
    g_controllerAimRotation[0] = rotation.x;
    g_controllerAimRotation[1] = rotation.y;
    g_controllerAimRotation[2] = rotation.z;
    g_controllerAimRotation[3] = rotation.w;
    g_controllerAimSampleId = sampleId;
    g_controllerAimTimestampNs = timestampNs;
    g_controllerAimTick = GetTickCount64();
    ReleaseSRWLockExclusive(&g_headAimLock);
    if (InterlockedCompareExchange(
            &g_controllerAimPublishedLogged, 1, 0) == 0 &&
        g_passThroughLog != nullptr) {
        g_passThroughLog(
            "m5_controller_aim_active",
            "source=right_aim_pose pose=position_and_rotation "
            "basis=lithtech_world "
            "openxr_minus_z_to_engine_plus_z=1 freshness_ms=250");
    }
}

void PublishTrackedControllerGripPose(
    const fearvr::TrackingVector& position,
    const fearvr::TrackingQuaternion& rotation,
    std::uint64_t sampleId,
    std::uint64_t timestampNs) noexcept {
    if (!g_headAimEnabled || !fearvr::IsFinite(position) ||
        !fearvr::IsFinite(rotation) || sampleId == 0 ||
        timestampNs == 0) {
        InvalidateTrackedControllerGripPose();
        return;
    }
    AcquireSRWLockExclusive(&g_headAimLock);
    g_controllerGripPosition[0] = position.x;
    g_controllerGripPosition[1] = position.y;
    g_controllerGripPosition[2] = position.z;
    g_controllerGripRotation[0] = rotation.x;
    g_controllerGripRotation[1] = rotation.y;
    g_controllerGripRotation[2] = rotation.z;
    g_controllerGripRotation[3] = rotation.w;
    g_controllerGripSampleId = sampleId;
    g_controllerGripTimestampNs = timestampNs;
    g_controllerGripTick = GetTickCount64();
    ReleaseSRWLockExclusive(&g_headAimLock);
}

void PublishTrackedControllerWeaponPose(
    const fearvr::TrackingVector& gripPosition,
    const fearvr::TrackingQuaternion& aimRotation,
    std::uint64_t sampleId,
    std::uint64_t timestampNs) noexcept {
    if (!g_headAimEnabled || !fearvr::IsFinite(gripPosition) ||
        !fearvr::IsFinite(aimRotation) || sampleId == 0 ||
        timestampNs == 0) {
        InvalidateTrackedControllerWeaponPose();
        return;
    }
    AcquireSRWLockExclusive(&g_headAimLock);
    g_controllerWeaponPosition[0] = gripPosition.x;
    g_controllerWeaponPosition[1] = gripPosition.y;
    g_controllerWeaponPosition[2] = gripPosition.z;
    g_controllerWeaponRotation[0] = aimRotation.x;
    g_controllerWeaponRotation[1] = aimRotation.y;
    g_controllerWeaponRotation[2] = aimRotation.z;
    g_controllerWeaponRotation[3] = aimRotation.w;
    g_controllerWeaponSampleId = sampleId;
    g_controllerWeaponTimestampNs = timestampNs;
    g_controllerWeaponTick = GetTickCount64();
    ReleaseSRWLockExclusive(&g_headAimLock);
    if (InterlockedCompareExchange(
            &g_controllerWeaponPublishedLogged, 1, 0) == 0 &&
        g_passThroughLog != nullptr) {
        g_passThroughLog(
            "m5_controller_weapon_pose_active",
            "position=right_grip_pose rotation=right_aim_pose "
            "basis=lithtech_world shared_by=model_and_collision "
            "freshness_ms=250");
    }
}

bool CopyFreshTrackedHeadAim(
    void** camera,
    float (&rotation)[4]) noexcept {
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_headAimLock);
    const bool fresh = g_headAimEnabled && g_headAimCamera != nullptr &&
        g_headAimTick != 0 &&
        now - g_headAimTick <= kHeadAimFreshnessMilliseconds;
    if (fresh) {
        if (camera != nullptr) {
            *camera = g_headAimCamera;
        }
        std::memcpy(rotation, g_headAimRotation, sizeof(g_headAimRotation));
    }
    ReleaseSRWLockShared(&g_headAimLock);
    return fresh;
}

bool CopyFreshTrackedHeadWorldPose(
    float (&position)[3],
    float (&rotation)[4]) noexcept {
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_headAimLock);
    const bool fresh = g_headAimEnabled && g_headAimCamera != nullptr &&
        g_headAimTick != 0 &&
        now - g_headAimTick <= kHeadAimFreshnessMilliseconds;
    if (fresh) {
        std::memcpy(position, g_headAimPosition, sizeof(g_headAimPosition));
        std::memcpy(rotation, g_headAimRotation, sizeof(g_headAimRotation));
    }
    ReleaseSRWLockShared(&g_headAimLock);
    return fresh;
}

bool CopyFreshTrackedControllerAimWorldPose(
    float (&position)[3],
    float (&rotation)[4],
    std::uint64_t& sampleId,
    std::uint64_t& timestampNs) noexcept {
    sampleId = 0;
    timestampNs = 0;
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_headAimLock);
    const bool fresh = g_headAimEnabled && g_controllerAimTick != 0 &&
        now - g_controllerAimTick <= kHeadAimFreshnessMilliseconds;
    if (fresh) {
        std::memcpy(
            position, g_controllerAimPosition,
            sizeof(g_controllerAimPosition));
        std::memcpy(
            rotation, g_controllerAimRotation,
            sizeof(g_controllerAimRotation));
        sampleId = g_controllerAimSampleId;
        timestampNs = g_controllerAimTimestampNs;
    }
    ReleaseSRWLockShared(&g_headAimLock);
    return fresh;
}

bool CopyFreshTrackedRawControllerAlignmentPoses(
    float (&gripPosition)[3],
    float (&gripRotation)[4],
    float (&aimRotation)[4],
    std::uint64_t& sampleId,
    std::uint64_t& timestampNs) noexcept {
    sampleId = 0;
    timestampNs = 0;
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_headAimLock);
    const bool fresh = g_headAimEnabled &&
        g_controllerGripTick != 0 && g_controllerAimTick != 0 &&
        now - g_controllerGripTick <=
            kHeadAimFreshnessMilliseconds &&
        now - g_controllerAimTick <=
            kHeadAimFreshnessMilliseconds &&
        g_controllerGripSampleId != 0 &&
        g_controllerGripSampleId == g_controllerAimSampleId &&
        g_controllerGripTimestampNs != 0 &&
        g_controllerGripTimestampNs ==
            g_controllerAimTimestampNs;
    if (fresh) {
        std::memcpy(
            gripPosition, g_controllerGripPosition,
            sizeof(g_controllerGripPosition));
        std::memcpy(
            gripRotation, g_controllerGripRotation,
            sizeof(g_controllerGripRotation));
        std::memcpy(
            aimRotation, g_controllerAimRotation,
            sizeof(g_controllerAimRotation));
        sampleId = g_controllerGripSampleId;
        timestampNs = g_controllerGripTimestampNs;
    }
    ReleaseSRWLockShared(&g_headAimLock);
    return fresh;
}

bool CopyFreshTrackedControllerAim(
    float (&rotation)[4]) noexcept {
    float position[3]{};
    std::uint64_t sampleId = 0;
    std::uint64_t timestampNs = 0;
    return CopyFreshTrackedControllerAimWorldPose(
        position, rotation, sampleId, timestampNs);
}

bool CopyFreshTrackedControllerWorldPose(
    float (&position)[3],
    float (&rotation)[4],
    std::uint64_t& sampleId,
    std::uint64_t& timestampNs) noexcept {
    sampleId = 0;
    timestampNs = 0;
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_headAimLock);
    const bool fresh = g_headAimEnabled &&
        g_controllerWeaponTick != 0 &&
        now - g_controllerWeaponTick <=
            kHeadAimFreshnessMilliseconds;
    if (fresh) {
        std::memcpy(
            position, g_controllerWeaponPosition,
            sizeof(g_controllerWeaponPosition));
        std::memcpy(
            rotation, g_controllerWeaponRotation,
            sizeof(g_controllerWeaponRotation));
        sampleId = g_controllerWeaponSampleId;
        timestampNs = g_controllerWeaponTimestampNs;
    }
    ReleaseSRWLockShared(&g_headAimLock);
    return fresh;
}

bool CopyFreshTrackedMeleeAimBasis(
    float (&position)[3],
    float (&baseRotation)[4],
    float (&controllerRotation)[4]) noexcept {
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_headAimLock);
    const bool fresh = g_headAimEnabled && g_headAimTick != 0 &&
        g_controllerAimTick != 0 &&
        now - g_headAimTick <= kHeadAimFreshnessMilliseconds &&
        now - g_controllerAimTick <= kHeadAimFreshnessMilliseconds;
    if (fresh) {
        std::memcpy(position, g_headAimPosition, sizeof(g_headAimPosition));
        std::memcpy(
            baseRotation, g_headAimBaseRotation,
            sizeof(g_headAimBaseRotation));
        std::memcpy(
            controllerRotation, g_controllerAimRotation,
            sizeof(g_controllerAimRotation));
    }
    ReleaseSRWLockShared(&g_headAimLock);
    return fresh;
}

unsigned long __fastcall HookGetRigidTransformForHeadAim(
    void* client,
    void* ignoredEdx,
    void* object,
    RigidTransformAbi* transform) {
    (void)ignoredEdx;
    const unsigned long result = g_originalHeadAimGetRigidTransform(
        client, object, transform);
    if (result != 0UL || transform == nullptr) {
        return result;
    }
    void* camera = nullptr;
    float rotation[4]{};
    if (CopyFreshTrackedHeadAim(&camera, rotation) && object == camera) {
        std::memcpy(transform->rotation, rotation, sizeof(rotation));
        if (InterlockedCompareExchange(
                &g_headAimCameraReadLogged, 1, 0) == 0 &&
            g_passThroughLog != nullptr) {
            g_passThroughLog(
                "m5_head_camera_transform_active",
                "consumers=flashlight,focus_detectors "
                "source=fresh_hmd_world_rotation retail_base_unchanged=1");
        }
    }
    return result;
}

bool PressedOnce(int virtualKey, bool& wasDown) noexcept {
    const bool down = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    const bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}

struct RepeatingCalibrationKey {
    bool down{false};
    ULONGLONG nextRepeatTick{0};
};

bool PressedOrRepeated(
    int virtualKey,
    RepeatingCalibrationKey& state,
    ULONGLONG now) noexcept {
    const bool down = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    if (!down) {
        state.down = false;
        state.nextRepeatTick = 0;
        return false;
    }
    if (!state.down) {
        state.down = true;
        state.nextRepeatTick = now + 325;
        return true;
    }
    if (now >= state.nextRepeatTick) {
        state.nextRepeatTick = now + 60;
        return true;
    }
    return false;
}

bool GameOwnsForegroundWindow() noexcept {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return false;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId();
}

const char* WeaponGripCalibrationModeName(
    WeaponGripCalibrationMode mode) noexcept {
    return mode == WeaponGripCalibrationMode::Rotation
        ? "rotation" : "position";
}

constexpr float kWeaponGripTranslationSteps[] = {
    0.1F, 0.25F, 0.5F, 1.0F, 2.5F, 5.0F};
constexpr float kWeaponGripRotationSteps[] = {
    0.25F, 0.5F, 1.0F, 2.0F, 5.0F, 10.0F};
constexpr std::size_t kWeaponGripStepCount =
    sizeof(kWeaponGripTranslationSteps) /
    sizeof(kWeaponGripTranslationSteps[0]);

std::int32_t FindOrCreateWeaponGripCalibrationSlot(
    void* weapon,
    std::int32_t weaponIndex,
    void* modelObject,
    const fearvr::TrackingVector& basePosition,
    const fearvr::TrackingQuaternion& baseRotation,
    const PhysicalMeleeProfile& profile,
    WeaponSettingsStoreResult& persistentLoadResult,
    bool& persistentLoadAttempted,
    bool& inheritedPipeBaseline) noexcept {
    persistentLoadResult = WeaponSettingsStoreResult::NotFound;
    persistentLoadAttempted = false;
    inheritedPipeBaseline = false;
    if (weaponIndex < 0) {
        return -1;
    }
    const std::size_t replacement =
        SelectPhysicalMeleeCalibrationSlot(
            g_weaponGripCalibrationSlots, weaponIndex);
    if (replacement == kWeaponGripCalibrationSlotCount) {
        return -1;
    }
    WeaponGripCalibrationSlot& slot =
        g_weaponGripCalibrationSlots[replacement];
    if (slot.occupied && slot.weaponIndex == weaponIndex) {
        // Pointers are process-local instances. Refresh them when the same
        // stable Retail weapon is dropped, reacquired, or recreated after a
        // level transition while retaining its session tuning.
        slot.weapon = weapon;
        slot.modelObject = modelObject;
        slot.lastUsed = ++g_weaponGripCalibrationUseSequence;
        return static_cast<std::int32_t>(replacement);
    }
    slot = {};
    slot.weapon = weapon;
    slot.modelObject = modelObject;
    slot.weaponIndex = weaponIndex;
    slot.calibration.basePositionUnits = basePosition;
    slot.calibration.baseRotation = fearvr::Normalize(baseRotation);
    slot.calibration.positionUnits = basePosition;
    slot.calibration.baseSecondaryGripOffsetUnits =
        profile.secondaryGripOffsetUnits;
    slot.calibration.secondaryGripOffsetUnits =
        profile.secondaryGripOffsetUnits;
    slot.calibration.baseSecondaryGripGrabRadiusMeters =
        profile.secondaryGripGrabRadiusMeters;
    slot.calibration.secondaryGripGrabRadiusMeters =
        profile.secondaryGripGrabRadiusMeters;
    slot.calibration.baseSecondaryGripEnabled =
        profile.secondaryGripEnabled;
    slot.calibration.secondaryGripEnabled =
        profile.secondaryGripEnabled;
    WeaponGripSettings persisted{};
    persistentLoadAttempted = true;
    persistentLoadResult =
        LoadWeaponGripSettingsWithPipeOneHandedFallback(
            weaponIndex, profile.id, persisted,
            inheritedPipeBaseline);
    if (persistentLoadResult == WeaponSettingsStoreResult::Ok) {
        slot.calibration.positionUnits = persisted.positionUnits;
        slot.calibration.localRotationDegrees =
            persisted.localRotationDegrees;
        slot.calibration.secondaryGripOffsetUnits =
            persisted.secondaryGripOffsetUnits;
        slot.calibration.secondaryGripGrabRadiusMeters =
            persisted.secondaryGripGrabRadiusMeters;
        // A persisted record may disable a supported secondary grip, but it
        // must never enable one for a profile that has no authored anchor.
        slot.calibration.secondaryGripEnabled =
            profile.secondaryGripEnabled &&
            persisted.secondaryGripEnabled;
    }
    slot.lastUsed = ++g_weaponGripCalibrationUseSequence;
    slot.occupied = true;
    return static_cast<std::int32_t>(replacement);
}

bool CopyActiveWeaponGripCalibration(
    PhysicalMeleeGripCalibration& calibration,
    void*& weapon,
    std::int32_t& weaponIndex,
    void*& modelObject,
    std::uint64_t& sourceGeneration) noexcept {
    bool copied = false;
    AcquireSRWLockShared(&g_physicalMeleeVisualLock);
    const std::int32_t slotIndex =
        g_activeWeaponGripCalibrationSlot;
    if (slotIndex >= 0 &&
        static_cast<std::size_t>(slotIndex) <
            kWeaponGripCalibrationSlotCount) {
        const WeaponGripCalibrationSlot& slot =
            g_weaponGripCalibrationSlots[slotIndex];
        if (slot.occupied &&
            slot.weapon == g_physicalMeleeVisualWeapon &&
            slot.weaponIndex == g_physicalMeleeVisualWeaponIndex &&
            slot.modelObject == g_physicalMeleeVisualModel) {
            calibration = slot.calibration;
            weapon = slot.weapon;
            weaponIndex = slot.weaponIndex;
            modelObject = slot.modelObject;
            sourceGeneration =
                g_physicalMeleeVisualSourceGeneration;
            copied = true;
        }
    }
    ReleaseSRWLockShared(&g_physicalMeleeVisualLock);
    return copied;
}

void LogWeaponGripCalibrationState(
    const char* event,
    const char* action) noexcept {
    if (g_passThroughLog == nullptr) {
        return;
    }
    PhysicalMeleeGripCalibration calibration{};
    void* weapon = nullptr;
    void* modelObject = nullptr;
    std::int32_t weaponIndex = -1;
    std::uint64_t sourceGeneration = 0;
    const bool haveSource = CopyActiveWeaponGripCalibration(
        calibration, weapon, weaponIndex, modelObject,
        sourceGeneration);
    const fearvr::TrackingQuaternion resolvedRotation =
        ResolvePhysicalMeleeGripCalibrationRotation(calibration);
    char detail[1280]{};
    std::snprintf(
        detail, sizeof(detail),
        "action=%s active=%u source=%s mode=%s "
        "translation_step_units=%.2f rotation_step_degrees=%.2f "
        "weapon_index=%ld weapon=%p model_object=%p "
        "source_generation=%llu position_units=(%.3f,%.3f,%.3f) "
        "local_rotation_degrees=(%.3f,%.3f,%.3f) "
        "rotation_quaternion=(%.6f,%.6f,%.6f,%.6f) "
        "profile_position_units={%.3f,%.3f,%.3f} "
        "profile_rotation={%.6f,%.6f,%.6f,%.6f} "
        "secondary_enabled=%u "
        "secondary_offset_units={%.3f,%.3f,%.3f} "
        "secondary_grab_radius_m=%.3f",
        action,
        InterlockedCompareExchange(
            &g_weaponGripCalibrationActive, 0, 0) != 0 ? 1U : 0U,
        haveSource ? "equipped_weapon" : "none",
        WeaponGripCalibrationModeName(g_weaponGripCalibrationMode),
        kWeaponGripTranslationSteps[g_weaponGripCalibrationStepIndex],
        kWeaponGripRotationSteps[g_weaponGripCalibrationStepIndex],
        static_cast<long>(weaponIndex), weapon, modelObject,
        static_cast<unsigned long long>(sourceGeneration),
        calibration.positionUnits.x,
        calibration.positionUnits.y,
        calibration.positionUnits.z,
        calibration.localRotationDegrees.x,
        calibration.localRotationDegrees.y,
        calibration.localRotationDegrees.z,
        resolvedRotation.x, resolvedRotation.y,
        resolvedRotation.z, resolvedRotation.w,
        calibration.positionUnits.x,
        calibration.positionUnits.y,
        calibration.positionUnits.z,
        resolvedRotation.x, resolvedRotation.y,
        resolvedRotation.z, resolvedRotation.w,
        calibration.secondaryGripEnabled ? 1U : 0U,
        calibration.secondaryGripOffsetUnits.x,
        calibration.secondaryGripOffsetUnits.y,
        calibration.secondaryGripOffsetUnits.z,
        calibration.secondaryGripGrabRadiusMeters);
    g_passThroughLog(event, detail);
}

bool PersistActiveWeaponGripCalibration(
    const char* action) noexcept {
    PhysicalMeleeGripCalibration calibration{};
    void* weapon = nullptr;
    void* modelObject = nullptr;
    std::int32_t weaponIndex = -1;
    std::uint64_t sourceGeneration = 0;
    if (!CopyActiveWeaponGripCalibration(
            calibration, weapon, weaponIndex, modelObject,
            sourceGeneration)) {
        if (g_passThroughLog != nullptr) {
            char detail[192]{};
            std::snprintf(
                detail, sizeof(detail),
                "action=%s result=no_active_weapon",
                action != nullptr ? action : "unknown");
            g_passThroughLog(
                "m5_weapon_grip_settings_save_failed", detail);
        }
        return false;
    }

    WeaponGripSettings settings{};
    settings.positionUnits = calibration.positionUnits;
    settings.localRotationDegrees =
        calibration.localRotationDegrees;
    settings.secondaryGripOffsetUnits =
        calibration.secondaryGripOffsetUnits;
    settings.secondaryGripGrabRadiusMeters =
        calibration.secondaryGripGrabRadiusMeters;
    settings.secondaryGripEnabled =
        calibration.secondaryGripEnabled;
    const PhysicalMeleeProfile profile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    const WeaponSettingsStoreResult result =
        SaveWeaponGripSettings(weaponIndex, profile.id, settings);
    if (g_passThroughLog != nullptr) {
        char detail[640]{};
        std::snprintf(
            detail, sizeof(detail),
            "action=%s weapon_index=%ld profile=%s result=%s "
            "source_generation=%llu position_units=(%.3f,%.3f,%.3f) "
            "local_rotation_degrees=(%.3f,%.3f,%.3f) "
            "secondary_enabled=%u "
            "secondary_offset_units=(%.3f,%.3f,%.3f) "
            "secondary_grab_radius_m=%.3f",
            action != nullptr ? action : "unknown",
            static_cast<long>(weaponIndex),
            PhysicalMeleeProfileName(profile.id),
            WeaponSettingsStoreResultName(result),
            static_cast<unsigned long long>(sourceGeneration),
            settings.positionUnits.x, settings.positionUnits.y,
            settings.positionUnits.z,
            settings.localRotationDegrees.x,
            settings.localRotationDegrees.y,
            settings.localRotationDegrees.z,
            settings.secondaryGripEnabled ? 1U : 0U,
            settings.secondaryGripOffsetUnits.x,
            settings.secondaryGripOffsetUnits.y,
            settings.secondaryGripOffsetUnits.z,
            settings.secondaryGripGrabRadiusMeters);
        g_passThroughLog(
            result == WeaponSettingsStoreResult::Ok
                ? "m5_weapon_grip_settings_saved"
                : "m5_weapon_grip_settings_save_failed",
            detail);
    }
    // The live edit remains valid even if persistence fails. This return
    // reports that an active calibration was available and save was attempted.
    return true;
}

bool AdjustActiveWeaponGripCalibration(
    float x, float y, float z) noexcept {
    bool adjusted = false;
    AcquireSRWLockExclusive(&g_physicalMeleeVisualLock);
    const std::int32_t slotIndex =
        g_activeWeaponGripCalibrationSlot;
    if (slotIndex >= 0 &&
        static_cast<std::size_t>(slotIndex) <
            kWeaponGripCalibrationSlotCount) {
        WeaponGripCalibrationSlot& slot =
            g_weaponGripCalibrationSlots[slotIndex];
        if (slot.occupied &&
            slot.weapon == g_physicalMeleeVisualWeapon &&
            slot.weaponIndex == g_physicalMeleeVisualWeaponIndex &&
            slot.modelObject == g_physicalMeleeVisualModel) {
            if (g_weaponGripCalibrationMode ==
                WeaponGripCalibrationMode::Position) {
                slot.calibration.positionUnits.x = std::clamp(
                    slot.calibration.positionUnits.x + x,
                    -300.0F, 300.0F);
                slot.calibration.positionUnits.y = std::clamp(
                    slot.calibration.positionUnits.y + y,
                    -300.0F, 300.0F);
                slot.calibration.positionUnits.z = std::clamp(
                    slot.calibration.positionUnits.z + z,
                    -300.0F, 300.0F);
            } else {
                slot.calibration.localRotationDegrees.x =
                    PhysicalMeleeWrapDegrees(
                        slot.calibration.localRotationDegrees.x + x);
                slot.calibration.localRotationDegrees.y =
                    PhysicalMeleeWrapDegrees(
                        slot.calibration.localRotationDegrees.y + y);
                slot.calibration.localRotationDegrees.z =
                    PhysicalMeleeWrapDegrees(
                        slot.calibration.localRotationDegrees.z + z);
            }
            slot.lastUsed = ++g_weaponGripCalibrationUseSequence;
            g_physicalMeleeVisualModelLocalGripPosition =
                slot.calibration.positionUnits;
            g_physicalMeleeVisualModelLocalGripRotation =
                ResolvePhysicalMeleeGripCalibrationRotation(
                    slot.calibration);
            adjusted = true;
        }
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeVisualLock);
    return adjusted;
}

bool ResetActiveWeaponGripCalibration() noexcept {
    bool reset = false;
    AcquireSRWLockExclusive(&g_physicalMeleeVisualLock);
    const std::int32_t slotIndex =
        g_activeWeaponGripCalibrationSlot;
    if (slotIndex >= 0 &&
        static_cast<std::size_t>(slotIndex) <
            kWeaponGripCalibrationSlotCount) {
        WeaponGripCalibrationSlot& slot =
            g_weaponGripCalibrationSlots[slotIndex];
        if (slot.occupied &&
            slot.weapon == g_physicalMeleeVisualWeapon &&
            slot.weaponIndex == g_physicalMeleeVisualWeaponIndex &&
            slot.modelObject == g_physicalMeleeVisualModel) {
            slot.calibration.positionUnits =
                slot.calibration.basePositionUnits;
            slot.calibration.localRotationDegrees = {};
            slot.lastUsed = ++g_weaponGripCalibrationUseSequence;
            g_physicalMeleeVisualModelLocalGripPosition =
                slot.calibration.positionUnits;
            g_physicalMeleeVisualModelLocalGripRotation =
                slot.calibration.baseRotation;
            reset = true;
        }
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeVisualLock);
    return reset;
}

bool ActiveCalibrationSupportsSecondaryGrip(
    const WeaponGripCalibrationSlot& slot) noexcept {
    return slot.occupied &&
        slot.calibration.baseSecondaryGripEnabled &&
        PhysicalMeleeLength(
            slot.calibration.baseSecondaryGripOffsetUnits) >= 5.0F;
}

void ApplyActiveSecondaryGripCalibrationLocked(
    const WeaponGripCalibrationSlot& slot) noexcept {
    g_physicalMeleeSecondaryGripOffsetUnits =
        slot.calibration.secondaryGripOffsetUnits;
    g_physicalMeleeSecondaryGripGrabRadiusMeters =
        slot.calibration.secondaryGripGrabRadiusMeters;
    g_physicalMeleeSecondaryGripProfileEnabled =
        slot.calibration.secondaryGripEnabled;
}

bool ToggleActiveSecondaryGripCalibration() noexcept {
    bool changed = false;
    AcquireSRWLockExclusive(&g_physicalMeleeVisualLock);
    const std::int32_t slotIndex =
        g_activeWeaponGripCalibrationSlot;
    if (slotIndex >= 0 &&
        static_cast<std::size_t>(slotIndex) <
            kWeaponGripCalibrationSlotCount) {
        WeaponGripCalibrationSlot& slot =
            g_weaponGripCalibrationSlots[slotIndex];
        if (slot.weapon == g_physicalMeleeVisualWeapon &&
            slot.weaponIndex == g_physicalMeleeVisualWeaponIndex &&
            slot.modelObject == g_physicalMeleeVisualModel &&
            ActiveCalibrationSupportsSecondaryGrip(slot)) {
            slot.calibration.secondaryGripEnabled =
                !slot.calibration.secondaryGripEnabled;
            slot.lastUsed = ++g_weaponGripCalibrationUseSequence;
            ApplyActiveSecondaryGripCalibrationLocked(slot);
            changed = true;
        }
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeVisualLock);
    return changed;
}

bool AdjustActiveSecondaryGripCalibration(
    float x, float y, float z,
    float grabRadiusDeltaMeters) noexcept {
    bool adjusted = false;
    AcquireSRWLockExclusive(&g_physicalMeleeVisualLock);
    const std::int32_t slotIndex =
        g_activeWeaponGripCalibrationSlot;
    if (slotIndex >= 0 &&
        static_cast<std::size_t>(slotIndex) <
            kWeaponGripCalibrationSlotCount) {
        WeaponGripCalibrationSlot& slot =
            g_weaponGripCalibrationSlots[slotIndex];
        if (slot.weapon == g_physicalMeleeVisualWeapon &&
            slot.weaponIndex == g_physicalMeleeVisualWeaponIndex &&
            slot.modelObject == g_physicalMeleeVisualModel &&
            ActiveCalibrationSupportsSecondaryGrip(slot)) {
            fearvr::TrackingVector next =
                slot.calibration.secondaryGripOffsetUnits;
            next.x = std::clamp(next.x + x, -300.0F, 300.0F);
            next.y = std::clamp(next.y + y, -300.0F, 300.0F);
            next.z = std::clamp(next.z + z, -300.0F, 300.0F);
            const float nextLength = PhysicalMeleeLength(next);
            const float nextRadius = std::clamp(
                slot.calibration.secondaryGripGrabRadiusMeters +
                    grabRadiusDeltaMeters,
                0.05F, 0.50F);
            if (fearvr::IsFinite(next) &&
                std::isfinite(nextLength) && nextLength >= 5.0F &&
                nextLength <= 300.0F && std::isfinite(nextRadius)) {
                slot.calibration.secondaryGripOffsetUnits = next;
                slot.calibration.secondaryGripGrabRadiusMeters =
                    nextRadius;
                slot.lastUsed = ++g_weaponGripCalibrationUseSequence;
                ApplyActiveSecondaryGripCalibrationLocked(slot);
                adjusted = true;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeVisualLock);
    return adjusted;
}

bool ResetActiveSecondaryGripCalibration() noexcept {
    bool reset = false;
    AcquireSRWLockExclusive(&g_physicalMeleeVisualLock);
    const std::int32_t slotIndex =
        g_activeWeaponGripCalibrationSlot;
    if (slotIndex >= 0 &&
        static_cast<std::size_t>(slotIndex) <
            kWeaponGripCalibrationSlotCount) {
        WeaponGripCalibrationSlot& slot =
            g_weaponGripCalibrationSlots[slotIndex];
        if (slot.weapon == g_physicalMeleeVisualWeapon &&
            slot.weaponIndex == g_physicalMeleeVisualWeaponIndex &&
            slot.modelObject == g_physicalMeleeVisualModel &&
            ActiveCalibrationSupportsSecondaryGrip(slot)) {
            slot.calibration.secondaryGripOffsetUnits =
                slot.calibration.baseSecondaryGripOffsetUnits;
            slot.calibration.secondaryGripGrabRadiusMeters =
                slot.calibration.baseSecondaryGripGrabRadiusMeters;
            slot.calibration.secondaryGripEnabled =
                slot.calibration.baseSecondaryGripEnabled;
            slot.lastUsed = ++g_weaponGripCalibrationUseSequence;
            ApplyActiveSecondaryGripCalibrationLocked(slot);
            reset = true;
        }
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeVisualLock);
    return reset;
}

bool CaptureActiveSecondaryGripCalibration(
    const FearVrInputState& input,
    bool sampleFresh) noexcept {
    if (!fearvr::IsInputStateUsable(input, sampleFresh) ||
        (input.activeHands &
         (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT)) !=
            (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT) ||
        (input.gripPoseValidHands &
         (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT)) !=
            (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT) ||
        (input.aimPoseValidHands & FEARVR_HAND_MASK_RIGHT) == 0U ||
        !fearvr::IsValidPose(input.handGripPose[FEARVR_HAND_LEFT]) ||
        !fearvr::IsValidPose(input.handGripPose[FEARVR_HAND_RIGHT]) ||
        !fearvr::IsValidPose(input.handAimPose[FEARVR_HAND_RIGHT])) {
        return false;
    }

    std::int32_t weaponIndex = -1;
    AcquireSRWLockShared(&g_physicalMeleeVisualLock);
    weaponIndex = g_physicalMeleeVisualWeaponIndex;
    ReleaseSRWLockShared(&g_physicalMeleeVisualLock);
    const PhysicalMeleeProfile profile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    if (!profile.secondaryGripEnabled) {
        return false;
    }
    const fearvr::TrackingVector primaryMeters =
        fearvr::OpenXrToLithTech(fearvr::PosePosition(
            input.handGripPose[FEARVR_HAND_RIGHT]));
    const fearvr::TrackingVector secondaryMeters =
        fearvr::OpenXrToLithTech(fearvr::PosePosition(
            input.handGripPose[FEARVR_HAND_LEFT]));
    const PhysicalMeleePose primary{
        PhysicalMeleeScale(primaryMeters, profile.unitsPerMeter),
        fearvr::OpenXrToLithTech(fearvr::PoseRotation(
            input.handAimPose[FEARVR_HAND_RIGHT]))};
    fearvr::TrackingVector capturedOffset{};
    if (!ResolvePhysicalMeleeSecondaryGripOffset(
            primary,
            PhysicalMeleeScale(
                secondaryMeters, profile.unitsPerMeter),
            capturedOffset)) {
        return false;
    }

    bool captured = false;
    AcquireSRWLockExclusive(&g_physicalMeleeVisualLock);
    const std::int32_t slotIndex =
        g_activeWeaponGripCalibrationSlot;
    if (weaponIndex == g_physicalMeleeVisualWeaponIndex &&
        slotIndex >= 0 &&
        static_cast<std::size_t>(slotIndex) <
            kWeaponGripCalibrationSlotCount) {
        WeaponGripCalibrationSlot& slot =
            g_weaponGripCalibrationSlots[slotIndex];
        if (slot.weapon == g_physicalMeleeVisualWeapon &&
            slot.weaponIndex == weaponIndex &&
            slot.modelObject == g_physicalMeleeVisualModel &&
            ActiveCalibrationSupportsSecondaryGrip(slot)) {
            slot.calibration.secondaryGripOffsetUnits =
                capturedOffset;
            slot.calibration.secondaryGripEnabled = true;
            slot.lastUsed = ++g_weaponGripCalibrationUseSequence;
            ApplyActiveSecondaryGripCalibrationLocked(slot);
            captured = true;
        }
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeVisualLock);
    return captured;
}

void LogStereoTuningState(const char* action) noexcept;

ToolMenuMeleeSettings CopyToolMenuMeleeSettings(
    std::int32_t weaponIndex) noexcept {
    const PhysicalMeleeProfile baseProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    ToolMenuMeleeSettings settings =
        ToolMenuMeleeSettingsFromProfile(baseProfile);
    if (weaponIndex < 0) {
        return settings;
    }
    WeaponSettingsStoreResult loadResult =
        WeaponSettingsStoreResult::NotFound;
    bool loadAttempted = false;
    bool inheritedPipeBaseline = false;
    AcquireSRWLockExclusive(&g_toolMenuSettingsLock);
    ToolMenuWeaponSettingsSlot* slot =
        ResolveToolMenuWeaponSettingsSlot(
            g_toolMenuWeaponSettingsRegistry,
            weaponIndex, baseProfile);
    if (slot != nullptr) {
        if (!slot->persistentLoadAttempted) {
            slot->persistentLoadAttempted = true;
            ToolMenuMeleeSettings persisted{};
            loadResult =
                LoadWeaponToolSettingsWithPipeOneHandedFallback(
                    weaponIndex, baseProfile.id, persisted,
                    inheritedPipeBaseline);
            if (loadResult == WeaponSettingsStoreResult::Ok) {
                slot->settings = persisted;
            }
            loadAttempted = true;
        }
        settings = slot->settings;
    }
    ReleaseSRWLockExclusive(&g_toolMenuSettingsLock);
    if (loadAttempted && g_passThroughLog != nullptr) {
        char detail[192]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld profile=%s result=%s source=%s",
            static_cast<long>(weaponIndex),
            PhysicalMeleeProfileName(baseProfile.id),
            WeaponSettingsStoreResultName(loadResult),
            loadResult == WeaponSettingsStoreResult::Ok
                ? inheritedPipeBaseline
                    ? "pipe_baseline" : "weapon_record"
                : "profile_defaults");
        g_passThroughLog("m5_weapon_settings_loaded", detail);
    }
    return settings;
}

bool StoreToolMenuMeleeSettings(
    std::int32_t weaponIndex,
    const ToolMenuMeleeSettings& settings) noexcept {
    if (weaponIndex < 0 ||
        !ToolMenuMeleeSettingsAreValid(settings)) {
        return false;
    }
    const PhysicalMeleeProfile baseProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    bool stored = false;
    AcquireSRWLockExclusive(&g_toolMenuSettingsLock);
    ToolMenuWeaponSettingsSlot* slot =
        ResolveToolMenuWeaponSettingsSlot(
            g_toolMenuWeaponSettingsRegistry,
            weaponIndex, baseProfile);
    if (slot != nullptr) {
        slot->settings = settings;
        slot->persistentLoadAttempted = true;
        stored = true;
    }
    ReleaseSRWLockExclusive(&g_toolMenuSettingsLock);
    if (stored) {
        const WeaponSettingsStoreResult saveResult =
            SaveWeaponToolSettings(
                weaponIndex, baseProfile.id, settings);
        if (g_passThroughLog != nullptr) {
            char detail[192]{};
            std::snprintf(
                detail, sizeof(detail),
                "weapon_index=%ld profile=%s result=%s",
                static_cast<long>(weaponIndex),
                PhysicalMeleeProfileName(baseProfile.id),
                WeaponSettingsStoreResultName(saveResult));
            g_passThroughLog(
                saveResult == WeaponSettingsStoreResult::Ok
                    ? "m5_weapon_settings_saved"
                    : "m5_weapon_settings_save_failed",
                detail);
        }
    }
    return stored;
}

PhysicalMeleeBlockPoseSettings CopyToolMenuBlockPoseSettings(
    std::int32_t weaponIndex) noexcept {
    PhysicalMeleeBlockPoseSettings settings{};
    if (weaponIndex < 0) {
        return settings;
    }
    const PhysicalMeleeProfile baseProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    WeaponSettingsStoreResult loadResult =
        WeaponSettingsStoreResult::NotFound;
    bool loadAttempted = false;
    bool inheritedPipeBaseline = false;
    AcquireSRWLockExclusive(&g_toolMenuSettingsLock);
    ToolMenuWeaponSettingsSlot* slot =
        ResolveToolMenuWeaponSettingsSlot(
            g_toolMenuWeaponSettingsRegistry,
            weaponIndex, baseProfile);
    if (slot != nullptr) {
        if (!slot->blockPosePersistentLoadAttempted) {
            slot->blockPosePersistentLoadAttempted = true;
            PhysicalMeleeBlockPoseSettings persisted{};
            loadResult =
                LoadWeaponBlockPoseSettingsWithPipeOneHandedFallback(
                    weaponIndex, baseProfile.id, persisted,
                    inheritedPipeBaseline);
            if (loadResult == WeaponSettingsStoreResult::Ok) {
                slot->blockPoseSettings = persisted;
            }
            loadAttempted = true;
        }
        settings = slot->blockPoseSettings;
    }
    ReleaseSRWLockExclusive(&g_toolMenuSettingsLock);
    if (loadAttempted && g_passThroughLog != nullptr) {
        char detail[224]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld profile=%s result=%s source=%s",
            static_cast<long>(weaponIndex),
            PhysicalMeleeProfileName(baseProfile.id),
            WeaponSettingsStoreResultName(loadResult),
            loadResult == WeaponSettingsStoreResult::Ok
                ? inheritedPipeBaseline
                    ? "pipe_baseline" : "weapon_record"
                : "unconfigured");
        g_passThroughLog("m5_block_pose_settings_loaded", detail);
    }
    return settings;
}

bool StoreToolMenuBlockPoseSettings(
    std::int32_t weaponIndex,
    const PhysicalMeleeBlockPoseSettings& settings) noexcept {
    if (weaponIndex < 0 ||
        !PhysicalMeleeBlockPoseSettingsAreValid(settings)) {
        return false;
    }
    const PhysicalMeleeProfile baseProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    bool stored = false;
    AcquireSRWLockExclusive(&g_toolMenuSettingsLock);
    ToolMenuWeaponSettingsSlot* slot =
        ResolveToolMenuWeaponSettingsSlot(
            g_toolMenuWeaponSettingsRegistry,
            weaponIndex, baseProfile);
    if (slot != nullptr) {
        slot->blockPoseSettings = settings;
        slot->blockPosePersistentLoadAttempted = true;
        stored = true;
    }
    ReleaseSRWLockExclusive(&g_toolMenuSettingsLock);
    if (stored) {
        const WeaponSettingsStoreResult saveResult =
            SaveWeaponBlockPoseSettings(
                weaponIndex, baseProfile.id, settings);
        if (g_passThroughLog != nullptr) {
            char detail[384]{};
            std::snprintf(
                detail, sizeof(detail),
                "weapon_index=%ld profile=%s result=%s "
                "enabled=%u captured=%u position_tolerance_m=%.3f "
                "angle_tolerance_deg=%.1f",
                static_cast<long>(weaponIndex),
                PhysicalMeleeProfileName(baseProfile.id),
                WeaponSettingsStoreResultName(saveResult),
                settings.enabled ? 1U : 0U,
                settings.captured ? 1U : 0U,
                settings.positionToleranceMeters,
                settings.angleToleranceDegrees);
            g_passThroughLog(
                saveResult == WeaponSettingsStoreResult::Ok
                    ? "m5_block_pose_settings_saved"
                    : "m5_block_pose_settings_save_failed",
                detail);
        }
    }
    return stored;
}

ToolMenuBlockTimingSettings CopyToolMenuBlockTimingSettings(
    std::int32_t weaponIndex) noexcept {
    ToolMenuBlockTimingSettings settings{};
    if (weaponIndex < 0) {
        return settings;
    }
    const PhysicalMeleeProfile baseProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    WeaponSettingsStoreResult loadResult =
        WeaponSettingsStoreResult::NotFound;
    bool loadAttempted = false;
    AcquireSRWLockExclusive(&g_toolMenuSettingsLock);
    ToolMenuWeaponSettingsSlot* slot =
        ResolveToolMenuWeaponSettingsSlot(
            g_toolMenuWeaponSettingsRegistry,
            weaponIndex, baseProfile);
    if (slot != nullptr) {
        if (!slot->blockTimingPersistentLoadAttempted) {
            slot->blockTimingPersistentLoadAttempted = true;
            ToolMenuBlockTimingSettings persisted{};
            loadResult = LoadWeaponBlockTimingSettings(
                weaponIndex, baseProfile.id, persisted);
            if (loadResult == WeaponSettingsStoreResult::Ok) {
                slot->blockTimingSettings = persisted;
            }
            loadAttempted = true;
        }
        settings = slot->blockTimingSettings;
    }
    ReleaseSRWLockExclusive(&g_toolMenuSettingsLock);
    if (loadAttempted && g_passThroughLog != nullptr) {
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld profile=%s result=%s source=%s "
            "override=%u window_ms=%u",
            static_cast<long>(weaponIndex),
            PhysicalMeleeProfileName(baseProfile.id),
            WeaponSettingsStoreResultName(loadResult),
            loadResult == WeaponSettingsStoreResult::Ok
                ? "weapon_record" : "retail_default",
            settings.overrideEnabled ? 1U : 0U,
            settings.collisionWindowMilliseconds);
        g_passThroughLog("m5_block_timing_settings_loaded", detail);
    }
    return settings;
}

bool StoreToolMenuBlockTimingSettings(
    std::int32_t weaponIndex,
    const ToolMenuBlockTimingSettings& settings) noexcept {
    if (weaponIndex < 0 ||
        !ToolMenuBlockTimingSettingsAreValid(settings)) {
        return false;
    }
    const PhysicalMeleeProfile baseProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    bool stored = false;
    AcquireSRWLockExclusive(&g_toolMenuSettingsLock);
    ToolMenuWeaponSettingsSlot* slot =
        ResolveToolMenuWeaponSettingsSlot(
            g_toolMenuWeaponSettingsRegistry,
            weaponIndex, baseProfile);
    if (slot != nullptr) {
        slot->blockTimingSettings = settings;
        slot->blockTimingPersistentLoadAttempted = true;
        stored = true;
    }
    ReleaseSRWLockExclusive(&g_toolMenuSettingsLock);
    if (stored) {
        const WeaponSettingsStoreResult saveResult =
            SaveWeaponBlockTimingSettings(
                weaponIndex, baseProfile.id, settings);
        if (g_passThroughLog != nullptr) {
            char detail[256]{};
            std::snprintf(
                detail, sizeof(detail),
                "weapon_index=%ld profile=%s result=%s "
                "override=%u window_ms=%u",
                static_cast<long>(weaponIndex),
                PhysicalMeleeProfileName(baseProfile.id),
                WeaponSettingsStoreResultName(saveResult),
                settings.overrideEnabled ? 1U : 0U,
                settings.collisionWindowMilliseconds);
            g_passThroughLog(
                saveResult == WeaponSettingsStoreResult::Ok
                    ? "m5_block_timing_settings_saved"
                    : "m5_block_timing_settings_save_failed",
                detail);
        }
    }
    return stored;
}

ToolMenuColliderSettings CopyToolMenuColliderSettings(
    std::int32_t weaponIndex) noexcept {
    const PhysicalMeleeProfile baseProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    ToolMenuColliderSettings settings =
        ToolMenuColliderSettingsFromProfile(baseProfile);
    if (weaponIndex < 0) {
        return settings;
    }
    WeaponSettingsStoreResult loadResult =
        WeaponSettingsStoreResult::NotFound;
    bool loadAttempted = false;
    bool inheritedPipeBaseline = false;
    AcquireSRWLockExclusive(&g_toolMenuSettingsLock);
    ToolMenuWeaponSettingsSlot* slot =
        ResolveToolMenuWeaponSettingsSlot(
            g_toolMenuWeaponSettingsRegistry,
            weaponIndex, baseProfile);
    if (slot != nullptr) {
        if (!slot->colliderPersistentLoadAttempted) {
            slot->colliderPersistentLoadAttempted = true;
            ToolMenuColliderSettings persisted{};
            loadResult =
                LoadWeaponColliderSettingsWithPipeOneHandedFallback(
                    weaponIndex, baseProfile.id, persisted,
                    inheritedPipeBaseline);
            if (loadResult == WeaponSettingsStoreResult::Ok) {
                slot->colliderSettings = persisted;
            }
            loadAttempted = true;
        }
        settings = slot->colliderSettings;
    }
    ReleaseSRWLockExclusive(&g_toolMenuSettingsLock);
    if (loadAttempted && g_passThroughLog != nullptr) {
        char detail[224]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld profile=%s result=%s source=%s",
            static_cast<long>(weaponIndex),
            PhysicalMeleeProfileName(baseProfile.id),
            WeaponSettingsStoreResultName(loadResult),
            loadResult == WeaponSettingsStoreResult::Ok
                ? inheritedPipeBaseline
                    ? "pipe_baseline" : "weapon_record"
                : "profile_defaults");
        g_passThroughLog("m5_collider_settings_loaded", detail);
    }
    return settings;
}

bool StoreToolMenuColliderSettings(
    std::int32_t weaponIndex,
    const ToolMenuColliderSettings& settings) noexcept {
    if (weaponIndex < 0 ||
        !ToolMenuColliderSettingsAreValid(settings)) {
        return false;
    }
    const PhysicalMeleeProfile baseProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    bool stored = false;
    AcquireSRWLockExclusive(&g_toolMenuSettingsLock);
    ToolMenuWeaponSettingsSlot* slot =
        ResolveToolMenuWeaponSettingsSlot(
            g_toolMenuWeaponSettingsRegistry,
            weaponIndex, baseProfile);
    if (slot != nullptr) {
        slot->colliderSettings = settings;
        slot->colliderPersistentLoadAttempted = true;
        stored = true;
    }
    ReleaseSRWLockExclusive(&g_toolMenuSettingsLock);
    if (stored) {
        const WeaponSettingsStoreResult saveResult =
            SaveWeaponColliderSettings(
                weaponIndex, baseProfile.id, settings);
        if (g_passThroughLog != nullptr) {
            char detail[320]{};
            std::snprintf(
                detail, sizeof(detail),
                "weapon_index=%ld profile=%s result=%s "
                "position_x=%.3f position_y=%.3f position_z=%.3f "
                "rotation_x=%.3f rotation_y=%.3f "
                "rotation_z=%.3f "
                "length=%.2f radius=%.2f reversed=%u",
                static_cast<long>(weaponIndex),
                PhysicalMeleeProfileName(baseProfile.id),
                WeaponSettingsStoreResultName(saveResult),
                settings.positionOffsetUnits.x,
                settings.positionOffsetUnits.y,
                settings.positionOffsetUnits.z,
                settings.rotationOffsetDegrees.x,
                settings.rotationOffsetDegrees.y,
                settings.rotationOffsetDegrees.z,
                settings.lengthUnits, settings.radiusUnits,
                settings.reversed ? 1U : 0U);
            g_passThroughLog(
                saveResult == WeaponSettingsStoreResult::Ok
                    ? "m5_collider_settings_saved"
                    : "m5_collider_settings_save_failed",
                detail);
        }
    }
    return stored;
}

ToolMenuColliderSettings CopyToolMenuBlockColliderSettings(
    std::int32_t weaponIndex,
    bool& usesAttackColliderFallback) noexcept {
    const ToolMenuColliderSettings attackSettings =
        CopyToolMenuColliderSettings(weaponIndex);
    ToolMenuColliderSettings settings = attackSettings;
    usesAttackColliderFallback = true;
    if (weaponIndex < 0) {
        return settings;
    }
    const PhysicalMeleeProfile baseProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    WeaponSettingsStoreResult loadResult =
        WeaponSettingsStoreResult::NotFound;
    bool loadAttempted = false;
    AcquireSRWLockExclusive(&g_toolMenuSettingsLock);
    ToolMenuWeaponSettingsSlot* slot =
        ResolveToolMenuWeaponSettingsSlot(
            g_toolMenuWeaponSettingsRegistry,
            weaponIndex, baseProfile);
    if (slot != nullptr) {
        if (!slot->blockColliderPersistentLoadAttempted) {
            slot->blockColliderPersistentLoadAttempted = true;
            ToolMenuColliderSettings persisted{};
            loadResult = LoadWeaponBlockColliderSettings(
                weaponIndex, baseProfile.id, persisted);
            if (loadResult == WeaponSettingsStoreResult::Ok) {
                slot->blockColliderSettings = persisted;
                slot->blockColliderUsesAttackFallback = false;
            } else {
                slot->blockColliderSettings = attackSettings;
                slot->blockColliderUsesAttackFallback = true;
            }
            loadAttempted = true;
        }
        if (slot->blockColliderUsesAttackFallback) {
            // Follow live attack-collider edits until the first explicit block
            // edit creates an independent record.
            slot->blockColliderSettings = attackSettings;
        }
        settings = slot->blockColliderSettings;
        usesAttackColliderFallback =
            slot->blockColliderUsesAttackFallback;
    }
    ReleaseSRWLockExclusive(&g_toolMenuSettingsLock);
    if (loadAttempted && g_passThroughLog != nullptr) {
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld profile=%s result=%s source=%s",
            static_cast<long>(weaponIndex),
            PhysicalMeleeProfileName(baseProfile.id),
            WeaponSettingsStoreResultName(loadResult),
            loadResult == WeaponSettingsStoreResult::Ok
                ? "weapon_record" : "attack_collider_fallback");
        g_passThroughLog(
            "m5_block_collider_settings_loaded", detail);
    }
    return settings;
}

bool StoreToolMenuBlockColliderSettings(
    std::int32_t weaponIndex,
    const ToolMenuColliderSettings& settings) noexcept {
    if (weaponIndex < 0 ||
        !ToolMenuColliderSettingsAreValid(settings)) {
        return false;
    }
    const PhysicalMeleeProfile baseProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    bool stored = false;
    AcquireSRWLockExclusive(&g_toolMenuSettingsLock);
    ToolMenuWeaponSettingsSlot* slot =
        ResolveToolMenuWeaponSettingsSlot(
            g_toolMenuWeaponSettingsRegistry,
            weaponIndex, baseProfile);
    if (slot != nullptr) {
        slot->blockColliderSettings = settings;
        slot->blockColliderPersistentLoadAttempted = true;
        slot->blockColliderUsesAttackFallback = false;
        stored = true;
    }
    ReleaseSRWLockExclusive(&g_toolMenuSettingsLock);
    if (stored) {
        const WeaponSettingsStoreResult saveResult =
            SaveWeaponBlockColliderSettings(
                weaponIndex, baseProfile.id, settings);
        if (g_passThroughLog != nullptr) {
            char detail[352]{};
            std::snprintf(
                detail, sizeof(detail),
                "weapon_index=%ld profile=%s result=%s "
                "position_x=%.3f position_y=%.3f position_z=%.3f "
                "rotation_x=%.3f rotation_y=%.3f "
                "rotation_z=%.3f length=%.2f radius=%.2f reversed=%u",
                static_cast<long>(weaponIndex),
                PhysicalMeleeProfileName(baseProfile.id),
                WeaponSettingsStoreResultName(saveResult),
                settings.positionOffsetUnits.x,
                settings.positionOffsetUnits.y,
                settings.positionOffsetUnits.z,
                settings.rotationOffsetDegrees.x,
                settings.rotationOffsetDegrees.y,
                settings.rotationOffsetDegrees.z,
                settings.lengthUnits, settings.radiusUnits,
                settings.reversed ? 1U : 0U);
            g_passThroughLog(
                saveResult == WeaponSettingsStoreResult::Ok
                    ? "m5_block_collider_settings_saved"
                    : "m5_block_collider_settings_save_failed",
                detail);
        }
    }
    return stored;
}

void LogMagazineSocketAuthoring(
    const char* event,
    const char* action,
    const MagazineSocketAuthoringRuntimeState& state) noexcept {
    if (g_passThroughLog == nullptr) {
        return;
    }
    const MagazineInsertionSocketSettings& settings =
        state.editor.current;
    char detail[768]{};
    std::snprintf(
        detail, sizeof(detail),
        "action=%s phase=1 engine_handoff=none "
        "ammo_mutation=0 weapon_state_mutation=0 "
        "retail_state_mutation=0 "
        "revision=%llu process_id=%lu "
        "weapon_index=%ld weapon_name=%s "
        "source_generation=%llu configured=%u "
        "position_cm=(%.3f,%.3f,%.3f) "
        "rotation_deg=(%.3f,%.3f,%.3f) "
        "half_extents_cm=(%.3f,%.3f,%.3f) "
        "rail_cm=%.3f snap_cm=%.3f snap_deg=%.3f "
        "load=%s save=%s",
        action != nullptr ? action : "unknown",
        static_cast<unsigned long long>(state.revision),
        static_cast<unsigned long>(
            GetCurrentProcessId()),
        static_cast<long>(state.weaponIndex),
        state.weaponName[0] != '\0'
            ? state.weaponName : "unknown",
        static_cast<unsigned long long>(
            state.sourceGeneration),
        settings.configured ? 1U : 0U,
        settings.positionUnits.x,
        settings.positionUnits.y,
        settings.positionUnits.z,
        settings.rotationDegrees.x,
        settings.rotationDegrees.y,
        settings.rotationDegrees.z,
        settings.halfExtentsUnits.x,
        settings.halfExtentsUnits.y,
        settings.halfExtentsUnits.z,
        settings.approachLengthUnits,
        settings.snapDistanceUnits,
        settings.snapAngleDegrees,
        WeaponSettingsStoreResultName(state.lastLoadResult),
        WeaponSettingsStoreResultName(state.lastSaveResult));
    g_passThroughLog(event, detail);
}

bool ResolveMagazineSocketAuthoringIdentity(
    ToolMenuMeleeTelemetry& telemetry,
    std::int32_t& weaponIndex,
    std::uint64_t& sourceGeneration) noexcept {
    telemetry = {};
    weaponIndex = -1;
    sourceGeneration = 0U;
    ReadPhysicalMeleeToolTelemetry(telemetry);
    if (telemetry.weaponIndex < 0 ||
        telemetry.weaponIndex == kCondemnedUnarmedWeaponIndex ||
        !telemetry.weaponNameResolved ||
        !MagazineSocketWeaponNameIsValid(
            telemetry.weaponName)) {
        return false;
    }
    PhysicalMeleeGripCalibration calibration{};
    void* weapon = nullptr;
    void* modelObject = nullptr;
    if (!CopyActiveWeaponGripCalibration(
            calibration, weapon, weaponIndex, modelObject,
            sourceGeneration) ||
        weaponIndex != telemetry.weaponIndex ||
        sourceGeneration == 0U) {
        weaponIndex = -1;
        sourceGeneration = 0U;
        return false;
    }
    return true;
}

bool EnsureMagazineSocketAuthoringIdentity() noexcept {
    ToolMenuMeleeTelemetry telemetry{};
    std::int32_t weaponIndex = -1;
    std::uint64_t sourceGeneration = 0U;
    if (!ResolveMagazineSocketAuthoringIdentity(
            telemetry, weaponIndex, sourceGeneration)) {
        AcquireSRWLockExclusive(
            &g_magazineSocketAuthoringLock);
        g_magazineSocketAuthoringState.visualReady = false;
        ReleaseSRWLockExclusive(
            &g_magazineSocketAuthoringLock);
        return false;
    }

    AcquireSRWLockShared(&g_magazineSocketAuthoringLock);
    const bool alreadyBound =
        g_magazineSocketAuthoringState.weaponIndex ==
            weaponIndex &&
        g_magazineSocketAuthoringState.sourceGeneration ==
            sourceGeneration &&
        std::strcmp(
            g_magazineSocketAuthoringState.weaponName,
            telemetry.weaponName) == 0;
    const bool alreadyReady =
        g_magazineSocketAuthoringState.identityReady;
    ReleaseSRWLockShared(&g_magazineSocketAuthoringLock);
    if (alreadyBound) {
        return alreadyReady;
    }

    MagazineInsertionSocketSettings loaded{};
    const WeaponSettingsStoreResult loadResult =
        LoadMagazineInsertionSocketSettings(
            weaponIndex, telemetry.weaponName, loaded);
    const bool loadAccepted =
        loadResult == WeaponSettingsStoreResult::Ok ||
        loadResult == WeaponSettingsStoreResult::NotFound;
    if (loadResult == WeaponSettingsStoreResult::NotFound) {
        loaded = {};
    }

    AcquireSRWLockExclusive(&g_magazineSocketAuthoringLock);
    g_magazineSocketAuthoringState = {};
    g_magazineSocketAuthoringState.weaponIndex = weaponIndex;
    g_magazineSocketAuthoringState.sourceGeneration =
        sourceGeneration;
    strcpy_s(
        g_magazineSocketAuthoringState.weaponName,
        telemetry.weaponName);
    g_magazineSocketAuthoringState.lastLoadResult = loadResult;
    g_magazineSocketAuthoringState.identityReady = loadAccepted;
    SetMagazineSocketEditorValue(
        g_magazineSocketAuthoringState.editor,
        loaded, true);
    const MagazineSocketAuthoringRuntimeState logState =
        g_magazineSocketAuthoringState;
    ReleaseSRWLockExclusive(&g_magazineSocketAuthoringLock);
    LogMagazineSocketAuthoring(
        loadAccepted
            ? "m5_magazine_socket_settings_loaded"
            : "m5_magazine_socket_settings_load_rejected",
        loadResult == WeaponSettingsStoreResult::Ok
            ? "weapon_record"
            : loadResult == WeaponSettingsStoreResult::NotFound
                ? "unconfigured"
                : "fail_closed",
        logState);
    if (loadAccepted) {
        LogMagazineSocketAuthoring(
            "m5_live_magazine_socket_armed",
            "ready", logState);
    }
    return loadAccepted;
}

MagazineSocketAuthoringRuntimeState
CopyMagazineSocketAuthoringState() noexcept {
    AcquireSRWLockShared(&g_magazineSocketAuthoringLock);
    const MagazineSocketAuthoringRuntimeState copy =
        g_magazineSocketAuthoringState;
    ReleaseSRWLockShared(&g_magazineSocketAuthoringLock);
    return copy;
}

void UpdateMagazineSocketAuthoringVisual(
    std::int32_t weaponIndex,
    std::uint64_t sourceGeneration,
    const PhysicalMeleeRigidTransform& modelWorld,
    const PhysicalMeleeRigidTransform& cursorWorld,
    bool posesFresh) noexcept {
    if (!EnsureMagazineSocketAuthoringIdentity()) {
        return;
    }
    PhysicalMeleeRigidTransform cursorModelLocal{};
    const bool visualReady = posesFresh &&
        ResolveMagazineSocketCursorModelLocal(
            modelWorld, cursorWorld, cursorModelLocal);
    AcquireSRWLockExclusive(&g_magazineSocketAuthoringLock);
    if (g_magazineSocketAuthoringState.weaponIndex !=
            weaponIndex ||
        g_magazineSocketAuthoringState.sourceGeneration !=
            sourceGeneration) {
        g_magazineSocketAuthoringState.visualReady = false;
        ReleaseSRWLockExclusive(
            &g_magazineSocketAuthoringLock);
        return;
    }
    g_magazineSocketAuthoringState.visualReady = visualReady;
    if (visualReady) {
        g_magazineSocketAuthoringState.modelWorld = modelWorld;
        g_magazineSocketAuthoringState.cursorModelLocal =
            cursorModelLocal;
        g_magazineSocketAuthoringState.preview =
            ResolveMagazineSocketSnapPreview(
                g_magazineSocketAuthoringState.editor.current,
                cursorModelLocal);
        g_magazineSocketAuthoringState.visualTick =
            GetTickCount64();
    }
    ReleaseSRWLockExclusive(&g_magazineSocketAuthoringLock);
}

bool SaveMagazineSocketAuthoringEdit(
    MagazineSocketAuthoringRuntimeState& state,
    const MagazineSocketEditorState& previous) noexcept {
    state.lastSaveResult =
        SaveMagazineInsertionSocketSettings(
            state.weaponIndex, state.weaponName,
            state.editor.current);
    if (state.lastSaveResult !=
        WeaponSettingsStoreResult::Ok) {
        state.editor = previous;
        return false;
    }
    ++state.revision;
    state.preview = ResolveMagazineSocketSnapPreview(
        state.editor.current, state.cursorModelLocal);
    return true;
}

bool ApplyToolMenuMagazineSocketAdjustment(
    std::uint32_t row,
    int delta,
    bool activate) noexcept {
    if (InterlockedCompareExchange(
            &g_weaponGripCalibrationEnabled, 0, 0) == 0 ||
        !EnsureMagazineSocketAuthoringIdentity()) {
        return false;
    }
    AcquireSRWLockExclusive(&g_magazineSocketAuthoringLock);
    MagazineSocketAuthoringRuntimeState& state =
        g_magazineSocketAuthoringState;
    if (!state.identityReady) {
        ReleaseSRWLockExclusive(
            &g_magazineSocketAuthoringLock);
        return false;
    }
    if (row == 1U && (delta != 0 || activate)) {
        CycleMagazineSocketComponent(
            state.editor, delta != 0 ? delta : 1);
        ReleaseSRWLockExclusive(
            &g_magazineSocketAuthoringLock);
        return true;
    }
    if (row == 3U && (delta != 0 || activate)) {
        state.editor.coarse = !state.editor.coarse;
        ReleaseSRWLockExclusive(
            &g_magazineSocketAuthoringLock);
        return true;
    }

    const MagazineSocketEditorState previous = state.editor;
    bool changed = false;
    const char* action = "unknown";
    if (row == 2U && delta != 0 &&
        state.editor.current.configured) {
        changed = AdjustMagazineSocketComponent(
            state.editor, delta);
        action = "menu_adjust";
    } else if (row == 4U && activate) {
        fearvr::TrackingVector rotationDegrees{};
        const bool fresh = state.visualReady &&
            GetTickCount64() - state.visualTick <=
                kHeadAimFreshnessMilliseconds;
        if (fresh &&
            PhysicalMeleeLocalRotationDegreesFromQuaternion(
                state.cursorModelLocal.rotation,
                rotationDegrees)) {
            PushMagazineSocketUndo(state.editor);
            state.editor.current.positionUnits =
                state.cursorModelLocal.positionUnits;
            state.editor.current.rotationDegrees =
                rotationDegrees;
            state.editor.current.configured = true;
            changed = true;
            action = "capture_off_hand_grip";
        }
    } else if (row == 5U && activate) {
        changed = UndoMagazineSocketEdit(state.editor);
        action = "undo";
    } else if (row == 6U && activate) {
        changed = ResetMagazineSocketEdit(state.editor);
        action = "reset_loaded_baseline";
    }
    if (!changed) {
        ReleaseSRWLockExclusive(
            &g_magazineSocketAuthoringLock);
        return false;
    }
    const bool saved = SaveMagazineSocketAuthoringEdit(
        state, previous);
    const MagazineSocketAuthoringRuntimeState logState = state;
    ReleaseSRWLockExclusive(&g_magazineSocketAuthoringLock);
    LogMagazineSocketAuthoring(
        saved
            ? "m5_magazine_socket_authoring_applied"
            : "m5_magazine_socket_authoring_save_failed",
        action, logState);
    return saved;
}

void LogLiveMagazineSocketRejected(
    const LiveMagazineSocketCommand* command,
    const char* reason) noexcept {
    if (g_passThroughLog == nullptr) {
        return;
    }
    const MagazineSocketAuthoringRuntimeState state =
        CopyMagazineSocketAuthoringState();
    char detail[640]{};
    std::snprintf(
        detail, sizeof(detail),
        "revision=%llu base_revision=%llu reason=%s "
        "process_id=%lu target_process_id=%lu "
        "target_weapon_index=%ld active_weapon_index=%ld "
        "target_weapon_name=%s active_weapon_name=%s "
        "active_source_generation=%llu active_revision=%llu "
        "phase=1 engine_handoff=none ammo_mutation=0 "
        "weapon_state_mutation=0 retail_state_mutation=0",
        static_cast<unsigned long long>(
            command != nullptr ? command->revision : 0U),
        static_cast<unsigned long long>(
            command != nullptr ? command->baseRevision : 0U),
        reason != nullptr ? reason : "unknown",
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(
            command != nullptr ? command->processId : 0U),
        static_cast<long>(
            command != nullptr ? command->weaponIndex : -1),
        static_cast<long>(state.weaponIndex),
        command != nullptr ? command->weaponName : "unknown",
        state.weaponName[0] != '\0'
            ? state.weaponName : "unknown",
        static_cast<unsigned long long>(
            state.sourceGeneration),
        static_cast<unsigned long long>(
            state.revision));
    g_passThroughLog(
        "m5_live_magazine_socket_rejected", detail);
}

bool ApplyLiveMagazineSocketCommand(
    const LiveMagazineSocketCommand& command) noexcept {
    if (!EnsureMagazineSocketAuthoringIdentity()) {
        LogLiveMagazineSocketRejected(
            &command, "active_identity_unavailable");
        return false;
    }
    AcquireSRWLockExclusive(&g_magazineSocketAuthoringLock);
    MagazineSocketAuthoringRuntimeState& state =
        g_magazineSocketAuthoringState;
    const bool targetMatches =
        command.processId == GetCurrentProcessId() &&
        command.weaponIndex == state.weaponIndex &&
        std::strcmp(command.weaponName, state.weaponName) == 0;
    if (!targetMatches ||
        command.baseRevision != state.revision ||
        command.revision <= state.revision) {
        ReleaseSRWLockExclusive(
            &g_magazineSocketAuthoringLock);
        LogLiveMagazineSocketRejected(
            &command,
            !targetMatches ? "target_mismatch"
                : command.baseRevision != state.revision
                    ? "base_revision_mismatch"
                    : "revision_not_newer");
        return false;
    }
    const MagazineSocketEditorState previous = state.editor;
    PushMagazineSocketUndo(state.editor);
    state.editor.current = command.settings;
    state.lastSaveResult =
        SaveMagazineInsertionSocketSettings(
            state.weaponIndex, state.weaponName,
            state.editor.current);
    if (state.lastSaveResult !=
        WeaponSettingsStoreResult::Ok) {
        state.editor = previous;
        ReleaseSRWLockExclusive(
            &g_magazineSocketAuthoringLock);
        LogLiveMagazineSocketRejected(
            &command, "save_failed");
        return false;
    }
    state.revision = command.revision;
    state.preview = ResolveMagazineSocketSnapPreview(
        state.editor.current, state.cursorModelLocal);
    const MagazineSocketAuthoringRuntimeState logState = state;
    ReleaseSRWLockExclusive(&g_magazineSocketAuthoringLock);
    LogMagazineSocketAuthoring(
        "m5_live_magazine_socket_applied",
        "acknowledged_command", logState);
    return true;
}

void LogSlideGrabAuthoring(
    const char* event,
    const char* action,
    const SlideGrabAuthoringRuntimeState& state) noexcept {
    if (g_passThroughLog == nullptr) return;
    const SlideGrabRailSettings& settings = state.editor.current;
    char detail[1024]{};
    std::snprintf(
        detail, sizeof(detail),
        "pipeline=STATE->DECISION "
        "action=%s weapon_index=%ld weapon_name=%s "
        "source_generation=%llu node_name=%s "
        "configured=%u dirty=%u valid=%u "
        "activation=%s closed=(%.4f,%.4f,%.4f) "
        "axis=(%.6f,%.6f,%.6f) max_travel=%.4f "
        "rear_threshold=%.4f "
        "load=%s save=%s engine_handoff=none "
        "ammo_mutation=0 weapon_state_mutation=0 "
        "retail_state_mutation=0 persisted_node_handle=0",
        action != nullptr ? action : "unknown",
        static_cast<long>(state.weaponIndex),
        state.weaponName[0] != '\0' ? state.weaponName : "unknown",
        static_cast<unsigned long long>(state.sourceGeneration),
        settings.nodeName[0] != '\0' ? settings.nodeName : "unknown",
        settings.configured ? 1U : 0U,
        state.editor.dirty ? 1U : 0U,
        SlideGrabRailSettingsAreValid(settings) ? 1U : 0U,
        SlideGrabActivationInputName(settings.activationInput),
        settings.closedPositionUnits.x,
        settings.closedPositionUnits.y,
        settings.closedPositionUnits.z,
        settings.closedToRearAxis.x,
        settings.closedToRearAxis.y,
        settings.closedToRearAxis.z,
        settings.maximumTravelUnits,
        settings.rearThresholdUnits,
        WeaponSettingsStoreResultName(state.lastLoadResult),
        WeaponSettingsStoreResultName(state.lastSaveResult));
    g_passThroughLog(event, detail);
}

bool EnsureSlideGrabAuthoringIdentity() noexcept {
    ToolMenuMeleeTelemetry telemetry{};
    std::int32_t weaponIndex = -1;
    std::uint64_t sourceGeneration = 0U;
    if (!ResolveMagazineSocketAuthoringIdentity(
            telemetry, weaponIndex, sourceGeneration)) {
        AcquireSRWLockExclusive(&g_slideGrabAuthoringLock);
        g_slideGrabAuthoringState.visualReady = false;
        ReleaseSRWLockExclusive(&g_slideGrabAuthoringLock);
        return false;
    }
    AcquireSRWLockShared(&g_slideGrabAuthoringLock);
    const bool alreadyBound =
        g_slideGrabAuthoringState.weaponIndex == weaponIndex &&
        g_slideGrabAuthoringState.sourceGeneration == sourceGeneration &&
        std::strcmp(
            g_slideGrabAuthoringState.weaponName,
            telemetry.weaponName) == 0;
    const bool alreadyReady =
        g_slideGrabAuthoringState.identityReady;
    ReleaseSRWLockShared(&g_slideGrabAuthoringLock);
    if (alreadyBound) return alreadyReady;

    SlideGrabRailSettings loaded{};
    const WeaponSettingsStoreResult loadResult =
        LoadSlideGrabRailSettings(
            weaponIndex, telemetry.weaponName, loaded);
    const bool coltSeedAvailable =
        weaponIndex == kColtSlideGrabWeaponIndex &&
        std::strcmp(
            telemetry.weaponName,
            kColtSlideGrabWeaponName) == 0;
    const bool loadAccepted =
        loadResult == WeaponSettingsStoreResult::Ok ||
        (loadResult == WeaponSettingsStoreResult::NotFound &&
         coltSeedAvailable);
    if (loadResult == WeaponSettingsStoreResult::NotFound &&
        coltSeedAvailable) {
        loaded = ColtSlideGrabSeedSettings();
    }
    AcquireSRWLockExclusive(&g_slideGrabAuthoringLock);
    g_slideGrabAuthoringState = {};
    g_slideGrabAuthoringState.weaponIndex = weaponIndex;
    g_slideGrabAuthoringState.sourceGeneration = sourceGeneration;
    strcpy_s(
        g_slideGrabAuthoringState.weaponName,
        telemetry.weaponName);
    g_slideGrabAuthoringState.lastLoadResult = loadResult;
    g_slideGrabAuthoringState.identityReady = loadAccepted;
    g_slideGrabAuthoringState.invalidRecord =
        loadResult != WeaponSettingsStoreResult::Ok &&
        loadResult != WeaponSettingsStoreResult::NotFound;
    SetSlideGrabEditorValue(
        g_slideGrabAuthoringState.editor, loaded, true);
    const SlideGrabAuthoringRuntimeState logState =
        g_slideGrabAuthoringState;
    ReleaseSRWLockExclusive(&g_slideGrabAuthoringLock);
    LogSlideGrabAuthoring(
        loadAccepted
            ? "m5_slide_grab_settings_loaded"
            : "m5_slide_grab_settings_unavailable",
        loadResult == WeaponSettingsStoreResult::Ok
            ? "weapon_record"
            : coltSeedAvailable ? "verified_colt_seed"
                                : "fail_closed",
        logState);
    return loadAccepted;
}

SlideGrabAuthoringRuntimeState
CopySlideGrabAuthoringState() noexcept {
    AcquireSRWLockShared(&g_slideGrabAuthoringLock);
    const SlideGrabAuthoringRuntimeState copy =
        g_slideGrabAuthoringState;
    ReleaseSRWLockShared(&g_slideGrabAuthoringLock);
    return copy;
}

void UpdateSlideGrabAuthoringVisual(
    std::int32_t weaponIndex,
    std::uint64_t sourceGeneration,
    const PhysicalMeleeRigidTransform& modelWorld,
    const PhysicalMeleeRigidTransform& cursorWorld,
    bool posesFresh) noexcept {
    if (!EnsureSlideGrabAuthoringIdentity()) return;
    PhysicalMeleeRigidTransform cursorModelLocal{};
    const bool visualReady = posesFresh &&
        ResolveMagazineSocketCursorModelLocal(
            modelWorld, cursorWorld, cursorModelLocal);
    AcquireSRWLockExclusive(&g_slideGrabAuthoringLock);
    if (g_slideGrabAuthoringState.weaponIndex != weaponIndex ||
        g_slideGrabAuthoringState.sourceGeneration !=
            sourceGeneration) {
        g_slideGrabAuthoringState.visualReady = false;
        ReleaseSRWLockExclusive(&g_slideGrabAuthoringLock);
        return;
    }
    g_slideGrabAuthoringState.visualReady = visualReady;
    if (visualReady) {
        g_slideGrabAuthoringState.modelWorld = modelWorld;
        g_slideGrabAuthoringState.cursorModelLocal =
            cursorModelLocal;
        g_slideGrabAuthoringState.visualTick =
            GetTickCount64();
    }
    ReleaseSRWLockExclusive(&g_slideGrabAuthoringLock);
}

bool ApplyToolMenuSlideGrabAdjustment(
    std::uint32_t row,
    int delta,
    bool activate) noexcept {
    if (InterlockedCompareExchange(
            &g_weaponGripCalibrationEnabled, 0, 0) == 0 ||
        !EnsureSlideGrabAuthoringIdentity()) return false;
    AcquireSRWLockExclusive(&g_slideGrabAuthoringLock);
    SlideGrabAuthoringRuntimeState& state =
        g_slideGrabAuthoringState;
    if (!state.identityReady) {
        ReleaseSRWLockExclusive(&g_slideGrabAuthoringLock);
        return false;
    }
    bool changed = false;
    const char* action = "none";
    if (row == 1U && (delta != 0 || activate)) {
        CycleSlideGrabComponent(
            state.editor, delta != 0 ? delta : 1);
        changed = true;
        action = "component_selected";
    } else if (row == 2U && delta != 0) {
        changed = AdjustSlideGrabComponent(state.editor, delta);
        action = "component_adjusted_unsaved";
    } else if (row == 3U && (delta != 0 || activate)) {
        state.editor.coarse = !state.editor.coarse;
        changed = true;
        action = "movement_step_changed";
    } else if (row == 4U && activate) {
        const bool fresh = state.visualReady &&
            GetTickCount64() - state.visualTick <=
                kHeadAimFreshnessMilliseconds;
        if (fresh) {
            changed = CaptureSlideGrabFromController(
                state.editor, state.cursorModelLocal);
            action = "captured_grab_volume_and_hand_pose_unsaved";
        }
    } else if (row == 5U && activate) {
        changed = UndoSlideGrabEdit(state.editor);
        action = "undo";
    } else if (row == 6U && activate) {
        changed = ResetSlideGrabEdit(state.editor);
        action = "reset_loaded_settings";
    } else if (row == 7U && activate &&
               state.editor.dirty &&
               state.editor.current.configured &&
               SlideGrabRailSettingsAreValid(
                   state.editor.current)) {
        state.lastSaveResult = SaveSlideGrabRailSettings(
            state.weaponIndex, state.weaponName,
            state.editor.current);
        if (state.lastSaveResult ==
            WeaponSettingsStoreResult::Ok) {
            state.editor.baseline = state.editor.current;
            state.editor.baselineAvailable = true;
            state.editor.dirty = false;
            state.editor.undoCount = 0U;
            changed = true;
            action = "saved_exact_weapon_record";
        } else {
            changed = true;
            action = "save_failed_unsaved_retained";
        }
    }
    const SlideGrabAuthoringRuntimeState logState = state;
    ReleaseSRWLockExclusive(&g_slideGrabAuthoringLock);
    if (changed) {
        LogSlideGrabAuthoring(
            "m5_slide_grab_authoring_changed",
            action, logState);
    }
    return changed;
}

struct SlideGrabRuntimeOutput {
    PhysicalMeleeRigidTransform handTargetWorld{};
    SlideGrabFrameResult frame{};
    bool handTargetReady{false};
};

void LogSlideGrabRuntimeTransition(
    const SlideGrabAuthoringRuntimeState& authoring,
    const SlideGrabFrameInput& input,
    const SlideGrabFrameResult& result,
    const SlideGrabRuntimeOutput& output,
    bool nodeControlResult) noexcept {
    if (g_passThroughLog == nullptr ||
        (!result.stateChanged &&
         !result.requestNodeControlAttach &&
         !result.requestNodeControlDetach)) {
        return;
    }
    char detail[1400]{};
    std::snprintf(
        detail, sizeof(detail),
        "pipeline=INPUT->TRANSFORM->STATE->DECISION->ENGINE_HANDOFF->RESULT "
        "weapon_index=%ld weapon_name=%s source_generation=%llu "
        "node_name=%s node_resolution=%s controller=off_hand_left "
        "activation_input=%s grip=%.3f trigger=%.3f "
        "overlap=%u controller_model_local=(%.4f,%.4f,%.4f) "
        "state=%u projected_travel=%.4f clamped_travel=%.4f "
        "hand_target=(%.4f,%.4f,%.4f) "
        "hand_target_basis=authored_grip_plus_left_ik_calibration "
        "node_control_request=%s node_control_result=%s "
        "detach_reason=%s retail_ownership_restored=%u "
        "retail_attack_suppressed_grip=%u "
        "retail_attack_suppressed_trigger=%u",
        static_cast<long>(input.weaponIndex),
        authoring.weaponName[0] != '\0'
            ? authoring.weaponName : "unknown",
        static_cast<unsigned long long>(input.sourceGeneration),
        input.settings.nodeName,
        input.nodeResolved ? "ok" : "failed",
        SlideGrabActivationInputName(
            input.settings.activationInput),
        input.gripValue, input.triggerValue,
        result.overlap ? 1U : 0U,
        input.controllerModelLocal.x,
        input.controllerModelLocal.y,
        input.controllerModelLocal.z,
        static_cast<unsigned int>(result.state),
        result.projection.projectedTravelUnits,
        result.projection.clampedTravelUnits,
        output.handTargetWorld.positionUnits.x,
        output.handTargetWorld.positionUnits.y,
        output.handTargetWorld.positionUnits.z,
        result.requestNodeControlAttach
            ? "attach"
            : result.requestNodeControlDetach
                ? "detach" : "none",
        nodeControlResult ? "ok" : "none_or_failed",
        SlideGrabDetachReasonName(result.reason),
        result.requestNodeControlDetach ? 1U : 0U,
        result.captureGrip ? 1U : 0U,
        result.captureTrigger ? 1U : 0U);
    g_passThroughLog("m5_slide_grab_transition", detail);
}

bool ResolveSlideGrabSoundPath(
    SlideGrabSoundCue cue,
    wchar_t (&path)[MAX_PATH]) noexcept {
    const wchar_t* relativePath = nullptr;
    switch (cue) {
    case SlideGrabSoundCue::Pull:
        relativePath =
            L"sounds\\colt45_slide_pull.wav";
        break;
    case SlideGrabSoundCue::Return:
        relativePath =
            L"sounds\\colt45_slide_return.wav";
        break;
    default:
        return false;
    }

    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(
                &ResolveSlideGrabSoundPath),
            &module)) {
        return false;
    }
    const DWORD length = GetModuleFileNameW(
        module, path, MAX_PATH);
    if (length == 0U || length >= MAX_PATH) {
        return false;
    }
    wchar_t* const separator = wcsrchr(path, L'\\');
    if (separator == nullptr) {
        return false;
    }
    *(separator + 1) = L'\0';
    if (wcscat_s(path, relativePath) != 0) {
        return false;
    }
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U;
}

void ApplySlideGrabSoundCue(
    const SlideGrabSoundCueResult& sound,
    const SlideGrabAuthoringRuntimeState& authoring,
    const SlideGrabFrameInput& input,
    const SlideGrabFrameResult& frame,
    bool retailOwnershipRestored) noexcept {
    if (sound.cue == SlideGrabSoundCue::None &&
        !sound.stopPlayback) {
        return;
    }

    wchar_t path[MAX_PATH]{};
    bool available = false;
    BOOL handoffResult = FALSE;
    const char* asset = "none";
    if (sound.stopPlayback) {
        handoffResult = PlaySoundW(nullptr, nullptr, 0U);
    } else {
        available = ResolveSlideGrabSoundPath(
            sound.cue, path);
        asset = sound.cue == SlideGrabSoundCue::Pull
            ? "sounds/colt45_slide_pull.wav"
            : "sounds/colt45_slide_return.wav";
        if (available) {
            handoffResult = PlaySoundW(
                path, nullptr,
                SND_ASYNC | SND_FILENAME | SND_NODEFAULT);
        }
    }

    if (g_passThroughLog != nullptr) {
        char detail[900]{};
        std::snprintf(
            detail, sizeof(detail),
            "pipeline=STATE->DECISION->WINDOWS_AUDIO_HANDOFF->RESULT "
            "weapon_index=%ld weapon_name=%s source_generation=%llu "
            "cue=%s action=%s pull_cycle=%u travel=%.4f "
            "rear_threshold=%.4f asset=%s "
            "asset_available=%u handoff_request=%u handoff_result=%u "
            "detach_reason=%s retail_ownership_restored=%u",
            static_cast<long>(input.weaponIndex),
            authoring.weaponName[0] != '\0'
                ? authoring.weaponName : "unknown",
            static_cast<unsigned long long>(
                input.sourceGeneration),
            SlideGrabSoundCueName(sound.cue),
            sound.stopPlayback ? "stop" : "play_once",
            static_cast<unsigned int>(sound.pullCycle),
            sound.travelUnits,
            input.settings.rearThresholdUnits, asset,
            available ? 1U : 0U,
            (!sound.stopPlayback && available) ||
                    sound.stopPlayback
                ? 1U : 0U,
            handoffResult ? 1U : 0U,
            SlideGrabDetachReasonName(frame.reason),
            retailOwnershipRestored ? 1U : 0U);
        g_passThroughLog("m5_slide_grab_sound", detail);
    }
}

SlideGrabRuntimeOutput UpdateSlideGrabRuntime(
    const LiveEquippedWeaponVisualSource& source,
    const PhysicalMeleeRigidTransform& modelWorld,
    const PhysicalMeleeRigidTransform& offHandWorld,
    const FearVrInputState& controllerInput,
    bool modelWorldReady,
    bool offHandTrackingFresh) noexcept {
    SlideGrabRuntimeOutput output{};
    const bool authoringReady =
        EnsureSlideGrabAuthoringIdentity();
    const SlideGrabAuthoringRuntimeState authoring =
        CopySlideGrabAuthoringState();
    const SlideGrabRailSettings settings =
        authoring.editor.dirty &&
            authoring.editor.baselineAvailable
        ? authoring.editor.baseline
        : authoring.editor.current;
    PhysicalMeleeRigidTransform controllerModelLocal{};
    const bool transformReady = modelWorldReady &&
        offHandTrackingFresh &&
        ResolveMagazineSocketCursorModelLocal(
            modelWorld, offHandWorld,
            controllerModelLocal);
    SlideGrabState priorState = SlideGrabState::Idle;
    AcquireSRWLockShared(&g_slideGrabRuntimeLock);
    priorState = g_slideGrabStateMachine.state;
    ReleaseSRWLockShared(&g_slideGrabRuntimeLock);
    const bool possibleCandidate =
        transformReady && settings.configured &&
        SlideGrabRailSettingsAreValid(settings) &&
        SlideGrabVolumeContains(
            settings, controllerModelLocal.positionUnits);
    const bool sourceMatches =
        authoringReady && source.live &&
        authoring.weaponIndex == source.weaponIndex &&
        authoring.sourceGeneration == source.sourceGeneration &&
        authoring.weaponIndex == kColtSlideGrabWeaponIndex &&
        std::strcmp(
            authoring.weaponName,
            kColtSlideGrabWeaponName) == 0;
    bool nodeResolved = false;
    if (sourceMatches && (possibleCandidate ||
        priorState == SlideGrabState::Candidate ||
        priorState == SlideGrabState::Attached)) {
        nodeResolved = PrepareSlideNodeControlSource(
            source.modelObject, source.weaponIndex,
            source.sourceGeneration, settings);
    }
    const SlideNodeControlStatus nodeStatus =
        ReadSlideNodeControlStatus();
    if (nodeStatus.sourceResolved &&
        nodeStatus.weaponIndex == source.weaponIndex &&
        nodeStatus.sourceGeneration == source.sourceGeneration) {
        nodeResolved = true;
    }
    SlideGrabFrameInput input{};
    input.settings = settings;
    input.controllerModelLocal =
        controllerModelLocal.positionUnits;
    input.weaponIndex = source.weaponIndex;
    input.sourceGeneration = source.sourceGeneration;
    input.trackingFresh = offHandTrackingFresh &&
        fearvr::IsInputStateUsable(controllerInput, true);
    input.focused = GameOwnsForegroundWindow() &&
        (controllerInput.flags & FEARVR_IF_FOCUSED) != 0U;
    input.gamePlaying = BindingInputAllowsSlideGrab();
    input.exactWeaponIdentity = sourceMatches;
    input.modelAvailable = source.live &&
        source.modelObject != nullptr;
    input.nodeResolved = nodeResolved;
    input.transformValid = transformReady;
    input.toolMenuOpen = VrToolMenuIsOpen();
    input.retailAnimationIncompatible =
        nodeStatus.retailAnimationIncompatible;
    input.gripValue = controllerInput.squeeze[
        FEARVR_HAND_LEFT];
    input.triggerValue = controllerInput.trigger[
        FEARVR_HAND_LEFT];

    AcquireSRWLockExclusive(&g_slideGrabRuntimeLock);
    output.frame = UpdateSlideGrabStateMachine(
        g_slideGrabStateMachine, input);
    ReleaseSRWLockExclusive(&g_slideGrabRuntimeLock);
    bool nodeControlResult = false;
    if (output.frame.requestNodeControlAttach &&
        output.frame.projection.valid) {
        nodeControlResult = BeginSlideNodeControl(
            source.modelObject, source.weaponIndex,
            source.sourceGeneration, settings,
            output.frame.projection.slidePositionModelLocal,
            controllerInput.sampleId);
        if (!nodeControlResult) {
            AcquireSRWLockExclusive(&g_slideGrabRuntimeLock);
            RejectSlideGrabNodeControl(
                g_slideGrabStateMachine, output.frame);
            ReleaseSRWLockExclusive(&g_slideGrabRuntimeLock);
        }
    }
    if (output.frame.state == SlideGrabState::Attached &&
        output.frame.projection.valid) {
        nodeControlResult = UpdateSlideNodeControlTarget(
            source.modelObject, source.sourceGeneration,
            output.frame.projection.slidePositionModelLocal,
            output.frame.projection.projectedTravelUnits,
            output.frame.projection.clampedTravelUnits,
            controllerInput.sampleId);
        if (!nodeControlResult) {
            AcquireSRWLockExclusive(&g_slideGrabRuntimeLock);
            RejectSlideGrabNodeControl(
                g_slideGrabStateMachine, output.frame);
            ReleaseSRWLockExclusive(&g_slideGrabRuntimeLock);
        } else {
            PhysicalMeleeRigidTransform authoredGripWorld{};
            if (ComposePhysicalMeleeRigidTransforms(
                    modelWorld,
                    output.frame.projection.handTargetModelLocal,
                    authoredGripWorld)) {
                // AUTHOR captures the physical OpenXR left-grip pose. Feed
                // that pose through the same controller-local wrist
                // correction as the ordinary off-hand path before publishing
                // an IK target; bypassing it makes a correctly captured grip
                // look like an incorrectly rotated/translated hand.
                output.handTargetWorld =
                    ResolveToolMenuLeftHandIkTarget(
                        authoredGripWorld, ReadArmIkTuning());
                output.handTargetReady =
                    PhysicalMeleeRigidTransformIsValid(
                        output.handTargetWorld);
            }
        }
    }
    if (output.frame.requestNodeControlDetach ||
        output.frame.state == SlideGrabState::Released) {
        nodeControlResult = EndSlideNodeControl(
            SlideGrabDetachReasonName(output.frame.reason));
        output.handTargetReady = false;
    }
    SlideGrabSoundCueResult sound{};
    AcquireSRWLockExclusive(&g_slideGrabRuntimeLock);
    sound = UpdateSlideGrabSoundCueState(
        g_slideGrabSoundCueState, output.frame,
        input.settings.rearThresholdUnits,
        nodeControlResult);
    ReleaseSRWLockExclusive(&g_slideGrabRuntimeLock);
    ApplySlideGrabSoundCue(
        sound, authoring, input, output.frame,
        output.frame.requestNodeControlDetach &&
            nodeControlResult);
    InterlockedExchange(
        &g_slideGrabCaptureGrip,
        output.frame.captureGrip ? 1 : 0);
    InterlockedExchange(
        &g_slideGrabCaptureTrigger,
        output.frame.captureTrigger ? 1 : 0);
    LogSlideGrabRuntimeTransition(
        authoring, input, output.frame,
        output, nodeControlResult);
    return output;
}

bool SameFileTime(const FILETIME& left, const FILETIME& right) noexcept {
    return left.dwLowDateTime == right.dwLowDateTime &&
        left.dwHighDateTime == right.dwHighDateTime;
}

void LogLiveColliderAlignment(
    const char* event,
    std::uint64_t revision,
    std::int32_t weaponIndex,
    const ToolMenuColliderSettings& settings,
    const char* result) noexcept {
    if (g_passThroughLog == nullptr) {
        return;
    }
    char detail[512]{};
    std::snprintf(
        detail, sizeof(detail),
        "revision=%llu process_id=%lu weapon_index=%ld "
        "position_x=%.3f position_y=%.3f position_z=%.3f "
        "rotation_x=%.3f rotation_y=%.3f rotation_z=%.3f "
        "length=%.3f radius=%.3f reversed=%u result=%s",
        static_cast<unsigned long long>(revision),
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<long>(weaponIndex),
        settings.positionOffsetUnits.x,
        settings.positionOffsetUnits.y,
        settings.positionOffsetUnits.z,
        settings.rotationOffsetDegrees.x,
        settings.rotationOffsetDegrees.y,
        settings.rotationOffsetDegrees.z,
        settings.lengthUnits,
        settings.radiusUnits,
        settings.reversed ? 1U : 0U,
        result != nullptr ? result : "unknown");
    g_passThroughLog(event, detail);
}

void LogLiveColliderAlignmentRejected(
    std::uint64_t revision,
    const char* reason,
    std::uint32_t targetProcessId,
    std::int32_t targetWeaponIndex,
    std::int32_t activeWeaponIndex) noexcept {
    if (g_passThroughLog == nullptr) {
        return;
    }
    char detail[320]{};
    std::snprintf(
        detail, sizeof(detail),
        "revision=%llu reason=%s process_id=%lu "
        "target_process_id=%lu target_weapon_index=%ld "
        "active_weapon_index=%ld",
        static_cast<unsigned long long>(revision),
        reason != nullptr ? reason : "unknown",
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(targetProcessId),
        static_cast<long>(targetWeaponIndex),
        static_cast<long>(activeWeaponIndex));
    g_passThroughLog("m5_live_collider_alignment_rejected", detail);
}

bool ResolveLiveColliderCommandPath() noexcept {
    if (g_liveColliderCommandPathResolved) {
        return g_liveColliderCommandPathAvailable;
    }
    g_liveColliderCommandPathResolved = true;
    const DWORD length = GetEnvironmentVariableW(
        kLiveColliderCommandPathEnvironment,
        g_liveColliderCommandPath,
        static_cast<DWORD>(
            sizeof(g_liveColliderCommandPath) /
            sizeof(g_liveColliderCommandPath[0])));
    g_liveColliderCommandPathAvailable =
        length > 0U &&
        length < sizeof(g_liveColliderCommandPath) /
            sizeof(g_liveColliderCommandPath[0]);
    if (!g_liveColliderCommandPathAvailable) {
        g_liveColliderCommandPath[0] = L'\0';
    }
    return g_liveColliderCommandPathAvailable;
}

bool ReadLiveColliderCommandText(
    char (&text)[1024]) noexcept {
    text[0] = '\0';
    HANDLE file = CreateFileW(
        g_liveColliderCommandPath,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    DWORD bytesRead = 0U;
    const bool readable =
        GetFileSizeEx(file, &size) != FALSE &&
        size.QuadPart > 0 &&
        size.QuadPart < static_cast<LONGLONG>(sizeof(text)) &&
        ReadFile(
            file, text, static_cast<DWORD>(size.QuadPart),
            &bytesRead, nullptr) != FALSE &&
        bytesRead == static_cast<DWORD>(size.QuadPart);
    CloseHandle(file);
    if (!readable) {
        text[0] = '\0';
        return false;
    }
    text[bytesRead] = '\0';
    return true;
}

void ProcessLiveColliderAlignmentCommand(
    std::int32_t weaponIndex,
    ToolMenuColliderSettings& settings) noexcept {
    AcquireSRWLockExclusive(&g_liveColliderCommandLock);
    if (!ResolveLiveColliderCommandPath()) {
        ReleaseSRWLockExclusive(&g_liveColliderCommandLock);
        return;
    }
    if (weaponIndex >= 0 &&
        g_liveColliderCommandArmedWeaponIndex != weaponIndex) {
        g_liveColliderCommandArmedWeaponIndex = weaponIndex;
        LogLiveColliderAlignment(
            "m5_live_collider_alignment_armed",
            g_liveColliderCommandLastRevision,
            weaponIndex, settings, "ready");
    }
    const ULONGLONG now = GetTickCount64();
    if (now - g_liveColliderCommandLastPollTick <
        kLiveColliderCommandPollMilliseconds) {
        ReleaseSRWLockExclusive(&g_liveColliderCommandLock);
        return;
    }
    g_liveColliderCommandLastPollTick = now;
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (GetFileAttributesExW(
            g_liveColliderCommandPath,
            GetFileExInfoStandard,
            &attributes) == FALSE ||
        SameFileTime(
            attributes.ftLastWriteTime,
            g_liveColliderCommandLastWriteTime)) {
        ReleaseSRWLockExclusive(&g_liveColliderCommandLock);
        return;
    }
    g_liveColliderCommandLastWriteTime =
        attributes.ftLastWriteTime;
    char text[1024]{};
    if (!ReadLiveColliderCommandText(text)) {
        LogLiveColliderAlignmentRejected(
            0U, "read_failed", 0U, -1, weaponIndex);
        ReleaseSRWLockExclusive(&g_liveColliderCommandLock);
        return;
    }
    if (std::strncmp(text, "version=2 ", 10U) == 0) {
        LiveMagazineSocketCommand magazineCommand{};
        const LiveMagazineSocketCommandParseResult parseResult =
            ParseLiveMagazineSocketCommand(
                text, magazineCommand);
        if (parseResult !=
            LiveMagazineSocketCommandParseResult::Ok) {
            const LiveMagazineSocketCommand* const rejectedCommand =
                parseResult == LiveMagazineSocketCommandParseResult::InvalidValue
                    ? &magazineCommand : nullptr;
            LogLiveMagazineSocketRejected(
                rejectedCommand,
                LiveMagazineSocketCommandParseResultName(
                    parseResult));
            ReleaseSRWLockExclusive(
                &g_liveColliderCommandLock);
            return;
        }
        if (magazineCommand.revision <=
            g_liveMagazineSocketCommandLastRevision) {
            ReleaseSRWLockExclusive(
                &g_liveColliderCommandLock);
            return;
        }
        g_liveMagazineSocketCommandLastRevision =
            magazineCommand.revision;
        ApplyLiveMagazineSocketCommand(magazineCommand);
        ReleaseSRWLockExclusive(
            &g_liveColliderCommandLock);
        return;
    }
    LiveColliderAlignmentCommand command{};
    const LiveColliderAlignmentCommandParseResult parseResult =
        ParseLiveColliderAlignmentCommand(text, command);
    if (parseResult !=
        LiveColliderAlignmentCommandParseResult::Ok) {
        LogLiveColliderAlignmentRejected(
            0U,
            LiveColliderAlignmentCommandParseResultName(parseResult),
            0U, -1, weaponIndex);
        ReleaseSRWLockExclusive(&g_liveColliderCommandLock);
        return;
    }
    if (command.revision <=
        g_liveColliderCommandLastRevision) {
        ReleaseSRWLockExclusive(&g_liveColliderCommandLock);
        return;
    }
    g_liveColliderCommandLastRevision = command.revision;
    if (!LiveColliderAlignmentCommandMatchesTarget(
            command, GetCurrentProcessId(), weaponIndex)) {
        LogLiveColliderAlignmentRejected(
            command.revision, "target_mismatch",
            command.processId, command.weaponIndex, weaponIndex);
        ReleaseSRWLockExclusive(&g_liveColliderCommandLock);
        return;
    }
    if (!StoreToolMenuColliderSettings(
            weaponIndex, command.settings)) {
        LogLiveColliderAlignmentRejected(
            command.revision, "store_failed",
            command.processId, command.weaponIndex, weaponIndex);
        ReleaseSRWLockExclusive(&g_liveColliderCommandLock);
        return;
    }
    settings = command.settings;
    LogLiveColliderAlignment(
        "m5_live_collider_alignment_applied",
        command.revision, weaponIndex, settings, "applied");
    ReleaseSRWLockExclusive(&g_liveColliderCommandLock);
}

ToolMenuRightHandIkSettings CopyToolMenuRightHandIkSettings(
    std::int32_t weaponIndex) noexcept {
    ToolMenuRightHandIkSettings settings{};
    if (weaponIndex < 0) {
        return settings;
    }
    const PhysicalMeleeProfile baseProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    WeaponSettingsStoreResult loadResult =
        WeaponSettingsStoreResult::NotFound;
    bool loadAttempted = false;
    bool inheritedPipeBaseline = false;
    AcquireSRWLockExclusive(&g_toolMenuSettingsLock);
    ToolMenuWeaponSettingsSlot* slot =
        ResolveToolMenuWeaponSettingsSlot(
            g_toolMenuWeaponSettingsRegistry,
            weaponIndex, baseProfile);
    if (slot != nullptr) {
        if (!slot->rightHandIkPersistentLoadAttempted) {
            slot->rightHandIkPersistentLoadAttempted = true;
            ToolMenuRightHandIkSettings persisted{};
            loadResult =
                LoadWeaponRightHandIkSettingsWithPipeOneHandedFallback(
                    weaponIndex, baseProfile.id, persisted,
                    inheritedPipeBaseline);
            if (loadResult == WeaponSettingsStoreResult::Ok) {
                slot->rightHandIkSettings = persisted;
            }
            loadAttempted = true;
        }
        settings = slot->rightHandIkSettings;
    }
    ReleaseSRWLockExclusive(&g_toolMenuSettingsLock);
    if (loadAttempted && g_passThroughLog != nullptr) {
        char detail[224]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld profile=%s result=%s source=%s",
            static_cast<long>(weaponIndex),
            PhysicalMeleeProfileName(baseProfile.id),
            WeaponSettingsStoreResultName(loadResult),
            loadResult == WeaponSettingsStoreResult::Ok
                ? inheritedPipeBaseline
                    ? "pipe_baseline" : "weapon_record"
                : "zero_offsets");
        g_passThroughLog("m5_right_hand_ik_settings_loaded", detail);
    }
    return settings;
}

bool StoreToolMenuRightHandIkSettings(
    std::int32_t weaponIndex,
    const ToolMenuRightHandIkSettings& settings) noexcept {
    if (weaponIndex < 0 ||
        !ToolMenuRightHandIkSettingsAreValid(settings)) {
        return false;
    }
    const PhysicalMeleeProfile baseProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    bool stored = false;
    AcquireSRWLockExclusive(&g_toolMenuSettingsLock);
    ToolMenuWeaponSettingsSlot* slot =
        ResolveToolMenuWeaponSettingsSlot(
            g_toolMenuWeaponSettingsRegistry,
            weaponIndex, baseProfile);
    if (slot != nullptr) {
        slot->rightHandIkSettings = settings;
        slot->rightHandIkPersistentLoadAttempted = true;
        stored = true;
    }
    ReleaseSRWLockExclusive(&g_toolMenuSettingsLock);
    if (stored) {
        const WeaponSettingsStoreResult saveResult =
            SaveWeaponRightHandIkSettings(
                weaponIndex, baseProfile.id, settings);
        if (g_passThroughLog != nullptr) {
            char detail[256]{};
            std::snprintf(
                detail, sizeof(detail),
                "weapon_index=%ld profile=%s result=%s "
                "position_units=(%.3f,%.3f,%.3f) "
                "rotation_degrees=(%.2f,%.2f,%.2f)",
                static_cast<long>(weaponIndex),
                PhysicalMeleeProfileName(baseProfile.id),
                WeaponSettingsStoreResultName(saveResult),
                settings.positionOffsetUnits.x,
                settings.positionOffsetUnits.y,
                settings.positionOffsetUnits.z,
                settings.rotationOffsetDegrees.x,
                settings.rotationOffsetDegrees.y,
                settings.rotationOffsetDegrees.z);
            g_passThroughLog(
                saveResult == WeaponSettingsStoreResult::Ok
                    ? "m5_right_hand_ik_settings_saved"
                    : "m5_right_hand_ik_settings_save_failed",
                detail);
        }
    }
    return stored;
}

bool ToolMenuUsesEmptyRightHandAlignmentPage() noexcept {
    return EmptyRightHandAlignmentIsActive(
               g_emptyRightHandAlignmentState) ||
        g_rightHandIkTargetSource !=
            RightHandIkTargetSource::WeaponWeightedAim;
}

const char* EmptyRightHandAlignmentEventName(
    EmptyRightHandAlignmentEvent event) noexcept {
    switch (event) {
    case EmptyRightHandAlignmentEvent::ReferenceCaptured:
        return "reference_captured";
    case EmptyRightHandAlignmentEvent::Completed:
        return "completed";
    case EmptyRightHandAlignmentEvent::PoseUnavailable:
        return "pose_unavailable";
    case EmptyRightHandAlignmentEvent::SolveRejected:
        return "solve_rejected";
    case EmptyRightHandAlignmentEvent::None:
    default:
        return "none";
    }
}

void LogEmptyRightHandAlignment(
    const char* action,
    const PhysicalMeleeRigidTransform& referencePose = {},
    const PhysicalMeleeRigidTransform& controllerPose = {},
    const char* persistence = "not_attempted") noexcept {
    if (g_passThroughLog == nullptr) {
        return;
    }
    char detail[768]{};
    std::snprintf(
        detail, sizeof(detail),
        "action=%s phase=%s last_event=%s persistence=%s "
        "offset_units=(%.3f,%.3f,%.3f) "
        "offset_q=(%.6f,%.6f,%.6f,%.6f) "
        "reference_position=(%.3f,%.3f,%.3f) "
        "reference_q=(%.6f,%.6f,%.6f,%.6f) "
        "controller_position=(%.3f,%.3f,%.3f) "
        "controller_q=(%.6f,%.6f,%.6f,%.6f)",
        action,
        EmptyRightHandAlignmentPhaseName(
            g_emptyRightHandAlignmentState.phase),
        EmptyRightHandAlignmentEventName(
            g_emptyRightHandAlignmentLastEvent),
        persistence,
        g_emptyRightHandAlignmentSettings.localPositionOffsetUnits.x,
        g_emptyRightHandAlignmentSettings.localPositionOffsetUnits.y,
        g_emptyRightHandAlignmentSettings.localPositionOffsetUnits.z,
        g_emptyRightHandAlignmentSettings.localRotationOffset.x,
        g_emptyRightHandAlignmentSettings.localRotationOffset.y,
        g_emptyRightHandAlignmentSettings.localRotationOffset.z,
        g_emptyRightHandAlignmentSettings.localRotationOffset.w,
        referencePose.positionUnits.x,
        referencePose.positionUnits.y,
        referencePose.positionUnits.z,
        referencePose.rotation.x,
        referencePose.rotation.y,
        referencePose.rotation.z,
        referencePose.rotation.w,
        controllerPose.positionUnits.x,
        controllerPose.positionUnits.y,
        controllerPose.positionUnits.z,
        controllerPose.rotation.x,
        controllerPose.rotation.y,
        controllerPose.rotation.z,
        controllerPose.rotation.w);
    g_passThroughLog("m5_empty_right_hand_alignment", detail);
}

WeaponSettingsStoreResult StoreEmptyRightHandAlignmentSettings(
    const EmptyRightHandAlignmentSettings& settings) noexcept {
    if (!EmptyRightHandAlignmentSettingsAreValid(settings)) {
        return WeaponSettingsStoreResult::InvalidArgument;
    }
    g_emptyRightHandAlignmentSettings = settings;
    g_emptyRightHandAlignmentSettings.localRotationOffset =
        fearvr::Normalize(
            g_emptyRightHandAlignmentSettings.localRotationOffset);
    g_emptyRightHandAlignmentLastSaveResult =
        SaveEmptyRightHandAlignmentSettings(
            g_emptyRightHandAlignmentSettings);
    return g_emptyRightHandAlignmentLastSaveResult;
}

void CancelEmptyRightHandAlignmentMode(
    const char* action) noexcept {
    if (!CancelEmptyRightHandAlignment(
            g_emptyRightHandAlignmentState)) {
        return;
    }
    g_emptyRightHandAlignmentLastEvent =
        EmptyRightHandAlignmentEvent::None;
    LogEmptyRightHandAlignment(action);
}

bool ApplyToolMenuEmptyRightHandAlignmentAction(
    std::uint32_t row,
    int delta,
    bool activate) noexcept {
    if (delta == 0 && !activate) {
        return false;
    }
    if (row == 0U && activate) {
        if (EmptyRightHandAlignmentIsActive(
                g_emptyRightHandAlignmentState)) {
            CancelEmptyRightHandAlignmentMode(
                "vr_tool_menu_cancel");
        } else {
            BeginEmptyRightHandAlignment(
                g_emptyRightHandAlignmentState);
            g_emptyRightHandAlignmentLastEvent =
                EmptyRightHandAlignmentEvent::None;
            LogEmptyRightHandAlignment(
                "vr_tool_menu_start");
        }
        return true;
    }
    if (row == 1U && activate) {
        CancelEmptyRightHandAlignment(
            g_emptyRightHandAlignmentState);
        g_emptyRightHandAlignmentLastEvent =
            EmptyRightHandAlignmentEvent::None;
        const EmptyRightHandAlignmentSettings identity{};
        const WeaponSettingsStoreResult result =
            StoreEmptyRightHandAlignmentSettings(identity);
        ResetArmIkBendMemory();
        LogEmptyRightHandAlignment(
            "vr_tool_menu_reset", {}, {},
            WeaponSettingsStoreResultName(result));
        return true;
    }
    return false;
}

void UpdateEmptyRightHandAlignmentCapture(
    const RightHandIkTargetResult& selectedTarget,
    const PhysicalMeleeRigidTransform& rawControllerPose,
    PhysicalMeleeRigidTransform& displayedHandPose,
    bool rightTriggerDown) noexcept {
    if (!EmptyRightHandAlignmentIsActive(
            g_emptyRightHandAlignmentState)) {
        return;
    }
    if (!GameOwnsForegroundWindow() || !VrToolMenuIsOpen()) {
        CancelEmptyRightHandAlignmentMode(
            "cancel_context_lost");
        return;
    }
    if (selectedTarget.source ==
        RightHandIkTargetSource::WeaponWeightedAim) {
        CancelEmptyRightHandAlignmentMode(
            "cancel_weapon_equipped");
        return;
    }

    const PhysicalMeleeRigidTransform referenceBefore =
        g_emptyRightHandAlignmentState.referenceHandPose;
    const EmptyRightHandAlignmentUpdateResult update =
        UpdateEmptyRightHandAlignment(
            g_emptyRightHandAlignmentState,
            rawControllerPose,
            displayedHandPose,
            selectedTarget.valid &&
                selectedTarget.source ==
                    RightHandIkTargetSource::EmptyGrip,
            rightTriggerDown);
    if (update.event == EmptyRightHandAlignmentEvent::None) {
        return;
    }
    g_emptyRightHandAlignmentLastEvent = update.event;
    if (update.event ==
        EmptyRightHandAlignmentEvent::ReferenceCaptured) {
        LogEmptyRightHandAlignment(
            "right_trigger_reference",
            g_emptyRightHandAlignmentState.referenceHandPose,
            rawControllerPose);
        return;
    }
    if (update.event ==
        EmptyRightHandAlignmentEvent::Completed) {
        const WeaponSettingsStoreResult saveResult =
            StoreEmptyRightHandAlignmentSettings(update.settings);
        displayedHandPose =
            ResolveEmptyRightHandAlignmentTarget(
                rawControllerPose,
                g_emptyRightHandAlignmentSettings);
        ResetArmIkBendMemory();
        LogEmptyRightHandAlignment(
            "right_trigger_controller_solved",
            referenceBefore, rawControllerPose,
            WeaponSettingsStoreResultName(saveResult));
        return;
    }
    LogEmptyRightHandAlignment(
        update.event ==
                EmptyRightHandAlignmentEvent::PoseUnavailable
            ? "right_trigger_pose_unavailable"
            : "right_trigger_solve_rejected",
        referenceBefore, rawControllerPose);
}


const char* HeldObjectAlignmentEventName(
    HeldObjectAlignmentEvent event) noexcept {
    switch (event) {
    case HeldObjectAlignmentEvent::ReferenceCaptured:
        return "reference_captured";
    case HeldObjectAlignmentEvent::Completed:
        return "completed";
    case HeldObjectAlignmentEvent::PoseUnavailable:
        return "pose_unavailable";
    case HeldObjectAlignmentEvent::SourceChanged:
        return "source_changed";
    case HeldObjectAlignmentEvent::SolveRejected:
        return "solve_rejected";
    case HeldObjectAlignmentEvent::None:
    default:
        return "none";
    }
}

bool GuidedAlignmentCapturesTriggers() noexcept {
    return EmptyRightHandAlignmentIsActive(
               g_emptyRightHandAlignmentState) ||
        HeldObjectAlignmentIsActive(g_heldObjectAlignmentState);
}

void LogHeldObjectAlignment(
    const char* action,
    std::int32_t weaponIndex,
    std::uint64_t sourceGeneration,
    const PhysicalMeleeRigidTransform& referenceObject = {},
    const PhysicalMeleeRigidTransform& referenceHand = {},
    const PhysicalMeleeRigidTransform& controllerPose = {},
    const HeldObjectAlignmentSolution* solution = nullptr) noexcept {
    if (g_passThroughLog == nullptr) {
        return;
    }
    const PhysicalMeleeRigidTransform solvedGrip =
        solution != nullptr ? solution->modelLocalGrip :
            PhysicalMeleeRigidTransform{};
    const EmptyRightHandAlignmentSettings solvedHand =
        solution != nullptr ? solution->rightHandAlignment :
            EmptyRightHandAlignmentSettings{};
    char detail[2048]{};
    std::snprintf(
        detail, sizeof(detail),
        "action=%s phase=%s last_event=%s "
        "weapon_index=%ld source_generation=%llu "
        "grip_persistence=%s hand_persistence=%s "
        "collider_persistence=%s "
        "model_local_grip_position=(%.3f,%.3f,%.3f) "
        "model_local_grip_q=(%.6f,%.6f,%.6f,%.6f) "
        "hand_local_position=(%.3f,%.3f,%.3f) "
        "hand_local_q=(%.6f,%.6f,%.6f,%.6f) "
        "reference_object_position=(%.3f,%.3f,%.3f) "
        "reference_object_q=(%.6f,%.6f,%.6f,%.6f) "
        "reference_hand_position=(%.3f,%.3f,%.3f) "
        "reference_hand_q=(%.6f,%.6f,%.6f,%.6f) "
        "controller_position=(%.3f,%.3f,%.3f) "
        "controller_q=(%.6f,%.6f,%.6f,%.6f)",
        action != nullptr ? action : "unknown",
        HeldObjectAlignmentPhaseName(
            g_heldObjectAlignmentState.phase),
        HeldObjectAlignmentEventName(
            g_heldObjectAlignmentLastEvent),
        static_cast<long>(weaponIndex),
        static_cast<unsigned long long>(sourceGeneration),
        WeaponSettingsStoreResultName(
            g_heldObjectAlignmentLastGripSaveResult),
        WeaponSettingsStoreResultName(
            g_heldObjectAlignmentLastHandSaveResult),
        WeaponSettingsStoreResultName(
            g_heldObjectAlignmentLastColliderSaveResult),
        solvedGrip.positionUnits.x, solvedGrip.positionUnits.y,
        solvedGrip.positionUnits.z, solvedGrip.rotation.x,
        solvedGrip.rotation.y, solvedGrip.rotation.z,
        solvedGrip.rotation.w,
        solvedHand.localPositionOffsetUnits.x,
        solvedHand.localPositionOffsetUnits.y,
        solvedHand.localPositionOffsetUnits.z,
        solvedHand.localRotationOffset.x,
        solvedHand.localRotationOffset.y,
        solvedHand.localRotationOffset.z,
        solvedHand.localRotationOffset.w,
        referenceObject.positionUnits.x,
        referenceObject.positionUnits.y,
        referenceObject.positionUnits.z,
        referenceObject.rotation.x, referenceObject.rotation.y,
        referenceObject.rotation.z, referenceObject.rotation.w,
        referenceHand.positionUnits.x, referenceHand.positionUnits.y,
        referenceHand.positionUnits.z, referenceHand.rotation.x,
        referenceHand.rotation.y, referenceHand.rotation.z,
        referenceHand.rotation.w, controllerPose.positionUnits.x,
        controllerPose.positionUnits.y, controllerPose.positionUnits.z,
        controllerPose.rotation.x, controllerPose.rotation.y,
        controllerPose.rotation.z, controllerPose.rotation.w);
    g_passThroughLog("m5_guided_held_object_alignment", detail);
}

void CancelHeldObjectAlignmentMode(const char* action) noexcept {
    const std::int32_t weaponIndex =
        g_heldObjectAlignmentState.weaponIndex;
    const std::uint64_t sourceGeneration =
        g_heldObjectAlignmentState.sourceGeneration;
    if (!CancelHeldObjectAlignment(g_heldObjectAlignmentState)) {
        return;
    }
    g_heldObjectAlignmentLastEvent = HeldObjectAlignmentEvent::None;
    ResetPhysicalMeleeWeaponWeight(
        fearvr::WeaponWeightResetReason::enabledChanged);
    ResetArmIkBendMemory();
    LogHeldObjectAlignment(action, weaponIndex, sourceGeneration);
}

bool ResolveActiveHeldObjectWorldPose(
    std::int32_t expectedWeaponIndex,
    std::uint64_t expectedGeneration,
    const PhysicalMeleeRigidTransform& desiredControllerPose,
    PhysicalMeleeRigidTransform& objectWorld) noexcept {
    objectWorld = {};
    PhysicalMeleeGripCalibration calibration{};
    void* weapon = nullptr;
    void* modelObject = nullptr;
    std::int32_t weaponIndex = -1;
    std::uint64_t sourceGeneration = 0;
    if (!CopyActiveWeaponGripCalibration(
            calibration, weapon, weaponIndex, modelObject,
            sourceGeneration) ||
        weaponIndex != expectedWeaponIndex ||
        sourceGeneration != expectedGeneration) {
        return false;
    }
    const PhysicalMeleeVisualProxyTransform solved =
        ResolvePhysicalMeleeHeldModelTransform(
            desiredControllerPose, calibration.positionUnits,
            ResolvePhysicalMeleeGripCalibrationRotation(calibration),
            true);
    if (!solved.active) {
        return false;
    }
    objectWorld = solved.objectWorld;
    return true;
}

bool ResolveActiveHeldObjectVisualDriverPose(
    std::int32_t expectedWeaponIndex,
    std::uint64_t expectedGeneration,
    const PhysicalMeleeRigidTransform& frozenObjectWorld,
    PhysicalMeleeRigidTransform& visualControllerPose) noexcept {
    visualControllerPose = {};
    PhysicalMeleeGripCalibration calibration{};
    void* weapon = nullptr;
    void* modelObject = nullptr;
    std::int32_t weaponIndex = -1;
    std::uint64_t sourceGeneration = 0;
    if (!PhysicalMeleeRigidTransformIsValid(frozenObjectWorld) ||
        !CopyActiveWeaponGripCalibration(
            calibration, weapon, weaponIndex, modelObject,
            sourceGeneration) ||
        weaponIndex != expectedWeaponIndex ||
        sourceGeneration != expectedGeneration) {
        return false;
    }
    const PhysicalMeleeRigidTransform modelLocalGrip{
        calibration.positionUnits,
        ResolvePhysicalMeleeGripCalibrationRotation(calibration)};
    return ComposePhysicalMeleeRigidTransforms(
        frozenObjectWorld, modelLocalGrip,
        visualControllerPose);
}

bool ApplyHeldObjectAlignmentSolution(
    std::int32_t expectedWeaponIndex,
    std::uint64_t expectedGeneration,
    const HeldObjectAlignmentSolution& solution,
    const char* action,
    WeaponSettingsStoreResult& gripSaveResult,
    WeaponSettingsStoreResult& handSaveResult,
    WeaponSettingsStoreResult& colliderSaveResult) noexcept {
    gripSaveResult = WeaponSettingsStoreResult::InvalidArgument;
    handSaveResult = WeaponSettingsStoreResult::InvalidArgument;
    colliderSaveResult = WeaponSettingsStoreResult::InvalidArgument;
    PhysicalMeleeGripCalibration calibration{};
    void* weapon = nullptr;
    void* modelObject = nullptr;
    std::int32_t weaponIndex = -1;
    std::uint64_t sourceGeneration = 0;
    if (!CopyActiveWeaponGripCalibration(
            calibration, weapon, weaponIndex, modelObject,
            sourceGeneration) ||
        weaponIndex != expectedWeaponIndex ||
        sourceGeneration != expectedGeneration ||
        !PhysicalMeleeRigidTransformIsValid(
            solution.modelLocalGrip) ||
        !EmptyRightHandAlignmentSettingsAreValid(
            solution.rightHandAlignment)) {
        return false;
    }

    const ToolMenuColliderSettings currentColliderSettings =
        CopyToolMenuColliderSettings(expectedWeaponIndex);
    bool blockColliderUsesAttackFallback = true;
    const ToolMenuColliderSettings currentBlockColliderSettings =
        CopyToolMenuBlockColliderSettings(
            expectedWeaponIndex,
            blockColliderUsesAttackFallback);
    const PhysicalMeleeRigidTransform currentModelLocalGrip{
        calibration.positionUnits,
        ResolvePhysicalMeleeGripCalibrationRotation(calibration)};
    const PhysicalMeleeRigidTransform currentColliderLocal{
        currentColliderSettings.positionOffsetUnits,
        PhysicalMeleeLocalRotationFromDegrees(
            currentColliderSettings.rotationOffsetDegrees)};
    PhysicalMeleeRigidTransform nextColliderLocal{};
    ToolMenuColliderSettings nextColliderSettings =
        currentColliderSettings;
    if (!ToolMenuColliderSettingsAreValid(
            currentColliderSettings) ||
        !RebasePhysicalMeleeAttachedLocalTransform(
            currentModelLocalGrip, solution.modelLocalGrip,
            currentColliderLocal, nextColliderLocal) ||
        !PhysicalMeleeLocalRotationDegreesFromQuaternion(
            nextColliderLocal.rotation,
            nextColliderSettings.rotationOffsetDegrees)) {
        return false;
    }
    nextColliderSettings.positionOffsetUnits =
        nextColliderLocal.positionUnits;
    if (!ToolMenuColliderSettingsAreValid(
            nextColliderSettings)) {
        return false;
    }

    ToolMenuColliderSettings nextBlockColliderSettings =
        currentBlockColliderSettings;
    if (!blockColliderUsesAttackFallback) {
        const PhysicalMeleeRigidTransform currentBlockColliderLocal{
            currentBlockColliderSettings.positionOffsetUnits,
            PhysicalMeleeLocalRotationFromDegrees(
                currentBlockColliderSettings.rotationOffsetDegrees)};
        PhysicalMeleeRigidTransform nextBlockColliderLocal{};
        if (!ToolMenuColliderSettingsAreValid(
                currentBlockColliderSettings) ||
            !RebasePhysicalMeleeAttachedLocalTransform(
                currentModelLocalGrip, solution.modelLocalGrip,
                currentBlockColliderLocal,
                nextBlockColliderLocal) ||
            !PhysicalMeleeLocalRotationDegreesFromQuaternion(
                nextBlockColliderLocal.rotation,
                nextBlockColliderSettings.rotationOffsetDegrees)) {
            return false;
        }
        nextBlockColliderSettings.positionOffsetUnits =
            nextBlockColliderLocal.positionUnits;
        if (!ToolMenuColliderSettingsAreValid(
                nextBlockColliderSettings)) {
            return false;
        }
    }

    const fearvr::TrackingQuaternion baseRotation =
        fearvr::Normalize(calibration.baseRotation);
    const fearvr::TrackingQuaternion localGripCorrection =
        fearvr::Multiply(
            fearvr::Conjugate(baseRotation),
            fearvr::Normalize(solution.modelLocalGrip.rotation));
    fearvr::TrackingVector localGripRotationDegrees{};
    if (!PhysicalMeleeLocalRotationDegreesFromQuaternion(
            localGripCorrection, localGripRotationDegrees)) {
        return false;
    }
    ToolMenuRightHandIkSettings rightHandSettings{};
    rightHandSettings.positionOffsetUnits =
        solution.rightHandAlignment.localPositionOffsetUnits;
    if (!PhysicalMeleeLocalRotationDegreesFromQuaternion(
            solution.rightHandAlignment.localRotationOffset,
            rightHandSettings.rotationOffsetDegrees) ||
        !ToolMenuRightHandIkSettingsAreValid(rightHandSettings)) {
        return false;
    }

    const PhysicalMeleeGripCalibration originalCalibration = calibration;
    calibration.positionUnits = solution.modelLocalGrip.positionUnits;
    calibration.localRotationDegrees = localGripRotationDegrees;
    bool gripApplied = false;
    AcquireSRWLockExclusive(&g_physicalMeleeVisualLock);
    const std::int32_t slotIndex =
        g_activeWeaponGripCalibrationSlot;
    if (slotIndex >= 0 &&
        static_cast<std::size_t>(slotIndex) <
            kWeaponGripCalibrationSlotCount) {
        WeaponGripCalibrationSlot& slot =
            g_weaponGripCalibrationSlots[slotIndex];
        if (slot.occupied && slot.weapon == weapon &&
            slot.weaponIndex == expectedWeaponIndex &&
            slot.modelObject == modelObject &&
            g_physicalMeleeVisualSourceGeneration ==
                expectedGeneration) {
            slot.calibration = calibration;
            slot.lastUsed = ++g_weaponGripCalibrationUseSequence;
            g_physicalMeleeVisualModelLocalGripPosition =
                calibration.positionUnits;
            g_physicalMeleeVisualModelLocalGripRotation =
                ResolvePhysicalMeleeGripCalibrationRotation(
                    calibration);
            gripApplied = true;
        }
    }
    ReleaseSRWLockExclusive(&g_physicalMeleeVisualLock);
    if (!gripApplied) {
        return false;
    }

    const PhysicalMeleeProfile profile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(
            expectedWeaponIndex);
    bool attachmentApplied = false;
    AcquireSRWLockExclusive(&g_toolMenuSettingsLock);
    ToolMenuWeaponSettingsSlot* attachmentSlot =
        ResolveToolMenuWeaponSettingsSlot(
            g_toolMenuWeaponSettingsRegistry,
            expectedWeaponIndex, profile);
    if (attachmentSlot != nullptr) {
        attachmentSlot->rightHandIkSettings = rightHandSettings;
        attachmentSlot->rightHandIkPersistentLoadAttempted = true;
        attachmentSlot->colliderSettings = nextColliderSettings;
        attachmentSlot->colliderPersistentLoadAttempted = true;
        if (!blockColliderUsesAttackFallback) {
            attachmentSlot->blockColliderSettings =
                nextBlockColliderSettings;
            attachmentSlot->blockColliderPersistentLoadAttempted = true;
            attachmentSlot->blockColliderUsesAttackFallback = false;
        }
        attachmentApplied = true;
    }
    ReleaseSRWLockExclusive(&g_toolMenuSettingsLock);
    if (!attachmentApplied) {
        AcquireSRWLockExclusive(&g_physicalMeleeVisualLock);
        const std::int32_t rollbackIndex =
            g_activeWeaponGripCalibrationSlot;
        if (rollbackIndex >= 0 &&
            static_cast<std::size_t>(rollbackIndex) <
                kWeaponGripCalibrationSlotCount) {
            WeaponGripCalibrationSlot& slot =
                g_weaponGripCalibrationSlots[rollbackIndex];
            if (slot.occupied && slot.weapon == weapon &&
                slot.weaponIndex == expectedWeaponIndex &&
                slot.modelObject == modelObject &&
                g_physicalMeleeVisualSourceGeneration ==
                    expectedGeneration) {
                slot.calibration = originalCalibration;
                g_physicalMeleeVisualModelLocalGripPosition =
                    originalCalibration.positionUnits;
                g_physicalMeleeVisualModelLocalGripRotation =
                    ResolvePhysicalMeleeGripCalibrationRotation(
                        originalCalibration);
            }
        }
        ReleaseSRWLockExclusive(&g_physicalMeleeVisualLock);
        return false;
    }

    WeaponGripSettings gripSettings{};
    gripSettings.positionUnits = calibration.positionUnits;
    gripSettings.localRotationDegrees =
        calibration.localRotationDegrees;
    gripSettings.secondaryGripOffsetUnits =
        calibration.secondaryGripOffsetUnits;
    gripSettings.secondaryGripGrabRadiusMeters =
        calibration.secondaryGripGrabRadiusMeters;
    gripSettings.secondaryGripEnabled =
        calibration.secondaryGripEnabled;
    gripSaveResult = SaveWeaponGripSettings(
        expectedWeaponIndex, profile.id, gripSettings);
    handSaveResult = SaveWeaponRightHandIkSettings(
        expectedWeaponIndex, profile.id, rightHandSettings);
    colliderSaveResult = SaveWeaponColliderSettings(
        expectedWeaponIndex, profile.id, nextColliderSettings);
    const WeaponSettingsStoreResult blockColliderSaveResult =
        blockColliderUsesAttackFallback
        ? WeaponSettingsStoreResult::NotFound
        : SaveWeaponBlockColliderSettings(
              expectedWeaponIndex, profile.id,
              nextBlockColliderSettings);
    LogWeaponGripCalibrationState(
        "m5_weapon_grip_calibration_snapshot",
        action != nullptr ? action : "held_object_attachment");
    if (g_passThroughLog != nullptr) {
        char detail[512]{};
        std::snprintf(
            detail, sizeof(detail),
            "action=%s weapon_index=%ld source_generation=%llu "
            "relationship=hand_parented model_to_hand_preserved=1 "
            "collider_model_relation_preserved=1 "
            "block_collider_model_relation_preserved=1 "
            "grip_persistence=%s hand_persistence=%s "
            "collider_persistence=%s block_collider_source=%s "
            "block_collider_persistence=%s",
            action != nullptr ? action : "unknown",
            static_cast<long>(expectedWeaponIndex),
            static_cast<unsigned long long>(expectedGeneration),
            WeaponSettingsStoreResultName(gripSaveResult),
            WeaponSettingsStoreResultName(handSaveResult),
            WeaponSettingsStoreResultName(colliderSaveResult),
            blockColliderUsesAttackFallback
                ? "attack_fallback" : "dedicated",
            blockColliderUsesAttackFallback
                ? "inherited"
                : WeaponSettingsStoreResultName(
                      blockColliderSaveResult));
        g_passThroughLog(
            "m5_held_object_attachment_applied", detail);
    }
    return true;
}

const char* HeldAssemblyControllerAlignmentEventName(
    HeldAssemblyControllerAlignmentEvent event) noexcept {
    switch (event) {
    case HeldAssemblyControllerAlignmentEvent::Applied:
        return "applied";
    case HeldAssemblyControllerAlignmentEvent::SourceUnavailable:
        return "source_unavailable";
    case HeldAssemblyControllerAlignmentEvent::PoseUnavailable:
        return "pose_unavailable";
    case HeldAssemblyControllerAlignmentEvent::SolveRejected:
        return "solve_rejected";
    case HeldAssemblyControllerAlignmentEvent::ApplyRejected:
        return "apply_rejected";
    case HeldAssemblyControllerAlignmentEvent::None:
    default:
        return "none";
    }
}

bool ApplyToolMenuHeldAssemblyControllerAlignmentAction() noexcept {
    g_heldAssemblyControllerAlignmentLastEvent =
        HeldAssemblyControllerAlignmentEvent::None;
    g_heldAssemblyControllerAlignmentLastGripSaveResult =
        WeaponSettingsStoreResult::NotFound;
    g_heldAssemblyControllerAlignmentLastHandSaveResult =
        WeaponSettingsStoreResult::NotFound;
    g_heldAssemblyControllerAlignmentLastColliderSaveResult =
        WeaponSettingsStoreResult::NotFound;
    g_heldObjectAlignmentLastEvent = HeldObjectAlignmentEvent::None;

    PhysicalMeleeGripCalibration calibration{};
    void* weapon = nullptr;
    void* modelObject = nullptr;
    std::int32_t weaponIndex = -1;
    std::uint64_t sourceGeneration = 0;
    std::uint64_t sampleId = 0;
    std::uint64_t timestampNs = 0;
    bool poseSnapshotReady = false;
    float gripPosition[3]{};
    float gripRotation[4]{};
    float aimRotation[4]{};
    PhysicalMeleeRigidTransform currentModelLocalGrip{};
    EmptyRightHandAlignmentSettings currentHandAlignment{};
    PhysicalMeleeRigidTransform rawGripWorld{};
    PhysicalMeleeRigidTransform controllerDriverWorld{};
    PhysicalMeleeRigidTransform desiredHandWorld{};
    HeldObjectAlignmentSolution solution{};

    const auto logResult = [&]() noexcept {
        if (g_passThroughLog == nullptr) {
            return;
        }
        float gripToAimDegrees = 0.0F;
        RightHandIkQuaternionAngularDifferenceDegrees(
            rawGripWorld.rotation, controllerDriverWorld.rotation,
            gripToAimDegrees);
        const PhysicalMeleeRigidTransform currentHandLocal{
            currentHandAlignment.localPositionOffsetUnits,
            currentHandAlignment.localRotationOffset};
        char detail[2048]{};
        std::snprintf(
            detail, sizeof(detail),
            "event=%s weapon_index=%ld source_generation=%llu "
            "raw_pose_fresh_same_sample=%u sample_id=%llu "
            "timestamp_ns=%llu "
            "raw_grip_position=(%.3f,%.3f,%.3f) "
            "raw_grip_q=(%.6f,%.6f,%.6f,%.6f) "
            "raw_aim_q=(%.6f,%.6f,%.6f,%.6f) "
            "grip_to_aim_degrees=%.3f "
            "current_grip_position=(%.3f,%.3f,%.3f) "
            "current_grip_q=(%.6f,%.6f,%.6f,%.6f) "
            "current_hand_position=(%.3f,%.3f,%.3f) "
            "current_hand_q=(%.6f,%.6f,%.6f,%.6f) "
            "desired_hand_position=(%.3f,%.3f,%.3f) "
            "desired_hand_q=(%.6f,%.6f,%.6f,%.6f) "
            "solved_grip_position=(%.3f,%.3f,%.3f) "
            "solved_grip_q=(%.6f,%.6f,%.6f,%.6f) "
            "solved_hand_position=(%.3f,%.3f,%.3f) "
            "solved_hand_q=(%.6f,%.6f,%.6f,%.6f) "
            "model_to_hand_preserved=1 collider_model_relation_preserved=1 "
            "grip_persistence=%s hand_persistence=%s "
            "collider_persistence=%s",
            HeldAssemblyControllerAlignmentEventName(
                g_heldAssemblyControllerAlignmentLastEvent),
            static_cast<long>(weaponIndex),
            static_cast<unsigned long long>(sourceGeneration),
            poseSnapshotReady ? 1U : 0U,
            static_cast<unsigned long long>(sampleId),
            static_cast<unsigned long long>(timestampNs),
            rawGripWorld.positionUnits.x,
            rawGripWorld.positionUnits.y,
            rawGripWorld.positionUnits.z,
            rawGripWorld.rotation.x, rawGripWorld.rotation.y,
            rawGripWorld.rotation.z, rawGripWorld.rotation.w,
            controllerDriverWorld.rotation.x,
            controllerDriverWorld.rotation.y,
            controllerDriverWorld.rotation.z,
            controllerDriverWorld.rotation.w,
            gripToAimDegrees,
            currentModelLocalGrip.positionUnits.x,
            currentModelLocalGrip.positionUnits.y,
            currentModelLocalGrip.positionUnits.z,
            currentModelLocalGrip.rotation.x,
            currentModelLocalGrip.rotation.y,
            currentModelLocalGrip.rotation.z,
            currentModelLocalGrip.rotation.w,
            currentHandLocal.positionUnits.x,
            currentHandLocal.positionUnits.y,
            currentHandLocal.positionUnits.z,
            currentHandLocal.rotation.x,
            currentHandLocal.rotation.y,
            currentHandLocal.rotation.z,
            currentHandLocal.rotation.w,
            desiredHandWorld.positionUnits.x,
            desiredHandWorld.positionUnits.y,
            desiredHandWorld.positionUnits.z,
            desiredHandWorld.rotation.x,
            desiredHandWorld.rotation.y,
            desiredHandWorld.rotation.z,
            desiredHandWorld.rotation.w,
            solution.modelLocalGrip.positionUnits.x,
            solution.modelLocalGrip.positionUnits.y,
            solution.modelLocalGrip.positionUnits.z,
            solution.modelLocalGrip.rotation.x,
            solution.modelLocalGrip.rotation.y,
            solution.modelLocalGrip.rotation.z,
            solution.modelLocalGrip.rotation.w,
            solution.rightHandAlignment.localPositionOffsetUnits.x,
            solution.rightHandAlignment.localPositionOffsetUnits.y,
            solution.rightHandAlignment.localPositionOffsetUnits.z,
            solution.rightHandAlignment.localRotationOffset.x,
            solution.rightHandAlignment.localRotationOffset.y,
            solution.rightHandAlignment.localRotationOffset.z,
            solution.rightHandAlignment.localRotationOffset.w,
            WeaponSettingsStoreResultName(
                g_heldAssemblyControllerAlignmentLastGripSaveResult),
            WeaponSettingsStoreResultName(
                g_heldAssemblyControllerAlignmentLastHandSaveResult),
            WeaponSettingsStoreResultName(
                g_heldAssemblyControllerAlignmentLastColliderSaveResult));
        g_passThroughLog(
            "m5_align_held_assembly_to_controller", detail);
    };

    if (!CopyActiveWeaponGripCalibration(
            calibration, weapon, weaponIndex, modelObject,
            sourceGeneration)) {
        g_heldAssemblyControllerAlignmentLastEvent =
            HeldAssemblyControllerAlignmentEvent::SourceUnavailable;
        logResult();
        return false;
    }
    currentModelLocalGrip = {
        calibration.positionUnits,
        ResolvePhysicalMeleeGripCalibrationRotation(calibration)};
    const ToolMenuRightHandIkSettings currentHandSettings =
        CopyToolMenuRightHandIkSettings(weaponIndex);
    currentHandAlignment.localPositionOffsetUnits =
        currentHandSettings.positionOffsetUnits;
    currentHandAlignment.localRotationOffset =
        PhysicalMeleeLocalRotationFromDegrees(
            currentHandSettings.rotationOffsetDegrees);
    poseSnapshotReady =
        CopyFreshTrackedRawControllerAlignmentPoses(
            gripPosition, gripRotation, aimRotation,
            sampleId, timestampNs);
    if (!poseSnapshotReady) {
        g_heldAssemblyControllerAlignmentLastEvent =
            HeldAssemblyControllerAlignmentEvent::PoseUnavailable;
        logResult();
        return false;
    }
    rawGripWorld = {
        {gripPosition[0], gripPosition[1], gripPosition[2]},
        {gripRotation[0], gripRotation[1],
         gripRotation[2], gripRotation[3]}};
    controllerDriverWorld = {
        rawGripWorld.positionUnits,
        {aimRotation[0], aimRotation[1],
         aimRotation[2], aimRotation[3]}};
    desiredHandWorld = ResolveEmptyRightHandAlignmentTarget(
        rawGripWorld, g_emptyRightHandAlignmentSettings);
    if (!SolveHandParentedHeldAssemblyControllerAlignment(
            currentModelLocalGrip, currentHandAlignment,
            rawGripWorld, controllerDriverWorld,
            g_emptyRightHandAlignmentSettings, solution)) {
        g_heldAssemblyControllerAlignmentLastEvent =
            HeldAssemblyControllerAlignmentEvent::SolveRejected;
        logResult();
        return false;
    }
    const bool applied = ApplyHeldObjectAlignmentSolution(
        weaponIndex, sourceGeneration, solution,
        "vr_tool_menu_align_assembly_to_controller",
        g_heldAssemblyControllerAlignmentLastGripSaveResult,
        g_heldAssemblyControllerAlignmentLastHandSaveResult,
        g_heldAssemblyControllerAlignmentLastColliderSaveResult);
    g_heldAssemblyControllerAlignmentLastEvent = applied
        ? HeldAssemblyControllerAlignmentEvent::Applied
        : HeldAssemblyControllerAlignmentEvent::ApplyRejected;
    if (applied) {
        ResetPhysicalMeleeWeaponWeight(
            fearvr::WeaponWeightResetReason::enabledChanged);
        ResetArmIkBendMemory();
    }
    logResult();
    return applied;
}

bool ApplyToolMenuHeldObjectAlignmentAction() noexcept {
    if (HeldObjectAlignmentIsActive(g_heldObjectAlignmentState)) {
        CancelHeldObjectAlignmentMode("vr_tool_menu_cancel");
        return true;
    }
    PhysicalMeleeGripCalibration calibration{};
    void* weapon = nullptr;
    void* modelObject = nullptr;
    std::int32_t weaponIndex = -1;
    std::uint64_t sourceGeneration = 0;
    if (!CopyActiveWeaponGripCalibration(
            calibration, weapon, weaponIndex, modelObject,
            sourceGeneration) ||
        !BeginHeldObjectAlignment(
            g_heldObjectAlignmentState, weaponIndex,
            sourceGeneration)) {
        g_heldObjectAlignmentLastEvent =
            HeldObjectAlignmentEvent::PoseUnavailable;
        LogHeldObjectAlignment(
            "vr_tool_menu_start_rejected",
            weaponIndex, sourceGeneration);
        return false;
    }
    g_toolMenuState.row = 9U;
    g_heldAssemblyControllerAlignmentLastEvent =
        HeldAssemblyControllerAlignmentEvent::None;
    g_heldObjectAlignmentLastEvent = HeldObjectAlignmentEvent::None;
    g_heldObjectAlignmentLastGripSaveResult =
        WeaponSettingsStoreResult::NotFound;
    g_heldObjectAlignmentLastHandSaveResult =
        WeaponSettingsStoreResult::NotFound;
    g_heldObjectAlignmentLastColliderSaveResult =
        WeaponSettingsStoreResult::NotFound;
    ResetPhysicalMeleeWeaponWeight(
        fearvr::WeaponWeightResetReason::enabledChanged);
    ResetArmIkBendMemory();
    LogHeldObjectAlignment(
        "vr_tool_menu_start", weaponIndex, sourceGeneration);
    return true;
}

void UpdateHeldObjectAlignmentCapture(
    bool liveHeldSource,
    std::int32_t weaponIndex,
    std::uint64_t sourceGeneration,
    const PhysicalMeleeRigidTransform& desiredControllerPose,
    const PhysicalMeleeRigidTransform& displayedObjectWorld,
    PhysicalMeleeRigidTransform& displayedHandWorld,
    bool posesFresh,
    bool rightTriggerDown) noexcept {
    if (!HeldObjectAlignmentIsActive(g_heldObjectAlignmentState)) {
        return;
    }
    if (!GameOwnsForegroundWindow() || !VrToolMenuIsOpen()) {
        CancelHeldObjectAlignmentMode("cancel_context_lost");
        return;
    }
    const std::int32_t expectedWeaponIndex =
        g_heldObjectAlignmentState.weaponIndex;
    const std::uint64_t expectedGeneration =
        g_heldObjectAlignmentState.sourceGeneration;
    const PhysicalMeleeRigidTransform referenceObjectBefore =
        g_heldObjectAlignmentState.referenceObjectWorld;
    const PhysicalMeleeRigidTransform referenceHandBefore =
        g_heldObjectAlignmentState.referenceHandWorld;
    const HeldObjectAlignmentUpdateResult update =
        UpdateHeldObjectAlignment(
            g_heldObjectAlignmentState,
            liveHeldSource ? weaponIndex : -1,
            liveHeldSource ? sourceGeneration : 0U,
            desiredControllerPose, displayedObjectWorld,
            displayedHandWorld, posesFresh && liveHeldSource,
            rightTriggerDown);
    if (update.event == HeldObjectAlignmentEvent::None) {
        return;
    }
    g_heldObjectAlignmentLastEvent = update.event;
    if (update.event ==
        HeldObjectAlignmentEvent::ReferenceCaptured) {
        LogHeldObjectAlignment(
            "right_trigger_reference", expectedWeaponIndex,
            expectedGeneration,
            g_heldObjectAlignmentState.referenceObjectWorld,
            g_heldObjectAlignmentState.referenceHandWorld,
            desiredControllerPose);
        return;
    }
    if (update.event == HeldObjectAlignmentEvent::Completed) {
        WeaponSettingsStoreResult gripSave =
            WeaponSettingsStoreResult::InvalidArgument;
        WeaponSettingsStoreResult handSave =
            WeaponSettingsStoreResult::InvalidArgument;
        WeaponSettingsStoreResult colliderSave =
            WeaponSettingsStoreResult::InvalidArgument;
        const bool applied = ApplyHeldObjectAlignmentSolution(
            expectedWeaponIndex, expectedGeneration,
            update.solution, "guided_held_object_alignment",
            gripSave, handSave, colliderSave);
        g_heldObjectAlignmentLastGripSaveResult = gripSave;
        g_heldObjectAlignmentLastHandSaveResult = handSave;
        g_heldObjectAlignmentLastColliderSaveResult =
            colliderSave;
        if (applied) {
            displayedHandWorld = ResolveToolMenuRightHandIkTarget(
                desiredControllerPose,
                CopyToolMenuRightHandIkSettings(
                    expectedWeaponIndex));
            ResetPhysicalMeleeWeaponWeight(
                fearvr::WeaponWeightResetReason::enabledChanged);
            ResetArmIkBendMemory();
            LogHeldObjectAlignment(
                "right_trigger_controller_solved",
                expectedWeaponIndex, expectedGeneration,
                referenceObjectBefore, referenceHandBefore,
                desiredControllerPose, &update.solution);
        } else {
            g_heldObjectAlignmentLastEvent =
                HeldObjectAlignmentEvent::SolveRejected;
            LogHeldObjectAlignment(
                "right_trigger_apply_rejected",
                expectedWeaponIndex, expectedGeneration,
                referenceObjectBefore, referenceHandBefore,
                desiredControllerPose, &update.solution);
        }
        return;
    }
    LogHeldObjectAlignment(
        update.event == HeldObjectAlignmentEvent::SourceChanged
            ? "capture_source_changed"
            : update.event ==
                  HeldObjectAlignmentEvent::PoseUnavailable
                ? "right_trigger_pose_unavailable"
                : "right_trigger_solve_rejected",
        expectedWeaponIndex, expectedGeneration,
        referenceObjectBefore, referenceHandBefore,
        desiredControllerPose);
}

void LogToolMenuRightHandIkState(
    const char* action,
    std::int32_t weaponIndex) noexcept {
    if (g_passThroughLog == nullptr) {
        return;
    }
    const PhysicalMeleeProfile profile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    const ToolMenuRightHandIkSettings settings =
        CopyToolMenuRightHandIkSettings(weaponIndex);
    char detail[384]{};
    std::snprintf(
        detail, sizeof(detail),
        "action=%s weapon_index=%ld profile=%s callback_active=%u "
        "position_units=(%.3f,%.3f,%.3f) "
        "rotation_degrees=(%.2f,%.2f,%.2f) "
        "translation_step_units=%.2f rotation_step_degrees=%.2f",
        action,
        static_cast<long>(weaponIndex),
        PhysicalMeleeProfileName(profile.id),
        ArmIkRightHandProofIsActive() ? 1U : 0U,
        settings.positionOffsetUnits.x,
        settings.positionOffsetUnits.y,
        settings.positionOffsetUnits.z,
        settings.rotationOffsetDegrees.x,
        settings.rotationOffsetDegrees.y,
        settings.rotationOffsetDegrees.z,
        kWeaponGripTranslationSteps[
            g_rightHandIkCalibrationStepIndex],
        kWeaponGripRotationSteps[
            g_rightHandIkCalibrationStepIndex]);
    g_passThroughLog("m5_right_hand_ik_calibration", detail);
}

void LogToolMenuElbowIkState(const char* action) noexcept {
    if (g_passThroughLog == nullptr) {
        return;
    }
    const fearvr::ArmIkTuning tuning = ReadArmIkTuning();
    char detail[256]{};
    std::snprintf(
        detail, sizeof(detail),
        "action=%s callback_active=%u outward=%.3f down=%.3f "
        "back=%.3f continuity=%u",
        action, ArmIkRightArmIsActive() ? 1U : 0U,
        tuning.elbowOutward, tuning.elbowDown, tuning.elbowBack,
        tuning.preserveElbowContinuity ? 1U : 0U);
    g_passThroughLog("m5_elbow_ik_calibration", detail);
}

void LogToolMenuLeftHandIkState(const char* action) noexcept {
    if (g_passThroughLog == nullptr) {
        return;
    }
    const fearvr::ArmIkTuning tuning = ReadArmIkTuning();
    char detail[320]{};
    std::snprintf(
        detail, sizeof(detail),
        "action=%s callback_active=%u position_m=(%.3f,%.3f,%.3f) "
        "rotation_degrees=(%.2f,%.2f,%.2f)",
        action, ArmIkLeftHandIsActive() ? 1U : 0U,
        tuning.leftHandRightMeters, tuning.leftHandUpMeters,
        tuning.leftHandForwardMeters,
        tuning.leftHandPitchDegrees, tuning.leftHandYawDegrees,
        tuning.leftHandRollDegrees);
    g_passThroughLog("m5_left_hand_ik_calibration", detail);
}

void LogToolMenuState(const char* action) noexcept {
    if (g_passThroughLog == nullptr) {
        return;
    }
    ToolMenuMeleeTelemetry telemetry{};
    ReadPhysicalMeleeToolTelemetry(telemetry);
    const PhysicalMeleeProfile profile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(
            telemetry.weaponIndex);
    const ToolMenuMeleeSettings settings =
        CopyToolMenuMeleeSettings(telemetry.weaponIndex);
    char detail[704]{};
    std::snprintf(
        detail, sizeof(detail),
        "action=%s open=%u tab=%s row=%u weapon_index=%ld profile=%s "
        "collider_draw=%u block_collider_draw=%u controller_draw=%u "
        "swing_enabled=%u trigger_mps=%.2f rearm_mps=%.2f "
        "pulse_ms=%u cooldown_ms=%u mass_kg=%.2f "
        "handling_weight=%.2f positional_follow=%.2f "
        "rotational_follow=%.2f catch_up=%.2f damping=%.2f "
        "two_hand_enabled=%u secondary_attached=%u "
        "secondary_distance_m=%.3f secondary_error_m=%.3f "
        "menu_scale_percent=%.0f menu_distance_m=%.2f",
        action,
        InterlockedCompareExchange(&g_toolMenuOpen, 0, 0) != 0
            ? 1U : 0U,
        ToolMenuTabName(g_toolMenuState.tab),
        g_toolMenuState.row,
        static_cast<long>(telemetry.weaponIndex),
        PhysicalMeleeProfileName(profile.id),
        InterlockedCompareExchange(
            &g_physicalMeleeColliderDebugDrawVisible, 0, 0) != 0
            ? 1U : 0U,
        InterlockedCompareExchange(
            &g_physicalMeleeBlockColliderDebugDrawVisible, 0, 0) != 0
            ? 1U : 0U,
        InterlockedCompareExchange(
            &g_weaponGripControllerDebugDrawVisible, 0, 0) != 0
            ? 1U : 0U,
        settings.swingAttackEnabled ? 1U : 0U,
        settings.swingTriggerSpeedMetersPerSecond,
        settings.swingRearmSpeedMetersPerSecond,
        settings.swingPulseMilliseconds,
        settings.swingCooldownMilliseconds,
        settings.massKilograms, settings.handlingWeight,
        settings.positionalFollow, settings.rotationalFollow,
        settings.catchUpStrength, settings.dampingRatio,
        telemetry.twoHandedEnabled ? 1U : 0U,
        telemetry.secondaryGripAttached ? 1U : 0U,
        telemetry.secondaryGripDistanceMeters,
        telemetry.secondaryGripAnchorErrorMeters,
        g_toolMenuPanelPlacement.scale * 100.0F,
        g_toolMenuPanelPlacement.distanceMeters);
    g_passThroughLog("m5_vr_tool_menu_changed", detail);
}

void UpdateToolMenuCalibrationVisibility() noexcept {
    const bool calibrationVisible =
        InterlockedCompareExchange(&g_toolMenuOpen, 0, 0) != 0 &&
        (g_toolMenuState.tab == ToolMenuTab::Grip ||
         g_toolMenuState.tab == ToolMenuTab::Author ||
         g_toolMenuState.tab == ToolMenuTab::TwoHand ||
         g_toolMenuState.tab == ToolMenuTab::HandIk ||
         g_toolMenuState.tab == ToolMenuTab::LeftHandIk) &&
        InterlockedCompareExchange(
            &g_weaponGripCalibrationEnabled, 0, 0) != 0;
    InterlockedExchange(
        &g_weaponGripCalibrationActive,
        calibrationVisible ? 1 : 0);
}

void SetToolMenuOpen(bool open) noexcept {
    const bool wasOpen = InterlockedCompareExchange(
        &g_toolMenuOpen, 0, 0) != 0;
    g_toolMenuState.open = open;
    InterlockedExchange(&g_toolMenuOpen, open ? 1 : 0);
    if (!open) {
        InterlockedExchange(&g_toolMenuReleaseCapture, 1);
    }
    if (!open || g_toolMenuState.tab != ToolMenuTab::Author) {
        AcquireSRWLockExclusive(
            &g_magazineSocketAuthoringLock);
        g_magazineSocketAuthoringState.visualReady = false;
        g_magazineSocketAuthoringState.visualTick = 0U;
        g_magazineSocketAuthoringState.preview = {};
        ReleaseSRWLockExclusive(
            &g_magazineSocketAuthoringLock);
        AcquireSRWLockExclusive(
            &g_slideGrabAuthoringLock);
        g_slideGrabAuthoringState.visualReady = false;
        g_slideGrabAuthoringState.visualTick = 0U;
        ReleaseSRWLockExclusive(
            &g_slideGrabAuthoringLock);
    }
    if (wasOpen != open) {
        EndSlideNodeControl(
            open ? "tool_menu_opened" : "tool_menu_closed");
        AcquireSRWLockExclusive(&g_slideGrabRuntimeLock);
        g_slideGrabStateMachine = {};
        g_slideGrabLastFrame = {};
        ReleaseSRWLockExclusive(&g_slideGrabRuntimeLock);
        InterlockedExchange(&g_slideGrabCaptureGrip, 0);
        InterlockedExchange(&g_slideGrabCaptureTrigger, 0);
    }
    UpdateToolMenuCalibrationVisibility();
}

bool ApplyToolMenuMeleeAdjustment(
    ToolMenuTab tab,
    std::uint32_t row,
    int delta,
    bool activate,
    std::int32_t weaponIndex) noexcept {
    if (weaponIndex < 0) {
        return false;
    }
    const PhysicalMeleeProfile baseProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    const ToolMenuMeleeSettings defaults =
        ToolMenuMeleeSettingsFromProfile(baseProfile);
    ToolMenuMeleeSettings settings =
        CopyToolMenuMeleeSettings(weaponIndex);
    const ToolMenuMeleeSettings original = settings;
    if (tab == ToolMenuTab::Melee) {
        switch (row) {
        case 0U:
            if (delta != 0 || activate) {
                settings.requireSwingForContactDamage =
                    !settings.requireSwingForContactDamage;
            }
            break;
        case 1U:
            settings.hitSpeedMetersPerSecond = std::clamp(
                settings.hitSpeedMetersPerSecond +
                    static_cast<float>(delta) * 0.25F,
                0.25F, 10.0F);
            break;
        case 2U:
            settings.contactRearmDistanceMeters = std::clamp(
                settings.contactRearmDistanceMeters +
                    static_cast<float>(delta) * 0.01F,
                0.02F, 1.0F);
            break;
        case 3U:
            if (activate) {
                settings.requireSwingForContactDamage =
                    defaults.requireSwingForContactDamage;
                settings.hitSpeedMetersPerSecond =
                    defaults.hitSpeedMetersPerSecond;
                settings.contactRearmDistanceMeters =
                    defaults.contactRearmDistanceMeters;
            }
            break;
        default:
            break;
        }
    } else if (tab == ToolMenuTab::Weapon) {
        switch (row) {
        case 0U:
            settings.massKilograms = std::clamp(
                settings.massKilograms +
                    static_cast<float>(delta) * 0.25F,
                0.50F, 20.0F);
            break;
        case 1U:
            settings.handlingWeight = std::clamp(
                settings.handlingWeight +
                    static_cast<float>(delta) * 0.25F,
                0.10F, 4.0F);
            break;
        case 2U:
            settings.positionalFollow = std::clamp(
                settings.positionalFollow + static_cast<float>(delta),
                2.0F, 40.0F);
            break;
        case 3U:
            settings.rotationalFollow = std::clamp(
                settings.rotationalFollow + static_cast<float>(delta),
                2.0F, 40.0F);
            break;
        case 4U:
            settings.catchUpStrength = std::clamp(
                settings.catchUpStrength +
                    static_cast<float>(delta) * 0.10F,
                0.0F, 4.0F);
            break;
        case 5U:
            settings.dampingRatio = std::clamp(
                settings.dampingRatio +
                    static_cast<float>(delta) * 0.05F,
                0.35F, 1.0F);
            break;
        case 6U:
            if (activate) {
                settings.massKilograms = defaults.massKilograms;
                settings.handlingWeight = defaults.handlingWeight;
                settings.positionalFollow = defaults.positionalFollow;
                settings.rotationalFollow = defaults.rotationalFollow;
                settings.catchUpStrength = defaults.catchUpStrength;
                settings.dampingRatio = defaults.dampingRatio;
            }
            break;
        default:
            break;
        }
    } else {
        return false;
    }
    if (std::memcmp(&settings, &original, sizeof(settings)) == 0) {
        return false;
    }
    return StoreToolMenuMeleeSettings(weaponIndex, settings);
}

bool ApplyToolMenuBlockPoseAdjustment(
    std::uint32_t row,
    int delta,
    bool activate,
    std::int32_t weaponIndex) noexcept {
    const PhysicalMeleeProfile profile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    if (!PhysicalMeleeProfileMatchesOneHandedWeaponIndex(
            weaponIndex, profile.id)) {
        return false;
    }
    if (row == 4U || row == 5U) {
        ToolMenuBlockTimingSettings timing =
            CopyToolMenuBlockTimingSettings(weaponIndex);
        if (!UpdateToolMenuBlockTimingSettings(
                timing, row - 4U, delta, activate)) {
            return false;
        }
        return StoreToolMenuBlockTimingSettings(
            weaponIndex, timing);
    }
    PhysicalMeleeBlockPoseSettings settings =
        CopyToolMenuBlockPoseSettings(weaponIndex);
    const PhysicalMeleeBlockPoseSettings original = settings;
    switch (row) {
    case 0U:
        if ((delta != 0 || activate) && settings.captured) {
            settings.enabled = !settings.enabled;
        }
        break;
    case 1U:
        if (activate) {
            float headPosition[3]{};
            float headRotation[4]{};
            float weaponPosition[3]{};
            float weaponRotation[4]{};
            std::uint64_t sampleId = 0U;
            std::uint64_t timestampNs = 0U;
            const bool posesFresh = CopyFreshTrackedHeadWorldPose(
                    headPosition, headRotation) &&
                CopyFreshTrackedControllerWorldPose(
                    weaponPosition, weaponRotation,
                    sampleId, timestampNs);
            const PhysicalMeleeBlockWorldPose head{
                {headPosition[0], headPosition[1], headPosition[2]},
                {headRotation[0], headRotation[1],
                 headRotation[2], headRotation[3]}};
            const PhysicalMeleeBlockWorldPose weapon{
                {weaponPosition[0], weaponPosition[1],
                 weaponPosition[2]},
                {weaponRotation[0], weaponRotation[1],
                 weaponRotation[2], weaponRotation[3]}};
            if (!posesFresh || !CapturePhysicalMeleeBlockPose(
                    head, weapon, profile.unitsPerMeter, settings)) {
                if (g_passThroughLog != nullptr) {
                    g_passThroughLog(
                        "m5_block_pose_capture_rejected",
                        posesFresh
                            ? "reason=invalid_relative_pose"
                            : "reason=tracking_stale");
                }
                return false;
            }
        }
        break;
    case 2U:
        settings.positionToleranceMeters = std::clamp(
            settings.positionToleranceMeters +
                static_cast<float>(delta) * 0.01F,
            kPhysicalMeleeBlockMinimumPositionToleranceMeters,
            kPhysicalMeleeBlockMaximumPositionToleranceMeters);
        break;
    case 3U:
        settings.angleToleranceDegrees = std::clamp(
            settings.angleToleranceDegrees +
                static_cast<float>(delta) * 2.5F,
            kPhysicalMeleeBlockMinimumAngleToleranceDegrees,
            kPhysicalMeleeBlockMaximumAngleToleranceDegrees);
        break;
    case 6U:
        if (activate) {
            settings = {};
        }
        break;
    default:
        return false;
    }
    if (std::memcmp(&settings, &original, sizeof(settings)) == 0) {
        return false;
    }
    return StoreToolMenuBlockPoseSettings(weaponIndex, settings);
}

bool ApplyToolMenuColliderAdjustment(
    std::uint32_t row,
    int delta,
    bool activate,
    std::int32_t weaponIndex,
    bool blockCollider) noexcept {
    if (weaponIndex < 0) {
        return false;
    }
    const PhysicalMeleeProfile baseProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    const ToolMenuColliderSettings defaults = blockCollider
        ? CopyToolMenuColliderSettings(weaponIndex)
        : ToolMenuColliderSettingsFromProfile(baseProfile);
    bool usesAttackColliderFallback = true;
    ToolMenuColliderSettings settings = blockCollider
        ? CopyToolMenuBlockColliderSettings(
              weaponIndex, usesAttackColliderFallback)
        : CopyToolMenuColliderSettings(weaponIndex);
    const ToolMenuColliderSettings original = settings;
    switch (row) {
    case 0U:
        settings.positionOffsetUnits.x = std::clamp(
            settings.positionOffsetUnits.x + static_cast<float>(delta),
            -kToolMenuColliderMaximumOffsetUnits,
            kToolMenuColliderMaximumOffsetUnits);
        break;
    case 1U:
        settings.positionOffsetUnits.y = std::clamp(
            settings.positionOffsetUnits.y + static_cast<float>(delta),
            -kToolMenuColliderMaximumOffsetUnits,
            kToolMenuColliderMaximumOffsetUnits);
        break;
    case 2U:
        settings.positionOffsetUnits.z = std::clamp(
            settings.positionOffsetUnits.z + static_cast<float>(delta),
            -kToolMenuColliderMaximumOffsetUnits,
            kToolMenuColliderMaximumOffsetUnits);
        break;
    case 3U:
        settings.rotationOffsetDegrees.x = std::clamp(
            settings.rotationOffsetDegrees.x +
                static_cast<float>(delta) * 5.0F,
            -180.0F, 180.0F);
        break;
    case 4U:
        settings.rotationOffsetDegrees.y = std::clamp(
            settings.rotationOffsetDegrees.y +
                static_cast<float>(delta) * 5.0F,
            -180.0F, 180.0F);
        break;
    case 5U:
        settings.rotationOffsetDegrees.z = std::clamp(
            settings.rotationOffsetDegrees.z +
                static_cast<float>(delta) * 5.0F,
            -180.0F, 180.0F);
        break;
    case 6U:
        settings.lengthUnits = std::clamp(
            settings.lengthUnits + static_cast<float>(delta) * 2.5F,
            5.0F, kToolMenuColliderMaximumLengthUnits);
        break;
    case 7U:
        settings.radiusUnits = std::clamp(
            settings.radiusUnits + static_cast<float>(delta) * 0.5F,
            0.5F, kToolMenuColliderMaximumRadiusUnits);
        break;
    case 8U:
        if (delta != 0 || activate) {
            settings.reversed = !settings.reversed;
        }
        break;
    case 9U:
        if (activate) {
            settings = defaults;
        }
        break;
    default:
        return false;
    }
    if (std::memcmp(&settings, &original, sizeof(settings)) == 0) {
        return false;
    }
    return blockCollider
        ? StoreToolMenuBlockColliderSettings(weaponIndex, settings)
        : StoreToolMenuColliderSettings(weaponIndex, settings);
}

bool ApplyToolMenuGripAdjustment(
    std::uint32_t row,
    int delta,
    bool activate) noexcept {
    if (InterlockedCompareExchange(
            &g_weaponGripCalibrationEnabled, 0, 0) == 0) {
        return false;
    }
    if (row <= 2U && delta != 0) {
        g_weaponGripCalibrationMode =
            WeaponGripCalibrationMode::Position;
        const float amount =
            kWeaponGripTranslationSteps[
                g_weaponGripCalibrationStepIndex] *
            static_cast<float>(delta);
        const bool changed = AdjustActiveWeaponGripCalibration(
            row == 0U ? amount : 0.0F,
            row == 1U ? amount : 0.0F,
            row == 2U ? amount : 0.0F);
        if (changed) {
            PersistActiveWeaponGripCalibration("vr_tool_menu_adjust");
        }
        return changed;
    }
    if (row >= 3U && row <= 5U && delta != 0) {
        g_weaponGripCalibrationMode =
            WeaponGripCalibrationMode::Rotation;
        const float amount =
            kWeaponGripRotationSteps[
                g_weaponGripCalibrationStepIndex] *
            static_cast<float>(delta);
        const bool changed = AdjustActiveWeaponGripCalibration(
            row == 3U ? amount : 0.0F,
            row == 4U ? amount : 0.0F,
            row == 5U ? amount : 0.0F);
        if (changed) {
            PersistActiveWeaponGripCalibration("vr_tool_menu_adjust");
        }
        return changed;
    }
    if (row == 6U && delta != 0) {
        const int next = std::clamp(
            static_cast<int>(g_weaponGripCalibrationStepIndex) + delta,
            0, static_cast<int>(kWeaponGripStepCount - 1U));
        const bool changed = next !=
            static_cast<int>(g_weaponGripCalibrationStepIndex);
        g_weaponGripCalibrationStepIndex =
            static_cast<std::size_t>(next);
        return changed;
    }
    if (row == 7U && activate) {
        const bool changed = ResetActiveWeaponGripCalibration();
        if (changed) {
            PersistActiveWeaponGripCalibration(
                "vr_tool_menu_reset");
        }
        return changed;
    }
    if (row == 8U && activate) {
        return ApplyToolMenuHeldAssemblyControllerAlignmentAction();
    }
    if (row == 9U && activate) {
        return ApplyToolMenuHeldObjectAlignmentAction();
    }
    return false;
}

bool ApplyToolMenuRightHandIkAdjustment(
    std::uint32_t row,
    int delta,
    bool activate,
    std::int32_t weaponIndex) noexcept {
    if (weaponIndex < 0) {
        return false;
    }
    ToolMenuRightHandIkSettings settings =
        CopyToolMenuRightHandIkSettings(weaponIndex);
    const ToolMenuRightHandIkSettings original = settings;
    if (row <= 2U && delta != 0) {
        const float amount =
            kWeaponGripTranslationSteps[
                g_rightHandIkCalibrationStepIndex] *
            static_cast<float>(delta);
        float* const component = row == 0U
            ? &settings.positionOffsetUnits.x
            : row == 1U
                ? &settings.positionOffsetUnits.y
                : &settings.positionOffsetUnits.z;
        *component = std::clamp(
            *component + amount,
            -kToolMenuRightHandIkMaximumPositionOffsetUnits,
            kToolMenuRightHandIkMaximumPositionOffsetUnits);
    } else if (row >= 3U && row <= 5U && delta != 0) {
        const float amount =
            kWeaponGripRotationSteps[
                g_rightHandIkCalibrationStepIndex] *
            static_cast<float>(delta);
        float* const component = row == 3U
            ? &settings.rotationOffsetDegrees.x
            : row == 4U
                ? &settings.rotationOffsetDegrees.y
                : &settings.rotationOffsetDegrees.z;
        *component = PhysicalMeleeWrapDegrees(*component + amount);
    } else if (row == 6U && delta != 0) {
        const int next = std::clamp(
            static_cast<int>(g_rightHandIkCalibrationStepIndex) + delta,
            0, static_cast<int>(kWeaponGripStepCount - 1U));
        const bool changed = next !=
            static_cast<int>(g_rightHandIkCalibrationStepIndex);
        g_rightHandIkCalibrationStepIndex =
            static_cast<std::size_t>(next);
        return changed;
    } else if (row == 7U && activate) {
        settings = {};
    } else if (row == 8U && activate) {
        LogToolMenuRightHandIkState(
            "vr_tool_menu_snapshot", weaponIndex);
        return true;
    } else {
        return false;
    }
    if (std::memcmp(
            &settings, &original, sizeof(settings)) == 0 &&
        !(row == 7U && activate)) {
        return false;
    }
    EmptyRightHandAlignmentSettings currentHandAlignment{};
    currentHandAlignment.localPositionOffsetUnits =
        original.positionOffsetUnits;
    currentHandAlignment.localRotationOffset =
        PhysicalMeleeLocalRotationFromDegrees(
            original.rotationOffsetDegrees);
    EmptyRightHandAlignmentSettings nextHandAlignment{};
    nextHandAlignment.localPositionOffsetUnits =
        settings.positionOffsetUnits;
    nextHandAlignment.localRotationOffset =
        PhysicalMeleeLocalRotationFromDegrees(
            settings.rotationOffsetDegrees);
    PhysicalMeleeGripCalibration calibration{};
    void* weapon = nullptr;
    void* modelObject = nullptr;
    std::int32_t activeWeaponIndex = -1;
    std::uint64_t sourceGeneration = 0;
    if (!CopyActiveWeaponGripCalibration(
            calibration, weapon, activeWeaponIndex, modelObject,
            sourceGeneration) ||
        activeWeaponIndex != weaponIndex) {
        return false;
    }
    const PhysicalMeleeRigidTransform currentModelLocalGrip{
        calibration.positionUnits,
        ResolvePhysicalMeleeGripCalibrationRotation(calibration)};
    PhysicalMeleeRigidTransform nextModelLocalGrip{};
    if (!ResolveHandParentedModelLocalGrip(
            currentModelLocalGrip, currentHandAlignment,
            nextHandAlignment, nextModelLocalGrip)) {
        return false;
    }
    HeldObjectAlignmentSolution solution{};
    solution.modelLocalGrip = nextModelLocalGrip;
    solution.rightHandAlignment = nextHandAlignment;
    WeaponSettingsStoreResult gripSave =
        WeaponSettingsStoreResult::InvalidArgument;
    WeaponSettingsStoreResult handSave =
        WeaponSettingsStoreResult::InvalidArgument;
    WeaponSettingsStoreResult colliderSave =
        WeaponSettingsStoreResult::InvalidArgument;
    const bool applied = ApplyHeldObjectAlignmentSolution(
        weaponIndex, sourceGeneration, solution,
        "vr_tool_menu_hand_parent_edit",
        gripSave, handSave, colliderSave);
    if (applied) {
        ResetPhysicalMeleeWeaponWeight(
            fearvr::WeaponWeightResetReason::enabledChanged);
        ResetArmIkBendMemory();
    }
    return applied;
}

bool ApplyToolMenuElbowIkAdjustment(
    std::uint32_t row,
    int delta,
    bool activate) noexcept {
    fearvr::ArmIkTuning tuning = ReadArmIkTuning();
    const fearvr::ArmIkTuning original = tuning;
    constexpr float kElbowStep = 0.05F;
    if (row <= 2U && delta != 0) {
        float* const component = row == 0U
            ? &tuning.elbowOutward
            : row == 1U
                ? &tuning.elbowDown
                : &tuning.elbowBack;
        const float minimum = row == 0U
            ? 0.20F : row == 1U ? 0.0F : -1.0F;
        const float maximum = row == 0U
            ? 2.0F : row == 1U ? 1.5F : 1.0F;
        *component = std::clamp(
            *component + static_cast<float>(delta) * kElbowStep,
            minimum, maximum);
    } else if (row == 3U && (delta != 0 || activate)) {
        tuning.preserveElbowContinuity =
            !tuning.preserveElbowContinuity;
    } else if (row == 4U && activate) {
        const fearvr::ArmIkTuning defaults{};
        tuning.elbowOutward = defaults.elbowOutward;
        tuning.elbowDown = defaults.elbowDown;
        tuning.elbowBack = defaults.elbowBack;
        tuning.preserveElbowContinuity =
            defaults.preserveElbowContinuity;
    } else if (row == 5U && activate) {
        LogToolMenuElbowIkState("vr_tool_menu_snapshot");
        return true;
    } else {
        return false;
    }
    const bool changed =
        tuning.elbowOutward != original.elbowOutward ||
        tuning.elbowDown != original.elbowDown ||
        tuning.elbowBack != original.elbowBack ||
        tuning.preserveElbowContinuity !=
            original.preserveElbowContinuity;
    if (!changed) {
        return false;
    }

    ApplyArmIkTuning(tuning);
    const WeaponSettingsStoreResult saveResult =
        SaveArmIkTuning(tuning);
    if (g_passThroughLog != nullptr) {
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "result=%s outward=%.3f down=%.3f back=%.3f continuity=%u",
            WeaponSettingsStoreResultName(saveResult),
            tuning.elbowOutward, tuning.elbowDown, tuning.elbowBack,
            tuning.preserveElbowContinuity ? 1U : 0U);
        g_passThroughLog(
            saveResult == WeaponSettingsStoreResult::Ok
                ? "m5_elbow_ik_settings_saved"
                : "m5_elbow_ik_settings_save_failed",
            detail);
    }
    return true;
}

bool ApplyToolMenuLeftHandIkAdjustment(
    std::uint32_t row,
    int delta,
    bool activate) noexcept {
    fearvr::ArmIkTuning tuning = ReadArmIkTuning();
    const fearvr::ArmIkTuning original = tuning;
    if (row <= 2U && delta != 0) {
        float* const component = row == 0U
            ? &tuning.leftHandRightMeters
            : row == 1U
                ? &tuning.leftHandUpMeters
                : &tuning.leftHandForwardMeters;
        *component = std::clamp(
            *component + static_cast<float>(delta) * 0.005F,
            -0.20F, 0.20F);
    } else if (row >= 3U && row <= 5U && delta != 0) {
        float* const component = row == 3U
            ? &tuning.leftHandPitchDegrees
            : row == 4U
                ? &tuning.leftHandYawDegrees
                : &tuning.leftHandRollDegrees;
        *component = PhysicalMeleeWrapDegrees(
            *component + static_cast<float>(delta) * 5.0F);
    } else if (row == 6U && activate) {
        const fearvr::ArmIkTuning defaults{};
        tuning.leftHandRightMeters = defaults.leftHandRightMeters;
        tuning.leftHandUpMeters = defaults.leftHandUpMeters;
        tuning.leftHandForwardMeters =
            defaults.leftHandForwardMeters;
        tuning.leftHandPitchDegrees = defaults.leftHandPitchDegrees;
        tuning.leftHandYawDegrees = defaults.leftHandYawDegrees;
        tuning.leftHandRollDegrees = defaults.leftHandRollDegrees;
    } else if (row == 7U && activate) {
        LogToolMenuLeftHandIkState("vr_tool_menu_snapshot");
        return true;
    } else {
        return false;
    }
    const bool changed =
        tuning.leftHandRightMeters != original.leftHandRightMeters ||
        tuning.leftHandUpMeters != original.leftHandUpMeters ||
        tuning.leftHandForwardMeters !=
            original.leftHandForwardMeters ||
        tuning.leftHandPitchDegrees != original.leftHandPitchDegrees ||
        tuning.leftHandYawDegrees != original.leftHandYawDegrees ||
        tuning.leftHandRollDegrees != original.leftHandRollDegrees;
    if (!changed) {
        return false;
    }

    ApplyArmIkTuning(tuning);
    const WeaponSettingsStoreResult saveResult =
        SaveArmIkTuning(tuning);
    if (g_passThroughLog != nullptr) {
        char detail[320]{};
        std::snprintf(
            detail, sizeof(detail),
            "result=%s position_m=(%.3f,%.3f,%.3f) "
            "rotation_degrees=(%.2f,%.2f,%.2f)",
            WeaponSettingsStoreResultName(saveResult),
            tuning.leftHandRightMeters, tuning.leftHandUpMeters,
            tuning.leftHandForwardMeters,
            tuning.leftHandPitchDegrees, tuning.leftHandYawDegrees,
            tuning.leftHandRollDegrees);
        g_passThroughLog(
            saveResult == WeaponSettingsStoreResult::Ok
                ? "m5_left_hand_ik_settings_saved"
                : "m5_left_hand_ik_settings_save_failed",
            detail);
    }
    return true;
}

bool ApplyToolMenuTwoHandAdjustment(
    std::uint32_t row,
    int delta,
    bool activate,
    const FearVrInputState& input,
    bool sampleFresh) noexcept {
    if (InterlockedCompareExchange(
            &g_weaponGripCalibrationEnabled, 0, 0) == 0) {
        return false;
    }
    if (row == 0U && (delta != 0 || activate)) {
        const bool changed =
            ToggleActiveSecondaryGripCalibration();
        if (changed) {
            PersistActiveWeaponGripCalibration(
                "vr_tool_menu_two_hand_adjust");
        }
        return changed;
    }
    if (row >= 1U && row <= 3U && delta != 0) {
        const float amount =
            kWeaponGripTranslationSteps[
                g_weaponGripCalibrationStepIndex] *
            static_cast<float>(delta);
        const bool changed = AdjustActiveSecondaryGripCalibration(
            row == 1U ? amount : 0.0F,
            row == 2U ? amount : 0.0F,
            row == 3U ? amount : 0.0F,
            0.0F);
        if (changed) {
            PersistActiveWeaponGripCalibration(
                "vr_tool_menu_two_hand_adjust");
        }
        return changed;
    }
    if (row == 4U && delta != 0) {
        const bool changed = AdjustActiveSecondaryGripCalibration(
            0.0F, 0.0F, 0.0F,
            static_cast<float>(delta) * 0.01F);
        if (changed) {
            PersistActiveWeaponGripCalibration(
                "vr_tool_menu_two_hand_adjust");
        }
        return changed;
    }
    if (row == 5U && activate) {
        const bool changed = CaptureActiveSecondaryGripCalibration(
            input, sampleFresh);
        if (changed) {
            PersistActiveWeaponGripCalibration(
                "vr_tool_menu_two_hand_capture");
        }
        return changed;
    }
    if (row == 6U && activate) {
        const bool changed =
            ResetActiveSecondaryGripCalibration();
        if (changed) {
            PersistActiveWeaponGripCalibration(
                "vr_tool_menu_two_hand_reset");
        }
        return changed;
    }
    if (row == 7U && activate) {
        const bool attempted = PersistActiveWeaponGripCalibration(
            "vr_tool_menu_two_hand_snapshot");
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_snapshot",
            "vr_tool_menu_two_hand_snapshot");
        return attempted;
    }
    return false;
}

bool ApplyToolMenuDisplayAdjustment(
    std::uint32_t row,
    int delta,
    bool activate) noexcept {
    bool changed = false;
    switch (row) {
    case 0U:
        if (delta != 0) {
            g_tuningFovScale = std::clamp(
                g_tuningFovScale + static_cast<float>(delta) * 0.05F,
                kMinimumFovScale, kMaximumFovScale);
            if (g_setFovScalePercent != nullptr) {
                g_setFovScalePercent(static_cast<std::uint32_t>(
                    g_tuningFovScale * 100.0F + 0.5F));
            }
            changed = true;
        }
        break;
    case 1U:
        if (delta != 0) {
            g_tuningUnitsPerMeter = std::clamp(
                g_tuningUnitsPerMeter +
                    static_cast<float>(delta) * 10.0F,
                10.0F, 300.0F);
            changed = true;
        }
        break;
    case 2U:
        if (delta != 0 || activate) {
            g_hmdTranslationEnabled = !g_hmdTranslationEnabled;
            changed = true;
        }
        break;
    case 3U:
        if (delta != 0 || activate) {
            g_tuningReversePolarity = !g_tuningReversePolarity;
            changed = true;
        }
        break;
    case 4U:
        if (delta != 0 || activate) {
            g_continuousStereoEnabled = !g_continuousStereoEnabled;
            changed = true;
        }
        break;
    case 5U:
        if (delta != 0) {
            g_toolMenuPanelPlacement.scale = std::clamp(
                g_toolMenuPanelPlacement.scale +
                    static_cast<float>(delta) * 0.05F,
                0.40F, 0.90F);
            changed = true;
        }
        break;
    case 6U:
        if (delta != 0) {
            g_toolMenuPanelPlacement.distanceMeters = std::clamp(
                g_toolMenuPanelPlacement.distanceMeters +
                    static_cast<float>(delta) * 0.10F,
                0.75F, 3.0F);
            changed = true;
        }
        break;
    case 7U:
        if (activate) {
            g_trackingRecenterPending = true;
            changed = true;
        }
        break;
    case 8U:
        if (activate) {
            g_tuningUnitsPerMeter = 100.0F;
            g_tuningReversePolarity = false;
            g_tuningFovScale = kCondemnedDefaultFovScale;
            g_hmdTranslationEnabled = true;
            g_continuousStereoEnabled = true;
            g_toolMenuPanelPlacement = {};
            if (g_setFovScalePercent != nullptr) {
                g_setFovScalePercent(130U);
            }
            g_trackingRecenterPending = true;
            changed = true;
        }
        break;
    default:
        break;
    }
    if (changed) {
        LogStereoTuningState("vr_tool_menu");
    }
    return changed;
}
bool ApplyToolMenuPlayerColliderAdjustment(
    std::uint32_t row,
    int delta,
    bool activate) noexcept {
    if (row == 2U) {
        if (!activate && delta == 0) {
            return false;
        }
        SetPlayerCollisionXrayEnabled(
            !PlayerCollisionXrayEnabled());
        return true;
    }
    PlayerColliderSettings settings =
        ReadPlayerColliderSettings();
    if (!UpdatePlayerColliderSettings(
            settings, row, delta, activate) ||
        !ConfigurePlayerColliderSettings(settings)) {
        return false;
    }

    const WeaponSettingsStoreResult saveResult =
        SavePlayerColliderSettings(settings);
    if (g_passThroughLog != nullptr) {
        char detail[192]{};
        std::snprintf(
            detail, sizeof(detail),
            "result=%s width_scale=%.2f runtime_configured=1 "
            "height_policy=retail_preserved",
            WeaponSettingsStoreResultName(saveResult),
            settings.widthScale);
        g_passThroughLog(
            saveResult == WeaponSettingsStoreResult::Ok
                ? "m5_player_collider_settings_saved"
                : "m5_player_collider_settings_save_failed",
            detail);
    }
    return true;
}


bool ApplyVrToolMenuDebugDrawAdjustment(
    std::uint32_t row,
    int delta,
    bool activate) noexcept {
    ToolMenuDebugDrawSettings settings{
        InterlockedCompareExchange(
            &g_physicalMeleeColliderDebugDrawVisible, 0, 0) != 0,
        InterlockedCompareExchange(
            &g_physicalMeleeBlockColliderDebugDrawVisible, 0, 0) != 0,
        InterlockedCompareExchange(
            &g_weaponGripControllerDebugDrawVisible, 0, 0) != 0};
    if (!UpdateToolMenuDebugDrawSettings(
            settings, row, delta, activate)) {
        return false;
    }
    InterlockedExchange(
        &g_physicalMeleeColliderDebugDrawVisible,
        settings.colliderVisible ? 1 : 0);
    InterlockedExchange(
        &g_physicalMeleeBlockColliderDebugDrawVisible,
        settings.blockColliderVisible ? 1 : 0);
    InterlockedExchange(
        &g_weaponGripControllerDebugDrawVisible,
        settings.controllerVisible ? 1 : 0);
    if (row == 0U && settings.colliderVisible) {
        InterlockedExchange(
            &g_physicalMeleeColliderFailureLogged, 0);
    } else if (row == 1U && settings.blockColliderVisible) {
        InterlockedExchange(
            &g_physicalMeleeBlockColliderFailureLogged, 0);
    } else if (row == 2U && settings.controllerVisible) {
        InterlockedExchange(
            &g_weaponGripControllerGizmoFailureLogged, 0);
    }
    const WeaponSettingsStoreResult saveResult =
        SaveDebugDrawSettings(settings);
    if (g_passThroughLog != nullptr) {
        char detail[160]{};
        std::snprintf(
            detail, sizeof(detail),
            "result=%s collider=%u block_collider=%u controller=%u",
            WeaponSettingsStoreResultName(saveResult),
            settings.colliderVisible ? 1U : 0U,
            settings.blockColliderVisible ? 1U : 0U,
            settings.controllerVisible ? 1U : 0U);
        g_passThroughLog(
            saveResult == WeaponSettingsStoreResult::Ok
                ? "m5_debug_draw_settings_saved"
                : "m5_debug_draw_settings_save_failed",
            detail);
    }
    return true;
}

struct ToolMenuButtonLatch {
    bool releaseRequired{true};
    bool wasDown{false};
};

bool ConsumeToolMenuButton(
    ToolMenuButtonLatch& latch,
    bool usable,
    bool down) noexcept {
    if (!usable) {
        latch.releaseRequired = true;
        latch.wasDown = false;
        return false;
    }
    if (!down) {
        latch.releaseRequired = false;
        latch.wasDown = false;
        return false;
    }
    if (latch.releaseRequired || latch.wasDown) {
        latch.wasDown = true;
        return false;
    }
    latch.wasDown = true;
    return true;
}

void HandleVrToolMenuControls() noexcept {
    if (InterlockedCompareExchange(&g_toolMenuEnabled, 0, 0) == 0) {
        return;
    }
    if (!GameOwnsForegroundWindow()) {
        CancelEmptyRightHandAlignmentMode(
            "cancel_focus_lost");
        CancelHeldObjectAlignmentMode(
            "cancel_focus_lost");
        return;
    }
    static bool f12Down = false;
    static bool keyPreviousTabDown = false;
    static bool keyNextTabDown = false;
    static bool keyPreviousRowDown = false;
    static bool keyNextRowDown = false;
    static bool keyDecreaseDown = false;
    static bool keyIncreaseDown = false;
    static bool keyActivateDown = false;
    static ToolMenuButtonLatch toggleLatch{};
    static ToolMenuButtonLatch closeLatch{};
    static ToolMenuButtonLatch previousTabLatch{};
    static ToolMenuButtonLatch nextTabLatch{};
    static ToolMenuButtonLatch previousRowLatch{};
    static ToolMenuButtonLatch nextRowLatch{};
    static ToolMenuButtonLatch decreaseLatch{};
    static ToolMenuButtonLatch increaseLatch{};
    static ToolMenuButtonLatch activateLatch{};
    static std::uint64_t observedSampleId = 0;
    static ULONGLONG observedTick = 0;

    const ULONGLONG now = GetTickCount64();
    FearVrInputState input{};
    bool fresh = false;
    if (g_getInputState != nullptr &&
        g_getInputState(&input) != FALSE && input.sampleId != 0U) {
        if (input.sampleId != observedSampleId) {
            observedSampleId = input.sampleId;
            observedTick = now;
        }
        fresh = observedTick != 0 &&
            now - observedTick <= kHeadAimFreshnessMilliseconds &&
            fearvr::IsInputStateUsable(input, true);
    }

    ToolMenuInputEvent event{};
    const bool toggleChord = ToolMenuToggleChordDown(input, fresh);
    if (!g_toolMenuState.open && fresh && !toggleChord &&
        (input.buttons &
         (FEARVR_IB_RIGHT_PRIMARY |
          FEARVR_IB_RIGHT_SECONDARY)) == 0U &&
        input.squeeze[FEARVR_HAND_LEFT] < 0.35F &&
        input.squeeze[FEARVR_HAND_RIGHT] < 0.35F &&
        input.trigger[FEARVR_HAND_LEFT] < 0.35F &&
        input.trigger[FEARVR_HAND_RIGHT] < 0.35F &&
        std::fabs(input.moveY) < 0.35F &&
        std::fabs(input.turnX) < 0.35F) {
        InterlockedExchange(&g_toolMenuReleaseCapture, 0);
    }
    const bool shortcutRequested =
        PressedOnce(VK_F12, f12Down) ||
        ConsumeToolMenuButton(
            toggleLatch, fresh, toggleChord);
    const bool menuWasOpen = g_toolMenuState.open;
    const bool shortcutEnabled = InterlockedCompareExchange(
        &g_toolMenuShortcutEnabled, 0, 0) != 0;
    event.toggle = ShouldActivateToolMenuShortcut(
        menuWasOpen, shortcutEnabled, shortcutRequested);
    if (event.toggle) {
        if (menuWasOpen) {
            CancelHeldObjectAlignmentMode(
                "cancel_menu_closed");
            CancelEmptyRightHandAlignmentMode(
                "cancel_menu_closed");
        }
        const ToolMenuTransition transition = UpdateToolMenuState(
            g_toolMenuState, event);
        SetToolMenuOpen(g_toolMenuState.open);
        LogToolMenuState(
            transition.opened ? "open" : "close_toggle");
        return;
    }
    if (!menuWasOpen) {
        closeLatch = {};
        previousTabLatch = {};
        nextTabLatch = {};
        previousRowLatch = {};
        nextRowLatch = {};
        decreaseLatch = {};
        increaseLatch = {};
        activateLatch = {};
        return;
    }

    event.close = ConsumeToolMenuButton(
        closeLatch, fresh,
        (input.buttons & FEARVR_IB_RIGHT_SECONDARY) != 0U);
    const bool alignmentCapturesControls =
        GuidedAlignmentCapturesTriggers();
    const bool previousTabRequested =
        PressedOnce(VK_OEM_4, keyPreviousTabDown) ||
        ConsumeToolMenuButton(
            previousTabLatch, fresh,
            input.trigger[FEARVR_HAND_LEFT] >= 0.65F);
    const bool nextTabRequested =
        PressedOnce(VK_OEM_6, keyNextTabDown) ||
        ConsumeToolMenuButton(
            nextTabLatch, fresh,
            input.trigger[FEARVR_HAND_RIGHT] >= 0.65F);
    event.previousTab =
        !alignmentCapturesControls && previousTabRequested;
    event.nextTab =
        !alignmentCapturesControls && nextTabRequested;
    event.previousRow =
        PressedOnce(VK_UP, keyPreviousRowDown) ||
        ConsumeToolMenuButton(
            previousRowLatch, fresh, input.moveY >= 0.65F);
    event.nextRow =
        PressedOnce(VK_DOWN, keyNextRowDown) ||
        ConsumeToolMenuButton(
            nextRowLatch, fresh, input.moveY <= -0.65F);
    event.decrease =
        PressedOnce(VK_LEFT, keyDecreaseDown) ||
        ConsumeToolMenuButton(
            decreaseLatch, fresh, input.turnX <= -0.65F);
    event.increase =
        PressedOnce(VK_RIGHT, keyIncreaseDown) ||
        ConsumeToolMenuButton(
            increaseLatch, fresh, input.turnX >= 0.65F);
    if (alignmentCapturesControls) {
        event.previousRow = false;
        event.nextRow = false;
        event.decrease = false;
        event.increase = false;
    }
    event.activate =
        PressedOnce(VK_RETURN, keyActivateDown) ||
        ConsumeToolMenuButton(
            activateLatch, fresh,
            (input.buttons & FEARVR_IB_RIGHT_PRIMARY) != 0U);

    const bool emptyHandIkPage =
        ToolMenuUsesEmptyRightHandAlignmentPage();
    const ToolMenuTab oldTab = g_toolMenuState.tab;
    const ToolMenuTransition transition = UpdateToolMenuState(
        g_toolMenuState, event, emptyHandIkPage);
    SetToolMenuOpen(g_toolMenuState.open);
    if (transition.closed) {
        CancelEmptyRightHandAlignmentMode(
            "cancel_menu_closed");
        CancelHeldObjectAlignmentMode(
            "cancel_menu_closed");
        LogToolMenuState("close_back");
        return;
    }
    if (oldTab != g_toolMenuState.tab) {
        if (EmptyRightHandAlignmentIsActive(
                g_emptyRightHandAlignmentState)) {
            CancelEmptyRightHandAlignmentMode("cancel_tab_changed");
        }
        if (HeldObjectAlignmentIsActive(
                g_heldObjectAlignmentState)) {
            CancelHeldObjectAlignmentMode("cancel_tab_changed");
        }
        UpdateToolMenuCalibrationVisibility();
    }
    bool valueChanged = false;
    if (g_toolMenuState.tab == ToolMenuTab::Melee ||
        g_toolMenuState.tab == ToolMenuTab::Weapon) {
        ToolMenuMeleeTelemetry telemetry{};
        ReadPhysicalMeleeToolTelemetry(telemetry);
        valueChanged = ApplyToolMenuMeleeAdjustment(
            g_toolMenuState.tab, g_toolMenuState.row,
            transition.valueDelta, transition.activate,
            telemetry.weaponIndex);
    } else if (g_toolMenuState.tab == ToolMenuTab::Block) {
        ToolMenuMeleeTelemetry telemetry{};
        ReadPhysicalMeleeToolTelemetry(telemetry);
        valueChanged = ApplyToolMenuBlockPoseAdjustment(
            g_toolMenuState.row, transition.valueDelta,
            transition.activate, telemetry.weaponIndex);
    } else if (g_toolMenuState.tab == ToolMenuTab::Grip) {
        valueChanged = ApplyToolMenuGripAdjustment(
            g_toolMenuState.row, transition.valueDelta,
            transition.activate);
    } else if (g_toolMenuState.tab == ToolMenuTab::Collider ||
               g_toolMenuState.tab == ToolMenuTab::BlockCollider) {
        ToolMenuMeleeTelemetry telemetry{};
        ReadPhysicalMeleeToolTelemetry(telemetry);
        valueChanged = ApplyToolMenuColliderAdjustment(
            g_toolMenuState.row, transition.valueDelta,
            transition.activate, telemetry.weaponIndex,
            g_toolMenuState.tab == ToolMenuTab::BlockCollider);
    } else if (g_toolMenuState.tab == ToolMenuTab::Author) {
        if (g_toolMenuState.row == 0U &&
            (transition.valueDelta != 0 ||
             transition.activate)) {
            CycleInteractionAuthoringPrimitive(
                g_authoringPrimitive,
                transition.valueDelta != 0
                    ? transition.valueDelta : 1);
            valueChanged = true;
        } else if (g_authoringPrimitive ==
                   InteractionAuthoringPrimitive::MagazineInsertSocket) {
            valueChanged =
                ApplyToolMenuMagazineSocketAdjustment(
                    g_toolMenuState.row,
                    transition.valueDelta,
                    transition.activate);
        } else {
            valueChanged = ApplyToolMenuSlideGrabAdjustment(
                g_toolMenuState.row,
                transition.valueDelta,
                transition.activate);
        }
    } else if (
        g_toolMenuState.tab == ToolMenuTab::PlayerCollider) {
        valueChanged = ApplyToolMenuPlayerColliderAdjustment(
            g_toolMenuState.row, transition.valueDelta,
            transition.activate);
    } else if (g_toolMenuState.tab == ToolMenuTab::TwoHand) {
        valueChanged = ApplyToolMenuTwoHandAdjustment(
            g_toolMenuState.row, transition.valueDelta,
            transition.activate, input, fresh);
    } else if (g_toolMenuState.tab == ToolMenuTab::HandIk) {
        if (emptyHandIkPage) {
            valueChanged =
                ApplyToolMenuEmptyRightHandAlignmentAction(
                    g_toolMenuState.row, transition.valueDelta,
                    transition.activate);
        } else {
            ToolMenuMeleeTelemetry telemetry{};
            ReadPhysicalMeleeToolTelemetry(telemetry);
            valueChanged = ApplyToolMenuRightHandIkAdjustment(
                g_toolMenuState.row, transition.valueDelta,
                transition.activate, telemetry.weaponIndex);
        }
    } else if (g_toolMenuState.tab == ToolMenuTab::LeftHandIk) {
        valueChanged = ApplyToolMenuLeftHandIkAdjustment(
            g_toolMenuState.row, transition.valueDelta,
            transition.activate);
    } else if (g_toolMenuState.tab == ToolMenuTab::ElbowIk) {
        valueChanged = ApplyToolMenuElbowIkAdjustment(
            g_toolMenuState.row, transition.valueDelta,
            transition.activate);
    } else if (g_toolMenuState.tab == ToolMenuTab::Display) {
        valueChanged = ApplyToolMenuDisplayAdjustment(
            g_toolMenuState.row, transition.valueDelta,
            transition.activate);
    } else if (g_toolMenuState.tab == ToolMenuTab::Debug) {
        valueChanged = ApplyVrToolMenuDebugDrawAdjustment(
            g_toolMenuState.row, transition.valueDelta,
            transition.activate);
    }
    if (valueChanged &&
        (g_toolMenuState.tab == ToolMenuTab::Grip ||
         g_toolMenuState.tab == ToolMenuTab::TwoHand)) {
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_changed",
            "vr_tool_menu_adjust");
    }
    if (valueChanged &&
        g_toolMenuState.tab == ToolMenuTab::HandIk &&
        !emptyHandIkPage) {
        ToolMenuMeleeTelemetry telemetry{};
        ReadPhysicalMeleeToolTelemetry(telemetry);
        LogToolMenuRightHandIkState(
            "vr_tool_menu_adjust", telemetry.weaponIndex);
    }
    if (valueChanged &&
        g_toolMenuState.tab == ToolMenuTab::ElbowIk) {
        LogToolMenuElbowIkState("vr_tool_menu_adjust");
    }
    if (valueChanged &&
        g_toolMenuState.tab == ToolMenuTab::LeftHandIk) {
        LogToolMenuLeftHandIkState("vr_tool_menu_adjust");
    }
    if (transition.selectionChanged || valueChanged) {
        LogToolMenuState(
            valueChanged ? "adjust" : "navigate");
    }
}

void AddToolMenuRow(
    ToolMenuOverlay& overlay,
    std::uint32_t row,
    const char* text,
    bool selected,
    std::uint32_t textColor = 0xFFF2F6FAU) noexcept {
    constexpr float left = -0.75F;
    constexpr float rowTop = 0.31F;
    constexpr float rowStep = 0.105F;
    const float y = rowTop - static_cast<float>(row) * rowStep;
    if (selected) {
        AddToolMenuRectangle(
            overlay, -0.79F, y + 0.035F,
            0.79F, y - 0.070F, 0xB8327898U);
    }
    AddToolMenuText(
        overlay, left, y, 0.0046F, 0.0072F,
        text, selected ? 0xFFFFFFFFU : textColor);
}

void DrawVrToolMenuOverlay(
    std::uint32_t eye,
    float interpupillaryDistanceMeters,
    float horizontalFovRadians) noexcept {
    if (InterlockedCompareExchange(&g_toolMenuOpen, 0, 0) == 0 ||
        g_drawOverlayTriangles == nullptr) {
        return;
    }
    ToolMenuOverlay& overlay = g_toolMenuOverlay;
    overlay.count = 0U;
    overlay.overflowed = false;
    AddToolMenuRectangle(
        overlay, -0.91F, 0.80F, 0.91F, -0.78F, 0xD0141B26U);
    AddToolMenuRectangle(
        overlay, -0.91F, 0.80F, 0.91F, 0.786F, 0xFF53C7E8U);
    AddToolMenuRectangle(
        overlay, -0.91F, -0.766F, 0.91F, -0.78F, 0xFF53C7E8U);
    AddToolMenuText(
        overlay, -0.79F, 0.70F, 0.0054F, 0.0090F,
        "CONDEMNED VR TOOLS", 0xFF76DBF4U);

    constexpr float tabStart = -0.80F;
    constexpr float tabSpan = 1.60F;
    constexpr float tabWidth = tabSpan /
        static_cast<float>(ToolMenuTab::Count);
    for (std::uint32_t index = 0;
         index < static_cast<std::uint32_t>(ToolMenuTab::Count);
         ++index) {
        const float x = tabStart + static_cast<float>(index) * tabWidth;
        const bool selected = index ==
            static_cast<std::uint32_t>(g_toolMenuState.tab);
        if (selected) {
            AddToolMenuRectangle(
                overlay, x - 0.025F, 0.565F,
                x + tabWidth - 0.035F, 0.465F, 0xCC24566FU);
        }
        AddToolMenuText(
            overlay, x, 0.535F, 0.00335F, 0.0060F,
            ToolMenuTabName(static_cast<ToolMenuTab>(index)),
            selected ? 0xFFFFFFFFU : 0xFF95A5B2U);
    }

    ToolMenuMeleeTelemetry telemetry{};
    ReadPhysicalMeleeToolTelemetry(telemetry);
    const PhysicalMeleeProfile baseProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(
            telemetry.weaponIndex);
    const ToolMenuMeleeSettings settings =
        CopyToolMenuMeleeSettings(telemetry.weaponIndex);
    const PhysicalMeleeBlockPoseSettings blockPoseSettings =
        CopyToolMenuBlockPoseSettings(telemetry.weaponIndex);
    const ToolMenuBlockTimingSettings blockTimingSettings =
        CopyToolMenuBlockTimingSettings(telemetry.weaponIndex);
    const ToolMenuColliderSettings colliderSettings =
        CopyToolMenuColliderSettings(telemetry.weaponIndex);
    bool blockColliderUsesAttackFallback = true;
    const ToolMenuColliderSettings blockColliderSettings =
        CopyToolMenuBlockColliderSettings(
            telemetry.weaponIndex,
            blockColliderUsesAttackFallback);
    const ToolMenuRightHandIkSettings rightHandIkSettings =
        CopyToolMenuRightHandIkSettings(telemetry.weaponIndex);
    const fearvr::ArmIkTuning armIkTuning = ReadArmIkTuning();
    PlayerColliderTelemetry playerColliderTelemetry{};
    ReadPlayerColliderTelemetry(playerColliderTelemetry);
    const bool emptyHandIkPage =
        ToolMenuUsesEmptyRightHandAlignmentPage();
    PhysicalMeleeGripCalibration grip{};
    void* gripWeapon = nullptr;
    void* gripModel = nullptr;
    std::int32_t gripWeaponIndex = -1;
    std::uint64_t gripGeneration = 0;
    const bool haveGrip = CopyActiveWeaponGripCalibration(
        grip, gripWeapon, gripWeaponIndex,
        gripModel, gripGeneration);
    PhysicalMeleeBlockPoseResult blockPosePreview{};
    bool blockPosePreviewTrackingFresh = false;
    if (PhysicalMeleeProfileMatchesOneHandedWeaponIndex(
            telemetry.weaponIndex, baseProfile.id)) {
        float headPosition[3]{};
        float headRotation[4]{};
        float weaponPosition[3]{};
        float weaponRotation[4]{};
        std::uint64_t sampleId = 0U;
        std::uint64_t timestampNs = 0U;
        blockPosePreviewTrackingFresh =
            CopyFreshTrackedHeadWorldPose(
                headPosition, headRotation) &&
            CopyFreshTrackedControllerWorldPose(
                weaponPosition, weaponRotation,
                sampleId, timestampNs);
        PhysicalMeleeBlockPoseState previewState{};
        const PhysicalMeleeBlockWorldPose head{
            {headPosition[0], headPosition[1], headPosition[2]},
            {headRotation[0], headRotation[1],
             headRotation[2], headRotation[3]}};
        const PhysicalMeleeBlockWorldPose weapon{
            {weaponPosition[0], weaponPosition[1], weaponPosition[2]},
            {weaponRotation[0], weaponRotation[1],
             weaponRotation[2], weaponRotation[3]}};
        blockPosePreview = EvaluatePhysicalMeleeBlockPose(
            blockPoseSettings, head, weapon,
            baseProfile.unitsPerMeter,
            blockPosePreviewTrackingFresh, previewState);
    }
    char rowText[128]{};
    const auto Row = [&](std::uint32_t row, const char* text) {
        AddToolMenuRow(
            overlay, row, text,
            row == g_toolMenuState.row);
    };

    if (g_toolMenuState.tab == ToolMenuTab::PlayerCollider) {
        std::snprintf(
            rowText, sizeof(rowText),
            "LOCAL PLAYER STICK-LOCOMOTION COLLIDER");
    } else if (telemetry.weaponIndex >= 0) {
        std::snprintf(
            rowText, sizeof(rowText),
            "EQUIPPED  %s   INDEX %ld",
            telemetry.weaponName[0] != '\0'
                ? telemetry.weaponName
                : ToolMenuWeaponProfileLabel(baseProfile.id),
            static_cast<long>(telemetry.weaponIndex));
    } else {
        std::snprintf(
            rowText, sizeof(rowText), "NO WEAPON EQUIPPED");
    }
    AddToolMenuText(
        overlay, -0.75F, 0.405F, 0.0041F, 0.0068F,
        rowText, g_toolMenuState.tab == ToolMenuTab::PlayerCollider ||
                telemetry.weaponIndex >= 0
            ? 0xFF76DBF4U : 0xFFFFB060U);

    switch (g_toolMenuState.tab) {
    case ToolMenuTab::Melee:
        std::snprintf(rowText, sizeof(rowText),
            "REQUIRE SWING                %s",
            settings.requireSwingForContactDamage ? "ON" : "OFF");
        Row(0U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "HIT SPEED                    %.2F M/S",
            settings.hitSpeedMetersPerSecond);
        Row(1U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "REARM TRAVEL                 %.2F M",
            settings.contactRearmDistanceMeters);
        Row(2U, rowText);
        Row(3U, "RESET PHYSICAL HIT DEFAULTS");
        if (telemetry.contactTrackingFresh) {
            std::snprintf(rowText, sizeof(rowText),
                "LIVE SPEED %.2F M/S  FAST ENOUGH %s",
                telemetry.contactSpeedMetersPerSecond,
                telemetry.contactFastEnough ? "YES" : "NO");
        } else {
            std::snprintf(rowText, sizeof(rowText),
                "LIVE SPEED -- M/S  FAST ENOUGH WAITING");
        }
        AddToolMenuRow(
            overlay, 4U, rowText, false,
            telemetry.contactTrackingFresh
                ? (telemetry.contactFastEnough
                       ? 0xFF50FF80U : 0xFFFFB060U)
                : 0xFF95A5B2U);
        if (telemetry.contactLatched) {
            std::snprintf(
                rowText, sizeof(rowText),
                "HIT STATE LATCHED  TRAVEL %s  RESET %u/%u (< %.2F M/S)",
                telemetry.contactRearmTravelReady ? "YES" : "NO",
                telemetry.contactReleaseSampleCount,
                kPhysicalMeleeContactReleaseSampleCount,
                telemetry.contactReleaseSpeedMetersPerSecond);
        } else {
            std::snprintf(
                rowText, sizeof(rowText),
                "HIT STATE READY  NEXT SWING MAY DAMAGE");
        }
        AddToolMenuRow(
            overlay, 5U, rowText, false,
            telemetry.contactLatched
                ? 0xFFFFB060U : 0xFF50FF80U);
        std::snprintf(rowText, sizeof(rowText),
            "CONTACT DAMAGE %s  CALLBACKS %u  HITS %u",
            telemetry.contactDamageEnabled ? "ON" : "OFF",
            telemetry.contactCallbackCount,
            telemetry.damageDispatchCount);
        AddToolMenuRow(overlay, 6U, rowText, false);
        break;
    case ToolMenuTab::Block:
        if (!PhysicalMeleeProfileMatchesOneHandedWeaponIndex(
                telemetry.weaponIndex, baseProfile.id)) {
            AddToolMenuRow(
                overlay, 0U,
                "EQUIP A SUPPORTED ONE-HANDED MELEE WEAPON",
                false, 0xFFFFB060U);
            AddToolMenuRow(
                overlay, 1U,
                "BLOCK POSES FAIL CLOSED FOR OTHER WEAPONS",
                false, 0xFF95A5B2U);
            break;
        }
        std::snprintf(
            rowText, sizeof(rowText),
            "POSE BLOCKING                 %s",
            !blockPoseSettings.captured
                ? "NOT SET"
                : blockPoseSettings.enabled ? "ON" : "OFF");
        Row(0U, rowText);
        Row(1U, "CAPTURE CURRENT GUARD POSE");
        std::snprintf(
            rowText, sizeof(rowText),
            "POSITION TOLERANCE            %.2F M",
            blockPoseSettings.positionToleranceMeters);
        Row(2U, rowText);
        std::snprintf(
            rowText, sizeof(rowText),
            "ANGLE TOLERANCE               %.1F DEG",
            blockPoseSettings.angleToleranceDegrees);
        Row(3U, rowText);
        std::snprintf(
            rowText, sizeof(rowText),
            "CUSTOM BLOCK WINDOW           %s",
            blockTimingSettings.overrideEnabled ? "ON" : "OFF (RETAIL)");
        Row(4U, rowText);
        std::snprintf(
            rowText, sizeof(rowText),
            "BLOCK WINDOW                  %u MS",
            blockTimingSettings.collisionWindowMilliseconds);
        Row(5U, rowText);
        Row(6U, "CLEAR SAVED GUARD POSE");
        std::snprintf(
            rowText, sizeof(rowText),
            "LIVE IN GUARD POSE            %s",
            !blockPoseSettings.captured
                ? "CAPTURE FIRST"
                : !blockPosePreviewTrackingFresh
                    ? "TRACKING WAIT"
                    : blockPosePreview.active ? "YES" : "NO");
        AddToolMenuRow(
            overlay, 7U, rowText, false,
            blockPosePreview.active
                ? 0xFF50FF80U
                : blockPosePreviewTrackingFresh
                    ? 0xFFFFB060U : 0xFF95A5B2U);
        if (blockPosePreview.poseValid) {
            std::snprintf(
                rowText, sizeof(rowText),
                "ERROR  POSITION %.2F M   ANGLE %.1F DEG",
                blockPosePreview.positionErrorMeters,
                blockPosePreview.angleErrorDegrees);
        } else {
            std::snprintf(
                rowText, sizeof(rowText),
                "ERROR  --   STATE %s",
                PhysicalMeleeBlockPoseReasonName(
                    blockPosePreview.reason));
        }
        AddToolMenuRow(overlay, 8U, rowText, false);
        AddToolMenuRow(
            overlay, 9U,
            "ENTER POSE = AUTO BLOCK   NO TRIGGER REQUIRED",
            false, 0xFF76DBF4U);
        std::snprintf(
            rowText, sizeof(rowText),
            "GAMEPLAY ACTIVE %s   ACTIVATIONS %u",
            telemetry.blockPoseActive ? "YES" : "NO",
            telemetry.blockPoseActivationCount);
        AddToolMenuRow(overlay, 10U, rowText, false,
            telemetry.blockPoseActive
                ? 0xFF50FF80U : 0xFF95A5B2U);
        if (telemetry.lastRetailBlockWindowMilliseconds > 0.0F) {
            std::snprintf(
                rowText, sizeof(rowText),
                "NATIVE BLOCK %s  RETAIL %.0F MS  APPLIED %.0F MS",
                telemetry.blockCollisionBodyLive ? "LIVE" : "RECENT",
                telemetry.lastRetailBlockWindowMilliseconds,
                telemetry.lastAppliedBlockWindowMilliseconds);
        } else {
            std::snprintf(
                rowText, sizeof(rowText),
                "NATIVE BLOCK WAITING FOR NEXT BLOCK ENTRY");
        }
        AddToolMenuRow(
            overlay, 11U, rowText, false,
            telemetry.blockCollisionBodyLive
                ? 0xFF50E8FFU : 0xFF95A5B2U);
        break;
    case ToolMenuTab::Weapon:
        std::snprintf(rowText, sizeof(rowText),
            "IMPACT MASS                   %.2F KG",
            settings.massKilograms);
        Row(0U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "HAND INERTIA                  %.2F",
            settings.handlingWeight);
        Row(1U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "POSITION FOLLOW               %.1F",
            settings.positionalFollow);
        Row(2U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "ROTATION FOLLOW               %.1F",
            settings.rotationalFollow);
        Row(3U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "CATCH UP                      %.2F",
            settings.catchUpStrength);
        Row(4U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "DAMPING                       %.2F",
            settings.dampingRatio);
        Row(5U, rowText);
        Row(6U, "RESET WEAPON DEFAULTS");
        break;
    case ToolMenuTab::Grip:
        if (!haveGrip) {
            AddToolMenuRow(
                overlay, 0U,
                "NO EQUIPPED WEAPON PROFILE AVAILABLE",
                false, 0xFFFFB060U);
            break;
        }
        std::snprintf(rowText, sizeof(rowText),
            "POSITION X                    %.3F",
            grip.positionUnits.x);
        Row(0U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "POSITION Y                    %.3F",
            grip.positionUnits.y);
        Row(1U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "POSITION Z                    %.3F",
            grip.positionUnits.z);
        Row(2U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "ROTATION X                    %.2F DEG",
            grip.localRotationDegrees.x);
        Row(3U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "ROTATION Y                    %.2F DEG",
            grip.localRotationDegrees.y);
        Row(4U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "ROTATION Z                    %.2F DEG",
            grip.localRotationDegrees.z);
        Row(5U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "ADJUSTMENT STEP               %.2F",
            kWeaponGripTranslationSteps[
                g_weaponGripCalibrationStepIndex]);
        Row(6U, rowText);
        Row(7U, "RESET CURRENT GRIP");
        Row(
            8U,
            "ALIGN HAND + WEAPON TO CONTROLLER");
        Row(
            9U,
            HeldObjectAlignmentIsActive(g_heldObjectAlignmentState)
                ? "CANCEL ADVANCED FROZEN ALIGNMENT"
                : "ADVANCED: FROZEN TWO-POSE ALIGNMENT");
        if (g_heldObjectAlignmentState.phase ==
            HeldObjectAlignmentPhase::AwaitReferencePoses) {
            std::snprintf(
                rowText, sizeof(rowText),
                "STEP 1  POSITION WEAPON, PULL TRIGGER TO FREEZE");
        } else if (g_heldObjectAlignmentState.phase ==
                   HeldObjectAlignmentPhase::AwaitControllerPose) {
            std::snprintf(
                rowText, sizeof(rowText),
                "STEP 2  WEAPON FROZEN: MOVE HAND INTO GRIP, PULL");
        } else if (g_heldObjectAlignmentLastEvent ==
                   HeldObjectAlignmentEvent::Completed) {
            if (g_heldObjectAlignmentLastGripSaveResult ==
                    WeaponSettingsStoreResult::Ok &&
                g_heldObjectAlignmentLastHandSaveResult ==
                    WeaponSettingsStoreResult::Ok &&
                g_heldObjectAlignmentLastColliderSaveResult ==
                    WeaponSettingsStoreResult::Ok) {
                std::snprintf(
                    rowText, sizeof(rowText),
                    "READY  LAST ALIGNMENT APPLIED AND SAVED");
            } else {
                std::snprintf(
                    rowText, sizeof(rowText),
                    "READY  SAVE FAILED  G %s  H %s  C %s",
                    WeaponSettingsStoreResultName(
                        g_heldObjectAlignmentLastGripSaveResult),
                    WeaponSettingsStoreResultName(
                        g_heldObjectAlignmentLastHandSaveResult),
                    WeaponSettingsStoreResultName(
                        g_heldObjectAlignmentLastColliderSaveResult));
            }
        } else if (g_heldObjectAlignmentLastEvent !=
                   HeldObjectAlignmentEvent::None) {
            std::snprintf(
                rowText, sizeof(rowText),
                "READY  ADVANCED EVENT %s - VALUES UNCHANGED",
                HeldObjectAlignmentEventName(
                    g_heldObjectAlignmentLastEvent));
        } else if (g_heldAssemblyControllerAlignmentLastEvent ==
                   HeldAssemblyControllerAlignmentEvent::Applied) {
            if (g_heldAssemblyControllerAlignmentLastGripSaveResult ==
                    WeaponSettingsStoreResult::Ok &&
                g_heldAssemblyControllerAlignmentLastHandSaveResult ==
                    WeaponSettingsStoreResult::Ok &&
                g_heldAssemblyControllerAlignmentLastColliderSaveResult ==
                    WeaponSettingsStoreResult::Ok) {
                std::snprintf(
                    rowText, sizeof(rowText),
                    "READY  HAND + WEAPON ALIGNED AND SAVED");
            } else {
                std::snprintf(
                    rowText, sizeof(rowText),
                    "READY  APPLIED, SAVE FAILED  G %s H %s C %s",
                    WeaponSettingsStoreResultName(
                        g_heldAssemblyControllerAlignmentLastGripSaveResult),
                    WeaponSettingsStoreResultName(
                        g_heldAssemblyControllerAlignmentLastHandSaveResult),
                    WeaponSettingsStoreResultName(
                        g_heldAssemblyControllerAlignmentLastColliderSaveResult));
            }
        } else if (g_heldAssemblyControllerAlignmentLastEvent !=
                   HeldAssemblyControllerAlignmentEvent::None) {
            std::snprintf(
                rowText, sizeof(rowText),
                "READY  ALIGN %s - VALUES UNCHANGED",
                HeldAssemblyControllerAlignmentEventName(
                    g_heldAssemblyControllerAlignmentLastEvent));
        } else {
            std::snprintf(
                rowText, sizeof(rowText),
                "ONE PRESS PRESERVES CURRENT GUN-IN-HAND FIT");
        }
        AddToolMenuRow(
            overlay, 10U, rowText, false,
            HeldObjectAlignmentIsActive(g_heldObjectAlignmentState)
                ? 0xFFFFD060U
                : (g_heldObjectAlignmentLastEvent ==
                           HeldObjectAlignmentEvent::Completed ||
                       g_heldAssemblyControllerAlignmentLastEvent ==
                           HeldAssemblyControllerAlignmentEvent::Applied)
                    ? 0xFF50FF80U
                    : (g_heldObjectAlignmentLastEvent !=
                               HeldObjectAlignmentEvent::None ||
                           g_heldAssemblyControllerAlignmentLastEvent !=
                               HeldAssemblyControllerAlignmentEvent::None)
                        ? 0xFFFF8080U : 0xFF76DBF4U);
        break;
    case ToolMenuTab::Collider:
        std::snprintf(rowText, sizeof(rowText),
            "POSITION X                    %.2F U",
            colliderSettings.positionOffsetUnits.x);
        Row(0U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "POSITION Y                    %.2F U",
            colliderSettings.positionOffsetUnits.y);
        Row(1U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "POSITION Z                    %.2F U",
            colliderSettings.positionOffsetUnits.z);
        Row(2U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "PITCH X                       %.1F DEG",
            colliderSettings.rotationOffsetDegrees.x);
        Row(3U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "YAW Y                         %.1F DEG",
            colliderSettings.rotationOffsetDegrees.y);
        Row(4U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "ROLL Z                        %.1F DEG",
            colliderSettings.rotationOffsetDegrees.z);
        Row(5U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "LENGTH                        %.1F U",
            colliderSettings.lengthUnits);
        Row(6U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "RADIUS                        %.1F U",
            colliderSettings.radiusUnits);
        Row(7U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "DIRECTION                     %s",
            colliderSettings.reversed ? "REVERSED" : "FORWARD");
        Row(8U, rowText);
        Row(9U, "RESET COLLIDER DEFAULTS");
        break;
    case ToolMenuTab::Author: {
        if (g_authoringPrimitive ==
            InteractionAuthoringPrimitive::SlideGrabRail) {
            const bool identityReady =
                EnsureSlideGrabAuthoringIdentity();
            const SlideGrabAuthoringRuntimeState authoring =
                CopySlideGrabAuthoringState();
            Row(0U, "PRIMITIVE                     SLIDE GRAB RAIL");
            if (!identityReady) {
                AddToolMenuRow(
                    overlay, 1U,
                    authoring.invalidRecord
                        ? "INVALID SAVED SLIDE RECORD - FAIL CLOSED"
                        : "SLIDE RAIL UNAVAILABLE FOR THIS EXACT WEAPON",
                    false, 0xFFFF6060U);
                AddToolMenuRow(
                    overlay, 2U,
                    "COLT INDEX 76 HAS A VERIFIED UNSAVED RAIL SEED",
                    false, 0xFF95A5B2U);
                AddToolMenuRow(
                    overlay, 3U,
                    "NO NODE HANDLE, MODEL POINTER OR PIVOT CONTACT IS INFERRED",
                    false, 0xFF76DBF4U);
                break;
            }
            const SlideGrabRailSettings& slide =
                authoring.editor.current;
            std::snprintf(
                rowText, sizeof(rowText),
                "COMPONENT                     %s",
                SlideGrabComponentName(
                    authoring.editor.component));
            Row(1U, rowText);
            if (authoring.editor.component ==
                SlideGrabComponent::Activation) {
                std::snprintf(
                    rowText, sizeof(rowText),
                    "VALUE                         %s",
                    SlideGrabActivationInputName(
                        slide.activationInput));
            } else {
                std::snprintf(
                    rowText, sizeof(rowText),
                    "VALUE                         %.4F %s",
                    SlideGrabComponentValue(
                        slide, authoring.editor.component),
                    SlideGrabComponentUsesDegrees(
                        authoring.editor.component)
                        ? "DEG" : "CM");
            }
            Row(2U, rowText);
            std::snprintf(
                rowText, sizeof(rowText),
                "MOVEMENT                      %s  %.2F CM / %.2F DEG",
                authoring.editor.coarse ? "COARSE" : "FINE",
                authoring.editor.coarse ? 1.0F : 0.1F,
                authoring.editor.coarse ? 5.0F : 0.25F);
            Row(3U, rowText);
            Row(4U, "CAPTURE GRAB BOX + HAND POSE FROM LEFT GRIP");
            std::snprintf(
                rowText, sizeof(rowText),
                "UNDO                          %s  (%zu)",
                authoring.editor.undoCount != 0U
                    ? "AVAILABLE" : "EMPTY",
                authoring.editor.undoCount);
            Row(5U, rowText);
            Row(6U, "RESET TO LOADED SLIDE SETTINGS");
            const char* const slideStatus =
                !SlideGrabRailSettingsAreValid(slide)
                    ? "INVALID - SAVE DISABLED"
                    : !slide.configured
                        ? "NOT CONFIGURED - CAPTURE FIRST"
                        : authoring.editor.dirty
                            ? "UNSAVED - ACTIVATE TO SAVE"
                            : "CONFIGURED / SAVED";
            std::snprintf(
                rowText, sizeof(rowText),
                "SAVE EXACT WEAPON RECORD       %s",
                slideStatus);
            Row(7U, rowText);
            std::snprintf(
                rowText, sizeof(rowText),
                "NODE %s  INPUT %s  LOAD %s  SAVE %s",
                slide.nodeName,
                SlideGrabActivationInputName(
                    slide.activationInput),
                WeaponSettingsStoreResultName(
                    authoring.lastLoadResult),
                WeaponSettingsStoreResultName(
                    authoring.lastSaveResult));
            AddToolMenuRow(
                overlay, 8U, rowText, false,
                !SlideGrabRailSettingsAreValid(slide)
                    ? 0xFFFF6060U
                    : slide.configured ? 0xFF50FF80U
                                       : 0xFFFFB060U);
            std::snprintf(
                rowText, sizeof(rowText),
                "RAIL %.4F CM  REAR %.3F  GRAB %s  CONTROL %s",
                slide.maximumTravelUnits,
                slide.rearThresholdUnits,
                authoring.visualReady
                    ? (SlideGrabVolumeContains(
                           slide,
                           authoring.cursorModelLocal.positionUnits)
                           ? "OVERLAP" : "OUTSIDE")
                    : "TRACKING WAIT",
                slide.configured
                    ? "GUARDED"
                    : "DISABLED");
            AddToolMenuRow(
                overlay, 9U, rowText, false, 0xFF76DBF4U);
            break;
        }
        const bool identityReady =
            EnsureMagazineSocketAuthoringIdentity();
        const MagazineSocketAuthoringRuntimeState authoring =
            CopyMagazineSocketAuthoringState();
        if (!identityReady) {
            AddToolMenuRow(
                overlay, 0U,
                "EXACT HELD WEAPON IDENTITY UNAVAILABLE",
                false, 0xFFFFB060U);
            AddToolMenuRow(
                overlay, 1U,
                "REQUIRES RESOLVED CATALOG NAME + LIVE MODEL",
                false, 0xFF95A5B2U);
            AddToolMenuRow(
                overlay, 2U,
                "NO OFFSETS, BONES OR LAYOUTS ARE INFERRED",
                false, 0xFF76DBF4U);
            break;
        }
        const MagazineInsertionSocketSettings& socket =
            authoring.editor.current;
        Row(0U, "PRIMITIVE                     MAG INSERT SOCKET");
        std::snprintf(
            rowText, sizeof(rowText),
            "COMPONENT                     %s",
            MagazineSocketComponentName(
                authoring.editor.component));
        Row(1U, rowText);
        if (socket.configured) {
            std::snprintf(
                rowText, sizeof(rowText),
                "VALUE                         %.3F %s",
                MagazineSocketComponentValue(
                    socket, authoring.editor.component),
                MagazineSocketComponentUsesDegrees(
                    authoring.editor.component)
                    ? "DEG" : "CM");
        } else {
            std::snprintf(
                rowText, sizeof(rowText),
                "VALUE                         CAPTURE FIRST");
        }
        Row(2U, rowText);
        std::snprintf(
            rowText, sizeof(rowText),
            "MOVEMENT                      %s  %.2F CM / %.2F DEG",
            authoring.editor.coarse ? "COARSE" : "FINE",
            authoring.editor.coarse ? 1.0F : 0.1F,
            authoring.editor.coarse ? 5.0F : 0.25F);
        Row(3U, rowText);
        Row(4U, "CAPTURE SOCKET FROM LEFT GRIP");
        std::snprintf(
            rowText, sizeof(rowText),
            "UNDO                          %s  (%zu)",
            authoring.editor.undoCount != 0U
                ? "AVAILABLE" : "EMPTY",
            authoring.editor.undoCount);
        Row(5U, rowText);
        Row(6U, "RESET TO LAST LOADED RECORD");
        std::snprintf(
            rowText, sizeof(rowText),
            "MAG %s  AUTO-SAVE  LOAD %s  SAVE %s",
            socket.configured ? "CONFIGURED" : "NOT CONFIGURED",
            WeaponSettingsStoreResultName(
                authoring.lastLoadResult),
            WeaponSettingsStoreResultName(
                authoring.lastSaveResult));
        AddToolMenuRow(
            overlay, 7U, rowText, false,
            socket.configured ? 0xFF50FF80U : 0xFFFFB060U);
        break;
    }
    case ToolMenuTab::PlayerCollider: {
        const PlayerColliderSettings playerColliderSettings =
            ReadPlayerColliderSettings();
        std::snprintf(
            rowText, sizeof(rowText),
            "PLAYER WIDTH SCALE            %.0F %%",
            playerColliderSettings.widthScale * 100.0F);
        Row(0U, rowText);
        Row(1U, "RESET TO RETAIL WIDTH");
        PlayerCollisionXraySnapshot xrayStatus{};
        (void)ReadPlayerCollisionXraySnapshot(xrayStatus);
        std::snprintf(
            rowText, sizeof(rowText),
            "COLLISION X-RAY              %s",
            !PlayerCollisionXrayEnabled()
                ? "OFF"
                : xrayStatus.movementTraceReady
                    ? "ON" : "UNAVAILABLE");
        Row(2U, rowText);
        const char* const status =
            !playerColliderTelemetry.hookReady
                ? "UNAVAILABLE - RETAIL PASS-THROUGH"
                : playerColliderTelemetry.reapplyPending
                    ? "APPLY PENDING"
                    : playerColliderTelemetry.runtimeDriftObserved
                        ? "WIDTH OVERRIDE LOST"
                        : !playerColliderTelemetry.actualDimensionsValid
                            ? "WAITING FOR PLAYER"
                        : playerColliderTelemetry
                                  .lastRequestSatisfied
                            ? "LIVE"
                            : "REQUEST NOT SATISFIED";
        AddToolMenuRow(
            overlay, 3U, status, false,
            !playerColliderTelemetry.hookReady ||
                    (playerColliderTelemetry.actualDimensionsValid &&
                     !playerColliderTelemetry
                          .lastRequestSatisfied)
                ? 0xFFFF8080U
                : playerColliderTelemetry.reapplyPending
                    ? 0xFFFFD060U
                    : playerColliderTelemetry.runtimeDriftObserved
                        ? 0xFFFF8080U
                        : 0xFF50FF80U);
        if (playerColliderTelemetry.retailDimensionsValid) {
            std::snprintf(
                rowText, sizeof(rowText),
                "RETAIL DIMS X %.2F  Y %.2F  Z %.2F",
                playerColliderTelemetry.retailDimensions.x,
                playerColliderTelemetry.retailDimensions.y,
                playerColliderTelemetry.retailDimensions.z);
        } else {
            std::snprintf(
                rowText, sizeof(rowText),
                "RETAIL DIMS WAITING FOR NATIVE REQUEST");
        }
        AddToolMenuRow(
            overlay, 4U, rowText, false, 0xFF76DBF4U);
        if (playerColliderTelemetry.retailDimensionsValid) {
            std::snprintf(
                rowText, sizeof(rowText),
                "REQUESTED X %.2F  Y %.2F  Z %.2F",
                playerColliderTelemetry.requestedDimensions.x,
                playerColliderTelemetry.requestedDimensions.y,
                playerColliderTelemetry.requestedDimensions.z);
        } else {
            std::snprintf(
                rowText, sizeof(rowText),
                "REQUESTED DIMS WAITING");
        }
        AddToolMenuRow(
            overlay, 5U, rowText, false, 0xFF76DBF4U);
        if (playerColliderTelemetry.actualDimensionsValid) {
            std::snprintf(
                rowText, sizeof(rowText),
                "ACTUAL DIMS X %.2F  Y %.2F  Z %.2F",
                playerColliderTelemetry.actualDimensions.x,
                playerColliderTelemetry.actualDimensions.y,
                playerColliderTelemetry.actualDimensions.z);
        } else {
            std::snprintf(
                rowText, sizeof(rowText),
                "ACTUAL DIMS WAITING");
        }
        AddToolMenuRow(
            overlay, 6U, rowText, false,
            !playerColliderTelemetry.actualDimensionsValid
                ? 0xFF95A5B2U
                : playerColliderTelemetry.runtimeDriftObserved
                    ? 0xFFFF8080U
                    : 0xFF50FF80U);
        AddToolMenuRow(
            overlay, 7U,
            "X/Z ONLY - RETAIL HEIGHT AND STANCE PRESERVED",
            false, 0xFF95A5B2U);
        AddToolMenuRow(
            overlay, 8U,
            "X-RAY BOXES ARE DIAGNOSTIC PROXIES - PHYSICS UNVERIFIED",
            false, 0xFFFFB060U);
        break;
    }
    case ToolMenuTab::BlockCollider:
        if (!PhysicalMeleeProfileMatchesOneHandedWeaponIndex(
                telemetry.weaponIndex, baseProfile.id)) {
            AddToolMenuRow(
                overlay, 0U,
                "EQUIP A SUPPORTED ONE-HANDED MELEE WEAPON",
                false, 0xFFFFB060U);
            AddToolMenuRow(
                overlay, 1U,
                "BLOCK COLLIDERS FAIL CLOSED FOR OTHER WEAPONS",
                false, 0xFF95A5B2U);
            break;
        }
        std::snprintf(rowText, sizeof(rowText),
            "POSITION X                    %.2F U",
            blockColliderSettings.positionOffsetUnits.x);
        Row(0U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "POSITION Y                    %.2F U",
            blockColliderSettings.positionOffsetUnits.y);
        Row(1U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "POSITION Z                    %.2F U",
            blockColliderSettings.positionOffsetUnits.z);
        Row(2U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "PITCH X                       %.1F DEG",
            blockColliderSettings.rotationOffsetDegrees.x);
        Row(3U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "YAW Y                         %.1F DEG",
            blockColliderSettings.rotationOffsetDegrees.y);
        Row(4U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "ROLL Z                        %.1F DEG",
            blockColliderSettings.rotationOffsetDegrees.z);
        Row(5U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "LENGTH                        %.1F U",
            blockColliderSettings.lengthUnits);
        Row(6U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "RADIUS                        %.1F U",
            blockColliderSettings.radiusUnits);
        Row(7U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "DIRECTION                     %s",
            blockColliderSettings.reversed ? "REVERSED" : "FORWARD");
        Row(8U, rowText);
        Row(9U, "COPY CURRENT ATTACK COLLIDER");
        AddToolMenuRow(
            overlay, 10U,
            blockColliderUsesAttackFallback
                ? "SOURCE ATTACK COLLIDER - FOLLOWS UNTIL FIRST EDIT"
                : "SOURCE DEDICATED SAVED BLOCK COLLIDER",
            false, blockColliderUsesAttackFallback
                ? 0xFF76DBF4U : 0xFF50E8FFU);
        break;
    case ToolMenuTab::TwoHand:
        if (!haveGrip || !baseProfile.secondaryGripEnabled) {
            AddToolMenuRow(
                overlay, 0U,
                "EQUIPPED WEAPON HAS NO SUPPORT GRIP PROFILE",
                false, 0xFFFFB060U);
            break;
        }
        std::snprintf(rowText, sizeof(rowText),
            "TWO HAND SUPPORT               %s",
            grip.secondaryGripEnabled ? "ON" : "OFF");
        Row(0U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "SUPPORT OFFSET X               %.2F",
            grip.secondaryGripOffsetUnits.x);
        Row(1U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "SUPPORT OFFSET Y               %.2F",
            grip.secondaryGripOffsetUnits.y);
        Row(2U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "SUPPORT OFFSET Z               %.2F",
            grip.secondaryGripOffsetUnits.z);
        Row(3U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "GRAB RADIUS                    %.2F M",
            grip.secondaryGripGrabRadiusMeters);
        Row(4U, rowText);
        Row(5U, "CAPTURE CURRENT LEFT HAND POSE");
        Row(6U, "RESET SUPPORT GRIP");
        Row(7U, "SAVE TWO HAND SNAPSHOT");
        std::snprintf(rowText, sizeof(rowText),
            "STATE %s   HAND %.2F M   ERROR %.2F M",
            telemetry.secondaryGripAttached
                ? "ATTACHED" : "FREE",
            telemetry.secondaryGripDistanceMeters,
            telemetry.secondaryGripAnchorErrorMeters);
        AddToolMenuRow(
            overlay, 8U, rowText, false,
            telemetry.secondaryGripAttached
                ? 0xFF50FF80U : 0xFF76DBF4U);
        break;
    case ToolMenuTab::HandIk:
        if (emptyHandIkPage) {
            Row(
                0U,
                EmptyRightHandAlignmentIsActive(
                    g_emptyRightHandAlignmentState)
                    ? "CANCEL GUIDED EMPTY-HAND ALIGNMENT"
                    : "START GUIDED EMPTY-HAND ALIGNMENT");
            Row(1U, "RESET EMPTY-HAND ALIGNMENT");
            if (g_emptyRightHandAlignmentState.phase ==
                EmptyRightHandAlignmentPhase::AwaitReferencePose) {
                std::snprintf(
                    rowText, sizeof(rowText),
                    "STEP 1  MAKE VIRTUAL HAND LOOK RIGHT, PULL RIGHT TRIGGER");
            } else if (g_emptyRightHandAlignmentState.phase ==
                EmptyRightHandAlignmentPhase::AwaitControllerPose) {
                std::snprintf(
                    rowText, sizeof(rowText),
                    "STEP 2  MOVE CONTROLLER WHERE IT SHOULD BE, PULL AGAIN");
            } else if (g_emptyRightHandAlignmentLastEvent ==
                       EmptyRightHandAlignmentEvent::Completed) {
                if (g_emptyRightHandAlignmentLastSaveResult ==
                    WeaponSettingsStoreResult::Ok) {
                    std::snprintf(
                        rowText, sizeof(rowText),
                        "READY  LAST ALIGNMENT APPLIED AND SAVED");
                } else {
                    std::snprintf(
                        rowText, sizeof(rowText),
                        "READY  APPLIED, BUT SAVE FAILED: %s",
                        WeaponSettingsStoreResultName(
                            g_emptyRightHandAlignmentLastSaveResult));
                }
            } else {
                std::snprintf(
                    rowText, sizeof(rowText),
                    "READY  SELECT START, THEN USE TWO RIGHT-TRIGGER CAPTURES");
            }
            AddToolMenuRow(
                overlay, 2U, rowText, false,
                EmptyRightHandAlignmentIsActive(
                    g_emptyRightHandAlignmentState)
                    ? 0xFFFFD060U
                    : g_emptyRightHandAlignmentLastEvent ==
                              EmptyRightHandAlignmentEvent::Completed
                        ? 0xFF50FF80U
                        : 0xFF76DBF4U);
            std::snprintf(
                rowText, sizeof(rowText),
                "LAST EVENT %-20s TARGET %s",
                EmptyRightHandAlignmentEventName(
                    g_emptyRightHandAlignmentLastEvent),
                RightHandIkTargetSourceName(
                    g_rightHandIkTargetSource));
            AddToolMenuRow(
                overlay, 3U, rowText, false,
                g_rightHandIkTargetSource ==
                        RightHandIkTargetSource::EmptyGrip
                    ? 0xFF50FF80U : 0xFFFFB060U);
            std::snprintf(
                rowText, sizeof(rowText),
                "OFFSET U  X %.2F  Y %.2F  Z %.2F",
                g_emptyRightHandAlignmentSettings
                    .localPositionOffsetUnits.x,
                g_emptyRightHandAlignmentSettings
                    .localPositionOffsetUnits.y,
                g_emptyRightHandAlignmentSettings
                    .localPositionOffsetUnits.z);
            AddToolMenuRow(overlay, 4U, rowText, false);
            std::snprintf(
                rowText, sizeof(rowText),
                "RIGHT HAND CALLBACK %s   RIGHT TRIGGER %s",
                ArmIkRightHandProofIsActive()
                    ? "ACTIVE" : "WAITING",
                EmptyRightHandAlignmentIsActive(
                    g_emptyRightHandAlignmentState)
                    ? "CAPTURE" : "TABS");
            AddToolMenuRow(
                overlay, 5U, rowText, false,
                ArmIkRightHandProofIsActive()
                    ? 0xFF50FF80U : 0xFFFFB060U);
            break;
        }
        if (telemetry.weaponIndex < 0) {
            AddToolMenuRow(
                overlay, 0U,
                "NO EQUIPPED WEAPON AVAILABLE FOR HAND IK",
                false, 0xFFFFB060U);
            break;
        }
        std::snprintf(rowText, sizeof(rowText),
            "HAND POSITION X               %.3F U",
            rightHandIkSettings.positionOffsetUnits.x);
        Row(0U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "HAND POSITION Y               %.3F U",
            rightHandIkSettings.positionOffsetUnits.y);
        Row(1U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "HAND POSITION Z               %.3F U",
            rightHandIkSettings.positionOffsetUnits.z);
        Row(2U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "HAND PITCH X                  %.2F DEG",
            rightHandIkSettings.rotationOffsetDegrees.x);
        Row(3U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "HAND YAW Y                    %.2F DEG",
            rightHandIkSettings.rotationOffsetDegrees.y);
        Row(4U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "HAND ROLL Z                   %.2F DEG",
            rightHandIkSettings.rotationOffsetDegrees.z);
        Row(5U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "ADJUSTMENT STEP               %.2F U  %.2F DEG",
            kWeaponGripTranslationSteps[
                g_rightHandIkCalibrationStepIndex],
            kWeaponGripRotationSteps[
                g_rightHandIkCalibrationStepIndex]);
        Row(6U, rowText);
        Row(7U, "RESET RIGHT HAND ALIGNMENT");
        Row(8U, "LOG RIGHT HAND SNAPSHOT");
        std::snprintf(rowText, sizeof(rowText),
            "RIGHT HAND CALLBACK           %s",
            ArmIkRightHandProofIsActive() ? "ACTIVE" : "WAITING");
        AddToolMenuRow(
            overlay, 9U, rowText, false,
            ArmIkRightHandProofIsActive()
                ? 0xFF50FF80U : 0xFFFFB060U);
        break;
    case ToolMenuTab::ElbowIk:
        std::snprintf(rowText, sizeof(rowText),
            "ELBOW OUTWARD                 %.2F",
            armIkTuning.elbowOutward);
        Row(0U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "ELBOW DOWN                    %.2F",
            armIkTuning.elbowDown);
        Row(1U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "ELBOW BACK                    %.2F",
            armIkTuning.elbowBack);
        Row(2U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "ELBOW CONTINUITY              %s",
            armIkTuning.preserveElbowContinuity ? "ON" : "OFF");
        Row(3U, rowText);
        Row(4U, "RESET ELBOW DEFAULTS");
        Row(5U, "LOG ARM IK SNAPSHOT");
        std::snprintf(rowText, sizeof(rowText),
            "FULL ARM CALLBACK             %s   STEP 0.05",
            ArmIkRightArmIsActive() ? "ACTIVE" : "WAITING");
        AddToolMenuRow(
            overlay, 6U, rowText, false,
            ArmIkRightArmIsActive()
                ? 0xFF50FF80U : 0xFFFFB060U);
        break;
    case ToolMenuTab::LeftHandIk:
        std::snprintf(rowText, sizeof(rowText),
            "HAND RIGHT                    %.1F CM",
            armIkTuning.leftHandRightMeters * 100.0F);
        Row(0U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "HAND UP                       %.1F CM",
            armIkTuning.leftHandUpMeters * 100.0F);
        Row(1U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "HAND FORWARD                  %.1F CM",
            armIkTuning.leftHandForwardMeters * 100.0F);
        Row(2U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "HAND PITCH                    %.1F DEG",
            armIkTuning.leftHandPitchDegrees);
        Row(3U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "HAND YAW                      %.1F DEG",
            armIkTuning.leftHandYawDegrees);
        Row(4U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "HAND ROLL                     %.1F DEG",
            armIkTuning.leftHandRollDegrees);
        Row(5U, rowText);
        Row(6U, "RESET LEFT HAND ALIGNMENT");
        Row(7U, "LOG LEFT HAND SNAPSHOT");
        std::snprintf(rowText, sizeof(rowText),
            "LEFT ARM CALLBACK             %s   STEP 0.5 CM / 5 DEG",
            ArmIkLeftHandIsActive() ? "ACTIVE" : "WAITING");
        AddToolMenuRow(
            overlay, 8U, rowText, false,
            ArmIkLeftHandIsActive()
                ? 0xFF50FF80U : 0xFFFFB060U);
        break;
    case ToolMenuTab::Display:
        std::snprintf(rowText, sizeof(rowText),
            "FOV SCALE                     %.0F %%",
            g_tuningFovScale * 100.0F);
        Row(0U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "WORLD SCALE                   %.0F U/M",
            g_tuningUnitsPerMeter);
        Row(1U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "HMD TRANSLATION               %s",
            g_hmdTranslationEnabled ? "ON" : "OFF");
        Row(2U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "EYE POLARITY                  %s",
            g_tuningReversePolarity ? "REVERSED" : "NORMAL");
        Row(3U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "STEREO                        %s",
            g_continuousStereoEnabled ? "ON" : "OFF");
        Row(4U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "MENU SIZE                     %.0F %%",
            g_toolMenuPanelPlacement.scale * 100.0F);
        Row(5U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "MENU DISTANCE                 %.2F M",
            g_toolMenuPanelPlacement.distanceMeters);
        Row(6U, rowText);
        Row(7U, "RECENTER NOW");
        Row(8U, "RESET DISPLAY DEFAULTS");
        break;
    case ToolMenuTab::Controls:
        AddToolMenuRow(overlay, 0U, "OPEN OR CLOSE     BOTH GRIPS + Y", false);
        AddToolMenuRow(overlay, 1U, "CHANGE TAB        LEFT OR RIGHT TRIGGER", false);
        AddToolMenuRow(overlay, 2U, "SELECT ROW        LEFT STICK UP OR DOWN", false);
        AddToolMenuRow(overlay, 3U, "ADJUST VALUE      RIGHT STICK LEFT OR RIGHT", false);
        AddToolMenuRow(overlay, 4U, "ACTIVATE          A", false);
        AddToolMenuRow(overlay, 5U, "CLOSE             B", false);
        AddToolMenuRow(overlay, 6U, "KEYBOARD          F12 ARROWS [ ] ENTER", false);
        break;
    case ToolMenuTab::Debug:
        std::snprintf(rowText, sizeof(rowText),
            "DRAW ATTACK COLLIDER          %s",
            InterlockedCompareExchange(
                &g_physicalMeleeColliderDebugDrawVisible, 0, 0) != 0
                ? "ON" : "OFF");
        Row(0U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "DRAW BLOCK COLLIDER           %s",
            InterlockedCompareExchange(
                &g_physicalMeleeBlockColliderDebugDrawVisible, 0, 0) != 0
                ? "ON" : "OFF");
        Row(1U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "DRAW CONTROLLERS              %s",
            InterlockedCompareExchange(
                &g_weaponGripControllerDebugDrawVisible, 0, 0) != 0
                ? "ON" : "OFF");
        Row(2U, rowText);
        std::snprintf(rowText, sizeof(rowText),
            "WEAPON %s   INDEX %ld",
            telemetry.weaponName[0] != '\0'
                ? telemetry.weaponName : "UNKNOWN",
            static_cast<long>(telemetry.weaponIndex));
        AddToolMenuRow(overlay, 3U, rowText, false);
        std::snprintf(rowText, sizeof(rowText),
            "ANIMATION PROPERTY            %s",
            telemetry.weaponAnimationProperty[0] != '\0'
                ? telemetry.weaponAnimationProperty : "UNKNOWN");
        AddToolMenuRow(overlay, 4U, rowText, false);
        std::snprintf(rowText, sizeof(rowText),
            "RETAIL POSE FAMILY            %s",
            RetailWeaponPoseFamilyLabel(
                telemetry.weaponPoseFamily));
        AddToolMenuRow(overlay, 5U, rowText, false);
        std::snprintf(rowText, sizeof(rowText),
            "SWING %s  SPEED %.2F M/S",
            telemetry.trackingFresh ? "FRESH" : "STALE",
            telemetry.swingSpeedMetersPerSecond);
        AddToolMenuRow(overlay, 6U, rowText, false);
        std::snprintf(rowText, sizeof(rowText),
            "CALLBACKS %u  DAMAGE %s  HITS %u",
            telemetry.contactCallbackCount,
            telemetry.contactDamageEnabled ? "ON" : "OFF",
            telemetry.damageDispatchCount);
        AddToolMenuRow(overlay, 7U, rowText, false);
        std::snprintf(rowText, sizeof(rowText),
            "PROXY W %s  MODEL %s  ATTACK %s  BLOCK %s",
            telemetry.wallProxyEnabled ? "ON" : "OFF",
            telemetry.visualProxyEnabled ? "ON" : "OFF",
            !telemetry.colliderDebugEnabled ? "OFF" :
                telemetry.collisionBodyLive ? "LIVE" : "PREVIEW",
            telemetry.blockCollisionBodyLive ? "LIVE" : "WAIT");
        AddToolMenuRow(overlay, 8U, rowText, false);
        std::snprintf(rowText, sizeof(rowText),
            "2-HAND %s  SUPPORT %s  HAND %.2F M  ERR %.2F M",
            telemetry.twoHandedEnabled ? "ON" : "OFF",
            telemetry.secondaryGripAttached ? "ATTACHED" : "FREE",
            telemetry.secondaryGripDistanceMeters,
            telemetry.secondaryGripAnchorErrorMeters);
        AddToolMenuRow(overlay, 9U, rowText, false);
        break;
    default:
        break;
    }

    AddToolMenuText(
        overlay, -0.78F, -0.69F, 0.00355F, 0.0060F,
        "TRIGGERS TABS   LEFT STICK ROW   RIGHT STICK VALUE   A SELECT   B CLOSE",
        0xFF95A5B2U);
    const ToolMenuPanelTransform panelTransform =
        ResolveToolMenuPanelTransform(
            eye, interpupillaryDistanceMeters,
            horizontalFovRadians, g_toolMenuPanelPlacement);
    const bool placed = ApplyToolMenuPanelTransform(
        overlay, panelTransform);
    const bool geometryReady =
        overlay.count != 0U && !overlay.overflowed && placed;
    const bool bridgeAccepted =
        geometryReady &&
        g_drawOverlayTriangles(
            overlay.vertices.data(),
            static_cast<std::uint32_t>(overlay.count)) != FALSE;
    if (!bridgeAccepted) {
        if (InterlockedCompareExchange(
                &g_toolMenuOverlayFailureLogged, 1, 0) == 0 &&
            g_passThroughLog != nullptr) {
            char detail[128]{};
            std::snprintf(
                detail, sizeof(detail),
                overlay.overflowed
                    ? "triangle_buffer_overflow=1 vertices=%zu limit=%u"
                : !placed
                    ? "stereo_panel_placement_invalid=1 vertices=%zu limit=%u"
                    : "bridge_draw_rejected=1 vertices=%zu limit=%u",
                overlay.count,
                static_cast<unsigned>(
                    FEARVR_OVERLAY_TRIANGLE_MAX_INPUT_VERTICES));
            g_passThroughLog(
                "m5_vr_tool_menu_overlay_failed",
                detail);
        }
    }
}

bool DrawWeaponGripCalibrationControllerGizmo(
    const RigidTransformAbi& eyeCamera,
    const fearvr::TrackingVector& gripWorldPosition,
    const fearvr::TrackingQuaternion& gripWorldRotation,
    const fearvr::TrackingQuaternion& aimWorldRotation,
    const fearvr::TrackingVector& secondaryGripWorldPosition,
    const fearvr::TrackingQuaternion& secondaryGripWorldRotation,
    const fearvr::TrackingQuaternion& secondaryAimWorldRotation,
    const fearvr::TrackingVector& secondaryTargetWorldPosition,
    float secondaryGrabRadiusUnits,
    bool secondaryReady,
    bool secondaryAttached,
    float horizontalFovRadians,
    float verticalFovRadians) noexcept {
    const bool authorPageVisible =
        InterlockedCompareExchange(&g_toolMenuOpen, 0, 0) != 0 &&
        g_toolMenuState.tab == ToolMenuTab::Author;
    if ((InterlockedCompareExchange(
             &g_weaponGripControllerDebugDrawVisible, 0, 0) == 0 &&
         !authorPageVisible) ||
        g_drawOverlayLines == nullptr ||
        InterlockedCompareExchange(
            &g_weaponGripCalibrationEnabled, 0, 0) == 0 ||
        InterlockedCompareExchange(
            &g_weaponGripCalibrationActive, 0, 0) == 0) {
        return false;
    }
    const WeaponGripCalibrationGizmoCamera camera{
        {eyeCamera.position[0], eyeCamera.position[1],
         eyeCamera.position[2]},
        {eyeCamera.rotation[0], eyeCamera.rotation[1],
         eyeCamera.rotation[2], eyeCamera.rotation[3]},
        horizontalFovRadians, verticalFovRadians};
    const auto DrawGizmo = [&](
        const WeaponGripCalibrationGizmo& gizmo) noexcept {
        FearVrOverlayLineVertex projected[
            kWeaponGripCalibrationGizmoMaximumLines * 2]{};
        const std::size_t vertexCount =
            ProjectWeaponGripCalibrationGizmoToNdc(
                gizmo, camera, projected,
                sizeof(projected) / sizeof(projected[0]));
        return vertexCount != 0U &&
            g_drawOverlayLines(
                projected,
                static_cast<std::uint32_t>(vertexCount)) != FALSE;
    };
    const bool primaryDrawn = DrawGizmo(
        BuildWeaponGripCalibrationGizmo(
            gripWorldPosition, gripWorldRotation,
            aimWorldRotation));
    bool secondaryDrawn = false;
    if (secondaryReady) {
        const WeaponGripCalibrationGizmoPalette secondaryPalette{
            0xE060E8FFU, 0xE040B8D8U, 0xFF40E8FFU,
            0xFFB0F8FFU, 0xFFFF6060U, 0xFF60FF80U,
            0xFF60A0FFU};
        const bool controllerDrawn = DrawGizmo(
            BuildWeaponGripCalibrationGizmo(
                secondaryGripWorldPosition,
                secondaryGripWorldRotation,
                secondaryAimWorldRotation,
                secondaryPalette));
        const bool targetDrawn = DrawGizmo(
            BuildWeaponSecondaryGripGizmo(
                gripWorldPosition,
                secondaryTargetWorldPosition,
                secondaryGripWorldPosition,
                secondaryGrabRadiusUnits,
                secondaryAttached));
        secondaryDrawn = controllerDrawn || targetDrawn;
    }
    const bool drawn = primaryDrawn || secondaryDrawn;
    if (drawn && InterlockedCompareExchange(
                     &g_weaponGripControllerGizmoActiveLogged,
                     1, 0) == 0 &&
        g_passThroughLog != nullptr) {
        g_passThroughLog(
            "m5_weapon_grip_controller_gizmo_active",
            "shape=dual_generic_controller_wireframe "
            "primary=right_grip_magenta secondary=left_grip_cyan "
            "support_target=green_attached_amber_near_red_far "
            "aim_rays=openxr_aim_poses "
            "projection=verified_per_eye_camera overlay_depth=always_visible");
    } else if (!drawn && InterlockedCompareExchange(
                            &g_weaponGripControllerGizmoFailureLogged,
                            1, 0) == 0 &&
               g_passThroughLog != nullptr) {
        g_passThroughLog(
            "m5_weapon_grip_controller_gizmo_failed",
            "bridge_overlay_draw_rejected=1 weapon_visual_continues=1");
    }
    return drawn;
}

bool DrawMagazineSocketAuthoringGizmo(
    const RigidTransformAbi& eyeCamera,
    float horizontalFovRadians,
    float verticalFovRadians) noexcept {
    if (g_drawOverlayLines == nullptr ||
        InterlockedCompareExchange(
            &g_weaponGripCalibrationEnabled, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_toolMenuOpen, 0, 0) == 0 ||
        g_toolMenuState.tab != ToolMenuTab::Author ||
        g_authoringPrimitive !=
            InteractionAuthoringPrimitive::MagazineInsertSocket) {
        return false;
    }
    const MagazineSocketAuthoringRuntimeState state =
        CopyMagazineSocketAuthoringState();
    if (!state.identityReady ||
        !state.visualReady ||
        GetTickCount64() - state.visualTick >
            kHeadAimFreshnessMilliseconds ||
        !state.editor.current.configured) {
        return false;
    }
    const WeaponGripCalibrationGizmo gizmo =
        BuildMagazineSocketAuthoringGizmo(
            state.editor.current, state.modelWorld,
            &state.preview);
    const WeaponGripCalibrationGizmoCamera camera{
        {eyeCamera.position[0], eyeCamera.position[1],
         eyeCamera.position[2]},
        {eyeCamera.rotation[0], eyeCamera.rotation[1],
         eyeCamera.rotation[2], eyeCamera.rotation[3]},
        horizontalFovRadians, verticalFovRadians};
    FearVrOverlayLineVertex projected[
        kWeaponGripCalibrationGizmoMaximumLines * 2]{};
    const std::size_t vertexCount =
        ProjectWeaponGripCalibrationGizmoToNdc(
            gizmo, camera, projected,
            sizeof(projected) / sizeof(projected[0]));
    const bool drawn = vertexCount != 0U &&
        g_drawOverlayLines(
            projected,
            static_cast<std::uint32_t>(vertexCount)) != FALSE;
    if (drawn && InterlockedCompareExchange(
                     &g_magazineSocketAuthoringGizmoLogged,
                     1, 0) == 0 &&
        g_passThroughLog != nullptr) {
        LogMagazineSocketAuthoring(
            "m5_magazine_socket_authoring_gizmo_active",
            "gizmo_active", state);
    } else if (!drawn && InterlockedCompareExchange(
                            &g_magazineSocketAuthoringGizmoFailureLogged,
                            1, 0) == 0 &&
               g_passThroughLog != nullptr) {
        LogMagazineSocketAuthoring(
            "m5_magazine_socket_authoring_gizmo_failed",
            "projection_or_bridge_draw_rejected", state);
    }
    return drawn;
}

bool DrawSlideGrabAuthoringGizmo(
    const RigidTransformAbi& eyeCamera,
    float horizontalFovRadians,
    float verticalFovRadians) noexcept {
    if (g_drawOverlayLines == nullptr ||
        InterlockedCompareExchange(
            &g_weaponGripCalibrationEnabled, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_toolMenuOpen, 0, 0) == 0 ||
        g_toolMenuState.tab != ToolMenuTab::Author ||
        g_authoringPrimitive !=
            InteractionAuthoringPrimitive::SlideGrabRail) {
        return false;
    }
    const SlideGrabAuthoringRuntimeState state =
        CopySlideGrabAuthoringState();
    if (!state.identityReady || !state.visualReady ||
        GetTickCount64() - state.visualTick >
            kHeadAimFreshnessMilliseconds) {
        return false;
    }
    const WeaponGripCalibrationGizmo gizmo =
        BuildSlideGrabAuthoringGizmo(
            state.editor.current, state.modelWorld,
            &state.cursorModelLocal);
    const WeaponGripCalibrationGizmoCamera camera{
        {eyeCamera.position[0], eyeCamera.position[1],
         eyeCamera.position[2]},
        {eyeCamera.rotation[0], eyeCamera.rotation[1],
         eyeCamera.rotation[2], eyeCamera.rotation[3]},
        horizontalFovRadians, verticalFovRadians};
    FearVrOverlayLineVertex projected[
        kWeaponGripCalibrationGizmoMaximumLines * 2]{};
    const std::size_t vertexCount =
        ProjectWeaponGripCalibrationGizmoToNdc(
            gizmo, camera, projected,
            sizeof(projected) / sizeof(projected[0]));
    const bool drawn = vertexCount != 0U &&
        g_drawOverlayLines(
            projected,
            static_cast<std::uint32_t>(vertexCount)) != FALSE;
    if (drawn && InterlockedCompareExchange(
                     &g_slideGrabAuthoringGizmoLogged,
                     1, 0) == 0 &&
        g_passThroughLog != nullptr) {
        LogSlideGrabAuthoring(
            "m5_slide_grab_authoring_gizmo_active",
            "grab_box_rail_endpoints_hand_pose",
            state);
    } else if (!drawn && InterlockedCompareExchange(
                            &g_slideGrabAuthoringGizmoFailureLogged,
                            1, 0) == 0 &&
               g_passThroughLog != nullptr) {
        LogSlideGrabAuthoring(
            "m5_slide_grab_authoring_gizmo_failed",
            "projection_or_bridge_draw_rejected",
            state);
    }
    return drawn;
}

bool DrawPhysicalMeleeColliderGizmo(
    const RigidTransformAbi& eyeCamera,
    float horizontalFovRadians,
    float verticalFovRadians) noexcept {
    if (InterlockedCompareExchange(
            &g_physicalMeleeColliderDebugDrawVisible, 0, 0) == 0 ||
        g_drawOverlayLines == nullptr) {
        return false;
    }
    PhysicalMeleeColliderDebugSnapshot snapshot{};
    if (!ReadPhysicalMeleeColliderDebugSnapshot(snapshot)) {
        return false;
    }
    const WeaponGripCalibrationGizmo gizmo =
        BuildPhysicalMeleeColliderGizmo(
            snapshot.baseUnits, snapshot.tipUnits,
            snapshot.collisionOriginUnits, snapshot.radiusUnits,
            snapshot.collisionBodyLive);
    const WeaponGripCalibrationGizmoCamera camera{
        {eyeCamera.position[0], eyeCamera.position[1],
         eyeCamera.position[2]},
        {eyeCamera.rotation[0], eyeCamera.rotation[1],
         eyeCamera.rotation[2], eyeCamera.rotation[3]},
        horizontalFovRadians, verticalFovRadians};
    FearVrOverlayLineVertex projected[
        kWeaponGripCalibrationGizmoMaximumLines * 2]{};
    const std::size_t vertexCount =
        ProjectWeaponGripCalibrationGizmoToNdc(
            gizmo, camera, projected,
            sizeof(projected) / sizeof(projected[0]));
    const bool drawn = vertexCount != 0U &&
        g_drawOverlayLines(
            projected,
            static_cast<std::uint32_t>(vertexCount)) != FALSE;
    if (drawn) {
        volatile LONG* const logged = snapshot.collisionBodyLive
            ? &g_physicalMeleeColliderLiveLogged
            : &g_physicalMeleeColliderPreviewLogged;
        if (InterlockedCompareExchange(logged, 1, 0) == 0 &&
            g_passThroughLog != nullptr) {
            g_passThroughLog(
                snapshot.collisionBodyLive
                    ? "m5_physical_melee_collider_debug_live"
                    : "m5_physical_melee_collider_debug_preview",
                snapshot.collisionBodyLive
                    ? "color=green retail_collision_body_live=1 "
                      "origin=controller_weapon_tip depth=always_visible"
                    : "color=amber retail_collision_body_live=0 "
                      "waiting_for_automatic_equip_seed=1 "
                      "depth=always_visible");
        }
    } else if (InterlockedCompareExchange(
                   &g_physicalMeleeColliderFailureLogged,
                   1, 0) == 0 &&
               g_passThroughLog != nullptr) {
        g_passThroughLog(
            "m5_physical_melee_collider_debug_failed",
            "projection_or_bridge_draw_rejected=1 gameplay_continues=1");
    }
    return drawn;
}

bool DrawPlayerCollisionXrayGizmo(
    const RigidTransformAbi& eyeCamera,
    float horizontalFovRadians,
    float verticalFovRadians) noexcept {
    if (g_drawOverlayLines == nullptr) {
        return false;
    }
    PlayerCollisionXraySnapshot snapshot{};
    if (!ReadPlayerCollisionXraySnapshot(snapshot)) {
        return false;
    }
    WeaponGripCalibrationGizmo gizmo{};
    const auto AddProxy = [&gizmo](
        const PlayerCollisionDiagnosticProxy& proxy,
        std::uint32_t color) noexcept {
        if (!proxy.valid) {
            return;
        }
        const fearvr::TrackingVector corners[8]{
            {proxy.minimum.x, proxy.minimum.y, proxy.minimum.z},
            {proxy.maximum.x, proxy.minimum.y, proxy.minimum.z},
            {proxy.maximum.x, proxy.maximum.y, proxy.minimum.z},
            {proxy.minimum.x, proxy.maximum.y, proxy.minimum.z},
            {proxy.minimum.x, proxy.minimum.y, proxy.maximum.z},
            {proxy.maximum.x, proxy.minimum.y, proxy.maximum.z},
            {proxy.maximum.x, proxy.maximum.y, proxy.maximum.z},
            {proxy.minimum.x, proxy.maximum.y, proxy.maximum.z}};
        constexpr std::size_t edges[12][2]{
            {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4},
            {0,4},{1,5},{2,6},{3,7}};
        for (const auto& edge : edges) {
            AddWeaponGripCalibrationGizmoLine(
                gizmo, corners[edge[0]], corners[edge[1]], color);
        }
    };
    const auto AddCross = [&gizmo](
        const PlayerCollisionDiagnosticPoint& point,
        float halfExtent,
        std::uint32_t color) noexcept {
        AddWeaponGripCalibrationGizmoLine(
            gizmo, {point.x - halfExtent, point.y, point.z},
            {point.x + halfExtent, point.y, point.z}, color);
        AddWeaponGripCalibrationGizmoLine(
            gizmo, {point.x, point.y - halfExtent, point.z},
            {point.x, point.y + halfExtent, point.z}, color);
        AddWeaponGripCalibrationGizmoLine(
            gizmo, {point.x, point.y, point.z - halfExtent},
            {point.x, point.y, point.z + halfExtent}, color);
    };
    AddProxy(
        BuildPlayerCollisionDiagnosticProxy(
            snapshot.playerOrigin, snapshot.playerDimensions),
        0xFFFF40E0U);
    if (snapshot.targetValid) {
        AddProxy(
            BuildPlayerCollisionDiagnosticProxy(
                snapshot.targetOrigin, snapshot.targetDimensions),
            0xFFFF9040U);
    }
    if (snapshot.headValid) {
        AddCross(snapshot.headOrigin, 5.0F, 0xFF40E8FFU);
        AddWeaponGripCalibrationGizmoLine(
            gizmo,
            {snapshot.playerOrigin.x, snapshot.playerOrigin.y,
             snapshot.playerOrigin.z},
            {snapshot.headOrigin.x, snapshot.headOrigin.y,
             snapshot.headOrigin.z},
            0xFF40E8FFU);
    }
    if (snapshot.contactValid) {
        AddCross(snapshot.contactPoint, 4.0F, 0xFFFFFF40U);
    }
    const WeaponGripCalibrationGizmoCamera camera{
        {eyeCamera.position[0], eyeCamera.position[1],
         eyeCamera.position[2]},
        {eyeCamera.rotation[0], eyeCamera.rotation[1],
         eyeCamera.rotation[2], eyeCamera.rotation[3]},
        horizontalFovRadians, verticalFovRadians};
    FearVrOverlayLineVertex projected[
        kWeaponGripCalibrationGizmoMaximumLines * 2]{};
    const std::size_t vertexCount =
        ProjectWeaponGripCalibrationGizmoToNdc(
            gizmo, camera, projected,
            sizeof(projected) / sizeof(projected[0]));
    const bool drawn = vertexCount != 0U &&
        g_drawOverlayLines(
            projected,
            static_cast<std::uint32_t>(vertexCount)) != FALSE;
    static volatile LONG activeLogged = 0;
    if (drawn && InterlockedCompareExchange(
                     &activeLogged, 1, 0) == 0 &&
        g_passThroughLog != nullptr) {
        g_passThroughLog(
            "m5_player_collision_xray_rendered",
            "player=magenta target=orange hmd=cyan contact=yellow "
            "all_boxes=diagnostic_proxy true_physics_geometry_verified=0 "
            "depth=always_visible mutation=none");
    }
    return drawn;
}

bool DrawPhysicalMeleeBlockColliderGizmo(
    const RigidTransformAbi& eyeCamera,
    float horizontalFovRadians,
    float verticalFovRadians) noexcept {
    if (InterlockedCompareExchange(
            &g_physicalMeleeBlockColliderDebugDrawVisible, 0, 0) == 0 ||
        g_drawOverlayLines == nullptr) {
        return false;
    }
    PhysicalMeleeColliderDebugSnapshot snapshot{};
    if (!ReadPhysicalMeleeBlockColliderDebugSnapshot(snapshot)) {
        return false;
    }
    const WeaponGripCalibrationGizmo gizmo =
        BuildPhysicalMeleeColliderGizmo(
            snapshot.baseUnits, snapshot.tipUnits,
            snapshot.collisionOriginUnits, snapshot.radiusUnits,
            snapshot.collisionBodyLive,
            PhysicalMeleeColliderGizmoRole::Block);
    const WeaponGripCalibrationGizmoCamera camera{
        {eyeCamera.position[0], eyeCamera.position[1],
         eyeCamera.position[2]},
        {eyeCamera.rotation[0], eyeCamera.rotation[1],
         eyeCamera.rotation[2], eyeCamera.rotation[3]},
        horizontalFovRadians, verticalFovRadians};
    FearVrOverlayLineVertex projected[
        kWeaponGripCalibrationGizmoMaximumLines * 2]{};
    const std::size_t vertexCount =
        ProjectWeaponGripCalibrationGizmoToNdc(
            gizmo, camera, projected,
            sizeof(projected) / sizeof(projected[0]));
    const bool drawn = vertexCount != 0U &&
        g_drawOverlayLines(
            projected,
            static_cast<std::uint32_t>(vertexCount)) != FALSE;
    if (drawn) {
        volatile LONG* const logged = snapshot.collisionBodyLive
            ? &g_physicalMeleeBlockColliderLiveLogged
            : &g_physicalMeleeBlockColliderPreviewLogged;
        if (InterlockedCompareExchange(logged, 1, 0) == 0 &&
            g_passThroughLog != nullptr) {
            g_passThroughLog(
                snapshot.collisionBodyLive
                    ? "m5_physical_melee_block_collider_debug_live"
                    : "m5_physical_melee_block_collider_debug_preview",
                snapshot.collisionBodyLive
                    ? "color=cyan retail_block_collision_body_live=1 "
                      "depth=always_visible"
                    : "color=blue retail_block_collision_body_live=0 "
                      "geometry=dedicated_or_attack_fallback "
                      "depth=always_visible");
        }
    } else if (InterlockedCompareExchange(
                   &g_physicalMeleeBlockColliderFailureLogged,
                   1, 0) == 0 &&
               g_passThroughLog != nullptr) {
        g_passThroughLog(
            "m5_physical_melee_block_collider_debug_failed",
            "projection_or_bridge_draw_rejected=1 gameplay_continues=1");
    }
    return drawn;
}

struct CalibrationButtonLatch {
    bool releaseRequired{true};
    bool wasDown{false};
};

bool ConsumeCalibrationButton(
    CalibrationButtonLatch& latch,
    bool captured,
    bool down) noexcept {
    if (!captured) {
        latch.releaseRequired = true;
        latch.wasDown = false;
        return false;
    }
    if (!down) {
        latch.releaseRequired = false;
        latch.wasDown = false;
        return false;
    }
    if (latch.releaseRequired || latch.wasDown) {
        latch.wasDown = true;
        return false;
    }
    latch.wasDown = true;
    return true;
}

void HandleWeaponGripCalibrationControls() noexcept {
    if (InterlockedCompareExchange(
            &g_weaponGripCalibrationEnabled, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_toolMenuOpen, 0, 0) != 0 ||
        !GameOwnsForegroundWindow()) {
        return;
    }

    static bool f11Down = false;
    static bool numpadModeDown = false;
    static bool letterModeDown = false;
    static bool numpadFinerDown = false;
    static bool commaFinerDown = false;
    static bool numpadCoarserDown = false;
    static bool periodCoarserDown = false;
    static bool numpadResetDown = false;
    static bool letterResetDown = false;
    static bool numpadSnapshotDown = false;
    static bool letterSnapshotDown = false;
    static RepeatingCalibrationKey xNegative{};
    static RepeatingCalibrationKey xPositive{};
    static RepeatingCalibrationKey yNegative{};
    static RepeatingCalibrationKey yPositive{};
    static RepeatingCalibrationKey zNegative{};
    static RepeatingCalibrationKey zPositive{};
    static RepeatingCalibrationKey letterXNegative{};
    static RepeatingCalibrationKey letterXPositive{};
    static RepeatingCalibrationKey letterYNegative{};
    static RepeatingCalibrationKey letterYPositive{};
    static RepeatingCalibrationKey letterZNegative{};
    static RepeatingCalibrationKey letterZPositive{};
    static CalibrationButtonLatch controllerPosition{};
    static CalibrationButtonLatch controllerRotation{};
    static CalibrationButtonLatch controllerReset{};
    static CalibrationButtonLatch controllerSnapshot{};
    static CalibrationButtonLatch controllerFiner{};
    static CalibrationButtonLatch controllerCoarser{};
    static std::uint64_t controllerObservedSampleId = 0;
    static ULONGLONG controllerObservedTick = 0;
    static std::uint64_t controllerAxisSampleId = 0;
    static ULONGLONG controllerAxisTick = 0;
    static ULONGLONG controllerAdjustmentLogTick = 0;
    static bool controllerWasCaptured = false;

    if (PressedOnce(VK_F11, f11Down)) {
        const bool active = InterlockedCompareExchange(
            &g_weaponGripCalibrationActive, 0, 0) == 0;
        InterlockedExchange(
            &g_weaponGripCalibrationActive, active ? 1 : 0);
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_changed",
            active ? "resume" : "pause");
    }
    if (InterlockedCompareExchange(
            &g_weaponGripCalibrationActive, 0, 0) == 0) {
        controllerPosition = {};
        controllerRotation = {};
        controllerReset = {};
        controllerSnapshot = {};
        controllerFiner = {};
        controllerCoarser = {};
        controllerAxisSampleId = 0;
        controllerAxisTick = 0;
        controllerWasCaptured = false;
        return;
    }

    const ULONGLONG now = GetTickCount64();
    FearVrInputState controllerState{};
    bool controllerFresh = false;
    if (g_getInputState != nullptr &&
        g_getInputState(&controllerState) != FALSE &&
        controllerState.sampleId != 0) {
        if (controllerState.sampleId != controllerObservedSampleId) {
            controllerObservedSampleId = controllerState.sampleId;
            controllerObservedTick = now;
        }
        controllerFresh = controllerObservedTick != 0 &&
            now - controllerObservedTick <=
                kHeadAimFreshnessMilliseconds;
    }
    const WeaponGripCalibrationControls controller =
        ResolveWeaponGripCalibrationControls(
            controllerState, controllerFresh);
    if (controller.captured != controllerWasCaptured) {
        controllerWasCaptured = controller.captured;
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_changed",
            controller.captured
                ? "controller_capture_begin"
                : "controller_capture_end");
    }

    if (ConsumeCalibrationButton(
            controllerPosition, controller.captured,
            controller.positionDown)) {
        g_weaponGripCalibrationMode =
            WeaponGripCalibrationMode::Position;
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_changed",
            "controller_position_mode");
    }
    if (ConsumeCalibrationButton(
            controllerRotation, controller.captured,
            controller.rotationDown)) {
        g_weaponGripCalibrationMode =
            WeaponGripCalibrationMode::Rotation;
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_changed",
            "controller_rotation_mode");
    }
    if (ConsumeCalibrationButton(
            controllerFiner, controller.captured,
            controller.finerDown)) {
        if (g_weaponGripCalibrationStepIndex > 0) {
            --g_weaponGripCalibrationStepIndex;
        }
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_changed",
            "controller_finer_step");
    }
    if (ConsumeCalibrationButton(
            controllerCoarser, controller.captured,
            controller.coarserDown)) {
        g_weaponGripCalibrationStepIndex = std::min(
            g_weaponGripCalibrationStepIndex + 1,
            kWeaponGripStepCount - 1);
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_changed",
            "controller_coarser_step");
    }
    if (ConsumeCalibrationButton(
            controllerReset, controller.captured,
            controller.resetDown) &&
        ResetActiveWeaponGripCalibration()) {
        PersistActiveWeaponGripCalibration(
            "controller_reset_weapon");
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_changed",
            "controller_reset_weapon");
    }
    if (ConsumeCalibrationButton(
            controllerSnapshot, controller.captured,
            controller.snapshotDown)) {
        PersistActiveWeaponGripCalibration(
            "controller_snapshot");
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_snapshot",
            "controller_snapshot");
    }

    if (!controller.captured) {
        controllerAxisSampleId = 0;
        controllerAxisTick = 0;
    } else if (controllerState.sampleId != controllerAxisSampleId) {
        float deltaSeconds = 0.0F;
        if (controllerAxisSampleId != 0 &&
            controllerAxisTick != 0 && now >= controllerAxisTick) {
            deltaSeconds = std::min(
                static_cast<float>(now - controllerAxisTick) /
                    1000.0F,
                0.05F);
        }
        controllerAxisSampleId = controllerState.sampleId;
        controllerAxisTick = now;
        const float step =
            g_weaponGripCalibrationMode ==
                    WeaponGripCalibrationMode::Position
                ? kWeaponGripTranslationSteps[
                      g_weaponGripCalibrationStepIndex]
                : kWeaponGripRotationSteps[
                      g_weaponGripCalibrationStepIndex];
        const float rate =
            g_weaponGripCalibrationMode ==
                    WeaponGripCalibrationMode::Position
                ? 20.0F : 30.0F;
        const float x = controller.x * step * rate * deltaSeconds;
        const float y = controller.y * step * rate * deltaSeconds;
        const float z = controller.z * step * rate * deltaSeconds;
        if (deltaSeconds > 0.0F &&
            (x != 0.0F || y != 0.0F || z != 0.0F) &&
            AdjustActiveWeaponGripCalibration(x, y, z) &&
            (controllerAdjustmentLogTick == 0 ||
             now - controllerAdjustmentLogTick >= 100)) {
            controllerAdjustmentLogTick = now;
            LogWeaponGripCalibrationState(
                "m5_weapon_grip_calibration_changed",
                g_weaponGripCalibrationMode ==
                        WeaponGripCalibrationMode::Position
                    ? "controller_adjust_position"
                    : "controller_adjust_rotation");
        }
    }

    const bool numpadModePressed =
        PressedOnce(VK_NUMPAD5, numpadModeDown);
    const bool letterModePressed =
        PressedOnce('T', letterModeDown);
    if (numpadModePressed || letterModePressed) {
        g_weaponGripCalibrationMode =
            g_weaponGripCalibrationMode ==
                    WeaponGripCalibrationMode::Position
                ? WeaponGripCalibrationMode::Rotation
                : WeaponGripCalibrationMode::Position;
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_changed", "toggle_mode");
    }
    const bool numpadFinerPressed =
        PressedOnce(VK_NUMPAD7, numpadFinerDown);
    const bool commaFinerPressed =
        PressedOnce(VK_OEM_COMMA, commaFinerDown);
    if (numpadFinerPressed || commaFinerPressed) {
        if (g_weaponGripCalibrationStepIndex > 0) {
            --g_weaponGripCalibrationStepIndex;
        }
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_changed", "finer_step");
    }
    const bool numpadCoarserPressed =
        PressedOnce(VK_NUMPAD9, numpadCoarserDown);
    const bool periodCoarserPressed =
        PressedOnce(VK_OEM_PERIOD, periodCoarserDown);
    if (numpadCoarserPressed || periodCoarserPressed) {
        g_weaponGripCalibrationStepIndex = std::min(
            g_weaponGripCalibrationStepIndex + 1,
            kWeaponGripStepCount - 1);
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_changed", "coarser_step");
    }
    const bool numpadResetPressed =
        PressedOnce(VK_NUMPAD0, numpadResetDown);
    const bool letterResetPressed =
        PressedOnce('R', letterResetDown);
    if ((numpadResetPressed || letterResetPressed) &&
        ResetActiveWeaponGripCalibration()) {
        PersistActiveWeaponGripCalibration(
            "keyboard_reset_weapon");
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_changed", "reset_weapon");
    }
    const bool numpadSnapshotPressed =
        PressedOnce(VK_DECIMAL, numpadSnapshotDown);
    const bool letterSnapshotPressed =
        PressedOnce('P', letterSnapshotDown);
    if (numpadSnapshotPressed || letterSnapshotPressed) {
        PersistActiveWeaponGripCalibration(
            "keyboard_snapshot");
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_snapshot", "snapshot");
    }

    const float step =
        g_weaponGripCalibrationMode ==
                WeaponGripCalibrationMode::Position
            ? kWeaponGripTranslationSteps[
                  g_weaponGripCalibrationStepIndex]
            : kWeaponGripRotationSteps[
                  g_weaponGripCalibrationStepIndex];
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    if (PressedOrRepeated(VK_NUMPAD4, xNegative, now)) {
        x -= step;
    }
    if (PressedOrRepeated('J', letterXNegative, now)) {
        x -= step;
    }
    if (PressedOrRepeated(VK_NUMPAD6, xPositive, now)) {
        x += step;
    }
    if (PressedOrRepeated('L', letterXPositive, now)) {
        x += step;
    }
    if (PressedOrRepeated(VK_NUMPAD2, yNegative, now)) {
        y -= step;
    }
    if (PressedOrRepeated('K', letterYNegative, now)) {
        y -= step;
    }
    if (PressedOrRepeated(VK_NUMPAD8, yPositive, now)) {
        y += step;
    }
    if (PressedOrRepeated('I', letterYPositive, now)) {
        y += step;
    }
    if (PressedOrRepeated(VK_NUMPAD1, zNegative, now)) {
        z -= step;
    }
    if (PressedOrRepeated('U', letterZNegative, now)) {
        z -= step;
    }
    if (PressedOrRepeated(VK_NUMPAD3, zPositive, now)) {
        z += step;
    }
    if (PressedOrRepeated('O', letterZPositive, now)) {
        z += step;
    }
    if ((x != 0.0F || y != 0.0F || z != 0.0F) &&
        AdjustActiveWeaponGripCalibration(x, y, z)) {
        LogWeaponGripCalibrationState(
            "m5_weapon_grip_calibration_changed",
            g_weaponGripCalibrationMode ==
                    WeaponGripCalibrationMode::Position
                ? "adjust_position" : "adjust_rotation");
    }
}

void LogStereoTuningState(const char* action) noexcept {
    if (g_passThroughLog == nullptr) {
        return;
    }
    char detail[320]{};
    std::snprintf(
        detail, sizeof(detail),
        "action=%s enabled=%u units_per_meter=%.1f polarity=%s "
        "fov_scale_percent=%.0f hmd_translation=%u "
        "menu_scale_percent=%.0f menu_distance_m=%.2f",
        action, g_continuousStereoEnabled ? 1U : 0U,
        g_tuningUnitsPerMeter,
        g_tuningReversePolarity ? "reversed" : "normal",
        g_tuningFovScale * 100.0F,
        g_hmdTranslationEnabled ? 1U : 0U,
        g_toolMenuPanelPlacement.scale * 100.0F,
        g_toolMenuPanelPlacement.distanceMeters);
    g_passThroughLog("m3_stereo_tuning_changed", detail);
}

void HandleStereoTuningControls() noexcept {
    if (!g_continuousStereoTuning) {
        return;
    }
    static bool f6Down = false;
    static bool f4Down = false;
    static bool f5Down = false;
    static bool f7Down = false;
    static bool f8Down = false;
    static bool f9Down = false;
    static bool f10Down = false;
    static bool pageDown = false;
    static bool pageUp = false;
    static bool homeDown = false;
    if (PressedOnce(VK_F4, f4Down)) {
        g_hmdTranslationEnabled = !g_hmdTranslationEnabled;
        LogStereoTuningState("toggle_hmd_translation");
    }
    if (PressedOnce(VK_F5, f5Down)) {
        g_trackingRecenterPending = true;
        LogStereoTuningState("recenter_requested");
    }
    if (PressedOnce(VK_F6, f6Down)) {
        g_continuousStereoEnabled = !g_continuousStereoEnabled;
        LogStereoTuningState("toggle_stereo");
    }
    if (PressedOnce(VK_F7, f7Down)) {
        g_tuningReversePolarity = !g_tuningReversePolarity;
        LogStereoTuningState("swap_polarity");
    }
    if (PressedOnce(VK_F8, f8Down)) {
        g_tuningUnitsPerMeter =
            std::max(0.0F, g_tuningUnitsPerMeter - 10.0F);
        LogStereoTuningState("decrease_baseline");
    }
    if (PressedOnce(VK_F9, f9Down)) {
        g_tuningUnitsPerMeter =
            std::min(300.0F, g_tuningUnitsPerMeter + 10.0F);
        LogStereoTuningState("increase_baseline");
    }
    if (PressedOnce(VK_F10, f10Down)) {
        g_tuningUnitsPerMeter = 0.0F;
        LogStereoTuningState("zero_baseline");
    }
    if (PressedOnce(VK_NEXT, pageDown)) {
        g_tuningFovScale = std::max(
            kMinimumFovScale, g_tuningFovScale - 0.05F);
        if (g_setFovScalePercent != nullptr) {
            g_setFovScalePercent(static_cast<std::uint32_t>(
                g_tuningFovScale * 100.0F + 0.5F));
        }
        LogStereoTuningState("decrease_fov_scale");
    }
    if (PressedOnce(VK_PRIOR, pageUp)) {
        g_tuningFovScale = std::min(
            kMaximumFovScale, g_tuningFovScale + 0.05F);
        if (g_setFovScalePercent != nullptr) {
            g_setFovScalePercent(static_cast<std::uint32_t>(
                g_tuningFovScale * 100.0F + 0.5F));
        }
        LogStereoTuningState("increase_fov_scale");
    }
    if (PressedOnce(VK_HOME, homeDown)) {
        g_tuningUnitsPerMeter = 100.0F;
        g_tuningReversePolarity = false;
        g_tuningFovScale = kCondemnedDefaultFovScale;
        if (g_setFovScalePercent != nullptr) {
            g_setFovScalePercent(130U);
        }
        g_trackingRecenterPending = true;
        LogStereoTuningState("reset");
    }
}

void SampleControllerInputReadOnly() noexcept {
    if (!g_continuousStereoTuning || g_getInputState == nullptr ||
        g_passThroughLog == nullptr) {
        return;
    }
    FearVrInputState input{};
    if (!g_getInputState(&input) || input.sampleId == 0 ||
        input.sampleId == g_lastInputSampleId) {
        return;
    }
    g_lastInputSampleId = input.sampleId;
    g_lastInputSampleTick = GetTickCount64();
    ++g_inputSamplesObserved;
    if (g_controllerRecenterEnabled) {
        const HWND foreground = GetForegroundWindow();
        DWORD foregroundProcessId = 0;
        if (foreground != nullptr) {
            GetWindowThreadProcessId(
                foreground, &foregroundProcessId);
        }
        const bool foregroundOwned =
            foreground != nullptr &&
            foregroundProcessId == GetCurrentProcessId();
        const bool calibrationCaptured =
            VrToolMenuCapturesControllerInput(
                input, foregroundOwned) ||
            (InterlockedCompareExchange(
                 &g_weaponGripCalibrationEnabled, 0, 0) != 0 &&
             InterlockedCompareExchange(
                 &g_weaponGripCalibrationActive, 0, 0) != 0 &&
             ResolveWeaponGripCalibrationControls(
                 input, foregroundOwned).captured);
        if (ConsumeRecenterPress(
                g_controllerRecenterLatch, input,
                foregroundOwned && !calibrationCaptured)) {
            g_controllerRecenterRequested = true;
        }
    }
    const std::uint32_t poseMasks =
        (input.aimPoseValidHands & 0xFFFFU) |
        ((input.gripPoseValidHands & 0xFFFFU) << 16U);
    const bool stateChanged =
        input.buttons != g_lastInputButtons ||
        input.activeHands != g_lastInputHands ||
        input.flags != g_lastInputFlags ||
        poseMasks != g_lastInputPoseMasks;
    if (g_inputSamplesObserved == 1 || stateChanged ||
        g_inputSamplesObserved % 180U == 0) {
        char detail[384]{};
        std::snprintf(
            detail, sizeof(detail),
            "sample=%llu observed=%llu flags=0x%X active_hands=0x%X "
            "buttons=0x%X aim_hands=0x%X grip_hands=0x%X "
            "move=(%.3f,%.3f) turn=(%.3f,%.3f) "
            "trigger=(%.3f,%.3f) squeeze=(%.3f,%.3f) changed=%u "
            "engine_writes=0",
            static_cast<unsigned long long>(input.sampleId),
            static_cast<unsigned long long>(g_inputSamplesObserved),
            input.flags, input.activeHands, input.buttons,
            input.aimPoseValidHands, input.gripPoseValidHands,
            input.moveX, input.moveY, input.turnX, input.turnY,
            input.trigger[FEARVR_HAND_LEFT],
            input.trigger[FEARVR_HAND_RIGHT],
            input.squeeze[FEARVR_HAND_LEFT],
            input.squeeze[FEARVR_HAND_RIGHT],
            stateChanged ? 1U : 0U);
        g_passThroughLog("m4_input_sample", detail);
    }
    g_lastInputButtons = input.buttons;
    g_lastInputHands = input.activeHands;
    g_lastInputFlags = input.flags;
    g_lastInputPoseMasks = poseMasks;
}

bool CameraRightVector(
    const RigidTransformAbi& transform,
    float (&right)[3]) noexcept {
    const float x = transform.rotation[0];
    const float y = transform.rotation[1];
    const float z = transform.rotation[2];
    const float w = transform.rotation[3];
    const float lengthSquared = x * x + y * y + z * z + w * w;
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.25F ||
        lengthSquared > 2.25F) {
        return false;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    const float qx = x * inverseLength;
    const float qy = y * inverseLength;
    const float qz = z * inverseLength;
    const float qw = w * inverseLength;
    right[0] = 1.0F - 2.0F * (qy * qy + qz * qz);
    right[1] = 2.0F * (qx * qy + qw * qz);
    right[2] = 2.0F * (qx * qz - qw * qy);
    return std::isfinite(right[0]) && std::isfinite(right[1]) &&
        std::isfinite(right[2]);
}

PhysicalMeleeRigidTransform ToPhysicalMeleeTransform(
    const RigidTransformAbi& transform) noexcept {
    return {
        {transform.position[0], transform.position[1],
         transform.position[2]},
        {transform.rotation[0], transform.rotation[1],
         transform.rotation[2], transform.rotation[3]}};
}

RigidTransformAbi ToRigidTransformAbi(
    const PhysicalMeleeRigidTransform& transform) noexcept {
    return {
        {transform.positionUnits.x, transform.positionUnits.y,
         transform.positionUnits.z},
        {transform.rotation.x, transform.rotation.y,
         transform.rotation.z, transform.rotation.w}};
}

bool RigidTransformNear(
    const RigidTransformAbi& left,
    const RigidTransformAbi& right) noexcept {
    constexpr float kPositionTolerance = 0.001F;
    constexpr float kRotationTolerance = 0.0001F;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(left.position[axis]) ||
            !std::isfinite(right.position[axis]) ||
            std::fabs(left.position[axis] - right.position[axis]) >
                kPositionTolerance) {
            return false;
        }
    }
    float quaternionDot = 0.0F;
    float leftLength = 0.0F;
    float rightLength = 0.0F;
    for (std::size_t axis = 0; axis < 4; ++axis) {
        if (!std::isfinite(left.rotation[axis]) ||
            !std::isfinite(right.rotation[axis])) {
            return false;
        }
        quaternionDot += left.rotation[axis] * right.rotation[axis];
        leftLength += left.rotation[axis] * left.rotation[axis];
        rightLength += right.rotation[axis] * right.rotation[axis];
    }
    if (leftLength < 0.25F || rightLength < 0.25F) {
        return false;
    }
    quaternionDot = std::fabs(
        quaternionDot / std::sqrt(leftLength * rightLength));
    return std::isfinite(quaternionDot) &&
        1.0F - quaternionDot <= kRotationTolerance;
}

void ClearPhysicalMeleeVisualSource() noexcept {
    AcquireSRWLockExclusive(&g_physicalMeleeVisualLock);
    g_physicalMeleeVisualWeaponReference = nullptr;
    g_physicalMeleeVisualWeapon = nullptr;
    g_physicalMeleeVisualWeaponIndex = -1;
    g_physicalMeleeVisualModelReference = nullptr;
    g_physicalMeleeVisualModel = nullptr;
    g_physicalMeleeVisualModelLocalGripPosition = {};
    g_physicalMeleeVisualModelLocalGripRotation = {
        0.0F, 0.0F, 0.0F, 1.0F};
    g_physicalMeleeSecondaryGripOffsetUnits = {};
    g_physicalMeleeSecondaryGripGrabRadiusMeters = 0.15F;
    g_physicalMeleeSecondaryGripProfileEnabled = false;
    g_activeWeaponGripCalibrationSlot = -1;
    ReleaseSRWLockExclusive(&g_physicalMeleeVisualLock);
    ResetPhysicalMeleeSecondaryGrip(true);
}

LiveEquippedWeaponVisualSource
ReadLiveEquippedWeaponVisualSource() noexcept {
    LiveEquippedWeaponVisualSource source{};
    void* const* weaponReference = nullptr;
    void* weapon = nullptr;
    void* const* modelReference = nullptr;
    void* modelObject = nullptr;
    AcquireSRWLockShared(&g_physicalMeleeVisualLock);
    weaponReference = g_physicalMeleeVisualWeaponReference;
    weapon = g_physicalMeleeVisualWeapon;
    source.weaponIndex = g_physicalMeleeVisualWeaponIndex;
    modelReference = g_physicalMeleeVisualModelReference;
    modelObject = g_physicalMeleeVisualModel;
    source.modelObject = modelObject;
    source.sourceGeneration =
        g_physicalMeleeVisualSourceGeneration;
    ReleaseSRWLockShared(&g_physicalMeleeVisualLock);

    const bool anySource = weaponReference != nullptr ||
        weapon != nullptr || modelReference != nullptr ||
        modelObject != nullptr;
    if (!anySource) {
        source.weaponIndex = -1;
        source.sourceGeneration = 0;
        source.modelObject = nullptr;
        return source;
    }
    if (weaponReference == nullptr || weapon == nullptr ||
        source.weaponIndex < 0 || modelReference == nullptr ||
        modelObject == nullptr || source.sourceGeneration == 0) {
        ClearPhysicalMeleeVisualSource();
        source.weaponIndex = -1;
        source.sourceGeneration = 0;
        source.modelObject = nullptr;
        return source;
    }

    void* referencedWeapon = nullptr;
    void* referencedModel = nullptr;
    bool referencesLive = false;
    __try {
        std::memcpy(
            &referencedWeapon, weaponReference,
            sizeof(referencedWeapon));
        std::memcpy(
            &referencedModel, modelReference,
            sizeof(referencedModel));
        referencesLive = referencedWeapon == weapon &&
            referencedModel == modelObject;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        referencesLive = false;
    }
    if (!referencesLive) {
        ClearPhysicalMeleeVisualSource();
        source.weaponIndex = -1;
        source.sourceGeneration = 0;
        source.modelObject = nullptr;
        return source;
    }
    source.live = true;
    return source;
}

bool UpdateRightHandIkTargetBasis(
    const RightHandIkTargetResult& target) noexcept {
    const bool changed = RightHandIkTargetBasisChanged(
        g_rightHandIkTargetSource,
        g_rightHandIkTargetWeaponIndex,
        g_rightHandIkTargetSourceGeneration,
        target);
    if (changed) {
        ResetArmIkBendMemory();
        ResetPhysicalMeleeWeaponWeight(
            fearvr::WeaponWeightResetReason::weaponChanged);
        ResetPhysicalMeleeSecondaryGrip(true);
    }
    g_rightHandIkTargetSource = target.source;
    g_rightHandIkTargetWeaponIndex =
        target.source == RightHandIkTargetSource::WeaponWeightedAim
            ? target.equippedWeaponIndex
            : -1;
    g_rightHandIkTargetSourceGeneration =
        target.source == RightHandIkTargetSource::WeaponWeightedAim
            ? target.sourceGeneration
            : 0;
    return changed;
}

void LogRightHandIkTargetSource(
    const LiveEquippedWeaponVisualSource& equippedSource,
    const RightHandIkTargetResult& selectedTarget,
    const PhysicalMeleeRigidTransform& finalTarget,
    bool finalTargetValid,
    bool gripPoseReady,
    bool aimPoseReady,
    bool weightedWeaponPoseReady,
    bool emptyHandCorrectionApplied,
    bool perWeaponCorrectionApplied,
    bool guidedHandPlacement,
    bool basisReset,
    const fearvr::TrackingQuaternion& gripWorldRotation,
    const fearvr::TrackingQuaternion& aimWorldRotation,
    std::uint64_t sampleId) noexcept {
    if (g_passThroughLog == nullptr) {
        return;
    }
    constexpr std::uint32_t kMaximumDiagnosticRecords = 160U;
    constexpr ULONGLONG kDiagnosticIntervalMilliseconds = 500U;
    const ULONGLONG now = GetTickCount64();
    const bool periodicSample =
        sampleId != 0 &&
        sampleId != g_rightHandIkTargetLastLoggedSampleId &&
        (g_rightHandIkTargetLastLogTick == 0 ||
         now - g_rightHandIkTargetLastLogTick >=
             kDiagnosticIntervalMilliseconds);
    if ((!basisReset && !periodicSample) ||
        g_rightHandIkTargetLogCount >= kMaximumDiagnosticRecords) {
        return;
    }

    float aimGripAngleDegrees = 0.0F;
    const bool angleValid = gripPoseReady && aimPoseReady &&
        RightHandIkQuaternionAngularDifferenceDegrees(
            gripWorldRotation, aimWorldRotation,
            aimGripAngleDegrees);
    char detail[1400]{};
    std::snprintf(
        detail, sizeof(detail),
        "sample_id=%llu equipped_index=%ld "
        "live_model_source=%u source_generation=%llu "
        "selected_source=%s grip_valid=%u aim_valid=%u "
        "weighted_valid=%u target_valid=%u "
        "grip_q=(%.6f,%.6f,%.6f,%.6f) "
        "aim_q=(%.6f,%.6f,%.6f,%.6f) "
        "aim_grip_angle_valid=%u aim_grip_angle_deg=%.3f "
        "target_position=(%.3f,%.3f,%.3f) "
        "target_q=(%.6f,%.6f,%.6f,%.6f) "
        "empty_hand_correction=%u per_weapon_correction=%u "
        "guided_hand_placement=%u basis_reset=%u",
        static_cast<unsigned long long>(sampleId),
        static_cast<long>(equippedSource.weaponIndex),
        equippedSource.live ? 1U : 0U,
        static_cast<unsigned long long>(
            equippedSource.sourceGeneration),
        RightHandIkTargetSourceName(selectedTarget.source),
        gripPoseReady ? 1U : 0U,
        aimPoseReady ? 1U : 0U,
        weightedWeaponPoseReady ? 1U : 0U,
        finalTargetValid ? 1U : 0U,
        gripWorldRotation.x, gripWorldRotation.y,
        gripWorldRotation.z, gripWorldRotation.w,
        aimWorldRotation.x, aimWorldRotation.y,
        aimWorldRotation.z, aimWorldRotation.w,
        angleValid ? 1U : 0U,
        angleValid ? aimGripAngleDegrees : -1.0F,
        finalTarget.positionUnits.x,
        finalTarget.positionUnits.y,
        finalTarget.positionUnits.z,
        finalTarget.rotation.x, finalTarget.rotation.y,
        finalTarget.rotation.z, finalTarget.rotation.w,
        emptyHandCorrectionApplied ? 1U : 0U,
        perWeaponCorrectionApplied ? 1U : 0U,
        guidedHandPlacement ? 1U : 0U,
        basisReset ? 1U : 0U);
    g_passThroughLog("m5_right_hand_ik_target_source", detail);
    g_rightHandIkTargetLastLoggedSampleId = sampleId;
    g_rightHandIkTargetLastLogTick = now;
    ++g_rightHandIkTargetLogCount;
}

bool CopyPhysicalMeleeSecondaryGripSettings(
    PhysicalMeleeSecondaryGripSettings& settings,
    std::int32_t& weaponIndex,
    std::uint64_t& sourceGeneration) noexcept {
    settings = {};
    weaponIndex = -1;
    sourceGeneration = 0;
    fearvr::TrackingVector offset{};
    float grabRadiusMeters = 0.15F;
    bool profileEnabled = false;
    bool sourceReady = false;
    AcquireSRWLockShared(&g_physicalMeleeVisualLock);
    weaponIndex = g_physicalMeleeVisualWeaponIndex;
    sourceGeneration = g_physicalMeleeVisualSourceGeneration;
    offset = g_physicalMeleeSecondaryGripOffsetUnits;
    grabRadiusMeters =
        g_physicalMeleeSecondaryGripGrabRadiusMeters;
    profileEnabled =
        g_physicalMeleeSecondaryGripProfileEnabled;
    sourceReady = g_physicalMeleeVisualWeapon != nullptr &&
        g_physicalMeleeVisualModel != nullptr;
    ReleaseSRWLockShared(&g_physicalMeleeVisualLock);
    const PhysicalMeleeProfile profile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    settings = PhysicalMeleeSecondaryGripSettingsFromProfile(profile);
    settings.offsetUnits = offset;
    settings.grabRadiusMeters = grabRadiusMeters;
    settings.enabled = sourceReady && profileEnabled &&
        InterlockedCompareExchange(
            &g_twoHandedMeleeEnabled, 0, 0) != 0;
    return sourceReady &&
        PhysicalMeleeSecondaryGripSettingsAreValid(settings);
}

PhysicalMeleeTwoHandPoseResult UpdatePhysicalMeleeTwoHandTarget(
    const fearvr::TrackingVector& primaryPositionUnits,
    const fearvr::TrackingQuaternion& primaryRotation,
    const fearvr::TrackingVector& secondaryPositionUnits,
    float secondarySqueeze,
    bool trackingFresh,
    bool contextEnabled,
    PhysicalMeleeSecondaryGripSettings& settings) noexcept {
    std::int32_t weaponIndex = -1;
    std::uint64_t sourceGeneration = 0;
    const bool settingsReady =
        CopyPhysicalMeleeSecondaryGripSettings(
            settings, weaponIndex, sourceGeneration);
    if (weaponIndex != g_physicalMeleeSecondaryGripWeaponIndex ||
        sourceGeneration !=
            g_physicalMeleeSecondaryGripSourceGeneration) {
        ResetPhysicalMeleeSecondaryGrip(true);
        g_physicalMeleeSecondaryGripWeaponIndex = weaponIndex;
        g_physicalMeleeSecondaryGripSourceGeneration =
            sourceGeneration;
    }
    const PhysicalMeleePose primary{
        primaryPositionUnits, primaryRotation};
    const PhysicalMeleeTwoHandPoseResult result =
        UpdatePhysicalMeleeSecondaryGrip(
            g_physicalMeleeSecondaryGripState,
            primary, secondaryPositionUnits, secondarySqueeze,
            trackingFresh,
            contextEnabled && settingsReady,
            settings);
    AcquireSRWLockExclusive(
        &g_physicalMeleeSecondaryGripTelemetryLock);
    g_physicalMeleeSecondaryGripDistanceMeters =
        result.handSeparationMeters;
    g_physicalMeleeSecondaryGripAnchorErrorMeters =
        result.attached
            ? result.anchorErrorMeters
            : result.grabDistanceMeters;
    ReleaseSRWLockExclusive(
        &g_physicalMeleeSecondaryGripTelemetryLock);
    InterlockedExchange(
        &g_physicalMeleeSecondaryGripAttached,
        result.attached ? 1 : 0);

    if (result.justAttached && g_passThroughLog != nullptr) {
        char detail[512]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld profile=%s hand=left "
            "grab_distance_m=%.3f hand_separation_m=%.3f "
            "offset_units=(%.3f,%.3f,%.3f) "
            "grab_radius_m=%.3f squeeze=%.3f "
            "dominant_hand=right solver=shortest_arc_no_scale "
            "weight_filter_preserved=1",
            static_cast<long>(weaponIndex),
            PhysicalMeleeProfileName(
                ResolvePhysicalMeleeProfileForRetailWeaponIndex(
                    weaponIndex).id),
            result.grabDistanceMeters,
            result.handSeparationMeters,
            settings.offsetUnits.x, settings.offsetUnits.y,
            settings.offsetUnits.z, settings.grabRadiusMeters,
            secondarySqueeze);
        g_passThroughLog(
            "m5_secondary_grip_attached", detail);
    }
    if (result.justReleased && g_passThroughLog != nullptr) {
        char detail[320]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld reason=%s squeeze=%.3f "
            "hand_separation_m=%.3f anchor_error_m=%.3f "
            "fallback=one_hand_weighted momentum_reset=0",
            static_cast<long>(weaponIndex),
            PhysicalMeleeSecondaryGripReleaseReasonName(
                result.releaseReason),
            std::isfinite(secondarySqueeze)
                ? secondarySqueeze : 0.0F,
            result.handSeparationMeters,
            result.anchorErrorMeters);
        g_passThroughLog(
            "m5_secondary_grip_released", detail);
    }
    return result;
}

bool ApplyPhysicalMeleeWeaponWeight(
    const fearvr::TrackingVector& rawPositionUnits,
    const fearvr::TrackingQuaternion& rawRotation,
    const fearvr::TrackingVector& referencePositionUnits,
    const fearvr::TrackingQuaternion& referenceRotation,
    std::uint64_t sampleId,
    std::uint64_t timestampNs,
    fearvr::TrackingVector& weightedPositionUnits,
    fearvr::TrackingQuaternion& weightedRotation) noexcept {
    weightedPositionUnits = rawPositionUnits;
    weightedRotation = rawRotation;
    if (!fearvr::IsFinite(rawPositionUnits) ||
        !fearvr::IsFinite(rawRotation) ||
        !fearvr::IsFinite(referencePositionUnits) ||
        !fearvr::IsFinite(referenceRotation)) {
        ResetPhysicalMeleeWeaponWeight(
            fearvr::WeaponWeightResetReason::nonFiniteValue);
        return false;
    }

    std::int32_t weaponIndex = -1;
    std::uint64_t sourceGeneration = 0;
    AcquireSRWLockShared(&g_physicalMeleeVisualLock);
    weaponIndex = g_physicalMeleeVisualWeaponIndex;
    sourceGeneration = g_physicalMeleeVisualSourceGeneration;
    ReleaseSRWLockShared(&g_physicalMeleeVisualLock);
    PhysicalMeleeProfile profile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(weaponIndex);
    ApplyToolMenuMeleeSettings(
        CopyToolMenuMeleeSettings(weaponIndex), profile);
    const bool weightedHandling =
        profile.handlingWeight > 1.001F;

    if (weaponIndex != g_physicalMeleeWeightedWeaponIndex ||
        sourceGeneration !=
            g_physicalMeleeWeightedSourceGeneration) {
        fearvr::ClearWeaponWeightFilter(
            g_physicalMeleeWeaponWeightFilter,
            fearvr::WeaponWeightResetReason::weaponChanged);
        g_physicalMeleeWeightedWeaponIndex = weaponIndex;
        g_physicalMeleeWeightedSourceGeneration = sourceGeneration;
        g_physicalMeleeWeightedSampleId = 0;
        g_physicalMeleeWeightedPoseValid = false;
        InterlockedExchange(&g_physicalMeleeWeightActiveLogged, 0);
    }
    if (!weightedHandling) {
        if (g_physicalMeleeWeaponWeightFilter.initialized) {
            fearvr::ClearWeaponWeightFilter(
                g_physicalMeleeWeaponWeightFilter,
                fearvr::WeaponWeightResetReason::enabledChanged);
        }
        g_physicalMeleeWeightedSampleId = 0;
        g_physicalMeleeWeightedPoseValid = false;
        return true;
    }
    if (sampleId == 0 || timestampNs == 0) {
        return true;
    }

    const float inverseUnitsPerMeter =
        1.0F / profile.unitsPerMeter;
    const fearvr::WeaponWeightReferenceFrame referenceFrame{
        {referencePositionUnits.x * inverseUnitsPerMeter,
         referencePositionUnits.y * inverseUnitsPerMeter,
         referencePositionUnits.z * inverseUnitsPerMeter},
        {referenceRotation.x, referenceRotation.y,
         referenceRotation.z, referenceRotation.w}};
    if (!fearvr::IsFinite(referenceFrame)) {
        ResetPhysicalMeleeWeaponWeight(
            fearvr::WeaponWeightResetReason::nonFiniteValue);
        return false;
    }

    const auto PublishLocalPose = [&](const fearvr::WeaponWeightPose& local) {
        const fearvr::WeaponWeightPose world =
            fearvr::WeaponWeightPoseFromReferenceFrame(
                referenceFrame, local);
        weightedPositionUnits = {
            world.position.x * profile.unitsPerMeter,
            world.position.y * profile.unitsPerMeter,
            world.position.z * profile.unitsPerMeter};
        weightedRotation = fearvr::Normalize(
            fearvr::TrackingQuaternion{
                world.orientation.x, world.orientation.y,
                world.orientation.z, world.orientation.w});
        return fearvr::IsFinite(weightedPositionUnits) &&
            fearvr::IsFinite(weightedRotation);
    };
    if (sampleId == g_physicalMeleeWeightedSampleId &&
        g_physicalMeleeWeightedPoseValid) {
        if (PublishLocalPose(g_physicalMeleeWeightedLocalPose)) {
            return true;
        }
        ResetPhysicalMeleeWeaponWeight(
            fearvr::WeaponWeightResetReason::nonFiniteValue);
        weightedPositionUnits = rawPositionUnits;
        weightedRotation = rawRotation;
        return true;
    }

    const fearvr::WeaponWeightPose worldTarget{
        {rawPositionUnits.x * inverseUnitsPerMeter,
         rawPositionUnits.y * inverseUnitsPerMeter,
         rawPositionUnits.z * inverseUnitsPerMeter},
        {rawRotation.x, rawRotation.y, rawRotation.z, rawRotation.w}};
    const fearvr::WeaponWeightPose localTarget =
        fearvr::WeaponWeightPoseToReferenceFrame(
            referenceFrame, worldTarget);
    if (!fearvr::IsFinite(localTarget)) {
        ResetPhysicalMeleeWeaponWeight(
            fearvr::WeaponWeightResetReason::nonFiniteValue);
        return false;
    }
    const fearvr::WeaponWeightProfile weightProfile{
        profile.handlingWeight,
        profile.positionalFollow,
        profile.rotationalFollow,
        profile.catchUpStrength,
        profile.dampingRatio};
    fearvr::WeaponWeightPose filtered{};
    fearvr::WeaponWeightDiagnostics diagnostics{};
    if (!fearvr::UpdateWeaponWeightFilter(
            g_physicalMeleeWeaponWeightFilter,
            localTarget, true, timestampNs, true,
            weightProfile, filtered, &diagnostics)) {
        return true;
    }

    if (!PublishLocalPose(filtered)) {
        ResetPhysicalMeleeWeaponWeight(
            fearvr::WeaponWeightResetReason::nonFiniteValue);
        weightedPositionUnits = rawPositionUnits;
        weightedRotation = rawRotation;
        return true;
    }
    g_physicalMeleeWeightedLocalPose = filtered;
    g_physicalMeleeWeightedSampleId = sampleId;
    g_physicalMeleeWeightedPoseValid = true;
    if (InterlockedCompareExchange(
            &g_physicalMeleeWeightActiveLogged, 1, 0) == 0 &&
        g_passThroughLog != nullptr) {
        char detail[384]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld profile=%s mass_kg=%.2f "
            "handling_weight=%.2f positional_follow=%.2f "
            "rotational_follow=%.2f catch_up=%.2f damping_ratio=%.2f "
            "filter=bounded_damped_spring momentum_follow_through=1 "
            "simulation_space=player_local locomotion_parent_rigid=1 "
            "bounded_tracking_fallback=1",
            static_cast<long>(weaponIndex),
            PhysicalMeleeProfileName(profile.id),
            profile.massKilograms, profile.handlingWeight,
            profile.positionalFollow, profile.rotationalFollow,
            profile.catchUpStrength, profile.dampingRatio);
        g_passThroughLog(
            "m5_physical_melee_weight_active", detail);
    }
    return true;
}

bool BeginPhysicalMeleeVisualOverride(
    const fearvr::TrackingVector& controllerPosition,
    const fearvr::TrackingQuaternion& controllerRotation,
    bool controllerPoseFresh,
    PhysicalMeleeVisualOverride& overrideState) noexcept {
    overrideState = {};
    if (!controllerPoseFresh ||
        InterlockedCompareExchange(
            &g_physicalMeleeVisualEnabled, 0, 0) == 0 ||
        g_client == nullptr || g_getRigidTransform == nullptr ||
        g_setRigidTransform == nullptr ||
        !fearvr::IsFinite(controllerPosition) ||
        !fearvr::IsFinite(controllerRotation)) {
        return false;
    }

    void* const* weaponReference = nullptr;
    void* weapon = nullptr;
    void* const* modelReference = nullptr;
    void* modelObject = nullptr;
    fearvr::TrackingVector modelLocalGripPosition{};
    fearvr::TrackingQuaternion modelLocalGripRotation{};
    std::uint64_t sourceGeneration = 0;
    AcquireSRWLockShared(&g_physicalMeleeVisualLock);
    weaponReference = g_physicalMeleeVisualWeaponReference;
    weapon = g_physicalMeleeVisualWeapon;
    modelReference = g_physicalMeleeVisualModelReference;
    modelObject = g_physicalMeleeVisualModel;
    modelLocalGripPosition =
        g_physicalMeleeVisualModelLocalGripPosition;
    modelLocalGripRotation =
        g_physicalMeleeVisualModelLocalGripRotation;
    sourceGeneration = g_physicalMeleeVisualSourceGeneration;
    ReleaseSRWLockShared(&g_physicalMeleeVisualLock);

    if (weaponReference == nullptr && weapon == nullptr &&
        modelReference == nullptr && modelObject == nullptr) {
        return false;
    }

    void* referencedWeapon = nullptr;
    void* referencedModel = nullptr;
    bool referencesLive = false;
    if (weaponReference != nullptr && weapon != nullptr &&
        modelReference != nullptr && modelObject != nullptr) {
        __try {
            std::memcpy(
                &referencedWeapon, weaponReference,
                sizeof(referencedWeapon));
            std::memcpy(
                &referencedModel, modelReference,
                sizeof(referencedModel));
            referencesLive = referencedWeapon == weapon &&
                referencedModel == modelObject;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            referencesLive = false;
        }
    }
    if (!referencesLive) {
        ClearPhysicalMeleeVisualSource();
        return false;
    }
    const fearvr::TrackingQuaternion desiredRotation =
        fearvr::Normalize(controllerRotation);
    const PhysicalMeleeRigidTransform desiredGripWorld{
        controllerPosition, desiredRotation};
    const PhysicalMeleeVisualProxyTransform solved =
        ResolvePhysicalMeleeHeldModelTransform(
            desiredGripWorld, modelLocalGripPosition,
            modelLocalGripRotation, true);
    if (!solved.active) {
        return false;
    }

    RigidTransformAbi original{};
    const RigidTransformAbi applied =
        ToRigidTransformAbi(solved.objectWorld);
    RigidTransformAbi readback{};
    bool set = false;
    bool verified = false;
    __try {
        set = g_getRigidTransform(
                  g_client, modelObject, &original) == 0UL &&
            PhysicalMeleeRigidTransformIsValid(
                ToPhysicalMeleeTransform(original)) &&
            g_setRigidTransform(
                g_client, modelObject, &applied) == 0UL;
        verified = set &&
            g_getRigidTransform(
                g_client, modelObject, &readback) == 0UL &&
            RigidTransformNear(readback, applied);
        if (set && !verified) {
            g_setRigidTransform(
                g_client, modelObject, &original);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (set) {
            __try {
                g_setRigidTransform(
                    g_client, modelObject, &original);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        verified = false;
    }
    if (!verified) {
        ClearPhysicalMeleeVisualSource();
        return false;
    }

    overrideState.object = modelObject;
    overrideState.original = original;
    overrideState.active = true;
    if (InterlockedCompareExchange(
            &g_physicalMeleeVisualActiveLogged, 1, 0) == 0 &&
        g_passThroughLog != nullptr) {
        char detail[384]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon=%p model_object=%p source_generation=%llu "
            "alignment=retail_model_grip_to_openxr_grip "
            "rotation=right_controller_aim "
            "temporary_render_override=1 native_impact_dispatch=blocked",
            weapon, modelObject,
            static_cast<unsigned long long>(sourceGeneration));
        g_passThroughLog(
            "m5_physical_melee_visual_proxy_active", detail);
    }
    return true;
}

bool EndPhysicalMeleeVisualOverride(
    PhysicalMeleeVisualOverride& overrideState) noexcept {
    if (!overrideState.active) {
        return true;
    }
    RigidTransformAbi readback{};
    bool restored = false;
    __try {
        restored = g_setRigidTransform(
                       g_client, overrideState.object,
                       &overrideState.original) == 0UL &&
            g_getRigidTransform(
                g_client, overrideState.object, &readback) == 0UL &&
            RigidTransformNear(readback, overrideState.original);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        restored = false;
    }
    overrideState.active = false;
    if (!restored) {
        InterlockedExchange(&g_physicalMeleeVisualEnabled, 0);
        if (InterlockedCompareExchange(
                &g_physicalMeleeVisualRestoreFailed, 1, 0) == 0 &&
            g_passThroughLog != nullptr) {
            g_passThroughLog(
                "m5_physical_melee_visual_proxy_restore_failed",
                "visual_proxy_disabled=1 exact_retail_transform_restore=0");
        }
    }
    return restored;
}

bool RestoreAndVerifyCamera(
    void* camera, const RigidTransformAbi& original,
    float originalFovX, float originalFovY) noexcept {
    if (g_client == nullptr || g_setRigidTransform == nullptr ||
        g_getRigidTransform == nullptr || g_setCameraFov == nullptr ||
        g_getCameraFov == nullptr) {
        return false;
    }
    RigidTransformAbi restored{};
    float restoredFovX = 0.0F;
    float restoredFovY = 0.0F;
    __try {
        const bool transformSet =
            g_setRigidTransform(g_client, camera, &original) == 0UL;
        g_setCameraFov(camera, originalFovX, originalFovY);
        const bool transformRead =
            g_getRigidTransform(g_client, camera, &restored) == 0UL;
        g_getCameraFov(camera, &restoredFovX, &restoredFovY);
        return transformSet && transformRead &&
            std::memcmp(&restored, &original, sizeof(original)) == 0 &&
            std::memcmp(&restoredFovX, &originalFovX, sizeof(float)) == 0 &&
            std::memcmp(&restoredFovY, &originalFovY, sizeof(float)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void SampleCameraReadOnly(void* camera, LONG renderCount) noexcept {
    if (!g_cameraReadProbe || g_client == nullptr || camera == nullptr ||
        g_getRigidTransform == nullptr || g_getCameraFov == nullptr ||
        g_passThroughLog == nullptr) {
        return;
    }

    RigidTransformAbi transform{};
    float fovX = 0.0F;
    float fovY = 0.0F;
    unsigned long transformResult = 1UL;
    bool exceptionOccurred = false;
    __try {
        transformResult = g_getRigidTransform(
            g_client, camera, &transform);
        g_getCameraFov(camera, &fovX, &fovY);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        exceptionOccurred = true;
    }

    const bool finite =
        std::isfinite(transform.position[0]) &&
        std::isfinite(transform.position[1]) &&
        std::isfinite(transform.position[2]) &&
        std::isfinite(transform.rotation[0]) &&
        std::isfinite(transform.rotation[1]) &&
        std::isfinite(transform.rotation[2]) &&
        std::isfinite(transform.rotation[3]) &&
        std::isfinite(fovX) && std::isfinite(fovY);
    if (exceptionOccurred || transformResult != 0UL || !finite) {
        if (InterlockedIncrement(&g_cameraReadFailures) == 1) {
            char detail[192]{};
            std::snprintf(
                detail, sizeof(detail),
                "exception=%u transform_result=%lu finite=%u",
                exceptionOccurred ? 1U : 0U, transformResult,
                finite ? 1U : 0U);
            g_passThroughLog("m3_camera_read_failed", detail);
        }
        return;
    }

    char detail[384]{};
    std::snprintf(
        detail, sizeof(detail),
        "count=%ld camera=%p pos=(%.6f,%.6f,%.6f) "
        "rotation=(%.6f,%.6f,%.6f,%.6f) fov_rad=(%.6f,%.6f) "
        "engine_writes=0",
        renderCount, camera,
        transform.position[0], transform.position[1],
        transform.position[2], transform.rotation[0],
        transform.rotation[1], transform.rotation[2],
        transform.rotation[3], fovX, fovY);
    g_passThroughLog("m3_camera_read_sample", detail);
}

bool ResolveDoubleRenderDiagnosticExports(HMODULE bridge) noexcept {
    g_getRenderRequest = reinterpret_cast<GetRenderRequestFunction>(
        GetProcAddress(bridge, "CondemnedVr_GetRenderRequest"));
    g_waitForNewRenderRequest =
        reinterpret_cast<WaitForNewRenderRequestFunction>(
            GetProcAddress(
                bridge, "CondemnedVr_WaitForNewRenderRequest"));
    g_beginEye = reinterpret_cast<BeginEyeFunction>(
        GetProcAddress(bridge, "CondemnedVr_BeginEye"));
    g_clearEye = reinterpret_cast<ClearEyeFunction>(
        GetProcAddress(bridge, "CondemnedVr_ClearEye"));
    g_captureEye = reinterpret_cast<CaptureEyeFunction>(
        GetProcAddress(bridge, "CondemnedVr_CaptureEye"));
    g_endStereoDiagnosticFrame =
        reinterpret_cast<EndStereoDiagnosticFrameFunction>(
            GetProcAddress(
                bridge, "CondemnedVr_EndStereoDiagnosticFrame"));
    g_endStereoFrame = reinterpret_cast<EndStereoFrameFunction>(
        GetProcAddress(bridge, "CondemnedVr_EndStereoFrame"));
    g_setFovScalePercent =
        reinterpret_cast<SetFovScalePercentFunction>(
            GetProcAddress(
                bridge, "CondemnedVr_SetFovScalePercent"));
    g_getInputState = reinterpret_cast<GetInputStateFunction>(
        GetProcAddress(bridge, "CondemnedVr_GetInputState"));
    g_drawOverlayLines = reinterpret_cast<DrawOverlayLinesFunction>(
        GetProcAddress(bridge, "CondemnedVr_DrawOverlayLines"));
    g_drawOverlayTriangles =
        reinterpret_cast<DrawOverlayTrianglesFunction>(
            GetProcAddress(
                bridge, "CondemnedVr_DrawOverlayTriangles"));
    const bool calibrationOverlayReady =
        InterlockedCompareExchange(
            &g_weaponGripCalibrationEnabled, 0, 0) == 0 ||
        g_drawOverlayLines != nullptr;
    return g_getRenderRequest != nullptr &&
        g_waitForNewRenderRequest != nullptr && g_beginEye != nullptr &&
        g_clearEye != nullptr && g_captureEye != nullptr &&
        g_endStereoDiagnosticFrame != nullptr &&
        g_endStereoFrame != nullptr &&
        g_setFovScalePercent != nullptr &&
        g_getInputState != nullptr && calibrationOverlayReady &&
        g_drawOverlayTriangles != nullptr;
}

void ReleaseStereoAttempt(bool retryOneShot) noexcept {
    if (g_continuousStereoTuning) {
        InterlockedExchange(&g_continuousRenderActive, 0);
    } else if (retryOneShot) {
        InterlockedExchange(&g_stereoDiagnosticState, 0);
    }
}

void EndFailedStereoAttempt() noexcept {
    if (g_continuousStereoTuning) {
        if (g_endStereoFrame != nullptr) {
            g_endStereoFrame(0);
        }
    } else if (g_endStereoDiagnosticFrame != nullptr) {
        g_endStereoDiagnosticFrame(0);
    }
}

bool TryDoubleRenderDiagnostic(
    void* renderer, void* camera, unsigned long& result) noexcept {
    if (!g_doubleRenderDiagnostic || g_renderCameraOverride == nullptr ||
        g_getRenderRequest == nullptr || g_beginEye == nullptr ||
        g_clearEye == nullptr || g_captureEye == nullptr ||
        g_endStereoDiagnosticFrame == nullptr ||
        (g_continuousStereoTuning && g_endStereoFrame == nullptr)) {
        return false;
    }
    if (g_continuousStereoTuning) {
        if (!g_continuousStereoEnabled ||
            InterlockedCompareExchange(
                &g_continuousRenderActive, 1, 0) != 0) {
            return false;
        }
    } else if (InterlockedCompareExchange(
                   &g_stereoDiagnosticState, 1, 0) != 0) {
        return false;
    }

    FearVrRenderRequest request{};
    if (!g_getRenderRequest(&request) || request.frameId == 0 ||
        (request.flags & FEARVR_RF_VALID) == 0) {
        InvalidateTrackedHeadAim();
        ReleaseStereoAttempt(true);
        return false;
    }
    if (g_continuousStereoTuning &&
        request.frameId == g_lastStereoRenderRequestId) {
        FearVrRenderRequest freshRequest{};
        constexpr std::uint32_t kMaximumPacingWaitMilliseconds = 20;
        if (g_waitForNewRenderRequest(
                request.frameId,
                kMaximumPacingWaitMilliseconds,
                &freshRequest) &&
            freshRequest.frameId != 0 &&
            (freshRequest.flags & FEARVR_RF_VALID) != 0) {
            request = freshRequest;
        }
    }
    if (g_controllerRecenterRequested) {
        g_controllerRecenterRequested = false;
        if ((request.flags & FEARVR_RF_FLATSCREEN) == 0) {
            g_trackingRecenterPending = true;
            if (g_passThroughLog != nullptr) {
                g_passThroughLog(
                    "m4_hmd_recenter_requested",
                    "button=right_stick path=tracked_camera_origin "
                    "yaw_only=1 translation_origin_reset=1");
            }
        } else if (g_passThroughLog != nullptr) {
            g_passThroughLog(
                "m4_panel_recenter_delegated",
                "button=right_stick path=openxr_host_flat_panel");
        }
    }
    if ((request.flags & FEARVR_RF_FLATSCREEN) != 0) {
        InvalidateTrackedHeadAim();
        // Retail composes menus, screens, and movies after RenderCamera. Let
        // the normal single camera call complete so Present capture can send
        // the finished backbuffer to both eyes as a world-locked panel.
        ReleaseStereoAttempt(true);
        return false;
    }

    RigidTransformAbi originalTransform{};
    float cameraRight[3]{};
    float halfIpdUnits = 0.0F;
    float originalFovX = 0.0F;
    float originalFovY = 0.0F;
    float stereoFovX = 0.0F;
    float stereoFovY = 0.0F;
    fearvr::RelativeEyePose trackedEye[FEARVR_EYE_COUNT]{};
    fearvr::TrackingVector headTranslation{};
    fearvr::TrackingVector headAimWorldPosition{};
    fearvr::TrackingQuaternion retailBaseWorldRotation{};
    fearvr::TrackingQuaternion headAimWorldRotation{};
    fearvr::TrackingVector controllerAimWorldPosition{};
    fearvr::TrackingQuaternion controllerAimWorldRotation{};
    fearvr::TrackingVector controllerWeaponWorldPosition{};
    fearvr::TrackingQuaternion controllerGripWorldRotation{};
    fearvr::TrackingQuaternion controllerWeaponWorldRotation{};
    fearvr::TrackingVector secondaryGripWorldPosition{};
    fearvr::TrackingQuaternion secondaryGripWorldRotation{};
    fearvr::TrackingQuaternion secondaryAimWorldRotation{};
    fearvr::TrackingVector physicalWeaponWorldPosition{};
    fearvr::TrackingQuaternion physicalWeaponWorldRotation{};
    fearvr::TrackingVector physicalSecondaryTargetWorldPosition{};
    PhysicalMeleeSecondaryGripSettings secondaryGripSettings{};
    PhysicalMeleeTwoHandPoseResult twoHandPose{};
    FearVrInputState controllerInput{};
    float secondaryGripSqueeze = 0.0F;
    std::uint64_t controllerAimSampleId = 0;
    std::uint64_t controllerAimTimestampNs = 0;
    bool headAimReady = false;
    bool controllerAimReady = false;
    bool controllerGripReady = false;
    bool controllerWeaponReady = false;
    bool secondaryGripReady = false;
    bool secondaryGripDebugReady = false;
    if (g_eyeOffsetDiagnostic) {
        const FearVrPose& left = request.eye[FEARVR_EYE_LEFT].pose;
        const FearVrPose& right = request.eye[FEARVR_EYE_RIGHT].pose;
        const float dx = right.px - left.px;
        const float dy = right.py - left.py;
        const float dz = right.pz - left.pz;
        const float ipdMeters = std::sqrt(dx * dx + dy * dy + dz * dz);
        const FearVrFov& leftFov = request.eye[FEARVR_EYE_LEFT].fov;
        const FearVrFov& rightFov = request.eye[FEARVR_EYE_RIGHT].fov;
        const float halfHorizontal = std::min(
            std::min(-leftFov.angleLeft, leftFov.angleRight),
            std::min(-rightFov.angleLeft, rightFov.angleRight));
        const float halfVertical = std::min(
            std::min(leftFov.angleUp, -leftFov.angleDown),
            std::min(rightFov.angleUp, -rightFov.angleDown));
        // The host has already applied the shared F.E.A.R.-style FOV scale
        // to the request and will submit that same projection to OpenXR.
        stereoFovX = halfHorizontal * 2.0F;
        stereoFovY = halfVertical * 2.0F;
        bool cameraReady = false;
        __try {
            cameraReady = std::isfinite(ipdMeters) &&
                ipdMeters >= 0.02F && ipdMeters <= 0.12F &&
                std::isfinite(stereoFovX) &&
                std::isfinite(stereoFovY) &&
                stereoFovX > 0.1F && stereoFovX < 3.0F &&
                stereoFovY > 0.1F && stereoFovY < 3.0F &&
                g_getRigidTransform != nullptr &&
                g_setRigidTransform != nullptr &&
                g_getCameraFov != nullptr &&
                g_setCameraFov != nullptr &&
                g_client != nullptr &&
                g_getRigidTransform(
                    g_client, camera, &originalTransform) == 0UL &&
                CameraRightVector(originalTransform, cameraRight);
            if (cameraReady) {
                g_getCameraFov(camera, &originalFovX, &originalFovY);
                cameraReady = std::isfinite(originalFovX) &&
                    std::isfinite(originalFovY);
            }
            if (cameraReady && g_continuousStereoTuning) {
                const FearVrPose currentCenter =
                    fearvr::CenterHeadPose(request);
                if (!fearvr::IsValidPose(currentCenter)) {
                    cameraReady = false;
                } else {
                    if (!g_trackingRecenterValid ||
                        g_trackingRecenterPending) {
                        const bool referenceChanged =
                            g_trackingRecenterPending ||
                            !g_trackingRecenterValid;
                        g_trackingRecenter =
                            fearvr::YawOnlyRecenterPose(currentCenter);
                        g_trackingRecenterValid =
                            fearvr::IsValidPose(g_trackingRecenter);
                        g_trackingRecenterPending = false;
                        if (g_trackingRecenterValid &&
                            referenceChanged) {
                            ResetPhysicalMeleeWeaponWeight(
                                fearvr::WeaponWeightResetReason::
                                    teleportedOrRecentered);
                            ResetArmIkBendMemory();
                        }
                        if (g_trackingRecenterValid &&
                            g_passThroughLog != nullptr) {
                            g_passThroughLog(
                                "m3_hmd_recentered",
                                "yaw_only=1 translation_origin_reset=1");
                        }
                    }
                    cameraReady = g_trackingRecenterValid;
                    if (cameraReady) {
                        headTranslation = g_hmdTranslationEnabled
                            ? fearvr::HeadTranslationRelativeToRecenter(
                                  g_trackingRecenter, currentCenter, 0.25F)
                            : fearvr::TrackingVector{};
                        for (std::uint32_t eye = 0;
                             eye < FEARVR_EYE_COUNT; ++eye) {
                            trackedEye[eye] =
                                fearvr::EyePoseRelativeToRecenter(
                                    g_trackingRecenter, currentCenter,
                                    request.eye[eye].pose, false);
                            cameraReady = cameraReady &&
                                trackedEye[eye].valid;
                        }
                        if (cameraReady && g_headAimEnabled) {
                            const fearvr::RelativeEyePose headRelative =
                                fearvr::TrackedPoseRelativeToRecenter(
                                    g_trackingRecenter, currentCenter);
                            cameraReady = headRelative.valid;
                            const fearvr::TrackingQuaternion baseRotation{
                                originalTransform.rotation[0],
                                originalTransform.rotation[1],
                                originalTransform.rotation[2],
                                originalTransform.rotation[3]};
                            retailBaseWorldRotation =
                                fearvr::Normalize(baseRotation);
                            const fearvr::TrackingVector worldHeadOffset =
                                fearvr::Rotate(
                                    retailBaseWorldRotation,
                                    {headTranslation.x * 100.0F,
                                     headTranslation.y * 100.0F,
                                     headTranslation.z * 100.0F});
                            headAimWorldPosition = {
                                originalTransform.position[0] +
                                    worldHeadOffset.x,
                                originalTransform.position[1] +
                                    worldHeadOffset.y,
                                originalTransform.position[2] +
                                    worldHeadOffset.z};
                            headAimWorldRotation = fearvr::Multiply(
                                retailBaseWorldRotation,
                                headRelative.rotation);
                            headAimReady = headRelative.valid &&
                                fearvr::IsFinite(headAimWorldPosition) &&
                                fearvr::IsFinite(retailBaseWorldRotation) &&
                                fearvr::IsFinite(headAimWorldRotation);

                            const ULONGLONG now = GetTickCount64();
                            const bool inputFresh =
                                g_getInputState != nullptr &&
                                g_getInputState(&controllerInput) != FALSE &&
                                controllerInput.sampleId != 0 &&
                                g_lastInputSampleTick != 0 &&
                                now - g_lastInputSampleTick <=
                                    kHeadAimFreshnessMilliseconds;
                            const ControllerAimWorldPose controllerAim =
                                ResolveControllerAimWorldPose(
                                    controllerInput, inputFresh,
                                    g_trackingRecenter,
                                    {originalTransform.position[0],
                                     originalTransform.position[1],
                                     originalTransform.position[2]},
                                    baseRotation, 100.0F);
                            controllerAimWorldPosition =
                                controllerAim.worldPosition;
                            controllerAimWorldRotation =
                                controllerAim.worldRotation;
                            controllerAimReady = controllerAim.active;
                            const ControllerAimWorldPose controllerGrip =
                                ResolveControllerGripWorldPose(
                                    controllerInput, inputFresh,
                                    g_trackingRecenter,
                                    {originalTransform.position[0],
                                     originalTransform.position[1],
                                     originalTransform.position[2]},
                                    baseRotation, 100.0F);
                            controllerWeaponWorldPosition =
                                controllerGrip.worldPosition;
                            controllerGripWorldRotation =
                                controllerGrip.worldRotation;
                            controllerGripReady = controllerGrip.active;
                            controllerWeaponWorldRotation =
                                controllerAim.worldRotation;
                            controllerWeaponReady =
                                controllerAim.active &&
                                controllerGripReady;
                            const ControllerAimWorldPose secondaryGrip =
                                ResolveControllerGripWorldPoseForHand(
                                    controllerInput, inputFresh,
                                    g_trackingRecenter,
                                    {originalTransform.position[0],
                                     originalTransform.position[1],
                                     originalTransform.position[2]},
                                    baseRotation,
                                    FEARVR_HAND_LEFT, 100.0F);
                            const ControllerAimWorldPose secondaryAim =
                                ResolveControllerAimWorldPoseForHand(
                                    controllerInput, inputFresh,
                                    g_trackingRecenter,
                                    {originalTransform.position[0],
                                     originalTransform.position[1],
                                     originalTransform.position[2]},
                                    baseRotation,
                                    FEARVR_HAND_LEFT, 100.0F);
                            secondaryGripWorldPosition =
                                secondaryGrip.worldPosition;
                            secondaryGripWorldRotation =
                                secondaryGrip.worldRotation;
                            secondaryAimWorldRotation =
                                secondaryAim.active
                                    ? secondaryAim.worldRotation
                                    : secondaryGrip.worldRotation;
                            secondaryGripReady = secondaryGrip.active;
                            secondaryGripSqueeze =
                                std::isfinite(controllerInput.squeeze[
                                    FEARVR_HAND_LEFT])
                                    ? controllerInput.squeeze[
                                          FEARVR_HAND_LEFT]
                                    : 0.0F;
                            if (controllerAimReady &&
                                controllerWeaponReady) {
                                controllerAimSampleId =
                                    controllerInput.sampleId;
                                controllerAimTimestampNs =
                                    controllerInput.predictedDisplayTimeNs;
                            }
                        }
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            cameraReady = false;
        }
        if (!cameraReady) {
            ReleaseStereoAttempt(true);
            return false;
        }
        halfIpdUnits = ipdMeters * 0.5F *
            (g_continuousStereoTuning
                 ? g_tuningUnitsPerMeter
                 : 100.0F);
    }

    const LiveEquippedWeaponVisualSource equippedWeaponSource =
        ReadLiveEquippedWeaponVisualSource();
    const bool heldWeaponSource =
        equippedWeaponSource.live &&
        equippedWeaponSource.weaponIndex !=
            kCondemnedUnarmedWeaponIndex;

    const fearvr::TrackingVector rawControllerWeaponWorldPosition =
        controllerWeaponWorldPosition;
    const fearvr::TrackingQuaternion rawControllerWeaponWorldRotation =
        controllerWeaponWorldRotation;
    const bool guidedHeldAlignment =
        HeldObjectAlignmentIsActive(g_heldObjectAlignmentState) &&
        heldWeaponSource &&
        g_heldObjectAlignmentState.weaponIndex ==
            equippedWeaponSource.weaponIndex &&
        g_heldObjectAlignmentState.sourceGeneration ==
            equippedWeaponSource.sourceGeneration;
    const bool guidedHeldHandPlacement =
        guidedHeldAlignment &&
        g_heldObjectAlignmentState.phase ==
            HeldObjectAlignmentPhase::AwaitControllerPose;

    if (heldWeaponSource) {
        twoHandPose = UpdatePhysicalMeleeTwoHandTarget(
            rawControllerWeaponWorldPosition,
            rawControllerWeaponWorldRotation,
            secondaryGripWorldPosition,
            secondaryGripSqueeze,
            controllerWeaponReady && secondaryGripReady,
            GameOwnsForegroundWindow() && !VrToolMenuIsOpen() &&
                !guidedHeldAlignment,
            secondaryGripSettings);
        if (!guidedHeldAlignment && controllerWeaponReady &&
            twoHandPose.poseValid) {
            controllerWeaponWorldPosition =
                twoHandPose.pose.gripPositionUnits;
            controllerWeaponWorldRotation =
                twoHandPose.pose.rotation;
        }
    }

    if (guidedHeldAlignment) {
        // Calibration must not depend on transient inertia or support-hand
        // state. Both captures use the same raw grip-position/aim-rotation
        // basis that owns the held-model transform.
        physicalWeaponWorldPosition =
            rawControllerWeaponWorldPosition;
        physicalWeaponWorldRotation =
            rawControllerWeaponWorldRotation;
    } else {
        physicalWeaponWorldPosition = controllerWeaponWorldPosition;
        physicalWeaponWorldRotation = controllerWeaponWorldRotation;
        if (controllerWeaponReady && heldWeaponSource) {
            const std::uint64_t weightTimestampNs =
                controllerAimTimestampNs != 0
                    ? controllerAimTimestampNs
                    : request.predictedDisplayTimeNs;
            controllerWeaponReady = ApplyPhysicalMeleeWeaponWeight(
                controllerWeaponWorldPosition,
                controllerWeaponWorldRotation,
                {originalTransform.position[0],
                 originalTransform.position[1],
                 originalTransform.position[2]},
                retailBaseWorldRotation,
                controllerAimSampleId, weightTimestampNs,
                physicalWeaponWorldPosition,
                physicalWeaponWorldRotation);
        } else if (!controllerGripReady || heldWeaponSource) {
            ResetPhysicalMeleeWeaponWeight(
                fearvr::WeaponWeightResetReason::trackingLost);
        }
    }
    secondaryGripDebugReady = heldWeaponSource &&
        controllerWeaponReady && secondaryGripReady &&
        PhysicalMeleeSecondaryGripSettingsAreValid(
            secondaryGripSettings) &&
        PhysicalMeleeLength(
            secondaryGripSettings.offsetUnits) >= 5.0F;
    if (secondaryGripDebugReady) {
        physicalSecondaryTargetWorldPosition = PhysicalMeleeAdd(
            physicalWeaponWorldPosition,
            fearvr::Rotate(
                physicalWeaponWorldRotation,
                secondaryGripSettings.offsetUnits));
        secondaryGripDebugReady = fearvr::IsFinite(
            physicalSecondaryTargetWorldPosition);
    }

    const RightHandIkTargetResult selectedRightHandTarget =
        ResolveRightHandIkTarget({
            controllerWeaponWorldPosition,
            controllerGripWorldRotation,
            physicalWeaponWorldPosition,
            physicalWeaponWorldRotation,
            equippedWeaponSource.weaponIndex,
            equippedWeaponSource.sourceGeneration,
            controllerGripReady,
            controllerWeaponReady && heldWeaponSource,
            equippedWeaponSource.live});
    const bool rightHandBasisReset =
        UpdateRightHandIkTargetBasis(selectedRightHandTarget);
    PhysicalMeleeRigidTransform armIkTarget{
        selectedRightHandTarget.worldPosition,
        selectedRightHandTarget.worldRotation};
    const PhysicalMeleeRigidTransform rawArmIkTarget =
        armIkTarget;
    bool emptyHandCorrectionApplied = false;
    bool perWeaponCorrectionApplied = false;
    if (selectedRightHandTarget.valid &&
        selectedRightHandTarget.source ==
            RightHandIkTargetSource::WeaponWeightedAim) {
        const ToolMenuRightHandIkSettings armIkSettings =
            CopyToolMenuRightHandIkSettings(
                selectedRightHandTarget.equippedWeaponIndex);
        armIkTarget = ResolveToolMenuRightHandIkTarget(
            armIkTarget, armIkSettings);
        perWeaponCorrectionApplied = true;
    } else if (selectedRightHandTarget.valid &&
               selectedRightHandTarget.source ==
                   RightHandIkTargetSource::EmptyGrip) {
        armIkTarget =
            ResolveEmptyRightHandAlignmentTarget(
                rawArmIkTarget,
                g_emptyRightHandAlignmentSettings);
        emptyHandCorrectionApplied = true;
    }
    if (guidedHeldHandPlacement && controllerGripReady) {
        const PhysicalMeleeRigidTransform rawGripHandTarget{
            rawControllerWeaponWorldPosition,
            controllerGripWorldRotation};
        armIkTarget = ResolveEmptyRightHandAlignmentTarget(
            rawGripHandTarget,
            g_emptyRightHandAlignmentSettings);
        emptyHandCorrectionApplied = true;
        perWeaponCorrectionApplied = false;
    }
    const bool rightTriggerDown =
        std::isfinite(
            controllerInput.trigger[FEARVR_HAND_RIGHT]) &&
        controllerInput.trigger[FEARVR_HAND_RIGHT] >= 0.65F;
    UpdateEmptyRightHandAlignmentCapture(
        selectedRightHandTarget, rawArmIkTarget,
        armIkTarget, rightTriggerDown);
    PhysicalMeleeRigidTransform displayedObjectWorld{};
    bool displayedObjectReady = false;
    const PhysicalMeleeRigidTransform desiredHeldControllerPose{
        physicalWeaponWorldPosition,
        physicalWeaponWorldRotation};
    if (HeldObjectAlignmentIsActive(g_heldObjectAlignmentState)) {
        if (guidedHeldHandPlacement) {
            displayedObjectWorld =
                g_heldObjectAlignmentState.referenceObjectWorld;
            displayedObjectReady =
                PhysicalMeleeRigidTransformIsValid(
                    displayedObjectWorld);
        } else {
            displayedObjectReady = ResolveActiveHeldObjectWorldPose(
                equippedWeaponSource.weaponIndex,
                equippedWeaponSource.sourceGeneration,
                desiredHeldControllerPose, displayedObjectWorld);
        }
    }
    const bool magazineAuthoringVisualRequested =
        InterlockedCompareExchange(&g_toolMenuOpen, 0, 0) != 0 &&
        g_toolMenuState.tab == ToolMenuTab::Author &&
        InterlockedCompareExchange(
            &g_weaponGripCalibrationEnabled, 0, 0) != 0 &&
        heldWeaponSource;
    if (magazineAuthoringVisualRequested &&
        !displayedObjectReady) {
        displayedObjectReady =
            ResolveActiveHeldObjectWorldPose(
                equippedWeaponSource.weaponIndex,
                equippedWeaponSource.sourceGeneration,
                desiredHeldControllerPose,
                displayedObjectWorld);
    }
    if (magazineAuthoringVisualRequested) {
        const PhysicalMeleeRigidTransform offHandCursorWorld{
            secondaryGripWorldPosition,
            secondaryGripWorldRotation};
        if (g_authoringPrimitive ==
            InteractionAuthoringPrimitive::MagazineInsertSocket) {
            UpdateMagazineSocketAuthoringVisual(
                equippedWeaponSource.weaponIndex,
                equippedWeaponSource.sourceGeneration,
                displayedObjectWorld,
                offHandCursorWorld,
                displayedObjectReady &&
                    controllerWeaponReady &&
                    secondaryGripReady);
        } else {
            UpdateSlideGrabAuthoringVisual(
                equippedWeaponSource.weaponIndex,
                equippedWeaponSource.sourceGeneration,
                displayedObjectWorld,
                offHandCursorWorld,
                displayedObjectReady &&
                    controllerWeaponReady &&
                    secondaryGripReady);
        }
    }
    if (heldWeaponSource &&
        equippedWeaponSource.weaponIndex ==
            kColtSlideGrabWeaponIndex &&
        !displayedObjectReady) {
        displayedObjectReady =
            ResolveActiveHeldObjectWorldPose(
                equippedWeaponSource.weaponIndex,
                equippedWeaponSource.sourceGeneration,
                desiredHeldControllerPose,
                displayedObjectWorld);
    }
    const PhysicalMeleeRigidTransform offHandWorld{
        secondaryGripWorldPosition,
        secondaryGripWorldRotation};
    const SlideGrabRuntimeOutput slideGrabRuntime =
        UpdateSlideGrabRuntime(
            equippedWeaponSource,
            displayedObjectWorld,
            offHandWorld,
            controllerInput,
            displayedObjectReady,
            secondaryGripReady);
    if (slideGrabRuntime.handTargetReady) {
        ResetPhysicalMeleeSecondaryGrip(true);
    }
    const bool armIkTargetValid =
        selectedRightHandTarget.valid &&
        PhysicalMeleeRigidTransformIsValid(armIkTarget);
    LogRightHandIkTargetSource(
        equippedWeaponSource, selectedRightHandTarget,
        armIkTarget, armIkTargetValid,
        controllerGripReady, controllerAimReady,
        controllerWeaponReady && heldWeaponSource,
        emptyHandCorrectionApplied,
        perWeaponCorrectionApplied, guidedHeldHandPlacement,
        rightHandBasisReset,
        controllerGripWorldRotation,
        controllerAimWorldRotation,
        controllerInput.sampleId);
    if (armIkTargetValid) {
        const float armIkPosition[3]{
            armIkTarget.positionUnits.x,
            armIkTarget.positionUnits.y,
            armIkTarget.positionUnits.z};
        const float armIkRotation[4]{
            armIkTarget.rotation.x,
            armIkTarget.rotation.y,
            armIkTarget.rotation.z,
            armIkTarget.rotation.w};
        const bool emptyHand =
            selectedRightHandTarget.source ==
                RightHandIkTargetSource::EmptyGrip;
        const std::uint64_t targetTimestampNs =
            emptyHand
                ? controllerInput.predictedDisplayTimeNs
                : controllerAimTimestampNs;
        PublishArmIkRightHandProofTarget(
            armIkPosition, armIkRotation,
            emptyHand
                ? controllerInput.sampleId
                : controllerAimSampleId,
            targetTimestampNs != 0
                ? targetTimestampNs
                : request.predictedDisplayTimeNs);
    } else {
        InvalidateArmIkRightHandProofTarget();
    }

    if (slideGrabRuntime.handTargetReady) {
        const float leftPositionValues[3]{
            slideGrabRuntime.handTargetWorld.positionUnits.x,
            slideGrabRuntime.handTargetWorld.positionUnits.y,
            slideGrabRuntime.handTargetWorld.positionUnits.z};
        const float leftRotationValues[4]{
            slideGrabRuntime.handTargetWorld.rotation.x,
            slideGrabRuntime.handTargetWorld.rotation.y,
            slideGrabRuntime.handTargetWorld.rotation.z,
            slideGrabRuntime.handTargetWorld.rotation.w};
        PublishArmIkLeftHandTarget(
            leftPositionValues, leftRotationValues,
            controllerInput.sampleId,
            controllerInput.predictedDisplayTimeNs != 0U
                ? controllerInput.predictedDisplayTimeNs
                : request.predictedDisplayTimeNs);
    } else if (secondaryGripReady) {
        const bool useSupportAnchor = twoHandPose.attached &&
            secondaryGripDebugReady;
        const fearvr::TrackingVector leftBasePosition =
            useSupportAnchor
                ? physicalSecondaryTargetWorldPosition
                : secondaryGripWorldPosition;
        fearvr::TrackingQuaternion leftBaseRotation{};
        const bool leftRotationReady =
            ResolvePhysicalMeleeSupportHandRotation(
                g_physicalMeleeSupportHandOrientation,
                physicalWeaponWorldRotation,
                secondaryGripWorldRotation,
                twoHandPose.attached,
                twoHandPose.justAttached,
                leftBaseRotation);
        const PhysicalMeleeRigidTransform leftTarget =
            ResolveToolMenuLeftHandIkTarget(
                {leftBasePosition, leftBaseRotation},
                ReadArmIkTuning());
        if (leftRotationReady &&
            PhysicalMeleeRigidTransformIsValid(leftTarget)) {
            const float leftPositionValues[3]{
                leftTarget.positionUnits.x,
                leftTarget.positionUnits.y,
                leftTarget.positionUnits.z};
            const float leftRotationValues[4]{
                leftTarget.rotation.x,
                leftTarget.rotation.y,
                leftTarget.rotation.z,
                leftTarget.rotation.w};
            PublishArmIkLeftHandTarget(
                leftPositionValues, leftRotationValues,
                controllerInput.sampleId,
                controllerInput.predictedDisplayTimeNs != 0U
                    ? controllerInput.predictedDisplayTimeNs
                    : request.predictedDisplayTimeNs);
        } else {
            InvalidateArmIkLeftHandTarget();
        }
    } else {
        g_physicalMeleeSupportHandOrientation = {};
        InvalidateArmIkLeftHandTarget();
    }

    PhysicalMeleeRigidTransform visualControllerPose =
        desiredHeldControllerPose;
    bool visualControllerPoseReady = controllerWeaponReady;
    if (guidedHeldHandPlacement) {
        visualControllerPoseReady =
            displayedObjectReady &&
            ResolveActiveHeldObjectVisualDriverPose(
                equippedWeaponSource.weaponIndex,
                equippedWeaponSource.sourceGeneration,
                displayedObjectWorld, visualControllerPose);
    }
    PhysicalMeleeVisualOverride meleeVisualOverride{};
    const bool visualOverrideReady =
        BeginPhysicalMeleeVisualOverride(
            visualControllerPose.positionUnits,
            visualControllerPose.rotation,
            visualControllerPoseReady,
            meleeVisualOverride);
    // A guided capture becomes authoritative only after the exact object
    // transform has been set and read back successfully for this frame.
    UpdateHeldObjectAlignmentCapture(
        heldWeaponSource, equippedWeaponSource.weaponIndex,
        equippedWeaponSource.sourceGeneration,
        desiredHeldControllerPose, displayedObjectWorld,
        armIkTarget,
        visualOverrideReady && visualControllerPoseReady &&
            displayedObjectReady && armIkTargetValid &&
            selectedRightHandTarget.valid &&
            selectedRightHandTarget.source ==
                RightHandIkTargetSource::WeaponWeightedAim,
        rightTriggerDown);
    unsigned long eyeResult[FEARVR_EYE_COUNT]{1UL, 1UL};
    std::uint32_t renderedEyes = 0;
    bool exceptionOccurred = false;
    bool restoreFailed = false;
    __try {
        for (std::uint32_t eye = 0; eye < FEARVR_EYE_COUNT; ++eye) {
            bool eyeStateSet = false;
            RigidTransformAbi renderedEyeTransform = originalTransform;
            __try {
                if (g_eyeOffsetDiagnostic) {
                    RigidTransformAbi eyeTransform = originalTransform;
                    if (g_continuousStereoTuning) {
                        const fearvr::TrackingQuaternion baseRotation{
                            originalTransform.rotation[0],
                            originalTransform.rotation[1],
                            originalTransform.rotation[2],
                            originalTransform.rotation[3]};
                        const float eyeScale =
                            g_tuningReversePolarity ? -1.0F : 1.0F;
                        const fearvr::TrackingVector localOffset{
                            headTranslation.x * 100.0F +
                                trackedEye[eye].positionMeters.x *
                                    g_tuningUnitsPerMeter * eyeScale,
                            headTranslation.y * 100.0F +
                                trackedEye[eye].positionMeters.y *
                                    g_tuningUnitsPerMeter * eyeScale,
                            headTranslation.z * 100.0F +
                                trackedEye[eye].positionMeters.z *
                                    g_tuningUnitsPerMeter * eyeScale};
                        const fearvr::TrackingVector worldOffset =
                            fearvr::Rotate(baseRotation, localOffset);
                        eyeTransform.position[0] += worldOffset.x;
                        eyeTransform.position[1] += worldOffset.y;
                        eyeTransform.position[2] += worldOffset.z;
                        const fearvr::TrackingQuaternion eyeRotation =
                            fearvr::Multiply(
                                baseRotation,
                                trackedEye[eye].rotation);
                        eyeTransform.rotation[0] = eyeRotation.x;
                        eyeTransform.rotation[1] = eyeRotation.y;
                        eyeTransform.rotation[2] = eyeRotation.z;
                        eyeTransform.rotation[3] = eyeRotation.w;
                    } else {
                        const float signedOffset =
                            eye == FEARVR_EYE_LEFT
                                ? -halfIpdUnits
                                : halfIpdUnits;
                        const float diagnosticOffset =
                            g_zeroEyeOffsetDiagnostic
                                ? 0.0F
                            : g_reverseEyeOffsetDiagnostic
                                ? -signedOffset
                                : signedOffset;
                        eyeTransform.position[0] +=
                            cameraRight[0] * diagnosticOffset;
                        eyeTransform.position[1] +=
                            cameraRight[1] * diagnosticOffset;
                        eyeTransform.position[2] +=
                            cameraRight[2] * diagnosticOffset;
                    }
                    eyeStateSet = g_setRigidTransform(
                        g_client, camera, &eyeTransform) == 0UL;
                    if (!eyeStateSet) {
                        break;
                    }
                    renderedEyeTransform = eyeTransform;
                    g_setCameraFov(camera, stereoFovX, stereoFovY);
                    float appliedFovX = 0.0F;
                    float appliedFovY = 0.0F;
                    g_getCameraFov(
                        camera, &appliedFovX, &appliedFovY);
                    if (std::memcmp(
                            &appliedFovX, &stereoFovX,
                            sizeof(float)) != 0 ||
                        std::memcmp(
                            &appliedFovY, &stereoFovY,
                            sizeof(float)) != 0) {
                        break;
                    }
                }
                g_beginEye(eye);
                if (!g_clearEye(eye)) {
                    break;
                }
                eyeResult[eye] = g_renderCameraOverride(
                    renderer, camera, nullptr);
                if (eyeResult[eye] == 0UL && controllerWeaponReady) {
                    DrawWeaponGripCalibrationControllerGizmo(
                        renderedEyeTransform,
                        controllerWeaponWorldPosition,
                        controllerGripWorldRotation,
                        controllerAimWorldRotation,
                        secondaryGripWorldPosition,
                        secondaryGripWorldRotation,
                        secondaryAimWorldRotation,
                        physicalSecondaryTargetWorldPosition,
                        secondaryGripSettings.grabRadiusMeters *
                            secondaryGripSettings.unitsPerMeter,
                        secondaryGripDebugReady,
                        twoHandPose.attached,
                        stereoFovX, stereoFovY);
                }
                if (eyeResult[eye] == 0UL) {
                    DrawMagazineSocketAuthoringGizmo(
                        renderedEyeTransform,
                        stereoFovX, stereoFovY);
                    DrawSlideGrabAuthoringGizmo(
                        renderedEyeTransform,
                        stereoFovX, stereoFovY);
                }
                if (eyeResult[eye] == 0UL) {
                    DrawPlayerCollisionXrayGizmo(
                        renderedEyeTransform,
                        stereoFovX, stereoFovY);
                    DrawPhysicalMeleeColliderGizmo(
                        renderedEyeTransform,
                        stereoFovX, stereoFovY);
                    DrawPhysicalMeleeBlockColliderGizmo(
                        renderedEyeTransform,
                        stereoFovX, stereoFovY);
                }
                if (eyeResult[eye] == 0UL) {
                    DrawVrToolMenuOverlay(
                        eye,
                        fearvr::InterpupillaryDistanceMeters(request),
                        stereoFovX);
                }
                g_captureEye(eye);
                ++renderedEyes;
            } __finally {
                if (eyeStateSet && !RestoreAndVerifyCamera(
                        camera, originalTransform,
                        originalFovX, originalFovY)) {
                    restoreFailed = true;
                }
            }
            if (restoreFailed || eyeResult[eye] != 0UL) {
                break;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        exceptionOccurred = true;
    }
    if (!EndPhysicalMeleeVisualOverride(meleeVisualOverride)) {
        restoreFailed = true;
    }

    if (exceptionOccurred || restoreFailed) {
        if (g_eyeOffsetDiagnostic) {
            RestoreAndVerifyCamera(
                camera, originalTransform,
                originalFovX, originalFovY);
        }
        EndFailedStereoAttempt();
        if (g_continuousStereoTuning) {
            g_continuousStereoEnabled = false;
            ReleaseStereoAttempt(false);
        } else {
            InterlockedExchange(&g_stereoDiagnosticState, 3);
        }
        if (g_passThroughLog != nullptr) {
            g_passThroughLog(
                g_eyeOffsetDiagnostic
                    ? "m3_eye_offset_diagnostic_failed"
                    : "m3_double_render_diagnostic_failed",
                restoreFailed
                    ? "restore_verified=0 partial_pair_discarded=1"
                    : "exception_caught=1 partial_pair_discarded=1");
        }
        result = 1UL;
        return !g_continuousStereoTuning;
    }

    const bool stereoComplete =
        renderedEyes == FEARVR_EYE_COUNT &&
        eyeResult[FEARVR_EYE_LEFT] == 0UL &&
        eyeResult[FEARVR_EYE_RIGHT] == 0UL;
    bool submitted = false;
    if (stereoComplete) {
        if (g_continuousStereoTuning) {
            g_endStereoFrame(request.frameId);
            submitted = true;
        } else {
            submitted = g_endStereoDiagnosticFrame(request.frameId) != FALSE;
        }
    }
    if (!submitted) {
        InvalidateTrackedHeadAim();
        EndFailedStereoAttempt();
        if (renderedEyes == 0) {
            ReleaseStereoAttempt(true);
            return false;
        }
        if (g_continuousStereoTuning) {
            ReleaseStereoAttempt(false);
        } else {
            InterlockedExchange(&g_stereoDiagnosticState, 3);
        }
        if (g_passThroughLog != nullptr) {
            g_passThroughLog(
                g_eyeOffsetDiagnostic
                    ? "m3_eye_offset_diagnostic_failed"
                    : "m3_double_render_diagnostic_failed",
                "partial_pair_discarded=1 mono_fallback_next_frame=1");
        }
        result = eyeResult[renderedEyes - 1];
        return !g_continuousStereoTuning;
    }

    if (headAimReady) {
        PublishTrackedHeadAim(
            camera, headAimWorldPosition,
            retailBaseWorldRotation, headAimWorldRotation);
        if (controllerGripReady && controllerAimReady) {
            PublishTrackedControllerGripPose(
                controllerWeaponWorldPosition,
                controllerGripWorldRotation,
                controllerAimSampleId,
                controllerAimTimestampNs);
        } else {
            InvalidateTrackedControllerGripPose();
        }
        if (controllerAimReady) {
            PublishTrackedControllerAim(
                controllerAimWorldPosition,
                controllerAimWorldRotation,
                controllerAimSampleId,
                controllerAimTimestampNs);
            if (controllerWeaponReady) {
                PublishTrackedControllerWeaponPose(
                    physicalWeaponWorldPosition,
                    physicalWeaponWorldRotation,
                    controllerAimSampleId,
                    controllerAimTimestampNs);
            } else {
                InvalidateTrackedControllerWeaponPose();
            }
        } else {
            InvalidateTrackedControllerAim();
        }
    } else {
        InvalidateTrackedHeadAim();
    }

    g_lastStereoRenderRequestId = request.frameId;

    if (g_continuousStereoTuning) {
        ReleaseStereoAttempt(false);
        if (InterlockedCompareExchange(
                &g_continuousStereoLogged, 1, 0) == 0 &&
            g_passThroughLog != nullptr) {
            LogStereoTuningState("continuous_stereo_started");
        }
        result = eyeResult[FEARVR_EYE_RIGHT];
        return true;
    }

    InterlockedExchange(&g_stereoDiagnosticState, 2);
    if (g_passThroughLog != nullptr) {
        char detail[192]{};
        if (g_eyeOffsetDiagnostic) {
            std::snprintf(
                detail, sizeof(detail),
                "frame=%llu world_render_calls=2 half_ipd_units=%.4f "
                "stereo_fov=(%.6f,%.6f) polarity=%s "
                "restore_verified=1 hold_ms=3000",
                static_cast<unsigned long long>(request.frameId),
                halfIpdUnits, stereoFovX, stereoFovY,
                g_zeroEyeOffsetDiagnostic
                    ? "zero"
                    : g_reverseEyeOffsetDiagnostic
                        ? "reversed"
                        : "normal");
        } else {
            std::snprintf(
                detail, sizeof(detail),
                "frame=%llu world_render_calls=2 camera_unchanged=1 "
                "fov_unchanged=1 hold_ms=1000",
                static_cast<unsigned long long>(request.frameId));
        }
        g_passThroughLog(
            g_eyeOffsetDiagnostic
                ? "m3_eye_offset_diagnostic_submitted"
                : "m3_double_render_diagnostic_submitted",
            detail);
    }
    result = eyeResult[FEARVR_EYE_RIGHT];
    return true;
}

unsigned long __fastcall HookRenderCamera(
    void* renderer,
    void* ignoredEdx,
    void* camera) {
    (void)ignoredEdx;
    const LONG count = InterlockedIncrement(&g_renderCameraCalls);
    HandleVrToolMenuControls();
    HandleStereoTuningControls();
    HandleWeaponGripCalibrationControls();
    SampleControllerInputReadOnly();
    InvalidateArmIkRightHandProofTarget();
    InvalidateArmIkLeftHandTarget();
    SampleArmIkDiscovery();
    const LiveEquippedWeaponVisualSource discoverySource =
        ReadLiveEquippedWeaponVisualSource();
    SampleWeaponModelDiscovery(
        discoverySource.live ? discoverySource.modelObject : nullptr,
        discoverySource.live ? discoverySource.weaponIndex : -1,
        discoverySource.live ? discoverySource.sourceGeneration : 0U);
    SampleArmIkRightHandProof();
    if (g_cameraReadProbe && (count == 1 || count % 600 == 0)) {
        SampleCameraReadOnly(camera, count);
    }
    if ((count == 1 || count % 600 == 0) && g_passThroughLog != nullptr) {
        char detail[192]{};
        std::snprintf(
            detail, sizeof(detail),
            g_continuousStereoTuning
                ? "count=%ld renderer=%p camera=%p continuous_tuning=1"
            : g_doubleRenderDiagnostic
                ? "count=%ld renderer=%p camera=%p bounded_pair_pending=1"
                : "count=%ld renderer=%p camera=%p original_calls=1",
            count, renderer, camera);
        g_passThroughLog("m3_render_camera_pass_through", detail);
    }
    RenderCameraFunction const original = g_originalRenderCamera;
    if (original == nullptr) {
        return 1UL;
    }
    unsigned long diagnosticResult = 1UL;
    if (TryDoubleRenderDiagnostic(
            renderer, camera, diagnosticResult)) {
        return diagnosticResult;
    }
    const unsigned long result = original(renderer, camera);
    if (g_submitStereoDiagnostic != nullptr &&
        InterlockedCompareExchange(
            &g_stereoDiagnosticState, 1, 0) == 0) {
        if (g_submitStereoDiagnostic()) {
            InterlockedExchange(&g_stereoDiagnosticState, 2);
            if (g_passThroughLog != nullptr) {
                g_passThroughLog(
                    "m3_stereo_diagnostic_submitted",
                    "one_pair=1 right_eye_marker=magenta hold_ms=1000");
            }
        } else {
            InterlockedExchange(&g_stereoDiagnosticState, 0);
        }
    }
    return result;
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
            array->count > 4096 || array->items == nullptr) {
            return nullptr;
        }
        for (std::uint32_t index = 0; index < array->count; ++index) {
            auto* const manager =
                static_cast<InterfaceNameManagerAbi*>(array->items[index]);
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

void ProbeRegisteredInterfaces(
    void* masterDatabase,
    RendererProbeLogFunction log) noexcept {
    __try {
        auto* const database =
            static_cast<InterfaceDatabaseAbi*>(masterDatabase);
        InterfaceArrayAbi* const array = database->interfaces;
        if (array == nullptr || array->count > array->capacity ||
            array->count > 4096 || array->items == nullptr) {
            log("m3_interface_database_invalid", "layout_guard_failed");
            return;
        }
        char summary[128]{};
        std::snprintf(
            summary, sizeof(summary), "registered_count=%lu",
            static_cast<unsigned long>(array->count));
        log("m3_interface_database", summary);
        for (std::uint32_t index = 0; index < array->count; ++index) {
            auto* const manager =
                static_cast<InterfaceNameManagerAbi*>(array->items[index]);
            if (manager == nullptr || manager->name == nullptr ||
                (std::strstr(manager->name, "ILTClient") == nullptr &&
                 std::strstr(manager->name, "ILTModel") == nullptr &&
                 std::strstr(manager->name, "ILTRenderer") == nullptr &&
                 std::strstr(manager->name, "IClientShell") == nullptr)) {
                continue;
            }
            char detail[384]{};
            std::snprintf(
                detail, sizeof(detail),
                "index=%lu name=%s version=%ld current=%p",
                static_cast<unsigned long>(index), manager->name,
                static_cast<long>(manager->version),
                manager->currentInterface);
            log("m3_interface_registration", detail);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log("m3_interface_database_invalid", "exception_while_reading");
    }
}

bool IsExecutableProtection(DWORD protection) noexcept {
    protection &= ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
    return protection == PAGE_EXECUTE ||
           protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

const char* BaseName(char* path) noexcept {
    char* const separator = std::strrchr(path, '\\');
    return separator == nullptr ? path : separator + 1;
}

void LogInterface(
    RendererProbeLogFunction log,
    const char* name,
    std::int32_t version,
    void* object) noexcept {
    void** vtable = nullptr;
    __try {
        if (object != nullptr) {
            vtable = *static_cast<void***>(object);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        vtable = nullptr;
    }
    char detail[384]{};
    std::snprintf(
        detail, sizeof(detail),
        "name=%s version=%ld object=%p vtable=%p",
        name, static_cast<long>(version), object, vtable);
    log("m3_interface", detail);
}

void ProbeRendererSlots(
    void* renderer,
    RendererProbeLogFunction log) noexcept {
    void** vtable = nullptr;
    __try {
        if (renderer != nullptr) {
            vtable = *static_cast<void***>(renderer);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        vtable = nullptr;
    }
    if (vtable == nullptr) {
        log("m3_renderer_probe_failed", "renderer_vtable_unreadable");
        return;
    }

    for (std::uint32_t slot = 0; slot < 32; ++slot) {
        void* target = nullptr;
        unsigned char bytes[16]{};
        bool readable = false;
        __try {
            target = vtable[slot];
            if (target != nullptr) {
                std::memcpy(bytes, target, sizeof(bytes));
                readable = true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            target = nullptr;
            readable = false;
        }

        MEMORY_BASIC_INFORMATION memory{};
        const bool queried = target != nullptr &&
            VirtualQuery(target, &memory, sizeof(memory)) == sizeof(memory);
        char modulePath[MAX_PATH]{"<unknown>"};
        std::uintptr_t rva = 0;
        if (queried && memory.AllocationBase != nullptr) {
            GetModuleFileNameA(
                static_cast<HMODULE>(memory.AllocationBase),
                modulePath, static_cast<DWORD>(sizeof(modulePath)));
            rva = reinterpret_cast<std::uintptr_t>(target) -
                reinterpret_cast<std::uintptr_t>(memory.AllocationBase);
        }

        char byteText[sizeof(bytes) * 3 + 1]{"<unreadable>"};
        if (readable) {
            std::size_t offset = 0;
            for (unsigned char byte : bytes) {
                const int written = std::snprintf(
                    byteText + offset, sizeof(byteText) - offset,
                    offset == 0 ? "%02X" : " %02X",
                    static_cast<unsigned>(byte));
                if (written <= 0) {
                    break;
                }
                offset += static_cast<std::size_t>(written);
            }
        }

        char detail[768]{};
        std::snprintf(
            detail, sizeof(detail),
            "slot=%lu target=%p module=%s rva=0x%08lX protect=0x%08lX "
            "executable=%u bytes=%s",
            static_cast<unsigned long>(slot), target,
            BaseName(modulePath), static_cast<unsigned long>(rva),
            queried ? static_cast<unsigned long>(memory.Protect) : 0UL,
            queried && IsExecutableProtection(memory.Protect) ? 1U : 0U,
            byteText);
        log("m3_renderer_slot", detail);

        if (readable &&
            bytes[0] == 0x8B && bytes[1] == 0x54 &&
            bytes[2] == 0x24 && bytes[3] == 0x04 &&
            bytes[4] == 0x8B && bytes[5] == 0x01 &&
            bytes[6] == 0x6A && bytes[7] == 0x00 &&
            bytes[8] == 0x52 && bytes[9] == 0xFF &&
            bytes[10] == 0x50 && bytes[12] == 0xC2 &&
            bytes[13] == 0x04 && bytes[14] == 0x00 &&
            bytes[11] % sizeof(void*) == 0) {
            char candidate[192]{};
            std::snprintf(
                candidate, sizeof(candidate),
                "slot=%lu delegates_to_slot=%u target=%p",
                static_cast<unsigned long>(slot),
                static_cast<unsigned>(bytes[11] / sizeof(void*)),
                target);
            log("m3_render_camera_forwarder_candidate", candidate);
        }
    }
    log("m3_renderer_probe_complete", "read_only_slots=32");
}

} // namespace

void ProbeRendererInterfaces(
    void* masterDatabase,
    RendererProbeLogFunction log) noexcept {
    if (log == nullptr || masterDatabase == nullptr) {
        return;
    }
    ProbeRegisteredInterfaces(masterDatabase, log);
    void* const client = FindCurrentInterface(
        masterDatabase, "ILTClient.Default", 104);
    void* const renderer = FindCurrentInterface(
        masterDatabase, "ILTRenderer.Default", 0);
    void* const clientShell = FindCurrentInterface(
        masterDatabase, "IClientShell.Default", 4);
    void* const model = FindCurrentInterface(
        masterDatabase, "ILTModelClient.Default", 0);
    LogInterface(log, "ILTClient.Default", 104, client);
    LogInterface(log, "ILTModelClient.Default", 0, model);
    LogInterface(log, "ILTRenderer.Default", 0, renderer);
    LogInterface(log, "IClientShell.Default", 4, clientShell);
    ProbeRendererSlots(renderer, log);
}

bool InstallRendererPassThroughProbe(
    void* masterDatabase,
    RendererProbeLogFunction log,
    void* diagnosticBridgeModule,
    bool doubleRenderDiagnostic,
    bool cameraReadProbe,
    bool eyeOffsetDiagnostic,
    bool reverseEyeOffsetDiagnostic,
    bool zeroEyeOffsetDiagnostic,
    bool continuousStereoTuning,
    bool controllerRecenter,
    bool headAim) noexcept {
    if (masterDatabase == nullptr || log == nullptr) {
        return false;
    }
    AcquireSRWLockExclusive(&g_passThroughLock);
    if (g_originalRenderCamera != nullptr) {
        ReleaseSRWLockExclusive(&g_passThroughLock);
        return true;
    }
    const bool pairDiagnostic =
        doubleRenderDiagnostic || eyeOffsetDiagnostic ||
        continuousStereoTuning;
    const bool stereoCameraPair =
        eyeOffsetDiagnostic || continuousStereoTuning;
    if (diagnosticBridgeModule != nullptr && !pairDiagnostic) {
        g_submitStereoDiagnostic =
            reinterpret_cast<SubmitStereoDiagnosticFunction>(
                GetProcAddress(
                    static_cast<HMODULE>(diagnosticBridgeModule),
                    "CondemnedVr_SubmitStereoDiagnostic"));
        if (g_submitStereoDiagnostic == nullptr) {
            log(
                "m3_pass_through_rejected",
                "stereo_diagnostic_export_missing");
            ReleaseSRWLockExclusive(&g_passThroughLock);
            return false;
        }
    }
    if (diagnosticBridgeModule != nullptr && pairDiagnostic &&
        !ResolveDoubleRenderDiagnosticExports(
            static_cast<HMODULE>(diagnosticBridgeModule))) {
        log(
            "m3_pass_through_rejected",
            "double_render_diagnostic_exports_missing");
        ReleaseSRWLockExclusive(&g_passThroughLock);
        return false;
    }

    void* const renderer = FindCurrentInterface(
        masterDatabase, "ILTRenderer.Default", 0);
    void* const client = FindCurrentInterface(
        masterDatabase, "ILTClient.Default", 104);
    void** vtable = nullptr;
    void** clientVtable = nullptr;
    void* rigidTransformTarget = nullptr;
    void* setRigidTransformTarget = nullptr;
    void* cameraFovTarget = nullptr;
    void* setCameraFovTarget = nullptr;
    __try {
        if (renderer != nullptr) {
            vtable = *static_cast<void***>(renderer);
        }
        if (client != nullptr) {
            clientVtable = *static_cast<void***>(client);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        vtable = nullptr;
    }
    auto* const executableBase = reinterpret_cast<unsigned char*>(
        GetModuleHandleW(nullptr));
    void* forwarder = nullptr;
    void* overrideTarget = nullptr;
    bool signaturesMatch = false;
    __try {
        if (vtable != nullptr && executableBase != nullptr) {
            forwarder = vtable[kRenderCameraSlot];
            overrideTarget = vtable[kRenderCameraOverrideSlot];
            signaturesMatch =
                forwarder == executableBase + kRenderCameraForwarderRva &&
                overrideTarget == executableBase + kRenderCameraOverrideRva &&
                std::memcmp(
                    forwarder, kRenderCameraForwarder,
                    sizeof(kRenderCameraForwarder)) == 0 &&
                std::memcmp(
                    overrideTarget, kRenderCameraOverridePrefix,
                    sizeof(kRenderCameraOverridePrefix)) == 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        signaturesMatch = false;
    }
    if (!signaturesMatch) {
        char detail[256]{};
        std::snprintf(
            detail, sizeof(detail),
            "slot18=%p expected_rva=0x%08lX slot20=%p "
            "expected_override_rva=0x%08lX",
            forwarder,
            static_cast<unsigned long>(kRenderCameraForwarderRva),
            overrideTarget,
            static_cast<unsigned long>(kRenderCameraOverrideRva));
        log("m3_pass_through_rejected", detail);
        ReleaseSRWLockExclusive(&g_passThroughLock);
        return false;
    }

    const bool cameraAbiRequired =
        cameraReadProbe || stereoCameraPair || headAim;
    if (cameraAbiRequired) {
        bool cameraReadSignaturesMatch = false;
        __try {
            if (clientVtable != nullptr && executableBase != nullptr) {
                rigidTransformTarget =
                    clientVtable[kGetRigidTransformSlot];
                setRigidTransformTarget =
                    clientVtable[kSetRigidTransformSlot];
                cameraFovTarget = *reinterpret_cast<void**>(
                    static_cast<unsigned char*>(client) +
                    kGetCameraFovMemberOffset);
                setCameraFovTarget = *reinterpret_cast<void**>(
                    static_cast<unsigned char*>(client) +
                    kSetCameraFovMemberOffset);
                cameraReadSignaturesMatch =
                    rigidTransformTarget ==
                        executableBase + kGetRigidTransformRva &&
                    cameraFovTarget == executableBase + kGetCameraFovRva &&
                    std::memcmp(
                        rigidTransformTarget, kGetRigidTransformPrefix,
                        sizeof(kGetRigidTransformPrefix)) == 0 &&
                    std::memcmp(
                        cameraFovTarget, kGetCameraFovPrefix,
                        sizeof(kGetCameraFovPrefix)) == 0 &&
                    (!stereoCameraPair ||
                     (setRigidTransformTarget ==
                          executableBase + kSetRigidTransformRva &&
                      setCameraFovTarget ==
                          executableBase + kSetCameraFovRva &&
                      std::memcmp(
                          setRigidTransformTarget,
                          kSetRigidTransformPrefix,
                          sizeof(kSetRigidTransformPrefix)) == 0 &&
                      std::memcmp(
                          setCameraFovTarget,
                          kSetCameraFovPrefix,
                          sizeof(kSetCameraFovPrefix)) == 0));
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            cameraReadSignaturesMatch = false;
        }
        if (!cameraReadSignaturesMatch) {
            char detail[256]{};
            std::snprintf(
                detail, sizeof(detail),
                "slot21=%p expected_rva=0x%08lX slot23=%p "
                "expected_set_rva=0x%08lX get_fov=%p set_fov=%p",
                rigidTransformTarget,
                static_cast<unsigned long>(kGetRigidTransformRva),
                setRigidTransformTarget,
                static_cast<unsigned long>(kSetRigidTransformRva),
                cameraFovTarget, setCameraFovTarget);
            log("m3_camera_read_rejected", detail);
            ReleaseSRWLockExclusive(&g_passThroughLock);
            return false;
        }
    }

    if (headAim) {
        const MH_STATUS initialize = MH_Initialize();
        if (initialize != MH_OK &&
            initialize != MH_ERROR_ALREADY_INITIALIZED) {
            log(
                "m5_head_camera_transform_rejected",
                MH_StatusToString(initialize));
            ReleaseSRWLockExclusive(&g_passThroughLock);
            return false;
        }
        MH_STATUS status = MH_CreateHook(
            rigidTransformTarget,
            reinterpret_cast<void*>(&HookGetRigidTransformForHeadAim),
            reinterpret_cast<void**>(
                &g_originalHeadAimGetRigidTransform));
        if (status == MH_OK) {
            status = MH_EnableHook(rigidTransformTarget);
        }
        if (status != MH_OK) {
            MH_RemoveHook(rigidTransformTarget);
            g_originalHeadAimGetRigidTransform = nullptr;
            log(
                "m5_head_camera_transform_rejected",
                MH_StatusToString(status));
            ReleaseSRWLockExclusive(&g_passThroughLock);
            return false;
        }
        g_headAimGetRigidTransformTarget = rigidTransformTarget;
    }

    DWORD oldProtection = 0;
    if (!VirtualProtect(
            &vtable[kRenderCameraSlot], sizeof(void*),
            PAGE_READWRITE, &oldProtection)) {
        if (g_headAimGetRigidTransformTarget != nullptr) {
            MH_DisableHook(g_headAimGetRigidTransformTarget);
            MH_RemoveHook(g_headAimGetRigidTransformTarget);
            g_headAimGetRigidTransformTarget = nullptr;
            g_originalHeadAimGetRigidTransform = nullptr;
        }
        log("m3_pass_through_rejected", "vtable_protect_failed");
        ReleaseSRWLockExclusive(&g_passThroughLock);
        return false;
    }
    g_passThroughLog = log;
    g_originalRenderCamera =
        reinterpret_cast<RenderCameraFunction>(forwarder);
    g_renderCameraOverride =
        reinterpret_cast<RenderCameraOverrideFunction>(overrideTarget);
    g_doubleRenderDiagnostic = pairDiagnostic;
    g_eyeOffsetDiagnostic = stereoCameraPair;
    g_reverseEyeOffsetDiagnostic = reverseEyeOffsetDiagnostic;
    g_zeroEyeOffsetDiagnostic = zeroEyeOffsetDiagnostic;
    g_continuousStereoTuning = continuousStereoTuning;
    g_toolMenuState = {};
    g_toolMenuWeaponSettingsRegistry = {};
    g_toolMenuPanelPlacement = {};
    g_emptyRightHandAlignmentSettings = {};
    g_emptyRightHandAlignmentState = {};
    g_emptyRightHandAlignmentLastEvent =
        EmptyRightHandAlignmentEvent::None;
    g_emptyRightHandAlignmentLastSaveResult =
        WeaponSettingsStoreResult::NotFound;
    InterlockedExchange(
        &g_toolMenuEnabled,
        continuousStereoTuning && g_drawOverlayTriangles != nullptr
            ? 1 : 0);
    InterlockedExchange(&g_toolMenuShortcutEnabled, 0);
    InterlockedExchange(&g_toolMenuShortcutSettingsReady, 0);
    InterlockedExchange(&g_toolMenuOpen, 0);
    InterlockedExchange(&g_toolMenuReleaseCapture, 0);
    InterlockedExchange(&g_toolMenuOverlayFailureLogged, 0);
    g_controllerRecenterEnabled =
        controllerRecenter && continuousStereoTuning;
    g_headAimEnabled = headAim && continuousStereoTuning;
    InvalidateTrackedHeadAim();
    if (continuousStereoTuning) {
        g_tuningUnitsPerMeter = 100.0F;
        g_tuningReversePolarity = false;
        g_tuningFovScale = kCondemnedDefaultFovScale;
        g_hmdTranslationEnabled = true;
        g_trackingRecenterPending = true;
        g_setFovScalePercent(130U);
    }
    g_client = cameraAbiRequired ? client : nullptr;
    g_getRigidTransform = cameraAbiRequired
        ? (g_headAimEnabled
               ? g_originalHeadAimGetRigidTransform
               : reinterpret_cast<GetRigidTransformFunction>(
                     rigidTransformTarget))
        : nullptr;
    g_setRigidTransform = stereoCameraPair
        ? reinterpret_cast<SetRigidTransformFunction>(
              setRigidTransformTarget)
        : nullptr;
    g_getCameraFov = cameraAbiRequired
        ? reinterpret_cast<GetCameraFovFunction>(cameraFovTarget)
        : nullptr;
    g_setCameraFov = stereoCameraPair
        ? reinterpret_cast<SetCameraFovFunction>(setCameraFovTarget)
        : nullptr;
    g_cameraReadProbe = cameraReadProbe;
    void* const prior = InterlockedExchangePointer(
        reinterpret_cast<void* volatile*>(&vtable[kRenderCameraSlot]),
        reinterpret_cast<void*>(&HookRenderCamera));
    DWORD ignoredProtection = 0;
    const BOOL restored = VirtualProtect(
        &vtable[kRenderCameraSlot], sizeof(void*),
        oldProtection, &ignoredProtection);
    FlushInstructionCache(
        GetCurrentProcess(), &vtable[kRenderCameraSlot], sizeof(void*));
    if (prior != forwarder || !restored) {
        DWORD repairProtection = 0;
        if (VirtualProtect(
                &vtable[kRenderCameraSlot], sizeof(void*),
                PAGE_READWRITE, &repairProtection)) {
            InterlockedExchangePointer(
                reinterpret_cast<void* volatile*>(
                    &vtable[kRenderCameraSlot]),
                forwarder);
            DWORD ignored = 0;
            VirtualProtect(
                &vtable[kRenderCameraSlot], sizeof(void*),
                repairProtection, &ignored);
        }
        g_originalRenderCamera = nullptr;
        g_renderCameraOverride = nullptr;
        g_passThroughLog = nullptr;
        g_submitStereoDiagnostic = nullptr;
        g_doubleRenderDiagnostic = false;
        g_client = nullptr;
        g_getRigidTransform = nullptr;
        g_setRigidTransform = nullptr;
        g_getCameraFov = nullptr;
        g_setCameraFov = nullptr;
        g_setFovScalePercent = nullptr;
        g_getInputState = nullptr;
        g_drawOverlayLines = nullptr;
        g_drawOverlayTriangles = nullptr;
        InterlockedExchange(&g_toolMenuEnabled, 0);
        InterlockedExchange(&g_toolMenuShortcutEnabled, 0);
        InterlockedExchange(&g_toolMenuShortcutSettingsReady, 0);
        InterlockedExchange(&g_toolMenuOpen, 0);
        InterlockedExchange(&g_toolMenuReleaseCapture, 0);
        g_cameraReadProbe = false;
        g_eyeOffsetDiagnostic = false;
        g_reverseEyeOffsetDiagnostic = false;
        g_zeroEyeOffsetDiagnostic = false;
        g_continuousStereoTuning = false;
        g_headAimEnabled = false;
        InvalidateTrackedHeadAim();
        if (g_headAimGetRigidTransformTarget != nullptr) {
            MH_DisableHook(g_headAimGetRigidTransformTarget);
            MH_RemoveHook(g_headAimGetRigidTransformTarget);
            g_headAimGetRigidTransformTarget = nullptr;
            g_originalHeadAimGetRigidTransform = nullptr;
        }
        log("m3_pass_through_rejected", "vtable_exchange_failed");
        ReleaseSRWLockExclusive(&g_passThroughLock);
        return false;
    }

    bool toolMenuShortcutEnabled = true;
    const WeaponSettingsStoreResult toolMenuShortcutLoadResult =
        LoadToolMenuShortcutEnabled(toolMenuShortcutEnabled);
    const bool malformedToolMenuShortcut =
        toolMenuShortcutLoadResult ==
            WeaponSettingsStoreResult::ParseFailed ||
        toolMenuShortcutLoadResult ==
            WeaponSettingsStoreResult::ReadFailed;
    if (malformedToolMenuShortcut) {
        toolMenuShortcutEnabled = false;
    }
    InterlockedExchange(
        &g_toolMenuShortcutEnabled,
        toolMenuShortcutEnabled ? 1 : 0);
    InterlockedExchange(&g_toolMenuShortcutSettingsReady, 1);
    char toolMenuShortcutDetail[224]{};
    std::snprintf(
        toolMenuShortcutDetail, sizeof(toolMenuShortcutDetail),
        "result=%s enabled=%u capability_available=%u fallback=%s",
        WeaponSettingsStoreResultName(toolMenuShortcutLoadResult),
        toolMenuShortcutEnabled ? 1U : 0U,
        InterlockedCompareExchange(&g_toolMenuEnabled, 0, 0) != 0
            ? 1U : 0U,
        toolMenuShortcutLoadResult == WeaponSettingsStoreResult::Ok
            ? "stored_or_packaged"
            : malformedToolMenuShortcut
                ? "malformed_fail_closed_disabled"
                : "missing_preserve_enabled");
    log("m6_vr_tool_menu_shortcut_loaded", toolMenuShortcutDetail);
    PlayerColliderSettings playerColliderSettings{};
    const WeaponSettingsStoreResult playerColliderLoadResult =
        LoadPlayerColliderSettings(playerColliderSettings);
    if (playerColliderLoadResult !=
            WeaponSettingsStoreResult::Ok) {
        playerColliderSettings = {};
    }
    const bool playerColliderConfigured =
        ConfigurePlayerColliderSettings(
            playerColliderSettings);
    PlayerColliderTelemetry playerColliderTelemetry{};
    ReadPlayerColliderTelemetry(playerColliderTelemetry);
    char playerColliderDetail[256]{};
    std::snprintf(
        playerColliderDetail, sizeof(playerColliderDetail),
        "result=%s width_scale=%.2f configured=%u hook_ready=%u "
        "failure_default=retail_width",
        WeaponSettingsStoreResultName(
            playerColliderLoadResult),
        playerColliderSettings.widthScale,
        playerColliderConfigured ? 1U : 0U,
        playerColliderTelemetry.hookReady ? 1U : 0U);
    log(
        playerColliderConfigured
            ? "m5_player_collider_settings_loaded"
            : "m5_player_collider_settings_load_failed",
        playerColliderDetail);


    EmptyRightHandAlignmentSettings emptyHandSettings{};
    const WeaponSettingsStoreResult emptyHandLoadResult =
        LoadEmptyRightHandAlignmentSettings(emptyHandSettings);
    if (emptyHandLoadResult == WeaponSettingsStoreResult::Ok) {
        g_emptyRightHandAlignmentSettings =
            emptyHandSettings;
    } else {
        g_emptyRightHandAlignmentSettings = {};
    }
    char emptyHandDetail[320]{};
    std::snprintf(
        emptyHandDetail, sizeof(emptyHandDetail),
        "result=%s failure_default=identity "
        "offset_units=(%.3f,%.3f,%.3f) "
        "offset_q=(%.6f,%.6f,%.6f,%.6f)",
        WeaponSettingsStoreResultName(emptyHandLoadResult),
        g_emptyRightHandAlignmentSettings
            .localPositionOffsetUnits.x,
        g_emptyRightHandAlignmentSettings
            .localPositionOffsetUnits.y,
        g_emptyRightHandAlignmentSettings
            .localPositionOffsetUnits.z,
        g_emptyRightHandAlignmentSettings.localRotationOffset.x,
        g_emptyRightHandAlignmentSettings.localRotationOffset.y,
        g_emptyRightHandAlignmentSettings.localRotationOffset.z,
        g_emptyRightHandAlignmentSettings.localRotationOffset.w);
    log("m5_empty_right_hand_alignment_loaded", emptyHandDetail);

    ToolMenuDebugDrawSettings debugDrawSettings{};
    const WeaponSettingsStoreResult debugDrawLoadResult =
        LoadDebugDrawSettings(debugDrawSettings);
    if (debugDrawLoadResult != WeaponSettingsStoreResult::Ok) {
        debugDrawSettings = {};
    }
    InterlockedExchange(
        &g_physicalMeleeColliderDebugDrawVisible,
        debugDrawSettings.colliderVisible ? 1 : 0);
    InterlockedExchange(
        &g_physicalMeleeBlockColliderDebugDrawVisible,
        debugDrawSettings.blockColliderVisible ? 1 : 0);
    InterlockedExchange(
        &g_weaponGripControllerDebugDrawVisible,
        debugDrawSettings.controllerVisible ? 1 : 0);
    char debugDrawDetail[192]{};
    std::snprintf(
        debugDrawDetail, sizeof(debugDrawDetail),
        "result=%s collider=%u block_collider=%u controller=%u "
        "failure_default=hidden",
        WeaponSettingsStoreResultName(debugDrawLoadResult),
        debugDrawSettings.colliderVisible ? 1U : 0U,
        debugDrawSettings.blockColliderVisible ? 1U : 0U,
        debugDrawSettings.controllerVisible ? 1U : 0U);
    log("m5_debug_draw_settings_loaded", debugDrawDetail);

    log(
        "m3_pass_through_installed",
        doubleRenderDiagnostic
            ? "slot=18 delegate_slot=20 bounded_double_render=1"
            : continuousStereoTuning
                ? "slot=18 delegate_slot=20 continuous_stereo_tuning=1"
            : eyeOffsetDiagnostic
                ? "slot=18 delegate_slot=20 bounded_eye_offset=1"
            : "slot=18 delegate_slot=20 original_calls_per_hook=1");
    if (cameraReadProbe) {
        log(
            "m3_camera_read_armed",
            "transform_slot=21 fov_member_offset=0x5c sample_period=600 "
            "engine_writes=0");
    }
    if (eyeOffsetDiagnostic) {
        log(
            "m3_eye_offset_diagnostic_armed",
            zeroEyeOffsetDiagnostic
                ? "get_transform_slot=21 set_transform_slot=23 "
                  "get_fov_offset=0x5c set_fov_offset=0x60 "
                  "units_per_meter=100 exact_restore_after_each_eye=1 "
                  "polarity=zero"
            : reverseEyeOffsetDiagnostic
                ? "get_transform_slot=21 set_transform_slot=23 "
                  "get_fov_offset=0x5c set_fov_offset=0x60 "
                  "units_per_meter=100 exact_restore_after_each_eye=1 "
                  "polarity=reversed"
                : "get_transform_slot=21 set_transform_slot=23 "
                  "get_fov_offset=0x5c set_fov_offset=0x60 "
                  "units_per_meter=100 exact_restore_after_each_eye=1 "
                  "polarity=normal");
    }
    if (continuousStereoTuning) {
        log(
            "m3_stereo_tuning_armed",
            "enabled=1 units_per_meter=100 polarity=normal "
            "fov_scale_percent=130 fov_scale_range=100-150 "
            "hmd_rotation=1 hmd_translation=1 "
            "translation_limit_m=0.25 exact_restore_after_each_eye=1");
        log(
            "m4_input_probe_armed",
            "source=openxr_shared_state polling=render_camera "
            "engine_writes=0 state_changes_and_periodic_samples=1");
        log(
            "m5_vr_tool_menu_armed",
            "toggle=both_grips_plus_y keyboard=F12 "
            "tabs=melee,block,block-collider,weapon,grip,collider,"
            "player-collider,2-hand,hand-ik,left-ik,elbow,display,"
            "controls,debug "
            "navigation=triggers,left_stick,right_stick,a,b "
            "render=depth_aware_stereo_ndc_triangle_overlay "
            "menu_scale_percent=62 menu_distance_m=1.50 "
            "settings_scope=retail_weapon_index settings_slots=64 "
            "player_collider_scope=global_local_player "
            "empty_hand_alignment=two_pose_right_trigger "
            "empty_hand_persistence=arm_ik.empty_right_hand "
            "held_object_alignment=two_pose_right_trigger "
            "held_object_persistence=per_index_grip_plus_right_hand_ik "
            "held_object_capture_gate=verified_visual_override "
             "unknown_weapon_swing_attack=disabled "
             "debug_draw_defaults=hidden persistence=global_user_override "
             "shortcut_preference=retail_vr_settings_developer_tools "
             "gameplay_controller_input_captured_while_open=1");
    }
    if (g_controllerRecenterEnabled) {
        log(
            "m4_hmd_recenter_armed",
            "button=right_stick gameplay=yaw_and_translation_origin "
            "flat_panel=openxr_host release_gated=1 foreground_gated=1");
    }
    if (g_headAimEnabled) {
        log(
            "m5_head_camera_transform_armed",
            "get_transform_slot=21 freshness_ms=250 "
            "consumers=flashlight,focus_detectors "
            "retail_render_base_bypassed=1 flat_panel_disabled=1");
    }
    ReleaseSRWLockExclusive(&g_passThroughLock);
    return true;
}

bool ReadTrackedHeadAimRotation(float (&rotation)[4]) noexcept {
    return CopyFreshTrackedHeadAim(nullptr, rotation);
}

bool TrackedHeadAimIsFresh() noexcept {
    float rotation[4]{};
    return CopyFreshTrackedHeadAim(nullptr, rotation);
}

bool ReadTrackedHeadWorldPose(
    float (&position)[3],
    float (&rotation)[4]) noexcept {
    return CopyFreshTrackedHeadWorldPose(position, rotation);
}

bool ReadDiagnosticObjectRigidTransform(
    void* object,
    float (&position)[3],
    float (&rotation)[4]) noexcept {
    std::memset(position, 0, sizeof(position));
    std::memset(rotation, 0, sizeof(rotation));
    if (object == nullptr || g_client == nullptr ||
        g_getRigidTransform == nullptr) {
        return false;
    }
    RigidTransformAbi transform{};
    unsigned long result = ~0UL;
    __try {
        result = g_getRigidTransform(g_client, object, &transform);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (result != 0UL ||
        !std::isfinite(transform.position[0]) ||
        !std::isfinite(transform.position[1]) ||
        !std::isfinite(transform.position[2]) ||
        !std::isfinite(transform.rotation[0]) ||
        !std::isfinite(transform.rotation[1]) ||
        !std::isfinite(transform.rotation[2]) ||
        !std::isfinite(transform.rotation[3])) {
        return false;
    }
    std::memcpy(position, transform.position, sizeof(position));
    std::memcpy(rotation, transform.rotation, sizeof(rotation));
    return true;
}

bool ReadTrackedControllerAimRotation(float (&rotation)[4]) noexcept {
    return CopyFreshTrackedControllerAim(rotation);
}

bool ReadTrackedControllerAimWorldPose(
    float (&position)[3],
    float (&rotation)[4],
    std::uint64_t& sampleId,
    std::uint64_t& timestampNs) noexcept {
    return CopyFreshTrackedControllerAimWorldPose(
        position, rotation, sampleId, timestampNs);
}

bool ReadTrackedControllerWorldPose(
    float (&position)[3],
    float (&rotation)[4],
    std::uint64_t& sampleId,
    std::uint64_t& timestampNs) noexcept {
    return CopyFreshTrackedControllerWorldPose(
        position, rotation, sampleId, timestampNs);
}

bool ReadTrackedMeleeAimBasis(
    float (&position)[3],
    float (&baseRotation)[4],
    float (&controllerRotation)[4]) noexcept {
    return CopyFreshTrackedMeleeAimBasis(
        position, baseRotation, controllerRotation);
}

void SetPhysicalMeleeVisualProxyEnabled(bool enabled) noexcept {
    InterlockedExchange(
        &g_physicalMeleeVisualEnabled, enabled ? 1 : 0);
    AcquireSRWLockExclusive(&g_physicalMeleeVisualLock);
    g_physicalMeleeVisualWeaponReference = nullptr;
    g_physicalMeleeVisualWeapon = nullptr;
    g_physicalMeleeVisualWeaponIndex = -1;
    g_physicalMeleeVisualModelReference = nullptr;
    g_physicalMeleeVisualModel = nullptr;
    g_physicalMeleeVisualModelLocalGripPosition = {};
    g_physicalMeleeVisualModelLocalGripRotation = {
        0.0F, 0.0F, 0.0F, 1.0F};
    g_physicalMeleeSecondaryGripOffsetUnits = {};
    g_physicalMeleeSecondaryGripGrabRadiusMeters = 0.15F;
    g_physicalMeleeSecondaryGripProfileEnabled = false;
    g_physicalMeleeVisualSourceGeneration = 0;
    g_activeWeaponGripCalibrationSlot = -1;
    ReleaseSRWLockExclusive(&g_physicalMeleeVisualLock);
    g_rightHandIkTargetSource = RightHandIkTargetSource::Invalid;
    g_rightHandIkTargetWeaponIndex = -1;
    g_rightHandIkTargetSourceGeneration = 0;
    g_rightHandIkTargetLastLoggedSampleId = 0;
    g_rightHandIkTargetLastLogTick = 0;
    g_rightHandIkTargetLogCount = 0;
    g_emptyRightHandAlignmentState = {};
    g_emptyRightHandAlignmentLastEvent =
        EmptyRightHandAlignmentEvent::None;
    InterlockedExchange(&g_physicalMeleeVisualActiveLogged, 0);
    InterlockedExchange(&g_physicalMeleeVisualRestoreFailed, 0);
    ResetPhysicalMeleeSecondaryGrip(true);
    if (!enabled) {
        InterlockedExchange(&g_weaponGripCalibrationEnabled, 0);
        InterlockedExchange(&g_weaponGripCalibrationActive, 0);
    }
}

void SetTwoHandedMeleeEnabled(bool enabled) noexcept {
    InterlockedExchange(
        &g_twoHandedMeleeEnabled, enabled ? 1 : 0);
    ResetPhysicalMeleeSecondaryGrip(true);
}

bool PhysicalMeleeSecondaryGripConsumesLeftSqueeze() noexcept {
    return InterlockedCompareExchange(
               &g_twoHandedMeleeEnabled, 0, 0) != 0 &&
        InterlockedCompareExchange(
               &g_physicalMeleeSecondaryGripAttached, 0, 0) != 0;
}

bool PhysicalMeleeSecondaryGripCapturesInput(
    const FearVrInputState& input,
    bool sampleFresh) noexcept {
    if (PhysicalMeleeSecondaryGripConsumesLeftSqueeze()) {
        return true;
    }
    PhysicalMeleeSecondaryGripSettings settings{};
    std::int32_t weaponIndex = -1;
    std::uint64_t sourceGeneration = 0;
    if (!CopyPhysicalMeleeSecondaryGripSettings(
            settings, weaponIndex, sourceGeneration) ||
        !settings.enabled ||
        !fearvr::IsInputStateUsable(input, sampleFresh) ||
        (input.activeHands &
         (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT)) !=
            (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT) ||
        (input.gripPoseValidHands &
         (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT)) !=
            (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT) ||
        (input.aimPoseValidHands & FEARVR_HAND_MASK_RIGHT) == 0U ||
        !std::isfinite(input.squeeze[FEARVR_HAND_LEFT]) ||
        input.squeeze[FEARVR_HAND_LEFT] < settings.attachSqueeze ||
        !fearvr::IsValidPose(input.handGripPose[FEARVR_HAND_LEFT]) ||
        !fearvr::IsValidPose(input.handGripPose[FEARVR_HAND_RIGHT]) ||
        !fearvr::IsValidPose(input.handAimPose[FEARVR_HAND_RIGHT])) {
        return false;
    }
    const fearvr::TrackingVector primaryMeters =
        fearvr::OpenXrToLithTech(fearvr::PosePosition(
            input.handGripPose[FEARVR_HAND_RIGHT]));
    const fearvr::TrackingVector secondaryMeters =
        fearvr::OpenXrToLithTech(fearvr::PosePosition(
            input.handGripPose[FEARVR_HAND_LEFT]));
    const PhysicalMeleePose primary{
        PhysicalMeleeScale(primaryMeters, settings.unitsPerMeter),
        fearvr::OpenXrToLithTech(fearvr::PoseRotation(
            input.handAimPose[FEARVR_HAND_RIGHT]))};
    if (!PhysicalMeleePoseIsValid(primary)) {
        return false;
    }
    const fearvr::TrackingVector target = PhysicalMeleeAdd(
        primary.gripPositionUnits,
        fearvr::Rotate(primary.rotation, settings.offsetUnits));
    const float distanceMeters = PhysicalMeleeLength(
        PhysicalMeleeSubtract(
            PhysicalMeleeScale(
                secondaryMeters, settings.unitsPerMeter),
            target)) / settings.unitsPerMeter;
    return std::isfinite(distanceMeters) &&
        distanceMeters <= settings.grabRadiusMeters;
}

bool ResolvePhysicalMeleeTrackedTwoHandPose(
    const FearVrInputState& input,
    fearvr::TrackingVector& gripPositionMeters,
    fearvr::TrackingQuaternion& weaponRotation) noexcept {
    if (!PhysicalMeleeSecondaryGripConsumesLeftSqueeze() ||
        !fearvr::IsInputStateUsable(input, true) ||
        (input.activeHands &
         (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT)) !=
            (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT) ||
        (input.gripPoseValidHands &
         (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT)) !=
            (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT) ||
        (input.aimPoseValidHands & FEARVR_HAND_MASK_RIGHT) == 0U ||
        !fearvr::IsValidPose(input.handGripPose[FEARVR_HAND_LEFT])) {
        return false;
    }
    PhysicalMeleeSecondaryGripSettings settings{};
    std::int32_t weaponIndex = -1;
    std::uint64_t sourceGeneration = 0;
    if (!CopyPhysicalMeleeSecondaryGripSettings(
            settings, weaponIndex, sourceGeneration) ||
        !settings.enabled) {
        return false;
    }
    const fearvr::TrackingVector secondaryMeters =
        fearvr::OpenXrToLithTech(fearvr::PosePosition(
            input.handGripPose[FEARVR_HAND_LEFT]));
    const PhysicalMeleePose primary{
        PhysicalMeleeScale(
            gripPositionMeters, settings.unitsPerMeter),
        weaponRotation};
    const PhysicalMeleeTwoHandPoseResult solved =
        ResolvePhysicalMeleeTwoHandPose(
            primary,
            PhysicalMeleeScale(
                secondaryMeters, settings.unitsPerMeter),
            settings);
    if (!solved.poseValid) {
        return false;
    }
    gripPositionMeters = PhysicalMeleeScale(
        solved.pose.gripPositionUnits,
        1.0F / settings.unitsPerMeter);
    weaponRotation = solved.pose.rotation;
    return fearvr::IsFinite(gripPositionMeters) &&
        fearvr::IsFinite(weaponRotation);
}

void ReadPhysicalMeleeTwoHandTelemetry(
    ToolMenuMeleeTelemetry& telemetry) noexcept {
    telemetry.twoHandedEnabled = InterlockedCompareExchange(
        &g_twoHandedMeleeEnabled, 0, 0) != 0;
    telemetry.secondaryGripAttached =
        InterlockedCompareExchange(
            &g_physicalMeleeSecondaryGripAttached, 0, 0) != 0;
    AcquireSRWLockShared(
        &g_physicalMeleeSecondaryGripTelemetryLock);
    telemetry.secondaryGripDistanceMeters =
        g_physicalMeleeSecondaryGripDistanceMeters;
    telemetry.secondaryGripAnchorErrorMeters =
        g_physicalMeleeSecondaryGripAnchorErrorMeters;
    ReleaseSRWLockShared(
        &g_physicalMeleeSecondaryGripTelemetryLock);
}

void SetWeaponGripCalibrationEnabled(bool enabled) noexcept {
    InterlockedExchange(
        &g_weaponGripCalibrationEnabled, enabled ? 1 : 0);
    InterlockedExchange(
        &g_weaponGripCalibrationActive,
        enabled && InterlockedCompareExchange(
            &g_toolMenuEnabled, 0, 0) == 0 ? 1 : 0);
    AcquireSRWLockExclusive(&g_physicalMeleeVisualLock);
    if (!enabled && g_activeWeaponGripCalibrationSlot >= 0 &&
        static_cast<std::size_t>(
            g_activeWeaponGripCalibrationSlot) <
            kWeaponGripCalibrationSlotCount) {
        const WeaponGripCalibrationSlot& active =
            g_weaponGripCalibrationSlots[
                g_activeWeaponGripCalibrationSlot];
        if (active.occupied) {
            g_physicalMeleeVisualModelLocalGripPosition =
                active.calibration.basePositionUnits;
            g_physicalMeleeVisualModelLocalGripRotation =
                active.calibration.baseRotation;
            g_physicalMeleeSecondaryGripOffsetUnits =
                active.calibration.baseSecondaryGripOffsetUnits;
            g_physicalMeleeSecondaryGripGrabRadiusMeters =
                active.calibration
                    .baseSecondaryGripGrabRadiusMeters;
            g_physicalMeleeSecondaryGripProfileEnabled =
                active.calibration.baseSecondaryGripEnabled;
        }
    }
    for (WeaponGripCalibrationSlot& slot :
         g_weaponGripCalibrationSlots) {
        slot = {};
    }
    g_activeWeaponGripCalibrationSlot = -1;
    g_weaponGripCalibrationUseSequence = 0;
    g_weaponGripCalibrationMode =
        WeaponGripCalibrationMode::Position;
    g_weaponGripCalibrationStepIndex = 2;
    ReleaseSRWLockExclusive(&g_physicalMeleeVisualLock);
    InterlockedExchange(&g_weaponGripControllerGizmoActiveLogged, 0);
    InterlockedExchange(&g_weaponGripControllerGizmoFailureLogged, 0);
}

bool WeaponGripCalibrationAcceptsControllerInput() noexcept {
    return InterlockedCompareExchange(
               &g_weaponGripCalibrationEnabled, 0, 0) != 0 &&
        InterlockedCompareExchange(
               &g_weaponGripCalibrationActive, 0, 0) != 0;
}

bool VrToolMenuCapturesControllerInput(
    const FearVrInputState& input,
    bool sampleFresh) noexcept {
    if (InterlockedCompareExchange(&g_toolMenuEnabled, 0, 0) == 0) {
        return false;
    }
    return ShouldCaptureToolMenuShortcutInput(
        InterlockedCompareExchange(&g_toolMenuOpen, 0, 0) != 0,
        InterlockedCompareExchange(
            &g_toolMenuReleaseCapture, 0, 0) != 0,
        InterlockedCompareExchange(
            &g_toolMenuShortcutEnabled, 0, 0) != 0,
        ToolMenuToggleChordDown(input, sampleFresh));
}

bool VrToolMenuIsOpen() noexcept {
    return InterlockedCompareExchange(&g_toolMenuOpen, 0, 0) != 0;
}

bool ReadVrToolMenuShortcutEnabled(bool& enabled) noexcept {
    if (InterlockedCompareExchange(
            &g_toolMenuShortcutSettingsReady, 0, 0) == 0) {
        return false;
    }
    enabled = InterlockedCompareExchange(
        &g_toolMenuShortcutEnabled, 0, 0) != 0;
    return true;
}

bool SetVrToolMenuShortcutEnabled(bool enabled) noexcept {
    if (InterlockedCompareExchange(
            &g_toolMenuShortcutSettingsReady, 0, 0) == 0) {
        if (g_passThroughLog != nullptr) {
            g_passThroughLog(
                "m6_vr_tool_menu_shortcut_save_failed",
                "reason=settings_not_ready runtime_mutated=0");
        }
        return false;
    }

    const WeaponSettingsStoreResult saveResult =
        SaveToolMenuShortcutEnabled(enabled);
    if (saveResult == WeaponSettingsStoreResult::Ok) {
        InterlockedExchange(
            &g_toolMenuShortcutEnabled, enabled ? 1 : 0);
    }
    if (g_passThroughLog != nullptr) {
        char detail[160]{};
        std::snprintf(
            detail, sizeof(detail),
            "result=%s enabled=%u runtime_mutated=%u",
            WeaponSettingsStoreResultName(saveResult),
            enabled ? 1U : 0U,
            saveResult == WeaponSettingsStoreResult::Ok ? 1U : 0U);
        g_passThroughLog(
            saveResult == WeaponSettingsStoreResult::Ok
                ? "m6_vr_tool_menu_shortcut_saved"
                : "m6_vr_tool_menu_shortcut_save_failed",
            detail);
    }
    return saveResult == WeaponSettingsStoreResult::Ok;
}

ToolMenuMeleeSettings ReadVrToolMenuMeleeSettings(
    std::int32_t weaponIndex) noexcept {
    return CopyToolMenuMeleeSettings(weaponIndex);
}

PhysicalMeleeBlockPoseSettings ReadVrToolMenuBlockPoseSettings(
    std::int32_t weaponIndex) noexcept {
    return CopyToolMenuBlockPoseSettings(weaponIndex);
}

ToolMenuBlockTimingSettings ReadVrToolMenuBlockTimingSettings(
    std::int32_t weaponIndex) noexcept {
    return CopyToolMenuBlockTimingSettings(weaponIndex);
}

ToolMenuColliderSettings ReadVrToolMenuColliderSettings(
    std::int32_t weaponIndex) noexcept {
    ToolMenuColliderSettings settings =
        CopyToolMenuColliderSettings(weaponIndex);
    ProcessLiveColliderAlignmentCommand(weaponIndex, settings);
    return settings;
}

ToolMenuColliderSettings ReadVrToolMenuBlockColliderSettings(
    std::int32_t weaponIndex,
    bool* usesAttackColliderFallback) noexcept {
    bool fallback = true;
    const ToolMenuColliderSettings settings =
        CopyToolMenuBlockColliderSettings(weaponIndex, fallback);
    if (usesAttackColliderFallback != nullptr) {
        *usesAttackColliderFallback = fallback;
    }
    return settings;
}

bool PublishEquippedWeaponVisualProxySource(
    void* const* equippedWeaponReference,
    void* equippedWeapon,
    std::int32_t equippedWeaponIndex,
    void* const* modelObjectReference,
    void* modelObject,
    const float (&modelLocalGripPositionUnits)[3],
    const float (&modelLocalGripRotation)[4]) noexcept {
    if (equippedWeaponReference == nullptr ||
        equippedWeapon == nullptr ||
        modelObjectReference == nullptr || modelObject == nullptr ||
        InterlockedCompareExchange(
            &g_physicalMeleeVisualEnabled, 0, 0) == 0) {
        return false;
    }
    const fearvr::TrackingVector localGripPosition{
        modelLocalGripPositionUnits[0],
        modelLocalGripPositionUnits[1],
        modelLocalGripPositionUnits[2]};
    const fearvr::TrackingQuaternion localGripRotation{
        modelLocalGripRotation[0], modelLocalGripRotation[1],
        modelLocalGripRotation[2], modelLocalGripRotation[3]};
    const PhysicalMeleeRigidTransform localGrip{
        localGripPosition, localGripRotation};
    const PhysicalMeleeProfile weaponProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(
            equippedWeaponIndex);
    if (!PhysicalMeleeRigidTransformIsValid(localGrip) ||
        PhysicalMeleeLength(localGripPosition) > 300.0F ||
        !PhysicalMeleeProfileIsValid(weaponProfile)) {
        return false;
    }

    void* referencedWeapon = nullptr;
    void* referencedModel = nullptr;
    bool referencesLive = false;
    __try {
        std::memcpy(
            &referencedWeapon, equippedWeaponReference,
            sizeof(referencedWeapon));
        std::memcpy(
            &referencedModel, modelObjectReference,
            sizeof(referencedModel));
        referencesLive = referencedWeapon == equippedWeapon &&
            referencedModel == modelObject;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        referencesLive = false;
    }
    if (!referencesLive) {
        return false;
    }

    bool sourceChanged = false;
    std::uint64_t generation = 0;
    WeaponSettingsStoreResult gripLoadResult =
        WeaponSettingsStoreResult::NotFound;
    bool gripLoadAttempted = false;
    bool gripLoadInheritedPipeBaseline = false;
    AcquireSRWLockExclusive(&g_physicalMeleeVisualLock);
    sourceChanged =
        equippedWeaponReference !=
            g_physicalMeleeVisualWeaponReference ||
        equippedWeapon != g_physicalMeleeVisualWeapon ||
        equippedWeaponIndex !=
            g_physicalMeleeVisualWeaponIndex ||
        modelObjectReference !=
            g_physicalMeleeVisualModelReference ||
        modelObject != g_physicalMeleeVisualModel;
    if (sourceChanged) {
        ++g_physicalMeleeVisualSourceGeneration;
    }
    g_physicalMeleeVisualWeaponReference =
        equippedWeaponReference;
    g_physicalMeleeVisualWeapon = equippedWeapon;
    g_physicalMeleeVisualWeaponIndex = equippedWeaponIndex;
    g_physicalMeleeVisualModelReference = modelObjectReference;
    g_physicalMeleeVisualModel = modelObject;
    const bool calibrationEnabled =
        InterlockedCompareExchange(
            &g_weaponGripCalibrationEnabled, 0, 0) != 0;
    if (calibrationEnabled &&
        (sourceChanged ||
         g_activeWeaponGripCalibrationSlot < 0)) {
        g_activeWeaponGripCalibrationSlot =
            FindOrCreateWeaponGripCalibrationSlot(
                equippedWeapon, equippedWeaponIndex, modelObject,
                localGripPosition, localGripRotation,
                weaponProfile, gripLoadResult,
                gripLoadAttempted,
                gripLoadInheritedPipeBaseline);
    }
    if (calibrationEnabled &&
        g_activeWeaponGripCalibrationSlot >= 0 &&
        static_cast<std::size_t>(
            g_activeWeaponGripCalibrationSlot) <
            kWeaponGripCalibrationSlotCount) {
        const WeaponGripCalibrationSlot& slot =
            g_weaponGripCalibrationSlots[
                g_activeWeaponGripCalibrationSlot];
        g_physicalMeleeVisualModelLocalGripPosition =
            slot.calibration.positionUnits;
        g_physicalMeleeVisualModelLocalGripRotation =
            ResolvePhysicalMeleeGripCalibrationRotation(
                slot.calibration);
        ApplyActiveSecondaryGripCalibrationLocked(slot);
    } else {
        g_activeWeaponGripCalibrationSlot = -1;
        g_physicalMeleeVisualModelLocalGripPosition =
            localGripPosition;
        g_physicalMeleeVisualModelLocalGripRotation =
            fearvr::Normalize(localGripRotation);
        g_physicalMeleeSecondaryGripOffsetUnits =
            weaponProfile.secondaryGripOffsetUnits;
        g_physicalMeleeSecondaryGripGrabRadiusMeters =
            weaponProfile.secondaryGripGrabRadiusMeters;
        g_physicalMeleeSecondaryGripProfileEnabled =
            weaponProfile.secondaryGripEnabled;
    }
    generation = g_physicalMeleeVisualSourceGeneration;
    ReleaseSRWLockExclusive(&g_physicalMeleeVisualLock);

    if (gripLoadAttempted && g_passThroughLog != nullptr) {
        char detail[224]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld profile=%s result=%s source=%s",
            static_cast<long>(equippedWeaponIndex),
            PhysicalMeleeProfileName(weaponProfile.id),
            WeaponSettingsStoreResultName(gripLoadResult),
            gripLoadResult == WeaponSettingsStoreResult::Ok
                ? gripLoadInheritedPipeBaseline
                    ? "pipe_baseline" : "weapon_record"
                : "profile_defaults");
        g_passThroughLog(
            "m5_weapon_grip_settings_loaded", detail);
    }

    if (sourceChanged && g_passThroughLog != nullptr) {
        char detail[448]{};
        std::snprintf(
            detail, sizeof(detail),
            "weapon_index=%ld weapon=%p model_object=%p "
            "source_generation=%llu "
            "acquisition=current_CClientWeaponMgr gameplay_update "
            "grip_calibration=%s "
            "lifetime_guard=current_weapon_and_model_LTObjRef",
            static_cast<long>(equippedWeaponIndex),
            equippedWeapon, modelObject,
            static_cast<unsigned long long>(generation),
            calibrationEnabled ? "persistent_per_weapon" :
                "profile_data");
        g_passThroughLog(
            "m5_physical_melee_visual_source_captured", detail);
        if (calibrationEnabled) {
            LogWeaponGripCalibrationState(
                "m5_weapon_grip_calibration_snapshot",
                "source_selected");
        }
    }
    return true;
}

bool ReadEquippedWeaponVisualSourceForFire(
    const void* expectedWeapon,
    std::int32_t& weaponIndex,
    void*& modelObject,
    float (&modelLocalGripPositionUnits)[3],
    float (&modelLocalGripRotation)[4],
    std::uint64_t& sourceGeneration) noexcept {
    weaponIndex = -1;
    modelObject = nullptr;
    std::memset(
        modelLocalGripPositionUnits, 0,
        sizeof(modelLocalGripPositionUnits));
    std::memset(
        modelLocalGripRotation, 0,
        sizeof(modelLocalGripRotation));
    sourceGeneration = 0U;
    if (expectedWeapon == nullptr ||
        InterlockedCompareExchange(
            &g_physicalMeleeVisualEnabled, 0, 0) == 0) {
        return false;
    }

    void* const* weaponReference = nullptr;
    void* weapon = nullptr;
    std::int32_t candidateWeaponIndex = -1;
    void* const* modelReference = nullptr;
    void* candidateModel = nullptr;
    fearvr::TrackingVector localGripPosition{};
    fearvr::TrackingQuaternion localGripRotation{};
    std::uint64_t candidateGeneration = 0U;
    AcquireSRWLockShared(&g_physicalMeleeVisualLock);
    weaponReference = g_physicalMeleeVisualWeaponReference;
    weapon = g_physicalMeleeVisualWeapon;
    candidateWeaponIndex = g_physicalMeleeVisualWeaponIndex;
    modelReference = g_physicalMeleeVisualModelReference;
    candidateModel = g_physicalMeleeVisualModel;
    localGripPosition = g_physicalMeleeVisualModelLocalGripPosition;
    localGripRotation = g_physicalMeleeVisualModelLocalGripRotation;
    candidateGeneration = g_physicalMeleeVisualSourceGeneration;
    ReleaseSRWLockShared(&g_physicalMeleeVisualLock);

    if (weaponReference == nullptr || weapon == nullptr ||
        modelReference == nullptr || candidateModel == nullptr ||
        weapon != expectedWeapon || candidateWeaponIndex < 0 ||
        candidateGeneration == 0U) {
        return false;
    }

    void* referencedWeapon = nullptr;
    void* referencedModel = nullptr;
    bool referencesLive = false;
    __try {
        std::memcpy(
            &referencedWeapon, weaponReference,
            sizeof(referencedWeapon));
        std::memcpy(
            &referencedModel, modelReference,
            sizeof(referencedModel));
        referencesLive =
            referencedWeapon == weapon &&
            referencedModel == candidateModel;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        referencesLive = false;
    }
    const PhysicalMeleeRigidTransform localGrip{
        localGripPosition, localGripRotation};
    if (!referencesLive ||
        !PhysicalMeleeRigidTransformIsValid(localGrip) ||
        PhysicalMeleeLength(localGripPosition) > 300.0F) {
        return false;
    }

    weaponIndex = candidateWeaponIndex;
    modelObject = candidateModel;
    modelLocalGripPositionUnits[0] = localGripPosition.x;
    modelLocalGripPositionUnits[1] = localGripPosition.y;
    modelLocalGripPositionUnits[2] = localGripPosition.z;
    modelLocalGripRotation[0] = localGripRotation.x;
    modelLocalGripRotation[1] = localGripRotation.y;
    modelLocalGripRotation[2] = localGripRotation.z;
    modelLocalGripRotation[3] = localGripRotation.w;
    sourceGeneration = candidateGeneration;
    return true;
}

void InvalidatePhysicalMeleeVisualProxySource() noexcept {
    EndSlideNodeControl("equipped_model_source_invalidated");
    AcquireSRWLockExclusive(&g_slideGrabRuntimeLock);
    g_slideGrabStateMachine = {};
    g_slideGrabLastFrame = {};
    ReleaseSRWLockExclusive(&g_slideGrabRuntimeLock);
    InterlockedExchange(&g_slideGrabCaptureGrip, 0);
    InterlockedExchange(&g_slideGrabCaptureTrigger, 0);
    ClearPhysicalMeleeVisualSource();
}

bool SlideGrabCapturesOffHandInput(
    const FearVrInputState& input,
    bool sampleFresh,
    bool gripInput) noexcept {
    if (!fearvr::IsInputStateUsable(input, sampleFresh) ||
        (input.activeHands & FEARVR_HAND_MASK_LEFT) == 0U) {
        return false;
    }
    const LONG captured = InterlockedCompareExchange(
        gripInput
            ? &g_slideGrabCaptureGrip
            : &g_slideGrabCaptureTrigger,
        0, 0);
    const float value = gripInput
        ? input.squeeze[FEARVR_HAND_LEFT]
        : input.trigger[FEARVR_HAND_LEFT];
    return captured != 0 && std::isfinite(value) &&
        value > 0.001F;
}

} // namespace condemnedvr
