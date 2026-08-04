#include "loader_event_format.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        ++g_failures;
    }
}

} // namespace

int main() {
    char normal[128]{};
    const std::size_t normalLength = condemnedvr::FormatLoaderEventLine(
        normal, sizeof(normal), "event", "detail");
    Check(
        std::strcmp(
            normal,
            "{\"event\":\"event\",\"detail\":\"detail\"}\r\n") == 0,
        "normal event is formatted");
    Check(normalLength == std::strlen(normal), "normal length is exact");

    char nullFields[64]{};
    const std::size_t nullLength = condemnedvr::FormatLoaderEventLine(
        nullFields, sizeof(nullFields), nullptr, nullptr);
    Check(
        std::strcmp(nullFields, "{\"event\":\"\",\"detail\":\"\"}\r\n") == 0,
        "null fields become empty strings");
    Check(nullLength == std::strlen(nullFields), "null-field length is exact");

    std::string oversized(8192U, 'x');
    char bounded[condemnedvr::kLoaderEventLineCapacity]{};
    const std::size_t boundedLength = condemnedvr::FormatLoaderEventLine(
        bounded, sizeof(bounded), "oversized", oversized.c_str());
    Check(
        boundedLength == sizeof(bounded) - 1U,
        "oversized event is bounded");
    Check(bounded[boundedLength] == '\0', "oversized event is terminated");
    Check(
        boundedLength >= 7U &&
            std::strcmp(bounded + boundedLength - 7U, "...\"}\r\n") == 0,
        "oversized event receives a valid suffix");

    char singleByte[1]{'x'};
    const std::size_t singleLength = condemnedvr::FormatLoaderEventLine(
        singleByte, sizeof(singleByte), "event", "detail");
    Check(singleLength == 0U, "single-byte output reports no payload");
    Check(singleByte[0] == '\0', "single-byte output is terminated");

    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("Loader event formatting tests passed.");
    return 0;
}
