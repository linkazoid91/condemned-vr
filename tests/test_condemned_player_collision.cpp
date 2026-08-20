#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "condemned_player_collision.h"

namespace {

int Fail(const char* message) {
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

bool Near(float left, float right) {
    return std::fabs(left - right) <= 1.0e-5F;
}

} // namespace

int main() {
    using namespace condemnedvr;

    struct CallsiteCase {
        std::uintptr_t returnRva;
        std::uintptr_t instructionRva;
        PlayerColliderSetDimensionsCallsite callsite;
        const char* name;
    };
    constexpr CallsiteCase callsiteCases[] = {
        {0x00031BFCU, 0x00031BF9U,
         PlayerColliderSetDimensionsCallsite::MoveManagerPrimary,
         "move_manager_primary"},
        {0x00031C16U, 0x00031C13U,
         PlayerColliderSetDimensionsCallsite::MoveManagerFallback,
         "move_manager_fallback"},
        {0x00031D68U, 0x00031D65U,
         PlayerColliderSetDimensionsCallsite::Routine31cf0YAttempt,
         "routine_31cf0_y_attempt"},
        {0x00031D86U, 0x00031D83U,
         PlayerColliderSetDimensionsCallsite::Routine31cf0OriginalRestore,
         "routine_31cf0_original_restore"},
        {0x000346BFU, 0x000346BCU,
         PlayerColliderSetDimensionsCallsite::WriterLiteralHalf,
         "verified_344e0_literal_half"},
        {0x0003476FU, 0x0003476CU,
         PlayerColliderSetDimensionsCallsite::WriterPrimary,
         "verified_344e0_primary"},
        {0x0003478AU, 0x00034787U,
         PlayerColliderSetDimensionsCallsite::WriterRetry,
         "verified_344e0_retry"},
    };
    for (const CallsiteCase& testCase : callsiteCases) {
        const PlayerColliderSetDimensionsCallsite classified =
            ClassifyPlayerColliderSetDimensionsReturnRva(
                testCase.returnRva);
        if (classified != testCase.callsite ||
            PlayerColliderSetDimensionsCallInstructionRva(classified) !=
                testCase.instructionRva ||
            std::strcmp(
                PlayerColliderSetDimensionsCallsiteName(classified),
                testCase.name) != 0 ||
            ClassifyPlayerColliderSetDimensionsReturnRva(
                testCase.returnRva - 1U) !=
                PlayerColliderSetDimensionsCallsite::Unknown ||
            ClassifyPlayerColliderSetDimensionsReturnRva(
                testCase.returnRva + 1U) !=
                PlayerColliderSetDimensionsCallsite::Unknown ||
            ClassifyPlayerColliderSetDimensionsReturnRva(
                testCase.instructionRva) !=
                PlayerColliderSetDimensionsCallsite::Unknown) {
            return Fail(
                "setter caller classification must require an exact return RVA");
        }
    }
    if (ClassifyPlayerColliderSetDimensionsReturnRva(0U) !=
            PlayerColliderSetDimensionsCallsite::Unknown ||
        PlayerColliderSetDimensionsCallInstructionRva(
            PlayerColliderSetDimensionsCallsite::Unknown) != 0U ||
        std::strcmp(
            PlayerColliderSetDimensionsCallsiteName(
                PlayerColliderSetDimensionsCallsite::Unknown),
            "unknown") != 0) {
        return Fail("unknown setter callers must fail closed");
    }

    int physicsToken = 0;
    int objectToken = 0;
    void* const expectedPhysics = &physicsToken;
    void* const expectedObject = &objectToken;
    constexpr std::uint32_t rawFlag = 0xA5C30001U;
    constexpr std::uint32_t rawResults[] = {
        0U, 1U, 0xDEADBEEFU};
    for (const std::uint32_t rawResult : rawResults) {
        int forwardCalls = 0;
        bool exactArguments = false;
        PlayerColliderDimensions forwardedDimensions{
            4.0F, 95.0F, 4.0F};
        PlayerColliderDimensions* const expectedDimensions =
            &forwardedDimensions;
        const std::uint32_t forwardedResult =
            ForwardPlayerColliderSetDimensionsExactlyOnce(
                [&](void* physics, void* object,
                    PlayerColliderDimensions* dimensions,
                    std::uint32_t flag) noexcept {
                    ++forwardCalls;
                    exactArguments =
                        physics == expectedPhysics &&
                        object == expectedObject &&
                        dimensions == expectedDimensions &&
                        flag == rawFlag;
                    if (dimensions != nullptr) {
                        dimensions->x = 5.0F;
                        dimensions->y = 96.0F;
                        dimensions->z = 6.0F;
                    }
                    return rawResult;
                },
                expectedPhysics, expectedObject,
                expectedDimensions, rawFlag);
        if (forwardCalls != 1 || !exactArguments ||
            forwardedResult != rawResult ||
            !Near(forwardedDimensions.x, 5.0F) ||
            !Near(forwardedDimensions.y, 96.0F) ||
            !Near(forwardedDimensions.z, 6.0F)) {
            return Fail(
                "setter forwarding must preserve exact-once arguments, raw result, and native in/out mutation");
        }
    }

    int velocityCalls = 0;
    PlayerCollisionDiagnosticPoint velocity{1.0F, 2.0F, 3.0F};
    const auto* const expectedVelocity = &velocity;
    const std::uint32_t velocityResult =
        ForwardPlayerCollisionSetVelocityExactlyOnce(
            [&](void* physics, void* object,
                const PlayerCollisionDiagnosticPoint* value) noexcept {
                ++velocityCalls;
                if (physics != expectedPhysics ||
                    object != expectedObject ||
                    value != expectedVelocity) {
                    return 0U;
                }
                return 0x12345678U;
            },
            expectedPhysics, expectedObject, expectedVelocity);
    if (velocityCalls != 1 || velocityResult != 0x12345678U) {
        return Fail(
            "velocity observer forwarding must preserve pointer, object, result, and exact-once dispatch");
    }

    const PlayerCollisionDiagnosticProxy playerProxy =
        BuildPlayerCollisionDiagnosticProxy(
            {100.0F, 50.0F, -20.0F}, {4.0F, 95.0F, 4.0F});
    const PlayerCollisionDiagnosticProxy targetProxy =
        BuildPlayerCollisionDiagnosticProxy(
            {120.0F, 50.0F, -20.0F}, {6.0F, 40.0F, 5.0F});
    if (!playerProxy.valid || !targetProxy.valid ||
        !Near(playerProxy.minimum.x, 96.0F) ||
        !Near(playerProxy.maximum.x, 104.0F) ||
        !Near(playerProxy.minimum.y, -45.0F) ||
        !Near(playerProxy.maximum.y, 145.0F) ||
        !Near(PlayerCollisionDiagnosticProxyHorizontalGap(
                  playerProxy, targetProxy), 10.0F) ||
        !Near(PlayerCollisionDiagnosticHorizontalDistance(
                  {100.0F, 0.0F, -20.0F},
                  {103.0F, 999.0F, -16.0F}), 5.0F)) {
        return Fail(
            "collision X-ray diagnostic proxies and horizontal metrics must be deterministic");
    }
    if (BuildPlayerCollisionDiagnosticProxy(
            {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 1.0F}).valid ||
        PlayerCollisionDiagnosticProxyHorizontalGap(
            {}, playerProxy) >= 0.0F ||
        PlayerCollisionDiagnosticHorizontalDistance(
            {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
            {}) >= 0.0F) {
        return Fail(
            "collision X-ray proxy geometry must fail closed on invalid inputs");
    }

    PlayerColliderSettings settings{};
    if (!PlayerColliderSettingsAreValid(settings) ||
        !Near(settings.widthScale, 1.0F) ||
        PlayerColliderRequiresOngoingAudit(settings) ||
        PlayerColliderDimensionAuditRequired(settings, false) ||
        !PlayerColliderDimensionAuditRequired(settings, true)) {
        return Fail("player collision must default to exact Retail width");
    }

    const PlayerColliderDimensions retail{40.0F, 95.0F, 30.0F};
    PlayerColliderDimensions resolved{};
    if (!ResolvePlayerColliderDimensions(
            retail, settings, resolved) ||
        !Near(resolved.x, 40.0F) ||
        !Near(resolved.y, 95.0F) ||
        !Near(resolved.z, 30.0F)) {
        return Fail("100 percent must preserve every Retail dimension");
    }

    if (!UpdatePlayerColliderSettings(
            settings, 0U, -1, false) ||
        !Near(settings.widthScale, 0.95F) ||
        !PlayerColliderRequiresOngoingAudit(settings) ||
        !PlayerColliderDimensionAuditRequired(settings, false) ||
        !ResolvePlayerColliderDimensions(
            retail, settings, resolved) ||
        !Near(resolved.x, 38.0F) ||
        !Near(resolved.y, 95.0F) ||
        !Near(resolved.z, 28.5F)) {
        return Fail("width adjustment must scale X/Z and preserve Y");
    }

    const PlayerColliderDimensions reducedExpected{
        38.0F, 95.0F, 28.5F};
    if (!PlayerColliderDimensionsMatch(
            resolved, reducedExpected) ||
        PlayerColliderDimensionsMatch(retail, reducedExpected)) {
        return Fail(
            "ongoing audit must distinguish retained width from Retail drift");
    }

    for (int index = 0; index < 30; ++index) {
        UpdatePlayerColliderSettings(
            settings, 0U, -1, false);
    }
    if (!Near(
            settings.widthScale,
            kPlayerColliderWidthScaleMinimum) ||
        UpdatePlayerColliderSettings(
            settings, 0U, -1, false) ||
        !ResolvePlayerColliderDimensions(
            retail, settings, resolved) ||
        !Near(resolved.x, 4.0F) ||
        !Near(resolved.y, 95.0F) ||
        !Near(resolved.z, 3.0F)) {
        return Fail(
            "player width must clamp at the positive diagnostic minimum");
    }
    for (int index = 0; index < 20; ++index) {
        UpdatePlayerColliderSettings(
            settings, 0U, 1, false);
    }
    if (!Near(
            settings.widthScale,
            kPlayerColliderWidthScaleMaximum) ||
        UpdatePlayerColliderSettings(
            settings, 0U, 1, false)) {
        return Fail("player width must clamp at exact Retail maximum");
    }

    settings.widthScale = 0.75F;
    if (!UpdatePlayerColliderSettings(
            settings, 1U, 0, true) ||
        !Near(settings.widthScale, 1.0F) ||
        PlayerColliderRequiresOngoingAudit(settings) ||
        UpdatePlayerColliderSettings(
            settings, 1U, 0, true) ||
        UpdatePlayerColliderSettings(
            settings, 2U, -1, false)) {
        return Fail("reset must restore Retail without accepting other rows");
    }

    PlayerColliderSettings invalid = settings;
    invalid.widthScale =
        std::numeric_limits<float>::quiet_NaN();
    if (PlayerColliderSettingsAreValid(invalid) ||
        ResolvePlayerColliderDimensions(
            retail, invalid, resolved) ||
        UpdatePlayerColliderSettings(
            invalid, 0U, -1, false)) {
        return Fail("non-finite player settings must fail closed");
    }

    PlayerColliderDimensions invalidDimensions = retail;
    invalidDimensions.x = 0.0F;
    if (PlayerColliderDimensionsAreValid(invalidDimensions) ||
        ResolvePlayerColliderDimensions(
            invalidDimensions, settings, resolved)) {
        return Fail("non-positive Retail dimensions must fail closed");
    }

    std::puts("Condemned player collision tests passed.");
    return 0;
}
