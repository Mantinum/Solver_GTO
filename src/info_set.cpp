#include "info_set.h" // Corrected include
#include <sstream>
#include <algorithm> // For std::sort

namespace gto_solver {

InfoSet::InfoSet(const std::vector<Card>& private_hand, const std::string& action_history)
    : private_hand_(private_hand), action_history_(action_history), key_("") { // Initialize key_ as empty
    // Ensure hand is sorted for consistent representation within the key
    std::sort(private_hand_.begin(), private_hand_.end());
    // Key generation is now deferred to get_key()
}

const std::vector<Card>& InfoSet::get_private_hand() const {
    return private_hand_;
}

const std::string& InfoSet::get_action_history() const {
    return action_history_;
}

// Now takes player_index and generates key on demand if not cached
std::string InfoSet::get_key(int player_index) const {
    if (key_.empty()) { // Generate key only if not already generated
        generate_key(player_index);
    }
    return key_;
}

bool InfoSet::operator==(const InfoSet& other) const {
    // Optimization: Compare keys first as they should be unique if generated correctly
    if (key_ != other.key_) {
        return false;
    }
    // Fallback: Full comparison if keys match (should ideally not happen with good key generation)
    // Note: Equality comparison might need adjustment if keys are generated lazily
    // For now, assume get_key() has been called on both if comparing non-default constructed InfoSets.
    return private_hand_ == other.private_hand_ && action_history_ == other.action_history_;
}

// Now takes player_index
void InfoSet::generate_key(int player_index) const {
    std::stringstream ss;
    // Add player index prefix for uniqueness across positions
    ss << "P" << player_index << ":";
    // Concatenate sorted hand cards
    for (const auto& card : private_hand_) {
        ss << card;
    }
    ss << "|"; // Separator
    ss << action_history_;
    key_ = ss.str(); // Assign to mutable member
}

} // namespace gto_solver
