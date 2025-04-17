#ifndef GTO_SOLVER_INFO_SET_H
#define GTO_SOLVER_INFO_SET_H

#include "game_state.h" // Corrected include
#include <string>
#include <vector>
#include <functional> // For std::hash

namespace gto_solver {

// Represents the information available to a player at a decision point.
// Used as a key to store regrets and strategies.
class InfoSet {
public:
    InfoSet(const std::vector<Card>& private_hand, const std::string& action_history);

    // Getters
    const std::vector<Card>& get_private_hand() const;
    const std::string& get_action_history() const;

    // Generate a unique string key for this infoset (useful for map keys)
    std::string get_key() const;

    // Equality operator for map comparisons
    bool operator==(const InfoSet& other) const;

private:
    std::vector<Card> private_hand_;
    std::string action_history_; // String representation of public actions
    std::string key_; // Cached key

    void generate_key();
};

// Hash function for InfoSet to allow usage in std::unordered_map
struct InfoSetHash {
    std::size_t operator()(const InfoSet& info_set) const {
        // Combine hashes of hand and history
        std::size_t h1 = 0;
        // Simple hash for hand (consider a better one for performance)
        for(const auto& card : info_set.get_private_hand()) {
             h1 ^= std::hash<std::string>{}(card) + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        }
        std::size_t h2 = std::hash<std::string>{}(info_set.get_action_history());
        return h1 ^ (h2 << 1); // Combine hashes
    }
};

} // namespace gto_solver

#endif // GTO_SOLVER_INFO_SET_H
