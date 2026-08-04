#pragma once

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

// Called from the already verified Retail render pass. It is a no-op unless
// discovery was explicitly requested and logs each distinct player-body
// object once without adding node controls or changing model state.
void SampleArmIkDiscovery() noexcept;

} // namespace condemnedvr
