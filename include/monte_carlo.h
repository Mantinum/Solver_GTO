
#ifndef GTO_SOLVER_MONTE_CARLO_H
#define GTO_SOLVER_MONTE_CARLO_H

#include <vector>
#include <string> // Include string for std::string
#include <random> // For std::mt19937
#include "game_state.h" // Include for Card = std::string alias
#include "hand_evaluator.h" // Include HandEvaluator

namespace gto_solver {

class MonteCarlo {
public:
    MonteCarlo();

    /**
     * @brief Estimates the equity of a hero hand against a random opponent hand given a board state.
     *
     * Performs a Monte Carlo simulation by dealing random opponent hands and remaining community cards.
     *
     * @param hero_hand The hero's 2 private cards.
     * @param board The current community cards (can be empty, 3, or 4 cards).
     * @param num_simulations The number of random matchups to simulate.
     * @return The estimated equity (win probability + 0.5 * tie probability) between 0.0 and 1.0.
     */
    double estimate_equity(const std::vector<Card>& hero_hand,
                           const std::vector<Card>& board,
                           int num_simulations);

private:
    HandEvaluator hand_evaluator_; // Hand evaluator instance
    std::mt19937 rng_;             // Random number generator
    std::vector<Card> full_deck_;  // Store the full deck for efficiency
};

} // namespace gto_solver

#endif // GTO_SOLVER_MONTE_CARLO_H
