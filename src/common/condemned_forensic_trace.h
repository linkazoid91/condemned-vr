#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "condemned_controller_input.h"

namespace condemnedvr {

// The live probe samples immediately after Retail consumes a forensic input,
// then backs off across the following client updates. The final sample is far
// enough out to cover normal draw/stow animations without reading every frame.
constexpr std::array<std::uint32_t, 10U>
    kForensicMemoryTraceSampleFrames{
        0U, 1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U, 256U};

inline int CondemnedForensicTraceCommandIndex(
    std::uint32_t command) noexcept {
    switch (command) {
    case kCondemnedToolsCommand:
        return 0;
    case kCondemnedFireCommand:
        return 1;
    case kCondemnedActivateCommand:
        return 2;
    default:
        return -1;
    }
}

inline const char* CondemnedForensicTraceCommandName(
    std::uint32_t command) noexcept {
    switch (command) {
    case kCondemnedToolsCommand:
        return "tools";
    case kCondemnedFireCommand:
        return "fire";
    case kCondemnedActivateCommand:
        return "activate";
    default:
        return "unmapped";
    }
}

inline bool ForensicTraceCommandValueActive(float value) noexcept {
    return std::isfinite(value) && std::fabs(value) >= 0.5F;
}

inline bool ConsumeForensicMemoryTraceSampleFrame(
    std::uint32_t frame,
    std::size_t& nextSampleIndex) noexcept {
    if (nextSampleIndex >=
        kForensicMemoryTraceSampleFrames.size() ||
        frame != kForensicMemoryTraceSampleFrames[nextSampleIndex]) {
        return false;
    }
    ++nextSampleIndex;
    return true;
}

inline std::uint32_t ForensicMemoryFnv1a(
    const unsigned char* bytes,
    std::size_t size) noexcept {
    if (bytes == nullptr && size != 0U) {
        return 0U;
    }
    std::uint32_t hash = 2166136261U;
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 16777619U;
    }
    return hash;
}

struct ForensicRayVector {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

struct ForensicControllerRay {
    ForensicRayVector start{};
    ForensicRayVector end{};
    float rangeUnits{0.0F};
    bool valid{false};
};

// Rebuilds only the segment geometry. Retail retains its original query
// flags, collision filter, result buffer, classification, and action route.
inline ForensicControllerRay BuildForensicControllerRay(
    const ForensicRayVector& retailStart,
    const ForensicRayVector& retailEnd,
    const ForensicRayVector& controllerOrigin,
    const ForensicRayVector& controllerForward) noexcept {
    const float retailX = retailEnd.x - retailStart.x;
    const float retailY = retailEnd.y - retailStart.y;
    const float retailZ = retailEnd.z - retailStart.z;
    const float retailRangeSquared =
        retailX * retailX + retailY * retailY + retailZ * retailZ;
    const float forwardLengthSquared =
        controllerForward.x * controllerForward.x +
        controllerForward.y * controllerForward.y +
        controllerForward.z * controllerForward.z;
    if (!std::isfinite(retailRangeSquared) ||
        !std::isfinite(forwardLengthSquared) ||
        retailRangeSquared <= 0.000001F ||
        forwardLengthSquared <= 0.000001F ||
        !std::isfinite(controllerOrigin.x) ||
        !std::isfinite(controllerOrigin.y) ||
        !std::isfinite(controllerOrigin.z)) {
        return {};
    }

    const float range = std::sqrt(retailRangeSquared);
    const float forwardScale =
        range / std::sqrt(forwardLengthSquared);
    ForensicControllerRay result{};
    result.start = controllerOrigin;
    result.end = {
        controllerOrigin.x + controllerForward.x * forwardScale,
        controllerOrigin.y + controllerForward.y * forwardScale,
        controllerOrigin.z + controllerForward.z * forwardScale};
    result.rangeUnits = range;
    result.valid =
        std::isfinite(result.end.x) &&
        std::isfinite(result.end.y) &&
        std::isfinite(result.end.z) &&
        std::isfinite(result.rangeUnits);
    return result.valid ? result : ForensicControllerRay{};
}

} // namespace condemnedvr
