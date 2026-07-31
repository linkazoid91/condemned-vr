#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "module_identity.h"

#include <cstdio>

int wmain(int argumentCount, wchar_t** arguments) {
    if (argumentCount != 2) {
        std::fputs("Expected a GameClient.dll path.\n", stderr);
        return 2;
    }

    const condemnedvr::ModuleIdentityResult result =
        condemnedvr::VerifyCondemnedGameClient(arguments[1]);
    if (result != condemnedvr::ModuleIdentityResult::ok) {
        std::fprintf(
            stderr,
            "Condemned GameClient identity failed: %s\n",
            condemnedvr::ModuleIdentityResultName(result));
        return 1;
    }

    std::puts("Condemned GameClient 1.0.314.0 identity verified.");
    return 0;
}
