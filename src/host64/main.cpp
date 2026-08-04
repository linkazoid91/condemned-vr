#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>

#include <Windows.h>

#include "fearvr-version.h"
#include "openxr_host.h"
#include "protocol.h"

namespace {

#if defined(CONDEMNEDVR_PRODUCT)
constexpr char kHostExecutableName[] = "condemnedvr-host";
#else
constexpr char kHostExecutableName[] = "fearvr-host";
#endif

BOOL WINAPI ConsoleControlHandler(DWORD controlType) {
    switch (controlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        fearvr::RequestOpenXrHostStop();
        return TRUE;
    default:
        return FALSE;
    }
}

void PrintUsage() {
    std::printf(
        "Usage: %s [options]\n"
        "  --log-dir <path>     Log directory (default: .\\logs)\n"
        "  --startup-image <path>  Optional PNG/JPEG shown before the game\n"
        "  --max-frames <N>     Exit cleanly after N submitted XR frames\n"
        "  --ipc-session <ID>   M2 IPC ID (decimal or 0x hexadecimal)\n"
        "  --exit-on-game-disconnect  Exit after game heartbeat timeout\n"
        "  --validate-only      Validate instance/system/D3D11/session/swapchains\n"
        "  --d3d-debug          Request the D3D11 debug layer if installed\n"
        "  --help               Show this help\n",
        kHostExecutableName);
}

bool ParseUnsigned(const char* text, std::uint64_t& value) {
    if (text == nullptr || text[0] == '\0' || text[0] == '-') {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    if (parsed > (std::numeric_limits<std::uint64_t>::max)()) {
        return false;
    }
    value = static_cast<std::uint64_t>(parsed);
    return true;
}

bool ParseSessionId(const char* text, std::uint64_t& value) {
    if (text == nullptr || text[0] == '\0' || text[0] == '-') {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0) {
        return false;
    }
    value = static_cast<std::uint64_t>(parsed);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::printf("%s %s (%s)\n", kHostExecutableName,
                FEARVR_VERSION_STRING, FEARVR_GIT_HASH);
    std::printf("Protokoll: magic=0x%08X version=%u header=%zu bytes\n",
                static_cast<unsigned>(FEARVR_PROTOCOL_MAGIC),
                static_cast<unsigned>(FEARVR_PROTOCOL_VERSION),
                sizeof(FearVrSharedHeader));

    fearvr::OpenXrHostOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            PrintUsage();
            return 0;
        }
        if (argument == "--validate-only") {
            options.validateOnly = true;
            continue;
        }
        if (argument == "--d3d-debug") {
            options.enableD3dDebug = true;
            continue;
        }
        if (argument == "--exit-on-game-disconnect") {
            options.exitOnGameDisconnect = true;
            continue;
        }
        if (argument == "--log-dir") {
            if (++index >= argc) {
                std::fprintf(stderr, "--log-dir requires a path.\n");
                return 2;
            }
            options.logDirectory = std::filesystem::u8path(argv[index]);
            continue;
        }
        if (argument == "--startup-image") {
            if (++index >= argc) {
                std::fprintf(stderr, "--startup-image requires a path.\n");
                return 2;
            }
            options.startupImage = std::filesystem::u8path(argv[index]);
            continue;
        }
        if (argument == "--max-frames") {
            if (++index >= argc ||
                !ParseUnsigned(argv[index], options.maxFrames) ||
                options.maxFrames == 0) {
                std::fprintf(stderr,
                             "--max-frames requires a positive integer.\n");
                return 2;
            }
            continue;
        }
        if (argument == "--ipc-session") {
            if (++index >= argc ||
                !ParseSessionId(argv[index], options.ipcSessionId)) {
                std::fprintf(
                    stderr,
                    "--ipc-session requires a non-zero ID.\n");
                return 2;
            }
            continue;
        }

        std::fprintf(stderr, "Unknown option: %s\n", argument.c_str());
        PrintUsage();
        return 2;
    }

    if (!SetConsoleCtrlHandler(ConsoleControlHandler, TRUE)) {
        std::fprintf(stderr,
                     "Warning: console control handler could not be installed "
                     "(Win32=%lu).\n",
                     GetLastError());
    }

    try {
        return fearvr::RunOpenXrHost(options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Unhandled host error: %s\n", error.what());
        return 1;
    }
}
