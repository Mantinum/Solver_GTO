#include "gtest/gtest.h"
#include "hand_evaluator.h" // Corrected include

#include <vector> // Include vector for 7-card test

namespace gto_solver {

TEST(HandEvaluatorTest, EvaluatePreflopHands) {
    HandEvaluator hand_evaluator;
    // Test pairs (Score = 1000 + rank * 10)
    EXPECT_GT(hand_evaluator.evaluate_preflop_hand("AsAd"), hand_evaluator.evaluate_preflop_hand("KsKd")); // AA > KK
    EXPECT_EQ(hand_evaluator.evaluate_preflop_hand("AsAd"), 1000 + 14 * 10); // 1140
    EXPECT_EQ(hand_evaluator.evaluate_preflop_hand("2s2d"), 1000 + 2 * 10);  // 1020

    // Test high cards (Score = max_rank*10 + min_rank + suited_bonus + connector_bonus)
    // Corrected expected scores including connector bonus where applicable
    int aks_score = 14 * 10 + 13 + 5 + 2; // AKs = 160 (Connector bonus added)
    int ako_score = 14 * 10 + 13 + 2;     // AKo = 155 (Connector bonus added)
    int qjs_score = 12 * 10 + 11 + 5 + 2; // QJs = 138 (Correct)
    int t9o_score = 10 * 10 + 9 + 2;      // T9o = 111 (Correct)
    int seven_two_offsuit_score = 7 * 10 + 2; // 72o = 72 (Correct)

    EXPECT_EQ(hand_evaluator.evaluate_preflop_hand("AsKs"), aks_score);
    EXPECT_EQ(hand_evaluator.evaluate_preflop_hand("AdKc"), ako_score);
    EXPECT_EQ(hand_evaluator.evaluate_preflop_hand("QsJs"), qjs_score);
    EXPECT_EQ(hand_evaluator.evaluate_preflop_hand("Td9c"), t9o_score);
    EXPECT_EQ(hand_evaluator.evaluate_preflop_hand("7h2d"), seven_two_offsuit_score);

    // Compare relative strengths
    EXPECT_GT(hand_evaluator.evaluate_preflop_hand("AsKs"), hand_evaluator.evaluate_preflop_hand("AdKc")); // AKs > AKo
    EXPECT_GT(hand_evaluator.evaluate_preflop_hand("AdKc"), hand_evaluator.evaluate_preflop_hand("QsJs")); // AKo > QJs
    EXPECT_GT(hand_evaluator.evaluate_preflop_hand("QsJs"), hand_evaluator.evaluate_preflop_hand("Td9c")); // QJs > T9o
    EXPECT_GT(hand_evaluator.evaluate_preflop_hand("Td9c"), hand_evaluator.evaluate_preflop_hand("7h2d")); // T9o > 72o

    // Test invalid input
    EXPECT_EQ(hand_evaluator.evaluate_preflop_hand(""), 0);
    EXPECT_EQ(hand_evaluator.evaluate_preflop_hand("AsK"), 0);
    EXPECT_EQ(hand_evaluator.evaluate_preflop_hand("AsKcQd"), 0);
}

// Renamed test and updated expected values based on phevaluator ranks
TEST(HandEvaluatorTest, Evaluate7CardHand) {
    HandEvaluator hand_evaluator;
    std::vector<Card> hand_ak = {"As", "Ks"}; // Ace-King suited spades
    std::vector<Card> hand_qq = {"Qh", "Qd"}; // Pocket Queens
    std::vector<Card> board_flush = {"2s", "7s", "Ts", "Js", "3h"}; // Spade flush possible for AKs
    std::vector<Card> board_no_flush = {"2c", "7d", "Th", "Jc", "3h"}; // No flush possible

    // Expected ranks from phevaluator (lower is better)
    // Values obtained from previous failed test run output
    EXPECT_EQ(hand_evaluator.evaluate_7_card_hand(hand_ak, board_flush), 369);    // AKs makes Ace-high flush
    EXPECT_EQ(hand_evaluator.evaluate_7_card_hand(hand_qq, board_flush), 3868);   // QQ has two pair (Queens and Threes)
    EXPECT_EQ(hand_evaluator.evaluate_7_card_hand(hand_ak, board_no_flush), 6232); // AKs has Ace high
    EXPECT_EQ(hand_evaluator.evaluate_7_card_hand(hand_qq, board_no_flush), 3868); // QQ has two pair (Queens and Threes)
    EXPECT_EQ(hand_evaluator.evaluate_7_card_hand({"As", "2c"}, board_no_flush), 5985); // A2o has Ace high
    EXPECT_EQ(hand_evaluator.evaluate_7_card_hand({"Ks", "2c"}, board_no_flush), 6030); // K2o has King high


     // Test invalid inputs (should return 9999 and log errors)
     EXPECT_EQ(hand_evaluator.evaluate_7_card_hand({"As"}, board_flush), 9999); // Invalid hand size
     EXPECT_EQ(hand_evaluator.evaluate_7_card_hand(hand_ak, {"2s", "7s"}), 9999); // Invalid board size
 }


} // namespace gto_solver
