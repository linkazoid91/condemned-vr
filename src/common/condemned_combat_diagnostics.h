#pragma once

#include <cmath>
#include <cstdint>

#include "head_tracking_math.h"

namespace condemnedvr {

struct CombatContactProximity {
    float headToContactMeters{0.0F};
    float headHorizontalToContactMeters{0.0F};
    float gripToContactMeters{0.0F};
    bool headValid{false};
    bool gripValid{false};
};

// Measures only coherent world-space snapshots. LithTech uses +Y as up, so
// the XZ projection is the useful stand-off distance while the full head
// distance retains vertical separation to the struck body part.
inline CombatContactProximity MeasureCombatContactProximity(
    const fearvr::TrackingVector& headPositionUnits,
    bool headFresh,
    const fearvr::TrackingVector& gripPositionUnits,
    bool gripFresh,
    const fearvr::TrackingVector& contactPositionUnits,
    bool contactPositionValid,
    float unitsPerMeter) noexcept {
    CombatContactProximity result{};
    if (!contactPositionValid ||
        !fearvr::IsFinite(contactPositionUnits) ||
        !std::isfinite(unitsPerMeter) || unitsPerMeter <= 0.0F) {
        return result;
    }

    if (headFresh && fearvr::IsFinite(headPositionUnits)) {
        const float dx = headPositionUnits.x - contactPositionUnits.x;
        const float dy = headPositionUnits.y - contactPositionUnits.y;
        const float dz = headPositionUnits.z - contactPositionUnits.z;
        result.headToContactMeters =
            std::sqrt(dx * dx + dy * dy + dz * dz) / unitsPerMeter;
        result.headHorizontalToContactMeters =
            std::sqrt(dx * dx + dz * dz) / unitsPerMeter;
        result.headValid =
            std::isfinite(result.headToContactMeters) &&
            std::isfinite(result.headHorizontalToContactMeters);
    }

    if (gripFresh && fearvr::IsFinite(gripPositionUnits)) {
        const float dx = gripPositionUnits.x - contactPositionUnits.x;
        const float dy = gripPositionUnits.y - contactPositionUnits.y;
        const float dz = gripPositionUnits.z - contactPositionUnits.z;
        result.gripToContactMeters =
            std::sqrt(dx * dx + dy * dy + dz * dz) / unitsPerMeter;
        result.gripValid = std::isfinite(result.gripToContactMeters);
    }
    return result;
}

struct RetailPlayerVitals {
    std::uint32_t currentHealth{0U};
    std::uint32_t maximumHealth{0U};
    float healthFraction{0.0F};
    bool valid{false};
};

inline RetailPlayerVitals ResolveRetailPlayerVitals(
    std::uint32_t currentHealth,
    std::uint32_t maximumHealth,
    bool sourceVerified) noexcept {
    constexpr std::uint32_t kMaximumPlausibleHealth = 1'000'000U;
    RetailPlayerVitals result{currentHealth, maximumHealth, 0.0F, false};
    if (!sourceVerified || maximumHealth == 0U ||
        maximumHealth > kMaximumPlausibleHealth ||
        currentHealth > maximumHealth) {
        return result;
    }
    result.healthFraction = static_cast<float>(currentHealth) /
        static_cast<float>(maximumHealth);
    result.valid = std::isfinite(result.healthFraction) &&
        result.healthFraction >= 0.0F && result.healthFraction <= 1.0F;
    return result;
}

} // namespace condemnedvr
