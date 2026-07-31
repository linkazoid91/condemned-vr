#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "renderer_probe.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "head_tracking_math.h"
#include "protocol.h"

namespace condemnedvr {
namespace {

static_assert(sizeof(void*) == 4, "The renderer probe is x86-only.");

using RenderCameraFunction = unsigned long(__thiscall*)(void*, void*);
using RenderCameraOverrideFunction =
    unsigned long(__thiscall*)(void*, void*, const char*);
using SubmitStereoDiagnosticFunction = BOOL(__cdecl*)();
using GetRenderRequestFunction = BOOL(__cdecl*)(FearVrRenderRequest*);
using BeginEyeFunction = void(__cdecl*)(std::uint32_t);
using ClearEyeFunction = BOOL(__cdecl*)(std::uint32_t);
using CaptureEyeFunction = void(__cdecl*)(std::uint32_t);
using EndStereoDiagnosticFrameFunction = BOOL(__cdecl*)(std::uint64_t);
using EndStereoFrameFunction = void(__cdecl*)(std::uint64_t);
using SetFovScalePercentFunction = void(__cdecl*)(std::uint32_t);
struct RigidTransformAbi {
    float position[3];
    float rotation[4];
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
BeginEyeFunction g_beginEye = nullptr;
ClearEyeFunction g_clearEye = nullptr;
CaptureEyeFunction g_captureEye = nullptr;
EndStereoDiagnosticFrameFunction g_endStereoDiagnosticFrame = nullptr;
EndStereoFrameFunction g_endStereoFrame = nullptr;
SetFovScalePercentFunction g_setFovScalePercent = nullptr;
void* g_client = nullptr;
GetRigidTransformFunction g_getRigidTransform = nullptr;
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
bool g_trackingRecenterPending = true;
bool g_trackingRecenterValid = false;
FearVrPose g_trackingRecenter{};
volatile LONG g_continuousRenderActive = 0;
volatile LONG g_continuousStereoLogged = 0;
volatile LONG g_cameraReadFailures = 0;

bool PressedOnce(int virtualKey, bool& wasDown) noexcept {
    const bool down = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    const bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}

void LogStereoTuningState(const char* action) noexcept {
    if (g_passThroughLog == nullptr) {
        return;
    }
    char detail[256]{};
    std::snprintf(
        detail, sizeof(detail),
        "action=%s enabled=%u units_per_meter=%.1f polarity=%s "
        "fov_scale_percent=%.0f hmd_translation=%u",
        action, g_continuousStereoEnabled ? 1U : 0U,
        g_tuningUnitsPerMeter,
        g_tuningReversePolarity ? "reversed" : "normal",
        g_tuningFovScale * 100.0F,
        g_hmdTranslationEnabled ? 1U : 0U);
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
    return g_getRenderRequest != nullptr && g_beginEye != nullptr &&
        g_clearEye != nullptr && g_captureEye != nullptr &&
        g_endStereoDiagnosticFrame != nullptr &&
        g_endStereoFrame != nullptr &&
        g_setFovScalePercent != nullptr;
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
                        g_trackingRecenter =
                            fearvr::YawOnlyRecenterPose(currentCenter);
                        g_trackingRecenterValid =
                            fearvr::IsValidPose(g_trackingRecenter);
                        g_trackingRecenterPending = false;
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

    unsigned long eyeResult[FEARVR_EYE_COUNT]{1UL, 1UL};
    std::uint32_t renderedEyes = 0;
    bool exceptionOccurred = false;
    bool restoreFailed = false;
    __try {
        for (std::uint32_t eye = 0; eye < FEARVR_EYE_COUNT; ++eye) {
            bool eyeStateSet = false;
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
    HandleStereoTuningControls();
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
    LogInterface(log, "ILTClient.Default", 104, client);
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
    bool continuousStereoTuning) noexcept {
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

    const bool cameraAbiRequired = cameraReadProbe || stereoCameraPair;
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

    DWORD oldProtection = 0;
    if (!VirtualProtect(
            &vtable[kRenderCameraSlot], sizeof(void*),
            PAGE_READWRITE, &oldProtection)) {
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
        ? reinterpret_cast<GetRigidTransformFunction>(
              rigidTransformTarget)
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
        g_cameraReadProbe = false;
        g_eyeOffsetDiagnostic = false;
        g_reverseEyeOffsetDiagnostic = false;
        g_zeroEyeOffsetDiagnostic = false;
        g_continuousStereoTuning = false;
        log("m3_pass_through_rejected", "vtable_exchange_failed");
        ReleaseSRWLockExclusive(&g_passThroughLock);
        return false;
    }

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
    }
    ReleaseSRWLockExclusive(&g_passThroughLock);
    return true;
}

} // namespace condemnedvr
