#include "hand_evaluator.h"

#include <string>
#include <vector>
#include <stdexcept> // For potential errors during conversion
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cmath>
#include "phevaluator/phevaluator.h" // Include PokerHandEvaluator header
#include "phevaluator/card.h"        // Include Card definition
#include "phevaluator/rank.h"        // Include Rank definition

namespace gto_solver {

// Define an invalid card index outside the valid 0-51 range
const int INVALID_PHE_CARD_INDEX = -1; // Use -1 as invalid for phevaluator

HandEvaluator::HandEvaluator() {
    // std::cout << "HandEvaluator created" << std::endl; // Replaced
    spdlog::debug("HandEvaluator created");
    // phevaluator uses free functions, no member initialization needed
}

// Helper function to convert our string card representation ("As", "Td")
// to PokerHandEvaluator's internal card index (0-51).
int convert_string_to_phe_card_index(const std::string& card_str) {
     if (card_str.length() != 2) {
         spdlog::error("Invalid card string format: {}", card_str);
         return INVALID_PHE_CARD_INDEX;
     }
     char rank_char = card_str[0];
     char suit_char = card_str[1];

    int rank = -1; // 0=2, 1=3, ..., 8=T, 9=J, 10=Q, 11=K, 12=A
    if (rank_char >= '2' && rank_char <= '9') rank = rank_char - '2';
    else if (rank_char == 'T') rank = 8;
    else if (rank_char == 'J') rank = 9;
    else if (rank_char == 'Q') rank = 10;
    else if (rank_char == 'K') rank = 11;
    else if (rank_char == 'A') rank = 12;

     int suit = -1; // 0=c, 1=d, 2=h, 3=s (PokerHandEvaluator suit order)
     if (suit_char == 'c') suit = 0;      // Clubs
     else if (suit_char == 'd') suit = 1; // Diamonds
     else if (suit_char == 'h') suit = 2; // Hearts
     else if (suit_char == 's') suit = 3; // Spades

     if (rank == -1 || suit == -1) {
          spdlog::error("Invalid rank or suit in card string: {}", card_str);
          return INVALID_PHE_CARD_INDEX;
     }

     // PokerHandEvaluator uses an index from 0 to 51: index = rank * 4 + suit
     int card_index = rank * 4 + suit;
     if (card_index < 0 || card_index >= 52) { // Check bounds
         spdlog::error("Calculated card index out of range: {}", card_index);
         return INVALID_PHE_CARD_INDEX;
     }
     return card_index;
}

// Helper function for debugging tests
void log_hand_details(const std::string& hand, int v1, int v2, bool suited, bool connector, int score) {
     spdlog::info("Eval Preflop Hand: '{}', v1={}, v2={}, suited={}, connector={}, score={}", hand, v1, v2, suited, connector, score);
}

// Placeholder evaluation - Returns a score, higher is better.
// This does NOT represent a standard 7-card evaluation score yet.
// It's just a relative ranking for preflop pairs and AK/AQ.
int HandEvaluator::evaluate_preflop_hand(const std::string& hand) { // Renamed
    if (hand.length() != 4) return 0; // Expecting 4 chars like "AsKc"

    char r1 = hand[0];
    char s1 = hand[1];
    char r2 = hand[2];
    char s2 = hand[3];

    // Basic ranking for pairs and high cards (preflop strength approximation)
    auto get_rank_value = [](char r) {
        if (r >= '2' && r <= '9') return r - '0';
        if (r == 'T') return 10;
        if (r == 'J') return 11;
        if (r == 'Q') return 12;
        if (r == 'K') return 13;
        if (r == 'A') return 14;
        return 0;
    };

    int v1 = get_rank_value(r1);
    int v2 = get_rank_value(r2);

    if (v1 == v2) { // Pair
        return 1000 + v1 * 10; // Higher pairs get higher score
    }

    // Suitedness doesn't affect preflop rank value in this simple model
    // Assign score based on highest card, then second highest
    int score = std::max(v1, v2) * 10 + std::min(v1, v2);

    // Give a bonus for connectors/suitedness (very basic)
    bool suited = (s1 == s2);
    bool connector = (abs(v1 - v2) == 1 || (std::max(v1,v2) == 14 && std::min(v1,v2) == 2)); // Ace-low straight possibility

    if (suited) score += 5;
    if (connector) score += 2;

    log_hand_details(hand, v1, v2, suited, connector, score); // Uncommented logging
    return score;
}

// using a library like PokerHandEvaluator.
int HandEvaluator::evaluate_7_card_hand(const std::vector<Card>& private_cards, const std::vector<Card>& community_cards) {
    // --- Input Validation ---
    if (private_cards.size() != 2) {
         spdlog::error("Invalid private card count for 7-card evaluation: {}", private_cards.size());
         return 9999; // Return worst rank on error
    }
    // IMPORTANT: This function assumes a complete 5-card board for 7-card evaluation.
    // Calls from CFR engine must ensure this condition or handle the error return value.
    if (community_cards.size() != 5) {
         spdlog::warn("evaluate_7_card_hand called with incomplete board ({} cards). Returning worst rank.", community_cards.size());
         return 9999; // Return worst rank if board is not complete
    }

    // --- Combine and Convert Cards ---
    int card_indices[7];
    card_indices[0] = convert_string_to_phe_card_index(private_cards[0]);
    card_indices[1] = convert_string_to_phe_card_index(private_cards[1]);
    for (size_t i = 0; i < 5; ++i) {
        card_indices[i + 2] = convert_string_to_phe_card_index(community_cards[i]);
    }

    // Check for conversion errors
    for (int i = 0; i < 7; ++i) {
        if (card_indices[i] == INVALID_PHE_CARD_INDEX) {
            // Error already logged in convert function
            return 9999; // Return worst rank on conversion error
        }
    }

    // Evaluate the hand using the PokerHandEvaluator function
    // Note: The C interface expects plain integers.
    int rank_value = evaluate_7cards(
        card_indices[0], card_indices[1], card_indices[2], card_indices[3],
        card_indices[4], card_indices[5], card_indices[6]
    );

    // The returned value is an int representing the hand rank (1 is best, 7462 is worst).
    // We can return this directly.
    return rank_value;
}


} // namespace gto_solver
