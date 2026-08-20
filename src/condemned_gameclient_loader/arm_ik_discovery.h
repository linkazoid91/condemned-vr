#pragma once

#include <cstdint>

namespace condemnedvr {

using ArmIkDiscoveryLogFunction =
    void (*)(const char* event, const char* detail) noexcept;

// Arms the observation-only player-body geometry pass. The installer verifies
// the Retail singleton code without invoking it, the live ILTModelClient
// interface, and the matching GameOrig global before any model method can be
// called.
bool InstallArmIkDiscovery(
    void* masterDatabase,
    void* gameClientModule,
    ArmIkDiscoveryLogFunction log) noexcept;

// Arms a separate read-only pass over the lifetime-validated equipped weapon
// model. It reuses the already verified ILTModel slots, enumerates every node,
// and reports model-local motion peaks so a moving slide/bolt candidate can be
// identified without guessing a node name, object offset, or layout.
bool InstallWeaponModelDiscovery(
    void* masterDatabase,
    void* gameClientModule,
    ArmIkDiscoveryLogFunction log) noexcept;

// Called from the already verified Retail render pass. It is a no-op unless
// discovery was explicitly requested and logs each distinct player-body
// object once without adding node controls or changing model state.
void SampleArmIkDiscovery() noexcept;

// Called only from the verified render path with the current equipped-model
// source. A null or changed source resets the bounded observation state.
void SampleWeaponModelDiscovery(
    void* modelObject,
    std::int32_t weaponIndex,
    std::uint64_t sourceGeneration) noexcept;

} // namespace condemnedvr
