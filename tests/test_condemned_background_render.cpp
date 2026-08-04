#include "condemned_background_render.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int Fail(const char* message) {
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

std::int32_t RelativeDisplacement(
    const std::array<std::uint8_t, 6>& instruction) {
    std::int32_t displacement = 0;
    std::memcpy(&displacement, instruction.data() + 2, sizeof(displacement));
    return displacement;
}

} // namespace

int main() {
    using namespace condemnedvr;

    if (ClassifyBackgroundRenderBytes(
            kBackgroundRenderExpectedBytes.data(),
            kBackgroundRenderExpectedBytes.size()) !=
        BackgroundRenderByteState::Retail) {
        return Fail("Retail focus branch must be recognized");
    }
    if (ClassifyBackgroundRenderBytes(
            kBackgroundRenderReplacementBytes.data(),
            kBackgroundRenderReplacementBytes.size()) !=
        BackgroundRenderByteState::Patched) {
        return Fail("patched focus branch must be recognized");
    }

    auto mismatch = kBackgroundRenderExpectedBytes;
    mismatch[0] = 0x90;
    if (ClassifyBackgroundRenderBytes(mismatch.data(), mismatch.size()) !=
        BackgroundRenderByteState::Mismatch) {
        return Fail("unknown focus bytes must fail closed");
    }

    constexpr std::uintptr_t kInstructionSize = 6U;
    const std::uintptr_t retailTarget =
        kBackgroundRenderBranchRva + kInstructionSize +
        static_cast<std::uintptr_t>(
            RelativeDisplacement(kBackgroundRenderExpectedBytes));
    const std::uintptr_t patchedTarget =
        kBackgroundRenderBranchRva + kInstructionSize +
        static_cast<std::uintptr_t>(
            RelativeDisplacement(kBackgroundRenderReplacementBytes));
    if (retailTarget != 0x0007C68EU) {
        return Fail("Retail branch must target renderer shutdown");
    }
    if (patchedTarget != 0x0007C6E9U) {
        return Fail("VR branch must target the common return path");
    }

    if (!ShouldForwardBackgroundCursorWarp(42U, 42U)) {
        return Fail("foreground game cursor warps must reach Retail");
    }
    if (ShouldForwardBackgroundCursorWarp(42U, 99U) ||
        ShouldForwardBackgroundCursorWarp(42U, 0U) ||
        ShouldForwardBackgroundCursorWarp(0U, 0U)) {
        return Fail("background or unknown cursor warps must be suppressed");
    }

    return 0;
}
