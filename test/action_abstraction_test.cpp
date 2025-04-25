#include "gtest/gtest.h"
#include "action_abstraction.h" // Corrected include
#include "game_state.h"         // Corrected include
#include <vector>
#include <string>
#include <set>
#include <algorithm> // For std::find_if

namespace gto_solver {

// Helper to check if an ActionSpec exists in a vector
bool contains_action(const std::vector<ActionSpec>& specs, ActionType type, double value = 0.0, SizingUnit unit = SizingUnit::BB) {
    return std::find_if(specs.begin(), specs.end(), [&](const ActionSpec& spec){
        bool match = (spec.type == type);
        if (type == ActionType::BET || type == ActionType::RAISE) {
            match = match && (std::abs(spec.value - value) < 1e-5) && (spec.unit == unit);
        } else if (type == ActionType::ALL_IN) {
             // For all-in, value/unit might not be consistently set, just check type
             match = (spec.type == type);
        }
        return match;
    }) != specs.end();
}

// Helper to check if ONLY specific actions exist (useful for exact matches)
::testing::AssertionResult ActionsMatch(const std::vector<ActionSpec>& actual_specs, const std::vector<ActionSpec>& expected_specs) {
    if (actual_specs.size() != expected_specs.size()) {
        return ::testing::AssertionFailure() << "Action count mismatch. Expected: " << expected_specs.size() << ", Actual: " << actual_specs.size();
    }
    // Convert to sets for easier comparison regardless of order
    std::set<ActionSpec> actual_set(actual_specs.begin(), actual_specs.end());
    std::set<ActionSpec> expected_set(expected_specs.begin(), expected_specs.end());

    if (actual_set == expected_set) {
        return ::testing::AssertionSuccess();
    } else {
        // Provide more details on mismatch
        std::stringstream ss_expected, ss_actual;
        for(const auto& s : expected_specs) ss_expected << s.to_string() << " ";
        for(const auto& s : actual_specs) ss_actual << s.to_string() << " ";
        return ::testing::AssertionFailure() << "Action sets do not match.\nExpected: " << ss_expected.str() << "\nActual:   " << ss_actual.str();
    }
}


TEST(ActionAbstractionTest, PreflopSBInitialActionsHU) {
    GameState initial_state(2, 100, 0, 0); // HU, BTN=SB=P0
    ActionAbstraction action_abstraction;
    std::vector<ActionSpec> actions = action_abstraction.get_possible_action_specs(initial_state);

    // Expected actions for SB HU: Call (limp), Raise 3bb, Raise 4bb
    EXPECT_TRUE(contains_action(actions, ActionType::CALL));
    EXPECT_TRUE(contains_action(actions, ActionType::RAISE, 3.0, SizingUnit::BB));
    EXPECT_TRUE(contains_action(actions, ActionType::RAISE, 4.0, SizingUnit::BB));
    EXPECT_EQ(actions.size(), 3); // Ensure no unexpected actions
}

TEST(ActionAbstractionTest, PreflopBBCheckOptionHU) {
    GameState state(2, 100, 0, 0); // HU, BTN=SB=P0
    ActionAbstraction action_abstraction;

    // SB calls (limps)
    Action sb_call;
    sb_call.type = Action::Type::CALL;
    sb_call.player_index = 0; // SB is P0
    state.apply_action(sb_call);

    // Now it's BB's turn (P1)
    ASSERT_EQ(state.get_current_player(), 1);
    std::vector<ActionSpec> actions = action_abstraction.get_possible_action_specs(state);

    // Expected actions for BB vs Limp: Check, Raise 3bb, Raise 4bb
    EXPECT_TRUE(contains_action(actions, ActionType::CHECK));
    EXPECT_TRUE(contains_action(actions, ActionType::RAISE, 3.0, SizingUnit::BB)); // 3bb total (raise of 2bb over the 1bb limp)
    EXPECT_TRUE(contains_action(actions, ActionType::RAISE, 4.0, SizingUnit::BB)); // 4bb total
    EXPECT_EQ(actions.size(), 3);
}

TEST(ActionAbstractionTest, PreflopBBFacesRaiseHU) {
    GameState state(2, 100, 0, 0); // HU, BTN=SB=P0
    ActionAbstraction action_abstraction;

    // SB Raises to 3bb
    ActionSpec sb_raise_spec = {ActionType::RAISE, 3.0, SizingUnit::BB};
    Action sb_raise;
    sb_raise.type = Action::Type::RAISE;
    sb_raise.player_index = 0;
    sb_raise.amount = action_abstraction.get_action_amount(sb_raise_spec, state); // Calculate amount
    state.apply_action(sb_raise);

    // Now it's BB's turn (P1)
    ASSERT_EQ(state.get_current_player(), 1);
    std::vector<ActionSpec> actions = action_abstraction.get_possible_action_specs(state);

    // Expected actions for BB vs 3bb Raise: Fold, Call, Raise 3x, Raise 4x, All-in (if stack <= 40bb, which it is)
    EXPECT_TRUE(contains_action(actions, ActionType::FOLD));
    EXPECT_TRUE(contains_action(actions, ActionType::CALL));
    EXPECT_TRUE(contains_action(actions, ActionType::RAISE, 3.0, SizingUnit::MULTIPLIER_X)); // 3x the raise size
    EXPECT_TRUE(contains_action(actions, ActionType::RAISE, 4.0, SizingUnit::MULTIPLIER_X)); // 4x the raise size
    EXPECT_TRUE(contains_action(actions, ActionType::ALL_IN)); // Stack is 100bb > 40bb, so All-in should be added vs RFI
    EXPECT_EQ(actions.size(), 5);
}

TEST(ActionAbstractionTest, PostflopBetActions) {
    GameState state(2, 100, 0, 0); // HU, BTN=SB=P0
    ActionAbstraction action_abstraction;
    // Simulate actions to get to flop, P0 (SB/BTN) acts first
    Action sb_call; sb_call.type = Action::Type::CALL; sb_call.player_index = 0; state.apply_action(sb_call);
    Action bb_check; bb_check.type = Action::Type::CHECK; bb_check.player_index = 1; state.apply_action(bb_check);
    state.deal_community_cards({"As", "Kd", "7h"}); // Deal flop

    ASSERT_EQ(state.get_current_street(), Street::FLOP);
    ASSERT_EQ(state.get_current_player(), 0); // P0 acts first postflop HU

    std::vector<ActionSpec> actions = action_abstraction.get_possible_action_specs(state);

    // Expected actions: Check, Bet 33%, Bet 50%, Bet 75%, Bet 100%, Bet 133%, All-in
    EXPECT_TRUE(contains_action(actions, ActionType::CHECK));
    EXPECT_TRUE(contains_action(actions, ActionType::BET, 33, SizingUnit::PCT_POT));
    EXPECT_TRUE(contains_action(actions, ActionType::BET, 50, SizingUnit::PCT_POT));
    EXPECT_TRUE(contains_action(actions, ActionType::BET, 75, SizingUnit::PCT_POT));
    EXPECT_TRUE(contains_action(actions, ActionType::BET, 100, SizingUnit::PCT_POT));
    EXPECT_TRUE(contains_action(actions, ActionType::BET, 133, SizingUnit::PCT_POT));
    EXPECT_TRUE(contains_action(actions, ActionType::ALL_IN));
    EXPECT_EQ(actions.size(), 7);
}


} // namespace gto_solver
