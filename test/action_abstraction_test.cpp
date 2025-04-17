#include "gtest/gtest.h"
#include "action_abstraction.h" // Corrected include
#include "game_state.h" // Need GameState for tests now
#include <vector>
#include <string>
#include <algorithm> // For std::sort, std::find

namespace gto_solver {

TEST(ActionAbstractionTest, PreflopSBInitialActions) {
    ActionAbstraction action_abstraction;
    // HU Game, BTN=P0, SB=P0, BB=P1. P0 to act.
    GameState initial_state(2, 100, 0, 0);
    std::vector<std::string> actions = action_abstraction.get_possible_actions(initial_state);
    // Expected: fold, call (1 more), raise_2.2x, raise_2.5x, raise_3x, all_in
    std::vector<std::string> expected_actions = {"fold", "call", "raise_2.2x", "raise_2.5x", "raise_3x", "all_in"};
    // Sort for comparison
    std::sort(actions.begin(), actions.end());
    std::sort(expected_actions.begin(), expected_actions.end());
    EXPECT_EQ(actions, expected_actions);
}

TEST(ActionAbstractionTest, PreflopBBCheckOption) {
    ActionAbstraction action_abstraction;
    // HU Game, BTN=P0, SB=P0, BB=P1.
    GameState state(2, 100, 0, 0);
    // SB calls
    Action sb_call;
    sb_call.type = Action::Type::CALL;
    sb_call.player_index = 0; // Set player index for SB
    state.apply_action(sb_call); // Apply SB call
    // Now it's BB's turn (P1), amount to call is 0
    ASSERT_EQ(state.get_current_player(), 1);
    ASSERT_EQ(state.get_amount_to_call(1), 0);

    std::vector<std::string> actions = action_abstraction.get_possible_actions(state);
    // Expected: check, fold, raise_2.2bb, raise_2.5bb, raise_3bb, all_in (since BB faces no bet)
    std::vector<std::string> expected_actions = {"fold", "check", "raise_2.2bb", "raise_2.5bb", "raise_3bb", "all_in"};
    // Sort for comparison
    std::sort(actions.begin(), actions.end());
    std::sort(expected_actions.begin(), expected_actions.end());
    EXPECT_EQ(actions, expected_actions);
}

// TODO: Add tests for postflop actions, different bet sizings, all-in scenarios

} // namespace gto_solver
