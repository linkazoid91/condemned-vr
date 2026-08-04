#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "bridge.h"

extern "C" __declspec(dllexport) BOOL __cdecl
CondemnedVr_SubmitStereoDiagnostic() {
    return fearvr::SubmitStereoDiagnostic();
}

extern "C" __declspec(dllexport) BOOL __cdecl
CondemnedVr_GetRenderRequest(FearVrRenderRequest* request) {
    return fearvr::GetRenderRequest(request);
}

extern "C" __declspec(dllexport) BOOL __cdecl
CondemnedVr_WaitForNewRenderRequest(
    std::uint64_t previousFrameId,
    std::uint32_t timeoutMilliseconds,
    FearVrRenderRequest* request) {
    return fearvr::WaitForNewRenderRequest(
        previousFrameId, timeoutMilliseconds, request);
}

extern "C" __declspec(dllexport) void __cdecl
CondemnedVr_BeginEye(std::uint32_t eye) {
    fearvr::BeginEye(eye);
}

extern "C" __declspec(dllexport) BOOL __cdecl
CondemnedVr_ClearEye(std::uint32_t eye) {
    return fearvr::ClearEye(eye);
}

extern "C" __declspec(dllexport) void __cdecl
CondemnedVr_CaptureEye(std::uint32_t eye) {
    fearvr::CaptureEye(eye);
}

extern "C" __declspec(dllexport) BOOL __cdecl
CondemnedVr_EndStereoDiagnosticFrame(std::uint64_t frameId) {
    return fearvr::EndStereoDiagnosticFrame(frameId);
}

extern "C" __declspec(dllexport) void __cdecl
CondemnedVr_EndStereoFrame(std::uint64_t frameId) {
    fearvr::EndStereoFrame(frameId);
}

extern "C" __declspec(dllexport) void __cdecl
CondemnedVr_SetFovScalePercent(std::uint32_t percent) {
    fearvr::SetFovScalePercent(percent);
}

extern "C" __declspec(dllexport) BOOL __cdecl
CondemnedVr_GetInputState(FearVrInputState* input) {
    return fearvr::GetInputState(input);
}

extern "C" __declspec(dllexport) BOOL __cdecl
CondemnedVr_SubmitHapticRequest(const FearVrHapticRequest* request) {
    return fearvr::SubmitHapticRequest(request);
}

extern "C" __declspec(dllexport) BOOL __cdecl
CondemnedVr_DrawOverlayLines(
    const FearVrOverlayLineVertex* vertices,
    std::uint32_t vertexCount) {
    return fearvr::DrawOverlayLines(vertices, vertexCount);
}

extern "C" __declspec(dllexport) BOOL __cdecl
CondemnedVr_DrawOverlayTriangles(
    const FearVrOverlayLineVertex* vertices,
    std::uint32_t vertexCount) {
    return fearvr::DrawOverlayTriangles(vertices, vertexCount);
}

extern "C" __declspec(dllexport) void __cdecl
CondemnedVr_SetMenuActive(BOOL active) {
    fearvr::SetMenuActive(active);
}
