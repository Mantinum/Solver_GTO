#include "info_set.h" // Corrected include
#include <sstream>
#include <algorithm> // For std::sort
#include <vector>    // For std::vector used in constructor
#include "spdlog/fmt/bundled/format.h" // Include fmt for logging vectors

namespace gto_solver {

// Private helper to generate the key string
void InfoSet::generate_key() {
    std::stringstream ss;
    // 1. Player Index
    ss << "P" << player_index_ << ":";
    // 2. Private Hand (Sorted)
    std::vector<Card> sorted_hand = private_hand_; // Copy to sort
    std::sort(sorted_hand.begin(), sorted_hand.end());
    for (const auto& card : sorted_hand) { ss << card; }
    ss << "|";
    // 3. Street
    ss << static_cast<int>(street_) << "|";
    // 4. Community Cards (Board - Size + Sorted, with placeholders)
    std::vector<Card> sorted_board = board_; // Copy to sort
    std::sort(sorted_board.begin(), sorted_board.end());
    ss << sorted_board.size();
    for (const auto& card : sorted_board) { ss << card; }
    for (size_t i = sorted_board.size(); i < 5; ++i) { ss << "--"; }
    ss << "|";
    // 5. Action History
    ss << action_history_;

    /* --- DEBUG ---
    // Temporarily log components just before assigning key_
    // Only log if history is not empty to focus on RFI extraction case from main.cpp
    if (!action_history_.empty()) {
         // Format hand for logging
         std::string hand_str_log = "";
         std::vector<Card> sorted_hand_log = private_hand_; // Copy to sort locally
         std::sort(sorted_hand_log.begin(), sorted_hand_log.end());
         for(const auto& c : sorted_hand_log) hand_str_log += c;

        // Use simpler string concatenation for logging to avoid potential linter issues with fmt
        std::stringstream log_ss;
        log_ss << "generate_key DEBUG: P=" << player_index_
               << ", Hand=" << hand_str_log
               << ", Street=" << static_cast<int>(street_)
               << ", BoardSize=" << board_.size()
               << ", History='" << action_history_ << "'";
        // spdlog::info(log_ss.str()); // Commented out due to linter issue
    }
    --- END DEBUG --- */

    key_ = ss.str(); // Assign the generated key
}


// Constructor for CFR traversal (uses current state)
InfoSet::InfoSet(const GameState& current_state, int player_index)
    : player_index_(player_index),
      private_hand_(current_state.get_player_hand(player_index)),
      action_history_(current_state.get_history_string()),
      street_(current_state.get_current_street()),
      board_(current_state.get_community_cards())
{
    generate_key(); // Generate the key upon construction
}

// Constructor for extraction (specify hand and history)
InfoSet::InfoSet(const std::vector<Card>& private_hand, const std::string& action_history, const GameState& state_for_context, int player_index)
    : player_index_(player_index),
      private_hand_(private_hand), // Use provided hand
      action_history_(action_history), // Use provided history
      street_(state_for_context.get_current_street()), // Use context for street
      board_(state_for_context.get_community_cards()) // Use context for board
{
    generate_key(); // Generate the key upon construction
}


// get_key() simply returns the cached key
const std::string& InfoSet::get_key() const {
    return key_;
}

// set_hand regenerates the key
void InfoSet::set_hand(const std::vector<Card>& hand) {
    private_hand_ = hand; // Update the stored hand
    generate_key();       // Regenerate the key
}


// operator== compares the generated keys
bool InfoSet::operator==(const InfoSet& other) const {
    return key_ == other.key_;
}

} // namespace gto_solver
