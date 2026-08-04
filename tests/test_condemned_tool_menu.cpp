#include <cmath>
#include <cstdio>
#include <cstring>

#include "condemned_tool_menu.h"

namespace {

int Fail(const char* message) {
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

bool Near(float left, float right, float tolerance = 0.001F) {
    return std::fabs(left - right) <= tolerance;
}

bool FitsCompleteMenuOverlay(
    const char* const* rows,
    std::size_t rowCount,
    std::size_t& vertexCount) {
    using namespace condemnedvr;
    static ToolMenuOverlay overlay{};
    overlay.count = 0U;
    overlay.overflowed = false;
    AddToolMenuRectangle(
        overlay, -0.91F, 0.80F, 0.91F, -0.78F, 0xE0141B26U);
    AddToolMenuRectangle(
        overlay, -0.91F, 0.80F, 0.91F, 0.786F, 0xFF53C7E8U);
    AddToolMenuRectangle(
        overlay, -0.91F, -0.766F, 0.91F, -0.78F, 0xFF53C7E8U);
    AddToolMenuText(
        overlay, -0.79F, 0.70F, 0.0054F, 0.0090F,
        "CONDEMNED VR TOOLS", 0xFF76DBF4U);
    for (std::uint32_t index = 0;
         index < static_cast<std::uint32_t>(ToolMenuTab::Count);
         ++index) {
        AddToolMenuText(
            overlay, -0.80F + static_cast<float>(index) * 0.265F,
            0.535F, 0.00375F, 0.0064F,
            ToolMenuTabName(static_cast<ToolMenuTab>(index)),
            0xFFFFFFFFU);
    }
    AddToolMenuText(
        overlay, -0.75F, 0.405F, 0.0041F, 0.0068F,
        "EQUIPPED  UNMAPPED WEAPON   INDEX 123",
        0xFF76DBF4U);
    AddToolMenuRectangle(
        overlay, -0.79F, 0.345F, 0.79F, 0.240F, 0xB8327898U);
    for (std::size_t row = 0; row < rowCount; ++row) {
        AddToolMenuText(
            overlay, -0.75F,
            0.31F - static_cast<float>(row) * 0.105F,
            0.0046F, 0.0072F, rows[row], 0xFFFFFFFFU);
    }
    AddToolMenuText(
        overlay, -0.78F, -0.69F, 0.00355F, 0.0060F,
        "TRIGGERS TABS   LEFT STICK ROW   RIGHT STICK VALUE   A SELECT   B CLOSE",
        0xFF95A5B2U);
    const ToolMenuPanelTransform transform =
        ResolveToolMenuPanelTransform(
            FEARVR_EYE_LEFT, 0.064F, 1.570796327F);
    const bool placed = ApplyToolMenuPanelTransform(
        overlay, transform);
    vertexCount = overlay.count;
    return placed && !overlay.overflowed && overlay.count > 0U;
}

} // namespace

int main() {
    using namespace condemnedvr;

    if (ToolMenuRowCount(ToolMenuTab::TwoHand) != 8U ||
        std::strcmp(
            ToolMenuTabName(ToolMenuTab::TwoHand),
            "2-HAND") != 0) {
        return Fail("two-hand setup must have a dedicated bounded menu tab");
    }

    ToolMenuState state{};
    ToolMenuInputEvent input{};
    input.toggle = true;
    ToolMenuTransition transition = UpdateToolMenuState(state, input);
    if (!state.open || !transition.opened || state.row != 1U) {
        return Fail("tool menu must open on the melee trigger row");
    }
    input = {};
    input.previousTab = true;
    transition = UpdateToolMenuState(state, input);
    if (state.tab != ToolMenuTab::Debug || state.row != 0U ||
        !transition.selectionChanged) {
        return Fail("tool-menu tabs must wrap and clamp their row");
    }
    input = {};
    input.nextTab = true;
    UpdateToolMenuState(state, input);
    if (state.tab != ToolMenuTab::Melee) {
        return Fail("next tab must wrap from debug to melee");
    }
    input = {};
    input.nextRow = true;
    UpdateToolMenuState(state, input);
    if (state.row != 1U) {
        return Fail("melee rows must advance deterministically");
    }
    input = {};
    input.increase = true;
    transition = UpdateToolMenuState(state, input);
    if (transition.valueDelta != 1) {
        return Fail("right adjustment must request one positive step");
    }
    input = {};
    input.close = true;
    transition = UpdateToolMenuState(state, input);
    if (state.open || !transition.closed) {
        return Fail("tool-menu back must close the overlay");
    }

    ToolMenuMeleeSettings settings{};
    if (!ToolMenuMeleeSettingsAreValid(settings) ||
        !Near(settings.swingTriggerSpeedMetersPerSecond, 3.0F)) {
        return Fail("tool-menu melee defaults must be safe and valid");
    }
    PhysicalMeleeProfile axe =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(
            kCondemnedFireAxeWeaponIndex);
    settings.swingTriggerSpeedMetersPerSecond = 4.25F;
    settings.massKilograms = 6.0F;
    settings.handlingWeight = 3.5F;
    ApplyToolMenuMeleeSettings(settings, axe);
    if (!Near(axe.swingAttackTriggerSpeedMetersPerSecond, 4.25F) ||
        !Near(axe.massKilograms, 6.0F) ||
        !Near(axe.handlingWeight, 3.5F)) {
        return Fail("live menu settings must update the axe profile");
    }
    PhysicalMeleeProfile fallback{};
    ApplyToolMenuMeleeSettings(settings, fallback);
    if (fallback.swingAttackEnabled ||
        !Near(fallback.massKilograms, 6.0F)) {
        return Fail(
            "generic weapons may tune handling but must not arm swing attack");
    }
    settings.swingRearmSpeedMetersPerSecond =
        settings.swingTriggerSpeedMetersPerSecond;
    if (ToolMenuMeleeSettingsAreValid(settings)) {
        return Fail("invalid menu hysteresis must fail closed");
    }

    ToolMenuWeaponSettingsRegistry registry{};
    const PhysicalMeleeProfile catalogAxe =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(
            kCondemnedFireAxeWeaponIndex);
    ToolMenuWeaponSettingsSlot* axeSlot =
        ResolveToolMenuWeaponSettingsSlot(
            registry, kCondemnedFireAxeWeaponIndex, catalogAxe);
    const PhysicalMeleeProfile genericProfile =
        ResolvePhysicalMeleeProfileForRetailWeaponIndex(8);
    ToolMenuWeaponSettingsSlot* genericSlot =
        ResolveToolMenuWeaponSettingsSlot(
            registry, 8, genericProfile);
    if (axeSlot == nullptr || genericSlot == nullptr ||
        axeSlot == genericSlot || !axeSlot->settings.swingAttackEnabled ||
        !Near(axeSlot->settings.massKilograms, 4.5F) ||
        genericSlot->settings.swingAttackEnabled ||
        !Near(genericSlot->settings.massKilograms, 1.5F)) {
        return Fail(
            "each Retail weapon index must receive isolated profile defaults");
    }
    axeSlot->settings.swingTriggerSpeedMetersPerSecond = 5.25F;
    genericSlot->settings.massKilograms = 2.75F;
    axeSlot = ResolveToolMenuWeaponSettingsSlot(
        registry, kCondemnedFireAxeWeaponIndex, catalogAxe);
    genericSlot = ResolveToolMenuWeaponSettingsSlot(
        registry, 8, genericProfile);
    if (axeSlot == nullptr || genericSlot == nullptr ||
        !Near(axeSlot->settings.swingTriggerSpeedMetersPerSecond, 5.25F) ||
        !Near(axeSlot->settings.massKilograms, 4.5F) ||
        !Near(genericSlot->settings.massKilograms, 2.75F) ||
        std::strcmp(
            ToolMenuWeaponProfileLabel(axeSlot->profileId),
            "FIRE AXE") != 0) {
        return Fail(
            "switching weapons must restore that weapon's own edited values");
    }

    FearVrInputState controller{};
    controller.flags = FEARVR_IF_VALID | FEARVR_IF_FOCUSED;
    controller.activeHands =
        FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT;
    controller.squeeze[FEARVR_HAND_LEFT] = 0.75F;
    controller.squeeze[FEARVR_HAND_RIGHT] = 0.75F;
    controller.buttons = FEARVR_IB_LEFT_SECONDARY;
    if (!ToolMenuToggleChordDown(controller, true)) {
        return Fail("both grips plus Y must resolve the menu chord");
    }
    controller.squeeze[FEARVR_HAND_RIGHT] = 0.74F;
    if (ToolMenuToggleChordDown(controller, true) ||
        ToolMenuToggleChordDown(controller, false)) {
        return Fail("loose grips or stale input must reject the menu chord");
    }

    ToolMenuOverlay overlay{};
    if (!AddToolMenuRectangle(
            overlay, -0.8F, 0.8F, 0.8F, -0.8F,
            0xCC101820U) ||
        !AddToolMenuText(
            overlay, -0.7F, 0.6F, 0.005F, 0.008F,
            "MELEE 3.00 M/S", 0xFFFFFFFFU) ||
        overlay.count <= 6U || (overlay.count % 3U) != 0U ||
        overlay.overflowed) {
        return Fail("tool-menu panel and font must emit valid triangles");
    }
    if (ToolMenuGlyphRow('A', 0U) == 0U ||
        ToolMenuGlyphRow('0', 3U) == 0U ||
        ToolMenuGlyphRow(' ', 3U) != 0U) {
        return Fail("tool-menu font must cover labels and numeric values");
    }
    const ToolMenuPanelTransform leftPanel =
        ResolveToolMenuPanelTransform(
            FEARVR_EYE_LEFT, 0.064F, 1.570796327F);
    const ToolMenuPanelTransform rightPanel =
        ResolveToolMenuPanelTransform(
            FEARVR_EYE_RIGHT, 0.064F, 1.570796327F);
    ToolMenuPanelPlacement closerPlacement{};
    closerPlacement.distanceMeters = 0.75F;
    const ToolMenuPanelTransform closerLeftPanel =
        ResolveToolMenuPanelTransform(
            FEARVR_EYE_LEFT, 0.064F, 1.570796327F,
            closerPlacement);
    if (!leftPanel.valid || !rightPanel.valid ||
        !closerLeftPanel.valid ||
        !Near(leftPanel.scale, kToolMenuDefaultScale) ||
        !Near(
            leftPanel.verticalOffsetNdc,
            kToolMenuDefaultVerticalOffsetNdc) ||
        leftPanel.horizontalOffsetNdc <= 0.0F ||
        rightPanel.horizontalOffsetNdc >= 0.0F ||
        !Near(
            leftPanel.horizontalOffsetNdc,
            -rightPanel.horizontalOffsetNdc) ||
        closerLeftPanel.horizontalOffsetNdc <=
            leftPanel.horizontalOffsetNdc ||
        ResolveToolMenuPanelTransform(
            FEARVR_EYE_COUNT, 0.064F, 1.570796327F).valid) {
        return Fail(
            "VR menu placement must be smaller, lowered, and converge per eye");
    }

    const char* gripRows[] = {
        "POSITION X                    -123.456",
        "POSITION Y                    -123.456",
        "POSITION Z                    -123.456",
        "ROTATION X                    -180.00 DEG",
        "ROTATION Y                    -180.00 DEG",
        "ROTATION Z                    -180.00 DEG",
        "ADJUSTMENT STEP               10.00",
        "RESET CURRENT GRIP",
        "LOG PROFILE SNAPSHOT"};
    std::size_t completeVertexCount = 0U;
    if (!FitsCompleteMenuOverlay(
            gripRows, sizeof(gripRows) / sizeof(gripRows[0]),
            completeVertexCount)) {
        std::fprintf(
            stderr,
            "complete grip menu exceeded its %zu-vertex budget at %zu\n",
            kToolMenuMaximumTriangleVertices, completeVertexCount);
        return 1;
    }

    return 0;
}
