#include <cmath>
#include <cstdio>
#include <limits>

#include "condemned_combat_diagnostics.h"

namespace {

bool Near(float left, float right, float tolerance = 0.0001F) {
    return std::fabs(left - right) <= tolerance;
}

int Fail(const char* message) {
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

} // namespace

int main() {
    using namespace condemnedvr;

    const CombatContactProximity proximity =
        MeasureCombatContactProximity(
            {300.0F, 400.0F, 500.0F}, true,
            {100.0F, 200.0F, 300.0F}, true,
            {0.0F, 0.0F, 0.0F}, true, 100.0F);
    if (!proximity.headValid || !proximity.gripValid ||
        !Near(proximity.headToContactMeters, std::sqrt(50.0F)) ||
        !Near(proximity.headHorizontalToContactMeters, std::sqrt(34.0F)) ||
        !Near(proximity.gripToContactMeters, std::sqrt(14.0F))) {
        return Fail("combat proximity must preserve full and XZ distances");
    }

    const CombatContactProximity staleHead =
        MeasureCombatContactProximity(
            {300.0F, 400.0F, 500.0F}, false,
            {100.0F, 200.0F, 300.0F}, true,
            {0.0F, 0.0F, 0.0F}, true, 100.0F);
    if (staleHead.headValid || !staleHead.gripValid) {
        return Fail(
            "stale head data must not discard an independent grip distance");
    }

    const float nan = std::numeric_limits<float>::quiet_NaN();
    if (MeasureCombatContactProximity(
            {}, true, {}, true, {nan, 0.0F, 0.0F}, true, 100.0F)
            .headValid ||
        MeasureCombatContactProximity(
            {}, true, {}, true, {}, true, 0.0F).gripValid) {
        return Fail("invalid contact geometry must fail closed");
    }

    const RetailPlayerVitals healthy =
        ResolveRetailPlayerVitals(75U, 100U, true);
    if (!healthy.valid || !Near(healthy.healthFraction, 0.75F)) {
        return Fail(
            "verified player vitals must expose a bounded fraction");
    }
    if (ResolveRetailPlayerVitals(75U, 100U, false).valid ||
        ResolveRetailPlayerVitals(101U, 100U, true).valid ||
        ResolveRetailPlayerVitals(1U, 0U, true).valid ||
        ResolveRetailPlayerVitals(1U, 1'000'001U, true).valid) {
        return Fail(
            "unverified or implausible player vitals must fail closed");
    }

    return 0;
}
