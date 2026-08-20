#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace condemnedvr {

constexpr float kPlayerColliderWidthScaleDefault = 1.0F;
// Keep the diagnostic floor positive for engine robustness. A reduced-width
// setting must remain under observation because Retail can reconcile the live
// player object back to its full dimensions after accepting a direct change.
constexpr float kPlayerColliderWidthScaleMinimum = 0.10F;
constexpr float kPlayerColliderWidthScaleMaximum = 1.0F;
constexpr float kPlayerColliderWidthScaleStep = 0.05F;

struct PlayerColliderDimensions {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

struct PlayerColliderSettings {
    float widthScale{kPlayerColliderWidthScaleDefault};
};

struct PlayerCollisionDiagnosticPoint {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

// GetObjectDims values are useful for comparison, but the live engine's true
// contact geometry and orientation have not been verified. This axis-aligned
// box is therefore always a diagnostic proxy, never an authoritative collider.
struct PlayerCollisionDiagnosticProxy {
    PlayerCollisionDiagnosticPoint minimum{};
    PlayerCollisionDiagnosticPoint maximum{};
    bool valid{false};
};

inline PlayerCollisionDiagnosticProxy BuildPlayerCollisionDiagnosticProxy(
    const PlayerCollisionDiagnosticPoint& origin,
    const PlayerColliderDimensions& dimensions) noexcept {
    PlayerCollisionDiagnosticProxy proxy{};
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) ||
        !std::isfinite(origin.z) ||
        !std::isfinite(dimensions.x) ||
        !std::isfinite(dimensions.y) ||
        !std::isfinite(dimensions.z) ||
        dimensions.x <= 0.0F || dimensions.y <= 0.0F ||
        dimensions.z <= 0.0F) {
        return proxy;
    }
    proxy.minimum = {origin.x - dimensions.x, origin.y - dimensions.y,
                     origin.z - dimensions.z};
    proxy.maximum = {origin.x + dimensions.x, origin.y + dimensions.y,
                     origin.z + dimensions.z};
    proxy.valid = std::isfinite(proxy.minimum.x) &&
        std::isfinite(proxy.minimum.y) && std::isfinite(proxy.minimum.z) &&
        std::isfinite(proxy.maximum.x) &&
        std::isfinite(proxy.maximum.y) && std::isfinite(proxy.maximum.z);
    return proxy;
}

inline float PlayerCollisionDiagnosticProxyHorizontalGap(
    const PlayerCollisionDiagnosticProxy& first,
    const PlayerCollisionDiagnosticProxy& second) noexcept {
    if (!first.valid || !second.valid) {
        return -1.0F;
    }
    const float gapX = std::max(
        0.0F, std::max(second.minimum.x - first.maximum.x,
                       first.minimum.x - second.maximum.x));
    const float gapZ = std::max(
        0.0F, std::max(second.minimum.z - first.maximum.z,
                       first.minimum.z - second.maximum.z));
    const float gap = std::sqrt(gapX * gapX + gapZ * gapZ);
    return std::isfinite(gap) ? gap : -1.0F;
}

inline float PlayerCollisionDiagnosticHorizontalDistance(
    const PlayerCollisionDiagnosticPoint& first,
    const PlayerCollisionDiagnosticPoint& second) noexcept {
    if (!std::isfinite(first.x) || !std::isfinite(first.z) ||
        !std::isfinite(second.x) || !std::isfinite(second.z)) {
        return -1.0F;
    }
    const float deltaX = second.x - first.x;
    const float deltaZ = second.z - first.z;
    const float distance = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
    return std::isfinite(distance) ? distance : -1.0F;
}

// These values classify the exact instruction immediately following a
// verified GameOrig ILTClientPhysics::SetObjectDims call. The instruction
// address itself, nearby addresses, and callers in other modules must remain
// Unknown.
enum class PlayerColliderSetDimensionsCallsite : std::uint8_t {
    Unknown,
    MoveManagerPrimary,
    MoveManagerFallback,
    Routine31cf0YAttempt,
    Routine31cf0OriginalRestore,
    WriterLiteralHalf,
    WriterPrimary,
    WriterRetry,
};

