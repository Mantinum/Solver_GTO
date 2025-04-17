#ifndef GTO_SOLVER_HAND_EVALUATOR_H
#define GTO_SOLVER_HAND_EVALUATOR_H

#include <string>
#include <vector>
#include "game_state.h" // Needed for Card definition (std::string)
#include "phevaluator/phevaluator.h" // Include PokerHandEvaluator header

namespace gto_solver {

class HandEvaluator {
public:
    HandEvaluator(); // Constructor might not be needed anymore

    // Evaluates a 2-card hand preflop (relative strength, placeholder)
    // This function remains unchanged for now.
    int evaluate_preflop_hand(const std::string& hand);

    // Evaluates a 7-card hand (2 private + 5 community) using PokerHandEvaluator
    // Returns a numerical score where LOWER is better (standard poker rank, 1 = Royal Flush).
    int evaluate_7_card_hand(const std::vector<Card>& private_cards, const std::vector<Card>& community_cards);

private:
    // PokerHandEvaluator functions are typically static or free functions,
    // so no member instance is needed.
};

} // namespace gto_solver

#endif // GTO_SOLVER_HAND_EVALUATOR_H
