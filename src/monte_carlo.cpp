#include "monte_carlo.h" // Corrected include
#include "hand_evaluator.h" // Corrected include
#include "game_state.h"   // Corrected include (for Card alias)

#include <vector>
#include <string>
#include <random>    // For random number generation
#include <algorithm> // For std::shuffle, std::remove, std::find
#include <stdexcept> // For std::invalid_argument
#include <chrono>    // For seeding RNG
#include <set>       // For efficient card removal check
#include "spdlog/spdlog.h" // Include spdlog

namespace gto_solver {

// --- MonteCarlo Implementation ---

MonteCarlo::MonteCarlo() : hand_evaluator_() {
    // Initialize RNG
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    rng_ = std::mt19937(seed);

    // Initialize the full deck
    const std::vector<char> ranks = {'2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K', 'A'};
    const std::vector<char> suits = {'c', 'd', 'h', 's'};
    full_deck_.reserve(52); // Reserve space
    for (char r : ranks) {
        for (char s : suits) {
            full_deck_.push_back(std::string(1, r) + s); // Create card string e.g., "Ah"
        }
    }
    spdlog::debug("MonteCarlo created and deck initialized.");
}

double MonteCarlo::estimate_equity(const std::vector<Card>& hero_hand,
                                   const std::vector<Card>& board,
                                   int num_simulations) {
    // --- Input Validation ---
    if (hero_hand.size() != 2) {
        throw std::invalid_argument("Hero hand must contain exactly 2 cards.");
    }
    if (board.size() > 5) {
        throw std::invalid_argument("Board cannot contain more than 5 cards.");
    }
    if (num_simulations <= 0) {
        return 0.0; // Or throw? Return 0 for now.
    }

    // --- Prepare Deck ---
    std::vector<Card> current_deck = full_deck_;
    std::set<Card> cards_to_remove(hero_hand.begin(), hero_hand.end());
    cards_to_remove.insert(board.begin(), board.end());

    // Remove known cards from the deck efficiently
    current_deck.erase(std::remove_if(current_deck.begin(), current_deck.end(),
                                      [&cards_to_remove](const Card& card) {
                                          return cards_to_remove.count(card);
                                      }),
                       current_deck.end());

    if (current_deck.size() < 2 + (5 - board.size())) {
         spdlog::error("Not enough cards remaining in deck ({}) to run simulation after removing hero ({}) and board ({}) cards.",
                       current_deck.size(), hero_hand.size(), board.size());
         return 0.0; // Cannot run simulation
    }


    // --- Simulation Loop ---
    int wins = 0;
    int ties = 0;

    for (int i = 0; i < num_simulations; ++i) {
        std::shuffle(current_deck.begin(), current_deck.end(), rng_);

        // Deal opponent hand and remaining board cards
        std::vector<Card> opponent_hand;
        opponent_hand.push_back(current_deck[0]);
        opponent_hand.push_back(current_deck[1]);

        std::vector<Card> current_sim_board = board;
        int cards_needed = 5 - board.size();
        for (int j = 0; j < cards_needed; ++j) {
            current_sim_board.push_back(current_deck[2 + j]); // Start from index 2
        }

        // Evaluate hands
        int hero_rank = hand_evaluator_.evaluate_7_card_hand(hero_hand, current_sim_board);
        int villain_rank = hand_evaluator_.evaluate_7_card_hand(opponent_hand, current_sim_board);

        // Update counters
        if (hero_rank < villain_rank) { // Lower rank is better
            wins++;
        } else if (hero_rank == villain_rank) {
            ties++;
        }
        // No need for loss counter
    }

    // Calculate equity
    double equity = (static_cast<double>(wins) + 0.5 * static_cast<double>(ties)) / static_cast<double>(num_simulations);
    return equity;
}

} // namespace gto_solver