inline PlayerColliderSetDimensionsCallsite
ClassifyPlayerColliderSetDimensionsReturnRva(
    std::uintptr_t returnRva) noexcept {
    switch (returnRva) {
    case 0x00031BFCU:
        return PlayerColliderSetDimensionsCallsite::MoveManagerPrimary;
    case 0x00031C16U:
        return PlayerColliderSetDimensionsCallsite::MoveManagerFallback;
    case 0x00031D68U:
        return PlayerColliderSetDimensionsCallsite::Routine31cf0YAttempt;
    case 0x00031D86U:
        return PlayerColliderSetDimensionsCallsite::Routine31cf0OriginalRestore;
    case 0x000346BFU:
        return PlayerColliderSetDimensionsCallsite::WriterLiteralHalf;
    case 0x0003476FU:
        return PlayerColliderSetDimensionsCallsite::WriterPrimary;
    case 0x0003478AU:
        return PlayerColliderSetDimensionsCallsite::WriterRetry;
    default:
        return PlayerColliderSetDimensionsCallsite::Unknown;
    }
}

inline std::uintptr_t PlayerColliderSetDimensionsCallInstructionRva(
    PlayerColliderSetDimensionsCallsite callsite) noexcept {
    switch (callsite) {
    case PlayerColliderSetDimensionsCallsite::MoveManagerPrimary:
        return 0x00031BF9U;
    case PlayerColliderSetDimensionsCallsite::MoveManagerFallback:
        return 0x00031C13U;
    case PlayerColliderSetDimensionsCallsite::Routine31cf0YAttempt:
        return 0x00031D65U;
    case PlayerColliderSetDimensionsCallsite::Routine31cf0OriginalRestore:
        return 0x00031D83U;
    case PlayerColliderSetDimensionsCallsite::WriterLiteralHalf:
        return 0x000346BCU;
    case PlayerColliderSetDimensionsCallsite::WriterPrimary:
        return 0x0003476CU;
    case PlayerColliderSetDimensionsCallsite::WriterRetry:
        return 0x00034787U;
    default:
        return 0U;
    }
}

inline const char* PlayerColliderSetDimensionsCallsiteName(
    PlayerColliderSetDimensionsCallsite callsite) noexcept {
    switch (callsite) {
    case PlayerColliderSetDimensionsCallsite::MoveManagerPrimary:
        return "move_manager_primary";
    case PlayerColliderSetDimensionsCallsite::MoveManagerFallback:
        return "move_manager_fallback";
    case PlayerColliderSetDimensionsCallsite::Routine31cf0YAttempt:
        return "routine_31cf0_y_attempt";
    case PlayerColliderSetDimensionsCallsite::Routine31cf0OriginalRestore:
        return "routine_31cf0_original_restore";
    case PlayerColliderSetDimensionsCallsite::WriterLiteralHalf:
        return "verified_344e0_literal_half";
    case PlayerColliderSetDimensionsCallsite::WriterPrimary:
        return "verified_344e0_primary";
    case PlayerColliderSetDimensionsCallsite::WriterRetry:
        return "verified_344e0_retry";
    default:
        return "unknown";
    }
}

// Keep the native invocation mechanically testable: the same interface,
// object, in/out buffer, and raw flag are forwarded exactly once, and the
// native result is returned bit-for-bit.
template <typename ForwardFunction, typename DimensionsPointer>
inline std::uint32_t ForwardPlayerColliderSetDimensionsExactlyOnce(
    ForwardFunction&& forward,
    void* physics,
    void* object,
    DimensionsPointer dimensions,
    std::uint32_t rawFlag) noexcept(noexcept(
        std::forward<ForwardFunction>(forward)(
            physics, object, dimensions, rawFlag))) {
    return std::forward<ForwardFunction>(forward)(
        physics, object, dimensions, rawFlag);
}

