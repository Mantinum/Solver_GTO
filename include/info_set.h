#ifndef GTO_SOLVER_INFO_SET_H
#define GTO_SOLVER_INFO_SET_H

#include "game_state.h" // Corrected include
#include <string>
#include <vector>
#include <functional> // For std::hash

#include "game_state.h" // Include full GameState definition here now

namespace gto_solver {

// Represents the information available to a player at a decision point.
// Used as a key to store regrets and strategies.
class InfoSet {
public:
    // Constructor for use during CFR traversal (uses current state)
    InfoSet(const GameState& current_state, int player_index);

    // Constructor for use during extraction (specify hand and history)
    InfoSet(const std::vector<Card>& private_hand, const std::string& action_history, const GameState& state_for_context, int player_index);

    // Getters
    // const std::vector<Card>& get_private_hand() const; // Can be derived from key if needed
    // const std::string& get_action_history() const; // Can be derived from key if needed

    // Generate a unique string key for this infoset (useful for map keys)
    const std::string& get_key() const; // Key is now generated at construction

    // Allow setting the hand after construction (for extraction)
    // This will regenerate the key based on the new hand.
    void set_hand(const std::vector<Card>& hand);

    // Equality operator for map comparisons (compares keys)
    bool operator==(const InfoSet& other) const;

private:
    // Store components needed to regenerate the key when hand changes
    std::vector<Card> private_hand_; // Store the hand
    std::string action_history_;     // Store the history used for the key
    Street street_;                  // Store the street used for the key
    std::vector<Card> board_;        // Store the board used for the key
    int player_index_;               // Store the player index used for the key
    std::string key_;                // The generated key

    // Helper function to generate the key from stored components
    void generate_key();
};

// Hash function for InfoSet to allow usage in std::unordered_map
struct InfoSetHash {
    std::size_t operator()(const InfoSet& info_set) const {
        // Hash the generated key directly
        return std::hash<std::string>{}(info_set.get_key());
    }
};


} // namespace gto_solver

#endif // GTO_SOLVER_INFO_SET_H
