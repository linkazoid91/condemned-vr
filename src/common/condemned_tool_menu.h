#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "condemned_physical_melee.h"
#include "input_state.h"
#include "protocol.h"

namespace condemnedvr {

enum class ToolMenuTab : std::uint8_t {
    Melee,
    Weapon,
    Grip,
    Display,
    Controls,
    Debug,
    Count
};

inline const char* ToolMenuTabName(ToolMenuTab tab) noexcept {
    switch (tab) {
    case ToolMenuTab::Melee:
        return "MELEE";
    case ToolMenuTab::Weapon:
        return "WEAPON";
    case ToolMenuTab::Grip:
        return "GRIP";
    case ToolMenuTab::Display:
        return "DISPLAY";
    case ToolMenuTab::Controls:
        return "CONTROLS";
    case ToolMenuTab::Debug:
        return "DEBUG";
    default:
        return "INVALID";
    }
}

inline std::uint32_t ToolMenuRowCount(ToolMenuTab tab) noexcept {
    switch (tab) {
    case ToolMenuTab::Melee:
        return 6U;
    case ToolMenuTab::Weapon:
        return 7U;
    case ToolMenuTab::Grip:
        return 9U;
    case ToolMenuTab::Display:
        return 9U;
    case ToolMenuTab::Controls:
    case ToolMenuTab::Debug:
        return 1U;
    default:
        return 0U;
    }
}

struct ToolMenuState {
    ToolMenuTab tab{ToolMenuTab::Melee};
    std::uint32_t row{1U};
    bool open{false};
};

struct ToolMenuInputEvent {
    bool toggle{false};
    bool close{false};
    bool previousTab{false};
    bool nextTab{false};
    bool previousRow{false};
    bool nextRow{false};
    bool decrease{false};
    bool increase{false};
    bool activate{false};
};

struct ToolMenuTransition {
    bool opened{false};
    bool closed{false};
    bool selectionChanged{false};
    int valueDelta{0};
    bool activate{false};
};

inline ToolMenuTransition UpdateToolMenuState(
    ToolMenuState& state,
    const ToolMenuInputEvent& input) noexcept {
    ToolMenuTransition result{};
    if (input.toggle) {
        state.open = !state.open;
        result.opened = state.open;
        result.closed = !state.open;
        return result;
    }
    if (!state.open) {
        return result;
    }
    if (input.close) {
        state.open = false;
        result.closed = true;
        return result;
    }

    const std::uint32_t tabCount =
        static_cast<std::uint32_t>(ToolMenuTab::Count);
    std::uint32_t tab = static_cast<std::uint32_t>(state.tab);
    if (input.previousTab) {
        tab = (tab + tabCount - 1U) % tabCount;
        result.selectionChanged = true;
    } else if (input.nextTab) {
        tab = (tab + 1U) % tabCount;
        result.selectionChanged = true;
    }
    state.tab = static_cast<ToolMenuTab>(tab);

    const std::uint32_t rows = ToolMenuRowCount(state.tab);
    if (rows == 0U) {
        state.row = 0U;
    } else {
        state.row = std::min(state.row, rows - 1U);
        if (input.previousRow) {
            state.row = (state.row + rows - 1U) % rows;
            result.selectionChanged = true;
        } else if (input.nextRow) {
            state.row = (state.row + 1U) % rows;
            result.selectionChanged = true;
        }
    }
    result.valueDelta = input.decrease ? -1 : input.increase ? 1 : 0;
    result.activate = input.activate;
    return result;
}

struct ToolMenuMeleeSettings {
    bool swingAttackEnabled{false};
    float swingTriggerSpeedMetersPerSecond{3.00F};
    float swingRearmSpeedMetersPerSecond{0.75F};
    std::uint32_t swingPulseMilliseconds{100U};
    std::uint32_t swingCooldownMilliseconds{450U};
    float massKilograms{1.5F};
    float handlingWeight{1.0F};
    float positionalFollow{18.0F};
    float rotationalFollow{20.0F};
    float catchUpStrength{1.50F};
    float dampingRatio{1.0F};
};

inline bool ToolMenuMeleeSettingsAreValid(
    const ToolMenuMeleeSettings& settings) noexcept {
    return std::isfinite(settings.swingTriggerSpeedMetersPerSecond) &&
        settings.swingTriggerSpeedMetersPerSecond >= 0.50F &&
        settings.swingTriggerSpeedMetersPerSecond <= 10.0F &&
        std::isfinite(settings.swingRearmSpeedMetersPerSecond) &&
        settings.swingRearmSpeedMetersPerSecond >= 0.0F &&
        settings.swingRearmSpeedMetersPerSecond <
            settings.swingTriggerSpeedMetersPerSecond &&
        settings.swingPulseMilliseconds >= 30U &&
        settings.swingPulseMilliseconds <= 500U &&
        settings.swingCooldownMilliseconds >=
            settings.swingPulseMilliseconds &&
        settings.swingCooldownMilliseconds <= 3000U &&
        std::isfinite(settings.massKilograms) &&
        settings.massKilograms >= 0.5F &&
        settings.massKilograms <= 20.0F &&
        std::isfinite(settings.handlingWeight) &&
        settings.handlingWeight >= 0.10F &&
        settings.handlingWeight <= 4.0F &&
        std::isfinite(settings.positionalFollow) &&
        settings.positionalFollow >= 2.0F &&
        settings.positionalFollow <= 40.0F &&
        std::isfinite(settings.rotationalFollow) &&
        settings.rotationalFollow >= 2.0F &&
        settings.rotationalFollow <= 40.0F &&
        std::isfinite(settings.catchUpStrength) &&
        settings.catchUpStrength >= 0.0F &&
        settings.catchUpStrength <= 4.0F &&
        std::isfinite(settings.dampingRatio) &&
        settings.dampingRatio >= 0.35F &&
        settings.dampingRatio <= 1.0F;
}

inline bool ToolMenuProfileSupportsSwingAttack(
    PhysicalMeleeProfileId id) noexcept {
    return id == PhysicalMeleeProfileId::Pipe ||
        id == PhysicalMeleeProfileId::Crowbar ||
        id == PhysicalMeleeProfileId::FireAxe ||
        id == PhysicalMeleeProfileId::Plank;
}

inline const char* ToolMenuWeaponProfileLabel(
    PhysicalMeleeProfileId id) noexcept {
    switch (id) {
    case PhysicalMeleeProfileId::Pipe:
        return "PIPE";
    case PhysicalMeleeProfileId::Crowbar:
        return "CROWBAR";
    case PhysicalMeleeProfileId::FireAxe:
        return "FIRE AXE";
    case PhysicalMeleeProfileId::Plank:
        return "PLANK";
    case PhysicalMeleeProfileId::GenericOneHanded:
        return "UNMAPPED WEAPON";
    default:
        return "INVALID PROFILE";
    }
}

inline ToolMenuMeleeSettings ToolMenuMeleeSettingsFromProfile(
    const PhysicalMeleeProfile& profile) noexcept {
    ToolMenuMeleeSettings settings{};
    if (!PhysicalMeleeProfileIsValid(profile)) {
        return settings;
    }
    settings.swingAttackEnabled = profile.swingAttackEnabled &&
        ToolMenuProfileSupportsSwingAttack(profile.id);
    settings.swingTriggerSpeedMetersPerSecond =
        profile.swingAttackTriggerSpeedMetersPerSecond;
    settings.swingRearmSpeedMetersPerSecond =
        profile.swingAttackRearmSpeedMetersPerSecond;
    settings.swingPulseMilliseconds =
        profile.swingAttackPulseMilliseconds;
    settings.swingCooldownMilliseconds =
        profile.swingAttackCooldownMilliseconds;
    settings.massKilograms = profile.massKilograms;
    settings.handlingWeight = profile.handlingWeight;
    settings.positionalFollow = profile.positionalFollow;
    settings.rotationalFollow = profile.rotationalFollow;
    settings.catchUpStrength = profile.catchUpStrength;
    settings.dampingRatio = profile.dampingRatio;
    return settings;
}

inline void ApplyToolMenuMeleeSettings(
    const ToolMenuMeleeSettings& settings,
    PhysicalMeleeProfile& profile) noexcept {
    if (!PhysicalMeleeProfileIsValid(profile) ||
        !ToolMenuMeleeSettingsAreValid(settings)) {
        return;
    }
    profile.swingAttackEnabled = settings.swingAttackEnabled &&
        ToolMenuProfileSupportsSwingAttack(profile.id);
    profile.swingAttackTriggerSpeedMetersPerSecond =
        settings.swingTriggerSpeedMetersPerSecond;
    profile.swingAttackRearmSpeedMetersPerSecond =
        settings.swingRearmSpeedMetersPerSecond;
    profile.swingAttackPulseMilliseconds =
        settings.swingPulseMilliseconds;
    profile.swingAttackCooldownMilliseconds =
        settings.swingCooldownMilliseconds;
    profile.massKilograms = settings.massKilograms;
    profile.handlingWeight = settings.handlingWeight;
    profile.positionalFollow = settings.positionalFollow;
    profile.rotationalFollow = settings.rotationalFollow;
    profile.catchUpStrength = settings.catchUpStrength;
    profile.dampingRatio = settings.dampingRatio;
}

// Tool settings are keyed only by Retail's stable weapon index. Runtime
// object/model pointers are deliberately excluded so dropping and reacquiring
// the same weapon keeps its tuning. The fixed-size LRU registry is bounded and
// contains no allocations in a render or input hook.
struct ToolMenuWeaponSettingsSlot {
    std::int32_t weaponIndex{-1};
    PhysicalMeleeProfileId profileId{
        PhysicalMeleeProfileId::GenericOneHanded};
    ToolMenuMeleeSettings settings{};
    std::uint64_t lastUsed{0U};
    bool occupied{false};
};

constexpr std::size_t kToolMenuWeaponSettingsSlotCount = 64U;

struct ToolMenuWeaponSettingsRegistry {
    std::array<
        ToolMenuWeaponSettingsSlot,
        kToolMenuWeaponSettingsSlotCount> slots{};
    std::uint64_t useSequence{0U};
};

inline ToolMenuWeaponSettingsSlot* FindToolMenuWeaponSettingsSlot(
    ToolMenuWeaponSettingsRegistry& registry,
    std::int32_t weaponIndex) noexcept {
    if (weaponIndex < 0) {
        return nullptr;
    }
    for (ToolMenuWeaponSettingsSlot& slot : registry.slots) {
        if (slot.occupied && slot.weaponIndex == weaponIndex) {
            return &slot;
        }
    }
    return nullptr;
}

inline ToolMenuWeaponSettingsSlot* ResolveToolMenuWeaponSettingsSlot(
    ToolMenuWeaponSettingsRegistry& registry,
    std::int32_t weaponIndex,
    const PhysicalMeleeProfile& baseProfile) noexcept {
    if (weaponIndex < 0 || !PhysicalMeleeProfileIsValid(baseProfile)) {
        return nullptr;
    }
    if (ToolMenuWeaponSettingsSlot* slot =
            FindToolMenuWeaponSettingsSlot(registry, weaponIndex)) {
        if (slot->profileId != baseProfile.id) {
            slot->profileId = baseProfile.id;
            slot->settings = ToolMenuMeleeSettingsFromProfile(baseProfile);
        }
        slot->lastUsed = ++registry.useSequence;
        return slot;
    }

    ToolMenuWeaponSettingsSlot* replacement = nullptr;
    for (ToolMenuWeaponSettingsSlot& slot : registry.slots) {
        if (!slot.occupied) {
            replacement = &slot;
            break;
        }
        if (replacement == nullptr ||
            slot.lastUsed < replacement->lastUsed) {
            replacement = &slot;
        }
    }
    if (replacement == nullptr) {
        return nullptr;
    }
    *replacement = {};
    replacement->weaponIndex = weaponIndex;
    replacement->profileId = baseProfile.id;
    replacement->settings = ToolMenuMeleeSettingsFromProfile(baseProfile);
    replacement->lastUsed = ++registry.useSequence;
    replacement->occupied = true;
    return replacement;
}

struct ToolMenuMeleeTelemetry {
    float swingSpeedMetersPerSecond{0.0F};
    std::uint32_t triggerCount{0U};
    std::int32_t weaponIndex{-1};
    bool trackingFresh{false};
    bool wallProxyEnabled{false};
    bool visualProxyEnabled{false};
};

inline bool ToolMenuToggleChordDown(
    const FearVrInputState& input,
    bool sampleFresh) noexcept {
    return fearvr::IsInputStateUsable(input, sampleFresh) &&
        (input.activeHands &
         (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT)) ==
            (FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT) &&
        std::isfinite(input.squeeze[FEARVR_HAND_LEFT]) &&
        std::isfinite(input.squeeze[FEARVR_HAND_RIGHT]) &&
        input.squeeze[FEARVR_HAND_LEFT] >= 0.75F &&
        input.squeeze[FEARVR_HAND_RIGHT] >= 0.75F &&
        (input.buttons & FEARVR_IB_LEFT_SECONDARY) != 0U;
}

constexpr std::size_t kToolMenuMaximumTriangleVertices = 24576U;

constexpr float kToolMenuDefaultScale = 0.62F;
constexpr float kToolMenuDefaultDistanceMeters = 1.50F;
constexpr float kToolMenuDefaultVerticalOffsetNdc = -0.08F;

struct ToolMenuPanelPlacement {
    float scale{kToolMenuDefaultScale};
    float distanceMeters{kToolMenuDefaultDistanceMeters};
    float verticalOffsetNdc{kToolMenuDefaultVerticalOffsetNdc};
};

struct ToolMenuPanelTransform {
    float scale{1.0F};
    float horizontalOffsetNdc{0.0F};
    float verticalOffsetNdc{0.0F};
    bool valid{false};
};

inline ToolMenuPanelTransform ResolveToolMenuPanelTransform(
    std::uint32_t eye,
    float interpupillaryDistanceMeters,
    float horizontalFovRadians,
    const ToolMenuPanelPlacement& placement = {}) noexcept {
    ToolMenuPanelTransform transform{};
    if (eye >= FEARVR_EYE_COUNT ||
        !std::isfinite(interpupillaryDistanceMeters) ||
        interpupillaryDistanceMeters < 0.03F ||
        interpupillaryDistanceMeters > 0.10F ||
        !std::isfinite(horizontalFovRadians) ||
        horizontalFovRadians <= 0.50F ||
        horizontalFovRadians >= 2.80F ||
        !std::isfinite(placement.scale) ||
        placement.scale < 0.35F || placement.scale > 1.0F ||
        !std::isfinite(placement.distanceMeters) ||
        placement.distanceMeters < 0.50F ||
        placement.distanceMeters > 5.0F ||
        !std::isfinite(placement.verticalOffsetNdc) ||
        std::fabs(placement.verticalOffsetNdc) > 0.50F) {
        return transform;
    }
    const float tangent = std::tan(horizontalFovRadians * 0.5F);
    if (!std::isfinite(tangent) || tangent <= 0.0F) {
        return transform;
    }
    const float convergenceOffset =
        interpupillaryDistanceMeters * 0.5F /
        (placement.distanceMeters * tangent);
    if (!std::isfinite(convergenceOffset) ||
        convergenceOffset < 0.0F || convergenceOffset > 0.25F) {
        return transform;
    }
    transform.scale = placement.scale;
    // Crossed disparity: the left-eye image moves right and the right-eye
    // image moves left, converging the head-relative panel at the requested
    // comfortable distance instead of optical infinity.
    transform.horizontalOffsetNdc =
        eye == FEARVR_EYE_LEFT
            ? convergenceOffset
            : -convergenceOffset;
    transform.verticalOffsetNdc = placement.verticalOffsetNdc;
    transform.valid = true;
    return transform;
}

struct ToolMenuOverlay {
    std::array<
        FearVrOverlayLineVertex,
        kToolMenuMaximumTriangleVertices> vertices{};
    std::size_t count{0U};
    bool overflowed{false};
};

inline bool ApplyToolMenuPanelTransform(
    ToolMenuOverlay& overlay,
    const ToolMenuPanelTransform& transform) noexcept {
    if (!transform.valid || overlay.overflowed ||
        overlay.count > overlay.vertices.size()) {
        return false;
    }
    for (std::size_t index = 0; index < overlay.count; ++index) {
        FearVrOverlayLineVertex& vertex = overlay.vertices[index];
        vertex.ndcX = vertex.ndcX * transform.scale +
            transform.horizontalOffsetNdc;
        vertex.ndcY = vertex.ndcY * transform.scale +
            transform.verticalOffsetNdc;
        if (!std::isfinite(vertex.ndcX) ||
            !std::isfinite(vertex.ndcY) ||
            std::fabs(vertex.ndcX) > 1.0F ||
            std::fabs(vertex.ndcY) > 1.0F) {
            overlay.overflowed = true;
            return false;
        }
    }
    return true;
}

inline bool AddToolMenuTriangle(
    ToolMenuOverlay& overlay,
    float x0, float y0,
    float x1, float y1,
    float x2, float y2,
    std::uint32_t argb) noexcept {
    if (overlay.count + 3U > overlay.vertices.size() ||
        !std::isfinite(x0) || !std::isfinite(y0) ||
        !std::isfinite(x1) || !std::isfinite(y1) ||
        !std::isfinite(x2) || !std::isfinite(y2)) {
        overlay.overflowed = true;
        return false;
    }
    overlay.vertices[overlay.count++] = {x0, y0, argb};
    overlay.vertices[overlay.count++] = {x1, y1, argb};
    overlay.vertices[overlay.count++] = {x2, y2, argb};
    return true;
}

inline bool AddToolMenuRectangle(
    ToolMenuOverlay& overlay,
    float left, float top,
    float right, float bottom,
    std::uint32_t argb) noexcept {
    if (!(left < right) || !(bottom < top)) {
        overlay.overflowed = true;
        return false;
    }
    return AddToolMenuTriangle(
               overlay, left, top, right, top,
               right, bottom, argb) &&
        AddToolMenuTriangle(
            overlay, left, top, right, bottom,
            left, bottom, argb);
}

inline std::uint8_t ToolMenuGlyphRow(
    char character,
    std::size_t row) noexcept {
    if (row >= 7U) {
        return 0U;
    }
    if (character >= 'a' && character <= 'z') {
        character = static_cast<char>(character - 'a' + 'A');
    }
    static constexpr std::uint8_t letters[26][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
        {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
        {17,17,17,17,21,27,17}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}};
    static constexpr std::uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}};
    if (character >= 'A' && character <= 'Z') {
        return letters[static_cast<std::size_t>(character - 'A')][row];
    }
    if (character >= '0' && character <= '9') {
        return digits[static_cast<std::size_t>(character - '0')][row];
    }
    switch (character) {
    case '-': {
        static constexpr std::uint8_t glyph[7] = {0,0,0,14,0,0,0};
        return glyph[row];
    }
    case '.': {
        static constexpr std::uint8_t glyph[7] = {0,0,0,0,0,6,6};
        return glyph[row];
    }
    case ':': {
        static constexpr std::uint8_t glyph[7] = {0,6,6,0,6,6,0};
        return glyph[row];
    }
    case '/': {
        static constexpr std::uint8_t glyph[7] = {1,2,2,4,8,8,16};
        return glyph[row];
    }
    case '+': {
        static constexpr std::uint8_t glyph[7] = {0,4,4,31,4,4,0};
        return glyph[row];
    }
    case '%': {
        static constexpr std::uint8_t glyph[7] = {25,25,2,4,8,19,19};
        return glyph[row];
    }
    case '_':
        return row == 6U ? 31U : 0U;
    case '[':
        return row == 0U || row == 6U ? 14U : 8U;
    case ']':
        return row == 0U || row == 6U ? 14U : 2U;
    case '(':
        return row == 0U || row == 6U ? 2U : 4U;
    case ')':
        return row == 0U || row == 6U ? 8U : 4U;
    case '=':
        return row == 2U || row == 4U ? 31U : 0U;
    case '<': {
        static constexpr std::uint8_t glyph[7] = {1,2,4,8,4,2,1};
        return glyph[row];
    }
    case '>': {
        static constexpr std::uint8_t glyph[7] = {16,8,4,2,4,8,16};
        return glyph[row];
    }
    default:
        return 0U;
    }
}

