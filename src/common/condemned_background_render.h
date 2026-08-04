#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace condemnedvr {

// Condemned 1.0.314.0, inside the WM_ACTIVATEAPP branch of the executable's
// window procedure. Retail normally jumps to renderer shutdown when wParam is
// false. Redirecting only that conditional target to the case's common return
// keeps a windowed VR session rendering while another desktop window is in the
// foreground. Input remains independently gated on foreground ownership.
constexpr std::uint32_t kBackgroundRenderExecutableTimestamp = 0x43FCFF00U;
constexpr std::uintptr_t kBackgroundRenderBranchRva = 0x0007C5E3U;
constexpr std::array<std::uint8_t, 6> kBackgroundRenderExpectedBytes{
    0x0F, 0x84, 0xA5, 0x00, 0x00, 0x00};
constexpr std::array<std::uint8_t, 6> kBackgroundRenderReplacementBytes{
    0x0F, 0x84, 0x00, 0x01, 0x00, 0x00};

enum class BackgroundRenderByteState : std::uint8_t {
    Retail,
    Patched,
    Mismatch
};

inline BackgroundRenderByteState ClassifyBackgroundRenderBytes(
    const std::uint8_t* bytes,
    std::size_t size) noexcept {
    if (bytes == nullptr || size < kBackgroundRenderExpectedBytes.size()) {
        return BackgroundRenderByteState::Mismatch;
    }
    if (std::memcmp(
            bytes,
            kBackgroundRenderExpectedBytes.data(),
            kBackgroundRenderExpectedBytes.size()) == 0) {
        return BackgroundRenderByteState::Retail;
    }
    if (std::memcmp(
            bytes,
            kBackgroundRenderReplacementBytes.data(),
            kBackgroundRenderReplacementBytes.size()) == 0) {
        return BackgroundRenderByteState::Patched;
    }
    return BackgroundRenderByteState::Mismatch;
}

inline bool ShouldForwardBackgroundCursorWarp(
    std::uint32_t currentProcessId,
    std::uint32_t foregroundProcessId) noexcept {
    return currentProcessId != 0U &&
        currentProcessId == foregroundProcessId;
}

} // namespace condemnedvr
