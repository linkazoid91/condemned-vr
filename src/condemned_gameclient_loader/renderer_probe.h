#pragma once

namespace condemnedvr {

using RendererProbeLogFunction =
    void (*)(const char* event, const char* detail) noexcept;

// Read-only M3 discovery. This function does not invoke an engine interface,
// replace a vtable entry, or write into the master interface database.
void ProbeRendererInterfaces(
    void* masterDatabase,
    RendererProbeLogFunction log) noexcept;

// Installs a version- and signature-gated pass-through on the confirmed
// one-argument RenderCamera slot. The hook calls Retail exactly once.
bool InstallRendererPassThroughProbe(
    void* masterDatabase,
    RendererProbeLogFunction log,
    void* diagnosticBridgeModule = nullptr,
    bool doubleRenderDiagnostic = false,
    bool cameraReadProbe = false,
    bool eyeOffsetDiagnostic = false,
    bool reverseEyeOffsetDiagnostic = false,
    bool zeroEyeOffsetDiagnostic = false,
    bool continuousStereoTuning = false) noexcept;

} // namespace condemnedvr
