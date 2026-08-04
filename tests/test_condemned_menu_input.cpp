#include <cmath>
#include <cstdio>
#include <limits>

#include "condemned_controller_input.h"
#include "condemned_menu_input.h"

namespace {

int Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

FearVrInputState NeutralInput() {
    FearVrInputState input{};
    input.flags = FEARVR_IF_VALID | FEARVR_IF_FOCUSED;
    input.activeHands =
        FEARVR_HAND_MASK_LEFT | FEARVR_HAND_MASK_RIGHT;
    return input;
}

} // namespace

int main() {
    using condemnedvr::MenuNavigationAction;
    using condemnedvr::MenuNavigationState;
    using condemnedvr::UpdateMenuNavigation;

    FearVrInputState input = NeutralInput();
    MenuNavigationState state{};

    input.buttons = FEARVR_IB_RIGHT_PRIMARY;
    if (UpdateMenuNavigation(state, input, true, true, 0) !=
        MenuNavigationAction::none) {
        return Fail("a menu entered with A held must require release");
    }
    input.buttons = 0;
    UpdateMenuNavigation(state, input, true, true, 1);
    input.buttons = FEARVR_IB_RIGHT_PRIMARY;
    if (UpdateMenuNavigation(state, input, true, true, 2) !=
            MenuNavigationAction::accept ||
        UpdateMenuNavigation(state, input, true, true, 3) !=
            MenuNavigationAction::none) {
        return Fail("A must emit one Accept edge");
    }

    input.buttons = 0;
    UpdateMenuNavigation(state, input, true, true, 4);
    input.buttons = FEARVR_IB_RIGHT_SECONDARY;
    if (UpdateMenuNavigation(state, input, true, true, 5) !=
            MenuNavigationAction::back ||
        UpdateMenuNavigation(state, input, true, true, 6) !=
            MenuNavigationAction::none) {
        return Fail("B must emit one Back edge");
    }

    input.buttons = 0;
    input.trigger[FEARVR_HAND_RIGHT] = 0.7F;
    if (UpdateMenuNavigation(state, input, true, true, 7) !=
        MenuNavigationAction::accept) {
        return Fail("right trigger must also accept");
    }
    input.trigger[FEARVR_HAND_RIGHT] = 0.0F;
    UpdateMenuNavigation(state, input, true, true, 8);

    input.moveY = 0.8F;
    if (UpdateMenuNavigation(state, input, true, true, 100) !=
            MenuNavigationAction::up ||
        UpdateMenuNavigation(state, input, true, true, 449) !=
            MenuNavigationAction::none ||
        UpdateMenuNavigation(state, input, true, true, 450) !=
            MenuNavigationAction::up ||
        UpdateMenuNavigation(state, input, true, true, 559) !=
            MenuNavigationAction::none ||
        UpdateMenuNavigation(state, input, true, true, 560) !=
            MenuNavigationAction::up) {
        return Fail("held Up must use bounded initial and steady repeat");
    }
    input.moveY = 0.2F;
    UpdateMenuNavigation(state, input, true, true, 561);
    input.moveY = 0.8F;
    if (UpdateMenuNavigation(state, input, true, true, 562) !=
        MenuNavigationAction::up) {
        return Fail("stick release must re-arm an immediate direction");
    }

    input.moveX = -0.9F;
    input.moveY = 0.7F;
    UpdateMenuNavigation(state, input, false, true, 600);
    input.moveX = 0.0F;
    input.moveY = 0.0F;
    UpdateMenuNavigation(state, input, true, true, 601);
    input.moveX = -0.9F;
    input.moveY = 0.7F;
    if (UpdateMenuNavigation(state, input, true, true, 602) !=
        MenuNavigationAction::left) {
        return Fail("the dominant diagonal axis must win");
    }

    input.moveX = std::numeric_limits<float>::quiet_NaN();
    input.moveY = 0.0F;
    UpdateMenuNavigation(state, input, false, true, 700);
    UpdateMenuNavigation(state, input, true, true, 701);
    input.moveX = 0.8F;
    if (UpdateMenuNavigation(state, input, true, false, 702) !=
            MenuNavigationAction::none ||
        UpdateMenuNavigation(state, input, true, true, 703) !=
            MenuNavigationAction::none) {
        return Fail("leaving a menu must require neutral before re-entry");
    }
    input.moveX = 0.0F;
    UpdateMenuNavigation(state, input, true, true, 704);
    input.moveX = 0.8F;
    if (UpdateMenuNavigation(state, input, true, true, 705) !=
        MenuNavigationAction::right) {
        return Fail("neutral input must re-arm after menu re-entry");
    }

    input = NeutralInput();
    state = {};
    UpdateMenuNavigation(state, input, true, true, 800);
    input.activeHands = FEARVR_HAND_MASK_LEFT;
    input.buttons = FEARVR_IB_RIGHT_PRIMARY;
    if (UpdateMenuNavigation(state, input, true, true, 801) !=
        MenuNavigationAction::none) {
        return Fail("loss of either controller must suppress menu input");
    }

    for (int gameState = 0;
         gameState < condemnedvr::kCondemnedGameStateCount;
        ++gameState) {
        const bool expected =
            gameState == condemnedvr::kCondemnedGameStateMenu ||
            gameState == condemnedvr::kCondemnedGameStateScreen;
        if (condemnedvr::CondemnedGameStateAllowsMenuNavigation(
                gameState) != expected) {
            return Fail(
                "menu navigation must remain limited to states 5 and 6");
        }
    }

    std::puts("Condemned menu-input tests passed.");
    return 0;
}
