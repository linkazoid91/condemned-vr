#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace condemnedvr {

// Loader diagnostics run inside Retail game hooks, so formatting must never
// invoke the CRT invalid-parameter handler. The largest current record is the
// M5 melee-impact snapshot (under 2 KiB); leave headroom for future evidence.
constexpr std::size_t kLoaderEventLineCapacity = 4096U;

inline std::size_t FormatLoaderEventLine(
    char* output,
    std::size_t outputCapacity,
    const char* event,
    const char* detail) noexcept {
    if (output == nullptr || outputCapacity == 0U) {
        return 0U;
    }

    output[0] = '\0';
    const int required = std::snprintf(
        output,
        outputCapacity,
        "{\"event\":\"%s\",\"detail\":\"%s\"}\r\n",
        event == nullptr ? "" : event,
        detail == nullptr ? "" : detail);
    if (required < 0) {
        output[0] = '\0';
        return 0U;
    }
    if (static_cast<std::size_t>(required) < outputCapacity) {
        return static_cast<std::size_t>(required);
    }

    // snprintf has already bounded the write. Restore a recognizable JSON
    // suffix when there is room, while still returning a single bounded line.
    constexpr char kTruncatedSuffix[] = "...\"}\r\n";
    constexpr std::size_t kSuffixLength =
        sizeof(kTruncatedSuffix) - 1U;
    const std::size_t length = outputCapacity - 1U;
    if (outputCapacity > sizeof(kTruncatedSuffix)) {
        std::memcpy(
            output + length - kSuffixLength,
            kTruncatedSuffix,
            sizeof(kTruncatedSuffix));
    } else {
        output[length] = '\0';
    }
    return length;
}

} // namespace condemnedvr