inline bool AddToolMenuText(
    ToolMenuOverlay& overlay,
    float x, float y,
    float pixelWidth, float pixelHeight,
    const char* text,
    std::uint32_t argb) noexcept {
    if (text == nullptr || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(pixelWidth) || !std::isfinite(pixelHeight) ||
        pixelWidth <= 0.0F || pixelHeight <= 0.0F) {
        overlay.overflowed = true;
        return false;
    }
    const float originX = x;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor == '\n') {
            x = originX;
            y -= pixelHeight * 9.0F;
            continue;
        }
        for (std::size_t row = 0; row < 7U; ++row) {
            const std::uint8_t bits = ToolMenuGlyphRow(*cursor, row);
            std::size_t column = 0U;
            while (column < 5U) {
                const std::uint8_t mask = static_cast<std::uint8_t>(
                    1U << (4U - column));
                if ((bits & mask) == 0U) {
                    ++column;
                    continue;
                }
                const std::size_t runStart = column;
                do {
                    ++column;
                } while (column < 5U &&
                    (bits & static_cast<std::uint8_t>(
                        1U << (4U - column))) != 0U);
                const float left = x +
                    static_cast<float>(runStart) * pixelWidth;
                const float right = x +
                    static_cast<float>(column) * pixelWidth;
                const float top = y -
                    static_cast<float>(row) * pixelHeight;
                const float bottom = top - pixelHeight * 0.82F;
                if (!AddToolMenuRectangle(
                        overlay, left, top, right, bottom, argb)) {
                    return false;
                }
            }
        }
        x += pixelWidth * 6.0F;
    }
    return !overlay.overflowed;
}

} // namespace condemnedvr
