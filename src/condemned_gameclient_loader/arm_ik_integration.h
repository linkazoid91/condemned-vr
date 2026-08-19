#pragma once

#include <cstdint>

#include "arm_ik.h"

namespace condemnedvr {

using ArmIkIntegrationLogFunction =
    void (*)(const char* event, const char* detail) noexcept;

// Arms the first mutation gate for Condemned arm IK. Retail identity,
// singleton layout, model-interface identity, and the exact node-control
// vtable slots are all verified before this can become active.
bool InstallArmIkRightHandProof(
    void* masterDatabase,
    void* gameClientModule,
    ArmIkIntegrationLogFunction log) noexcept;

// Second mutation gate. It retains the proven socket callback and adds
// parent-to-child controls for Right_armu and Right_arml using the measured
// authored bone vectors and the portable two-bone solver.
bool InstallArmIkRightArm(
    void* masterDatabase,
    void* gameClientModule,
    ArmIkIntegrationLogFunction log) noexcept;

// Observes player-body lifecycle from the already verified render path and
// installs one Right_hand callback only after the live body geometry resolves.
void SampleArmIkRightHandProof() noexcept;

// Receives the already verified CInterfaceMgr game state. A witnessed Loading
// state invalidates both callback chains even when Retail reuses the same
// player-body address; the next Playing state permits one transactional
// reinstall on the render path.
void NotifyArmIkRetailGameState(int gameState) noexcept;

// Publishes the coherent pose already used by Condemned's weighted physical
// weapon. The callback expires this snapshot after a short freshness window.
void PublishArmIkRightHandProofTarget(
    const float (&position)[3],
    const float (&rotation)[4],
    std::uint64_t sampleId,
    std::uint64_t timestampNs) noexcept;

void InvalidateArmIkRightHandProofTarget() noexcept;

// Mirrored full-arm target. While the support grip is free this is the raw
// left grip pose; while attached it is the weapon-local support anchor after
// the weighted weapon pose has been resolved.
void PublishArmIkLeftHandTarget(
    const float (&position)[3],
    const float (&rotation)[4],
    std::uint64_t sampleId,
    std::uint64_t timestampNs) noexcept;

void InvalidateArmIkLeftHandTarget() noexcept;

// Used by the in-headset calibration panel to distinguish an installed live
// hand callback from settings that are merely being edited for a later run.
bool ArmIkRightHandProofIsActive() noexcept;
bool ArmIkLeftHandIsActive() noexcept;

// Thread-safe live elbow tuning used by the full-arm callback. Applying a new
// pole clears the remembered bend direction so the next solve adopts it
// immediately instead of preserving the old hemisphere.
fearvr::ArmIkTuning ReadArmIkTuning() noexcept;
bool ApplyArmIkTuning(const fearvr::ArmIkTuning& tuning) noexcept;
void ResetArmIkBendMemory() noexcept;
bool ArmIkRightArmIsActive() noexcept;

} // namespace condemnedvr
