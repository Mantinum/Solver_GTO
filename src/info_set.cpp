#include "info_set.h" // Corrected include
#include <sstream>
#include <algorithm> // For std::sort

namespace gto_solver {

InfoSet::InfoSet(const std::vector<Card>& private_hand, const std::string& action_history)
    : private_hand_(private_hand), action_history_(action_history) {
    // Ensure hand is sorted for consistent key generation
    std::sort(private_hand_.begin(), private_hand_.end());
    generate_key();
}

const std::vector<Card>& InfoSet::get_private_hand() const {
    return private_hand_;
}

const std::string& InfoSet::get_action_history() const {
    return action_history_;
}

std::string InfoSet::get_key() const {
    return key_;
}

bool InfoSet::operator==(const InfoSet& other) const {
    // Optimization: Compare keys first as they should be unique if generated correctly
    if (key_ != other.key_) {
        return false;
    }
    // Fallback: Full comparison if keys match (should ideally not happen with good key generation)
    return private_hand_ == other.private_hand_ && action_history_ == other.action_history_;
}

void InfoSet::generate_key() {
    std::stringstream ss;
    // Concatenate sorted hand cards
    for (const auto& card : private_hand_) {
        ss << card;
    }
    ss << "|"; // Separator
    ss << action_history_;
    key_ = ss.str();
}

} // namespace gto_solver
