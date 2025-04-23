#include "info_set.h" // Corrected include
#include <sstream>
#include <algorithm> // For std::sort
#include <vector>    // For std::vector used in constructor

namespace gto_solver {

// Constructor for CFR traversal (uses current state)
InfoSet::InfoSet(const GameState& current_state, int player_index) {
    std::stringstream ss;

    // 1. Player Index
    ss << "P" << player_index << ":";

    // 2. Private Hand (Sorted)
    std::vector<Card> private_hand = current_state.get_player_hand(player_index);
    std::sort(private_hand.begin(), private_hand.end());
    for (const auto& card : private_hand) { ss << card; }
    ss << "|";

    // 3. Street
    ss << static_cast<int>(current_state.get_current_street()) << "|"; // Add street enum value

    // 4. Community Cards (Board - Sorted, with placeholders)
    std::vector<Card> board = current_state.get_community_cards();
    std::sort(board.begin(), board.end());
    for (const auto& card : board) { ss << card; }
    // Add placeholders if board is not complete (e.g., flop, turn)
    for (size_t i = board.size(); i < 5; ++i) { ss << "--"; } // Placeholder for missing cards
    ss << "|";

    // 5. Action History
    ss << current_state.get_history_string(); // Use the history from GameState

    key_ = ss.str(); // Assign the generated key
}

// Constructor for extraction (specify hand and history, use context state for street/board)
InfoSet::InfoSet(const std::vector<Card>& private_hand, const std::string& action_history, const GameState& state_for_context, int player_index) {
    std::stringstream ss;

    // 1. Player Index
    ss << "P" << player_index << ":";

    // 2. Private Hand (Sorted) - Use the provided hand
    std::vector<Card> sorted_hand = private_hand;
    std::sort(sorted_hand.begin(), sorted_hand.end());
    for (const auto& card : sorted_hand) { ss << card; }
    ss << "|";

    // 3. Street - Use from context state
    ss << static_cast<int>(state_for_context.get_current_street()) << "|";

    // 4. Community Cards (Board - Sorted, with placeholders) - Use from context state
    std::vector<Card> board = state_for_context.get_community_cards();
    std::sort(board.begin(), board.end());
    for (const auto& card : board) { ss << card; }
    for (size_t i = board.size(); i < 5; ++i) { ss << "--"; }
    ss << "|";

    // 5. Action History - Use the provided history
    ss << action_history;

    key_ = ss.str(); // Assign the generated key
}


// get_key() simply returns the cached key
const std::string& InfoSet::get_key() const {
    return key_;
}

// operator== compares the generated keys
bool InfoSet::operator==(const InfoSet& other) const {
    return key_ == other.key_;
}

} // namespace gto_solver
