#include <cmath>
#include <cstdio>
#include <limits>

#include "mono_panel_anchor.h"

namespace {

constexpr float kSinThirtyDegrees = 0.5F;
constexpr float kCosThirtyDegrees = 0.86602540F;

int Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

bool Near(float actual, float expected, float tolerance = 0.0002F) {
    return std::fabs(actual - expected) <= tolerance;
}

FearVrPose Eye(
    float x,
    float y,
    float z,
    float qx,
    float qy,
    float qz,
    float qw) {
    return {x, y, z, qx, qy, qz, qw};
}

} // namespace

int main() {
    using fearvr::ConsumeFirstGameImageAnchor;
    using fearvr::MonoPanelStartupAnchorState;
    using fearvr::ResolveMonoPanelAnchor;

    MonoPanelStartupAnchorState startup{};
    if (ConsumeFirstGameImageAnchor(startup, false, false) ||
        ConsumeFirstGameImageAnchor(startup, true, false) ||
        !ConsumeFirstGameImageAnchor(startup, true, true) ||
        ConsumeFirstGameImageAnchor(startup, true, true)) {
        return Fail("only the first connected game image may re-anchor");
    }
    ConsumeFirstGameImageAnchor(startup, false, false);
    if (!ConsumeFirstGameImageAnchor(startup, true, true)) {
        return Fail("a reconnected game must receive a fresh startup anchor");
    }

    const FearVrPose neutralLeft = Eye(
        -0.032F, 1.6F, 0.25F, 0.0F, 0.0F, 0.0F, 1.0F);
    const FearVrPose neutralRight = Eye(
        0.032F, 1.6F, 0.25F, 0.0F, 0.0F, 0.0F, 1.0F);
    const fearvr::MonoPanelAnchor neutral = ResolveMonoPanelAnchor(
        neutralLeft, neutralRight);
    if (!neutral.valid ||
        !Near(neutral.positionMeters.x, 0.0F) ||
        !Near(neutral.positionMeters.y, 1.6F) ||
        !Near(neutral.positionMeters.z, -1.75F) ||
        !Near(neutral.orientation.x, 0.0F) ||
        !Near(neutral.orientation.y, 0.0F) ||
        !Near(neutral.orientation.z, 0.0F) ||
        !Near(neutral.orientation.w, 1.0F)) {
        return Fail("a level startup view must center the panel two metres ahead");
    }

    // Positive OpenXR X rotation points the -Z gaze ray 30 degrees upward.
    // The panel centre follows that exact ray while its surface remains level.
    const FearVrPose pitchedLeft = Eye(
        -0.032F, 1.6F, 0.25F,
        0.25881905F, 0.0F, 0.0F, 0.96592583F);
    const FearVrPose pitchedRight = Eye(
        0.032F, 1.6F, 0.25F,
        0.25881905F, 0.0F, 0.0F, 0.96592583F);
    const fearvr::MonoPanelAnchor pitched = ResolveMonoPanelAnchor(
        pitchedLeft, pitchedRight);
    if (!pitched.valid ||
        !Near(
            pitched.positionMeters.y,
            1.6F + kSinThirtyDegrees * 2.0F) ||
        !Near(
            pitched.positionMeters.z,
            0.25F - kCosThirtyDegrees * 2.0F) ||
        !Near(pitched.orientation.x, 0.0F) ||
        !Near(pitched.orientation.y, 0.0F) ||
        !Near(pitched.orientation.z, 0.0F) ||
        !Near(pitched.orientation.w, 1.0F)) {
        return Fail("panel centre must follow HMD pitch without tilting the panel");
    }

    FearVrPose invalid = neutralLeft;
    invalid.py = std::numeric_limits<float>::quiet_NaN();
    if (ResolveMonoPanelAnchor(invalid, neutralRight).valid ||
        ResolveMonoPanelAnchor(neutralLeft, neutralRight, 0.0F).valid) {
        return Fail("invalid startup poses and distances must fail closed");
    }

    std::puts("Mono panel-anchor tests passed.");
    return 0;
}