template <typename ForwardFunction, typename VectorPointer>
inline std::uint32_t ForwardPlayerCollisionSetVelocityExactlyOnce(
    ForwardFunction&& forward,
    void* physics,
    void* object,
    VectorPointer velocity) noexcept(noexcept(
        std::forward<ForwardFunction>(forward)(physics, object, velocity))) {
    return std::forward<ForwardFunction>(forward)(physics, object, velocity);
}

inline bool PlayerColliderSettingsAreValid(
    const PlayerColliderSettings& settings) noexcept {
    return std::isfinite(settings.widthScale) &&
        settings.widthScale >= kPlayerColliderWidthScaleMinimum &&
        settings.widthScale <= kPlayerColliderWidthScaleMaximum;
}

inline bool PlayerColliderDimensionsAreValid(
    const PlayerColliderDimensions& dimensions) noexcept {
    return std::isfinite(dimensions.x) &&
        std::isfinite(dimensions.y) &&
        std::isfinite(dimensions.z) &&
        dimensions.x > 0.0F && dimensions.y > 0.0F &&
        dimensions.z > 0.0F;
}

inline bool PlayerColliderDimensionsMatch(
    const PlayerColliderDimensions& left,
    const PlayerColliderDimensions& right,
    float tolerance = 0.05F) noexcept {
    return PlayerColliderDimensionsAreValid(left) &&
        PlayerColliderDimensionsAreValid(right) &&
        std::isfinite(tolerance) && tolerance >= 0.0F &&
        std::fabs(left.x - right.x) <= tolerance &&
        std::fabs(left.y - right.y) <= tolerance &&
        std::fabs(left.z - right.z) <= tolerance;
}

inline bool PlayerColliderRequiresOngoingAudit(
    const PlayerColliderSettings& settings) noexcept {
    return PlayerColliderSettingsAreValid(settings) &&
        settings.widthScale < kPlayerColliderWidthScaleDefault;
}

inline bool PlayerColliderDimensionAuditRequired(
    const PlayerColliderSettings& settings,
    bool reapplyPending) noexcept {
    return reapplyPending ||
        PlayerColliderRequiresOngoingAudit(settings);
}

inline bool ResolvePlayerColliderDimensions(
    const PlayerColliderDimensions& retailDimensions,
    const PlayerColliderSettings& settings,
    PlayerColliderDimensions& resolvedDimensions) noexcept {
    resolvedDimensions = {};
    if (!PlayerColliderDimensionsAreValid(retailDimensions) ||
        !PlayerColliderSettingsAreValid(settings)) {
        return false;
    }

    PlayerColliderDimensions resolved{
        retailDimensions.x * settings.widthScale,
        retailDimensions.y,
        retailDimensions.z * settings.widthScale};
    if (!PlayerColliderDimensionsAreValid(resolved)) {
        return false;
    }
    resolvedDimensions = resolved;
    return true;
}

inline bool UpdatePlayerColliderSettings(
    PlayerColliderSettings& settings,
    std::uint32_t row,
    int delta,
    bool activate) noexcept {
    if (!PlayerColliderSettingsAreValid(settings)) {
        return false;
    }
    const float originalScale = settings.widthScale;
    switch (row) {
    case 0U:
        if (delta == 0) {
            return false;
        }
        settings.widthScale = std::clamp(
            settings.widthScale +
                static_cast<float>(delta) *
                    kPlayerColliderWidthScaleStep,
            kPlayerColliderWidthScaleMinimum,
            kPlayerColliderWidthScaleMaximum);
        break;
    case 1U:
        if (!activate) {
            return false;
        }
        settings.widthScale = kPlayerColliderWidthScaleDefault;
        break;
    default:
        return false;
    }
    return settings.widthScale != originalScale;
}

} // namespace condemnedvr
