#include "gtest/gtest.h"
#include "game_state.h" // Corrected include
#include <vector>       // Include vector
#include <numeric>      // Include numeric for std::accumulate
#include <algorithm>    // Include algorithm for std::sort, std::find

namespace gto_solver {

// Test fixture for GameState tests
class GameStateTest : public ::testing::Test {
protected:
    GameState state_hu; // Heads-Up state (BTN=P0, SB=P0, BB=P1)

    // Use default constructor for state_hu initialization
    GameStateTest() : state_hu(2, 100, 0, 0) {} // 2 players, 100 stack, ante 0, BTN 0

    void SetUp() override {
        // Reset state before each test if needed
        state_hu = GameState(2, 100, 0, 0); // BTN=P0
        // Deal some hands for tests that need them
        // Note: Hands dealt don't affect initial state tests but are needed for some action tests
        state_hu.deal_hands({ { "As", "Ks" }, { "Qh", "Qd" } });
    }
};

TEST_F(GameStateTest, InitialState) {
    ASSERT_EQ(state_hu.get_num_players(), 2);
    ASSERT_EQ(state_hu.get_button_position(), 0); // BTN=P0 in setup
    ASSERT_EQ(state_hu.get_current_player(), 0); // SB acts first in HU (button is SB)
    // Pot size includes blinds posted
    ASSERT_EQ(state_hu.get_pot_size(), 3); // SB(1) + BB(2) = 3
    ASSERT_EQ(state_hu.get_player_stacks().size(), 2);
    ASSERT_EQ(state_hu.get_player_stacks()[0], 99); // Stack after SB post
    ASSERT_EQ(state_hu.get_player_stacks()[1], 98); // Stack after BB post
    ASSERT_EQ(state_hu.get_bets_this_round().size(), 2);
    ASSERT_EQ(state_hu.get_bet_this_round(0), 1); // SB posted
    ASSERT_EQ(state_hu.get_bet_this_round(1), 2); // BB posted
    ASSERT_EQ(state_hu.get_current_street(), Street::PREFLOP);
    ASSERT_FALSE(state_hu.is_terminal());
    ASSERT_EQ(state_hu.get_amount_to_call(0), 1); // SB needs 1 more to call BB
    ASSERT_EQ(state_hu.get_amount_to_call(1), 0); // BB needs 0 more
}

TEST_F(GameStateTest, ApplyActionFold) {
    Action fold_action;
    fold_action.type = Action::Type::FOLD;
    fold_action.player_index = 0; // Player 0 (SB) acts
    state_hu.apply_action(fold_action);

    // Folding makes the state terminal immediately in HU
    EXPECT_TRUE(state_hu.is_terminal());
    // Player stack should remain unchanged after fold, contribution is lost in payoff calc
    EXPECT_EQ(state_hu.get_player_stacks()[0], 99);
    EXPECT_EQ(state_hu.get_player_stacks()[1], 98); // BB stack unchanged
}

TEST_F(GameStateTest, ApplyActionCall) {
    Action call_action;
    call_action.type = Action::Type::CALL;
    call_action.player_index = 0; // Player 0 (SB) acts
    state_hu.apply_action(call_action);

    // Calling should move action to BB
    EXPECT_FALSE(state_hu.is_terminal());
    EXPECT_EQ(state_hu.get_current_player(), 1); // Action moves to BB
    EXPECT_EQ(state_hu.get_player_stacks()[0], 98); // SB called 1, stack 99-1=98
    EXPECT_EQ(state_hu.get_player_stacks()[1], 98); // BB stack unchanged
    EXPECT_EQ(state_hu.get_bet_this_round(0), 2); // SB bet now matches BB
    EXPECT_EQ(state_hu.get_bet_this_round(1), 2); // BB bet unchanged
}

TEST_F(GameStateTest, ApplyActionRaise) {
    Action raise_action;
    raise_action.type = Action::Type::RAISE;
    raise_action.amount = 6; // SB raises to 6 total
    raise_action.player_index = 0; // Player 0 (SB) acts
    state_hu.apply_action(raise_action);

    EXPECT_FALSE(state_hu.is_terminal());
    EXPECT_EQ(state_hu.get_current_player(), 1); // Action moves to BB
    EXPECT_EQ(state_hu.get_player_stacks()[0], 94); // SB started 99, bet 6 total (posted 1, added 5) -> 99-5=94
    EXPECT_EQ(state_hu.get_player_stacks()[1], 98); // BB stack unchanged
    EXPECT_EQ(state_hu.get_bet_this_round(0), 6); // SB bet is 6
    EXPECT_EQ(state_hu.get_bet_this_round(1), 2); // BB bet unchanged
    EXPECT_EQ(state_hu.get_amount_to_call(1), 4); // BB needs 4 more to call
}

TEST_F(GameStateTest, ApplyActionCheckInvalid) {
    // Player 0 (SB) cannot check because they face a bet (BB)
    Action check_action;
    check_action.type = Action::Type::CHECK;
    check_action.player_index = 0; // Player 0 (SB) acts
    // The function throws std::runtime_error, so the test should expect that.
    EXPECT_THROW(state_hu.apply_action(check_action), std::runtime_error);
}

TEST_F(GameStateTest, ApplyActionBBRaiseCall) {
    // SB calls
    Action sb_call;
    sb_call.type = Action::Type::CALL;
    sb_call.player_index = 0; // Player 0 (SB) acts
    state_hu.apply_action(sb_call);
    ASSERT_EQ(state_hu.get_current_player(), 1); // BB's turn

    // BB raises
    Action bb_raise;
    bb_raise.type = Action::Type::RAISE;
    bb_raise.amount = 8; // BB raises to 8 total
    bb_raise.player_index = 1; // Player 1 (BB) acts
    state_hu.apply_action(bb_raise);
    ASSERT_EQ(state_hu.get_current_player(), 0); // SB's turn
    ASSERT_EQ(state_hu.get_player_stacks()[1], 92); // BB started 98, bet 8 total (posted 2, added 6) -> 98-6=92
    ASSERT_EQ(state_hu.get_bet_this_round(1), 8);
    ASSERT_EQ(state_hu.get_amount_to_call(0), 6); // SB needs 6 more

    // SB calls
    Action sb_call_2;
    sb_call_2.type = Action::Type::CALL;
    sb_call_2.player_index = 0; // Player 0 (SB) acts
    state_hu.apply_action(sb_call_2);

    // Round should be over, street should advance
    EXPECT_EQ(state_hu.get_current_street(), Street::FLOP); // Check if round ended and advanced
    EXPECT_EQ(state_hu.get_player_stacks()[0], 92); // SB started 98, called 6 -> 98-6=92
    // Bets should be reset for the new street
    EXPECT_EQ(state_hu.get_bet_this_round(0), 0);
    EXPECT_EQ(state_hu.get_bet_this_round(1), 0);
    // Check pot size after round ends
    EXPECT_EQ(state_hu.get_pot_size(), 16); // 8 + 8 collected
}


// TODO: Add tests for postflop streets, all-in scenarios

// --- Multiway Tests ---

TEST(GameStateMultiwayTest, InitialState3Way) {
    // 3 players, 100 stack, button pos 0, ante 0
    GameState state_3way(3, 100, 0, 0);
    // Expected: P0=BTN, P1=SB(1), P2=BB(2)
    // Preflop action starts with P0 (UTG = player after BB)
    EXPECT_EQ(state_3way.get_num_players(), 3);
    EXPECT_EQ(state_3way.get_button_position(), 0);
    EXPECT_EQ(state_3way.get_player_stacks()[0], 100); // BTN stack unchanged
    EXPECT_EQ(state_3way.get_player_stacks()[1], 99);  // SB posted 1
    EXPECT_EQ(state_3way.get_player_stacks()[2], 98);  // BB posted 2
    EXPECT_EQ(state_3way.get_bet_this_round(0), 0);
    EXPECT_EQ(state_3way.get_bet_this_round(1), 1);
    EXPECT_EQ(state_3way.get_bet_this_round(2), 2);
    EXPECT_EQ(state_3way.get_current_player(), 0); // UTG (P0) acts first preflop
    EXPECT_EQ(state_3way.get_amount_to_call(0), 2); // BTN needs 2 to call BB
    EXPECT_EQ(state_3way.get_amount_to_call(1), 1); // SB needs 1 more to call BB
    EXPECT_EQ(state_3way.get_amount_to_call(2), 0); // BB needs 0 more
}

TEST(GameStateMultiwayTest, PostflopFirstPlayer3Way) {
    // 3 players, 100 stack, button pos 1, ante 0
    GameState state_3way(3, 100, 0, 1);
    // Expected: P1=BTN, P2=SB(1), P0=BB(2)
    // Preflop action starts with P1 (UTG = player after BB = BTN)
    EXPECT_EQ(state_3way.get_current_player(), 1);

    // Simulate actions to reach flop (e.g., BTN calls, SB calls, BB checks)
    Action call_action; call_action.type = Action::Type::CALL;
    Action check_action; check_action.type = Action::Type::CHECK;

    call_action.player_index = 1; // BTN calls
    state_3way.apply_action(call_action);
    ASSERT_EQ(state_3way.get_current_player(), 2); // SB's turn

    call_action.player_index = 2; // SB calls
    state_3way.apply_action(call_action);
    ASSERT_EQ(state_3way.get_current_player(), 0); // BB's turn

    check_action.player_index = 0; // BB checks
    state_3way.apply_action(check_action);

    // Should now be Flop street
    ASSERT_EQ(state_3way.get_current_street(), Street::FLOP);
    // First player to act postflop is SB (player after button)
    EXPECT_EQ(state_3way.get_current_player(), 2); // P2 is SB
}

TEST(GameStateMultiwayTest, PostflopCheckAroundEndsRound3Way) {
    // 3 players, 100 stack, button pos 2, ante 0
    GameState state_3way(3, 100, 0, 2);
    // Expected: P2=BTN, P0=SB(1), P1=BB(2)
    // Preflop: UTG=P2(BTN) -> SB=P0 -> BB=P1
    EXPECT_EQ(state_3way.get_current_player(), 2);

    // Simulate actions to reach flop (e.g., BTN calls, SB calls, BB checks)
    Action call_action; call_action.type = Action::Type::CALL;
    Action check_action; check_action.type = Action::Type::CHECK;

    call_action.player_index = 2; // BTN calls
    state_3way.apply_action(call_action);
    call_action.player_index = 0; // SB calls
    state_3way.apply_action(call_action);
    check_action.player_index = 1; // BB checks
    state_3way.apply_action(check_action);

    // Should be Flop, SB (P0) to act
    ASSERT_EQ(state_3way.get_current_street(), Street::FLOP);
    ASSERT_EQ(state_3way.get_current_player(), 0); // P0 is SB

    // Simulate check around on flop
    check_action.player_index = 0; // SB checks
    state_3way.apply_action(check_action);
    ASSERT_EQ(state_3way.get_current_player(), 1); // BB's turn

    check_action.player_index = 1; // BB checks
    state_3way.apply_action(check_action);
    ASSERT_EQ(state_3way.get_current_player(), 2); // BTN's turn

    check_action.player_index = 2; // BTN checks
    state_3way.apply_action(check_action);

    // Round should end, street should advance to Turn
    EXPECT_EQ(state_3way.get_current_street(), Street::TURN);
    // First player to act on Turn is SB (P0)
    EXPECT_EQ(state_3way.get_current_player(), 0);
}


} // namespace gto_solver
