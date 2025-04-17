#include "game_state.h" // Corrected include
#include <stdexcept> // For std::out_of_range
#include <numeric>   // For std::accumulate
#include <sstream>   // For std::stringstream
#include <algorithm> // For std::min, std::max
#include "spdlog/spdlog.h" // Include spdlog

namespace gto_solver {

// Constructor implementation
GameState::GameState(int num_players, int initial_stack, int ante_size, int button_position) // Added button_position
    : num_players_(num_players),
      button_position_(button_position), // Initialize button position
      current_player_index_(0), // Will be updated after blinds
      pot_size_(0),
      player_stacks_(num_players, initial_stack),
      bets_this_round_(num_players, 0),
      player_hands_(num_players), // Initialize empty hands
      current_street_(Street::PREFLOP),
      is_game_over_(false),
      last_raise_size_(0), // Initialize last_raise_size_
      aggressor_this_round_(-1), // No aggressor initially
      actions_this_round_(0),   // No actions initially - REMOVED (replaced by player_acted_this_sequence_)
      player_folded_(num_players, false), // Initialize all players as not folded
      player_all_in_(num_players, false), // Initialize all players as not all-in
      player_contributions_(num_players, 0), // Initialize contributions to 0
      ante_size_(ante_size),
      player_acted_this_sequence_(num_players, false) // Initialize acted flag
{
    if (num_players <= 1) {
         throw std::invalid_argument("Number of players must be greater than 1.");
    }
     if (button_position < 0 || button_position >= num_players) {
         throw std::invalid_argument("Invalid button position.");
     }

    // Determine SB and BB positions based on button
    int sb_index = -1;
    int bb_index = -1;
    if (num_players == 2) {
        // Special HU case: Button is SB
        sb_index = button_position_;
        bb_index = (button_position_ + 1) % num_players_;
    } else {
        // Standard case for 3+ players
        sb_index = (button_position_ + 1) % num_players_;
        bb_index = (button_position_ + 2) % num_players_;
    }


    // TODO: Make blind amounts configurable or dependent on game type
    int sb_amount = 1;
    int bb_amount = 2;

    // Call the NEW post_antes_and_blinds with calculated indices
    post_antes_and_blinds(sb_index, bb_index, sb_amount, bb_amount);

    // Determine the first player to act preflop
    if (num_players == 2) {
        // In HU, SB (button) acts first preflop
        current_player_index_ = sb_index;
    } else {
        // In 3+ players, player after BB (UTG) acts first preflop
        current_player_index_ = (bb_index + 1) % num_players_;
    }
     // Ensure the first player to act is valid (not folded/all-in, though unlikely at start)
     int initial_player = current_player_index_;
     while(player_folded_[current_player_index_] || player_all_in_[current_player_index_]) {
         current_player_index_ = (current_player_index_ + 1) % num_players_;
         if (current_player_index_ == initial_player) {
             spdlog::warn("Could not find a valid starting player after posting blinds.");
             is_game_over_ = true; // Should not happen
             break;
         }
     }
}

// Getter implementations
int GameState::get_num_players() const {
    return num_players_;
}

int GameState::get_button_position() const { // Added implementation
    return button_position_;
}

int GameState::get_current_player() const {
    return current_player_index_;
}

int GameState::get_pot_size() const {
    // Calculate current pot size including bets this round that haven't been collected yet
    return pot_size_ + std::accumulate(bets_this_round_.begin(), bets_this_round_.end(), 0);
}

const std::vector<int>& GameState::get_player_stacks() const {
    return player_stacks_;
}

const std::vector<Card>& GameState::get_player_hand(int player_index) const {
    if (player_index < 0 || player_index >= num_players_) {
        throw std::out_of_range("Invalid player index for get_player_hand.");
    }
    return player_hands_[player_index];
}

const std::vector<Card>& GameState::get_community_cards() const {
    return community_cards_;
}

Street GameState::get_current_street() const {
    return current_street_;
}

const std::vector<Action>& GameState::get_action_history() const {
    return action_history_;
}

bool GameState::is_terminal() const {
    if (is_game_over_) {
        return true;
    }

    int active_players = 0;
    // Count active players (not folded and have chips remaining)
    for (int i = 0; i < num_players_; ++i) {
        if (!player_folded_[i] && player_stacks_[i] > 0) {
            active_players++;
        }
    }
    // Terminal if only one player (or fewer) can still potentially act
    return active_players <= 1;

    // TODO: Add condition for showdown after river betting round completes naturally
}

int GameState::get_amount_to_call(int player_index) const {
     if (player_index < 0 || player_index >= num_players_) {
        throw std::out_of_range("Invalid player index for get_amount_to_call.");
    }
    // Player cannot call if folded or already all-in
    if (player_folded_[player_index] || player_all_in_[player_index]) {
        return 0;
    }

    int max_bet = 0;
    for(int bet : bets_this_round_) {
        max_bet = std::max(max_bet, bet);
    }
    int amount_needed = max_bet - bets_this_round_[player_index];
    // Cannot call more than remaining stack
    return std::min(amount_needed, player_stacks_[player_index]);
}

int GameState::get_bet_this_round(int player_index) const {
    if (player_index < 0 || player_index >= num_players_) {
        throw std::out_of_range("Invalid player index for get_bet_this_round.");
    }
    return bets_this_round_[player_index];
}

const std::vector<int>& GameState::get_bets_this_round() const {
    return bets_this_round_;
}

int GameState::get_last_raise_size() const {
    return last_raise_size_;
}

bool GameState::has_player_folded(int player_index) const {
    if (player_index < 0 || player_index >= num_players_) {
        throw std::out_of_range("Invalid player index for has_player_folded.");
    }
    return player_folded_[player_index];
}

bool GameState::is_player_all_in(int player_index) const {
     if (player_index < 0 || player_index >= num_players_) {
        throw std::out_of_range("Invalid player index for is_player_all_in.");
    }
    return player_all_in_[player_index];
}

int GameState::get_player_contribution(int player_index) const {
     if (player_index < 0 || player_index >= num_players_) {
        throw std::out_of_range("Invalid player index for get_player_contribution.");
    }
    return player_contributions_[player_index];
}


// Modifier implementations
void GameState::deal_hands(const std::vector<std::vector<Card>>& hands) {
    if (hands.size() != static_cast<size_t>(num_players_)) {
         throw std::invalid_argument("Number of hands dealt does not match number of players.");
    }
    player_hands_ = hands;
}

void GameState::deal_community_cards(const std::vector<Card>& cards) {
    community_cards_.insert(community_cards_.end(), cards.begin(), cards.end());
    // TODO: Validate number of cards based on street (e.g., 3 for flop, 1 for turn/river)
}

void GameState::apply_action(const Action& action) {
     // Ensure action has player index set correctly before calling this
     if (action.player_index != current_player_index_) {
         spdlog::error("Action applied by wrong player. Expected: {}, Got: {}", current_player_index_, action.player_index);
         throw std::logic_error("Action applied by wrong player.");
     }
     if (is_terminal()) {
         spdlog::warn("Attempted to apply action to a terminal state. History: {}", get_history_string());
         return; // Or throw, depending on desired behavior
     }
     if (player_folded_[current_player_index_] || (player_all_in_[current_player_index_] && get_amount_to_call(current_player_index_) == 0)) {
         // Explicitly cast vector<bool> access results to bool for formatting
         spdlog::error("Attempted action by inactive player {}. Folded: {} All-in: {}",
                       current_player_index_,
                       static_cast<bool>(player_folded_[current_player_index_]),
                       static_cast<bool>(player_all_in_[current_player_index_]));
         throw std::logic_error("Attempted action by inactive player.");
     }

    int amount_put_in_this_action = 0;

    if (action.type == Action::Type::FOLD) {
        player_folded_[current_player_index_] = true;
    } else if (action.type == Action::Type::CALL) {
        int amount_to_call = get_amount_to_call(current_player_index_);
        amount_put_in_this_action = amount_to_call; // Amount added to pot this action
        if (amount_put_in_this_action > player_stacks_[current_player_index_]) {
             spdlog::error("Internal error: Call amount {} exceeds player stack {}.", amount_put_in_this_action, player_stacks_[current_player_index_]);
             amount_put_in_this_action = player_stacks_[current_player_index_]; // Correct to all-in amount
        }
        player_stacks_[current_player_index_] -= amount_put_in_this_action;
        bets_this_round_[current_player_index_] += amount_put_in_this_action;
        player_contributions_[current_player_index_] += amount_put_in_this_action; // Update total contribution
        if (player_stacks_[current_player_index_] == 0) {
            player_all_in_[current_player_index_] = true;
        }
    } else if (action.type == Action::Type::RAISE || action.type == Action::Type::BET) {
        int amount_to_call = get_amount_to_call(current_player_index_);
        int total_bet_this_round = action.amount; // This is the TOTAL amount bet this round by the player

        // --- Basic Validation ---
        int min_raise_increment = (last_raise_size_ > 0) ? last_raise_size_ : (ante_size_ > 0 ? ante_size_ : 2); // Default to BB or Ante
        int min_legal_total_bet = bets_this_round_[current_player_index_] + amount_to_call + min_raise_increment;
        bool is_all_in_action = (total_bet_this_round == player_stacks_[current_player_index_] + bets_this_round_[current_player_index_]);

        if (total_bet_this_round < min_legal_total_bet && !is_all_in_action) {
             spdlog::warn("Illegal bet/raise size: {} < min {}. Clamping. History: {}", total_bet_this_round, min_legal_total_bet, get_history_string());
             total_bet_this_round = std::min(min_legal_total_bet, player_stacks_[current_player_index_] + bets_this_round_[current_player_index_]);
        }
        if (total_bet_this_round > player_stacks_[current_player_index_] + bets_this_round_[current_player_index_]) {
             spdlog::warn("Bet/raise size {} exceeds stack {}. Clamping to all-in. History: {}", total_bet_this_round, player_stacks_[current_player_index_], get_history_string());
             total_bet_this_round = player_stacks_[current_player_index_] + bets_this_round_[current_player_index_]; // Clamp to all-in
        }
        // --- End Validation ---

        amount_put_in_this_action = total_bet_this_round - bets_this_round_[current_player_index_];

        // Calculate the size of this specific bet/raise increment
        int previous_total_bet = bets_this_round_[current_player_index_] + amount_to_call; // Amount needed to just call
        int raise_increment = total_bet_this_round - previous_total_bet;
        last_raise_size_ = raise_increment; // Update the last raise size

        // Update aggressor and reset acted flags
        aggressor_this_round_ = current_player_index_;
        std::fill(player_acted_this_sequence_.begin(), player_acted_this_sequence_.end(), false); // Reset acted flags

        // Apply the bet/raise
        player_stacks_[current_player_index_] -= amount_put_in_this_action;
        bets_this_round_[current_player_index_] = total_bet_this_round;
        player_contributions_[current_player_index_] += amount_put_in_this_action; // Update total contribution
        if (player_stacks_[current_player_index_] == 0) {
            player_all_in_[current_player_index_] = true; // Mark if raise makes player all-in
        }

    } else if (action.type == Action::Type::CHECK) {
        int amount_to_call = get_amount_to_call(current_player_index_);
        if (amount_to_call > 0) {
            spdlog::error("Illegal check when facing a bet of {}. History: {}", amount_to_call, get_history_string());
            throw std::logic_error("Cannot check when facing a bet.");
        }
        // No change in stack or bet amount for check
    } else {
         throw std::invalid_argument("Unknown action type.");
    }

    // Mark the current player as having acted in this sequence
    player_acted_this_sequence_[current_player_index_] = true;
    // actions_this_round_++; // No longer needed

    // Store the action
    action_history_.push_back(action);

    // --- Check for end of betting round ---
    bool round_over = false;

    // Count players who are not folded and not all-in (can still make decisions)
    int players_can_decide = 0;
    for (int i = 0; i < num_players_; ++i) {
        if (!player_folded_[i] && !player_all_in_[i]) {
            players_can_decide++;
        }
    }

    // If only one player (or zero) can still make decisions, the betting round ends
    if (players_can_decide <= 1) {
        round_over = true;
    } else {
        // Check if the action is closed based on bets and number of actions
        int max_bet = 0;
        for(int bet : bets_this_round_) max_bet = std::max(max_bet, bet);
        bool bets_matched = true;
        int active_players_this_round = 0; // Players not folded
        for(int i=0; i<num_players_; ++i) {
            if (!player_folded_[i]) {
                active_players_this_round++;
                // Check if bets are matched among players who are not folded and not already all-in
                if (!player_all_in_[i] && bets_this_round_[i] != max_bet) {
                    bets_matched = false;
                }
            }
        }

        // Round ends if:
        // 1. Bets are matched by all active, non-all-in players
        // 2. AND EITHER:
        // Round ends if:
        // 1. Bets are matched by all active, non-all-in players
        // 2. AND All active, non-all-in players have acted in this betting sequence.
        // 3. OR Special HU Preflop BB check case.
        bool action_closed = false;
        if (bets_matched) {
            bool all_active_acted = true;
            for(int i=0; i<num_players_; ++i) {
                // Check only players who are still in the hand and not all-in
                if (!player_folded_[i] && !player_all_in_[i]) {
                    if (!player_acted_this_sequence_[i]) {
                        all_active_acted = false;
                        break;
                    }
                }
            }

            // Action is closed if bets are matched AND everyone who could act has acted.
            // Exception: If no aggression happened yet (e.g. checks around), action continues until someone bets/raises or street ends.
            // The check for `aggressor_this_round_ != -1` handles the case where the initial betting round starts (e.g. preflop blinds posted, or postflop checks)
            // We need to ensure at least one full rotation of checks or the initial bet/raise has been made and called around.
            // The `all_active_acted` flag covers this: once a bet/raise occurs, this flag resets, and action continues until it becomes true again.
            // If no bet/raise occurs (checks around), this flag becomes true after everyone checks.
            if (all_active_acted) {
                 bool aggression_occurred_this_street = (aggressor_this_round_ != -1);

                 // Special case: Preflop HU, BB check closes action if SB just called.
                 bool preflop_hu_bb_check_closes = (current_street_ == Street::PREFLOP &&
                                                    num_players_ == 2 &&
                                                    action.type == Action::Type::CHECK &&
                                                    current_player_index_ == ( (button_position_ + 2) % num_players_ ) /* BB index */ &&
                                                    !aggression_occurred_this_street); // Ensure no prior raise

                 // Action closes if:
                 // 1. Aggression occurred and everyone acted since.
                 // 2. No aggression occurred (checks around postflop) and everyone acted. (Corrected logic)
                 // 3. Special HU preflop BB check case.
                 // 4. Only one player can decide anyway.
                 if ( (aggression_occurred_this_street) || // If aggression happened, all_active_acted means action is closed
                      (!aggression_occurred_this_street && current_street_ != Street::PREFLOP) || // If no aggression postflop, all_active_acted means checks around, action closed
                      preflop_hu_bb_check_closes ||
                      players_can_decide <= 1 )
                 {
                      action_closed = true;
                 }
                 // Otherwise (e.g., preflop, no aggression yet, not the special check case), action continues.
            }
        }


        if (action_closed) {
            round_over = true;
        }
    }

    // --- Update State ---
    if (round_over) {
        // Collect bets into the main pot before advancing
        pot_size_ += std::accumulate(bets_this_round_.begin(), bets_this_round_.end(), 0);
        advance_to_next_street(); // This will reset bets_this_round_, aggressor, etc.
    } else {
        // Check if the game ended due to folds, even if round wasn't closed by betting
        int active_players_check = 0;
        for (int i = 0; i < num_players_; ++i) {
            if (!player_folded_[i]) active_players_check++;
        }
        if (active_players_check <= 1) {
            is_game_over_ = true;
            pot_size_ += std::accumulate(bets_this_round_.begin(), bets_this_round_.end(), 0); // Collect final bets
        } else {
             update_next_player(); // Advance to the next player if game continues
        }
    }
}

void GameState::advance_to_next_street() {
    if (current_street_ == Street::RIVER) {
        current_street_ = Street::SHOWDOWN;
        is_game_over_ = true; // Mark game over for showdown
    } else {
        current_street_ = static_cast<Street>(static_cast<uint8_t>(current_street_) + 1);
        reset_bets_for_new_street();

        // Determine the first player to act on the new street (postflop)
        // It's the first active player (not folded, not all-in) starting from the SB position (left of button).
        int potential_starter = (button_position_ + 1) % num_players_; // Start checking from SB
        int checked_count = 0;
        while(checked_count < num_players_ && (player_folded_[potential_starter] || player_all_in_[potential_starter])) {
            potential_starter = (potential_starter + 1) % num_players_;
            checked_count++;
        }

        if (checked_count < num_players_) {
            // Found an active player
            current_player_index_ = potential_starter;
        } else {
            // Everyone is folded or all-in, the hand should already be terminal or at showdown.
            // This state might occur if the last two players go all-in on the previous street.
            spdlog::debug("Could not find starting player for new street {}, likely already terminal or all-in showdown.", static_cast<int>(current_street_));
            is_game_over_ = true;
        }
    }
    // TODO: Deal community cards if applicable (Flop, Turn, River) based on new street_
    // Example:
    // if (current_street_ == Street::FLOP) deal_community_cards(deck.draw(3));
    // if (current_street_ == Street::TURN) deal_community_cards(deck.draw(1));
    // if (current_street_ == Street::RIVER) deal_community_cards(deck.draw(1));
}

std::string GameState::get_history_string() const {
    std::stringstream ss;
    // TODO: Improve history string (e.g., indicate streets)
    for(const auto& action : action_history_) {
        switch(action.type) {
            case Action::Type::FOLD: ss << 'f'; break;
            case Action::Type::CALL: ss << 'c'; break;
            case Action::Type::CHECK: ss << 'k'; break; // Use 'k' for check
            case Action::Type::RAISE: ss << 'r' << action.amount; break;
            case Action::Type::BET: ss << 'b' << action.amount; break;
        }
        // Optionally add player index: ss << "(" << action.player_index << ")";
    }
    return ss.str();
}

// --- Private Helper Methods ---

void GameState::update_next_player() {
    if (is_terminal()) return; // No need to update if game is over

    int start_index = current_player_index_;
    do {
        current_player_index_ = (current_player_index_ + 1) % num_players_;
        // Stop if we looped back (shouldn't happen if round end logic is correct)
        if (current_player_index_ == start_index) {
             spdlog::warn("Looped back to same player in update_next_player. Hand should likely be over.");
             is_game_over_ = true; // Force end if stuck
             break;
        }
        // Stop if we found a player who can act (not folded and not all-in)
        if (!player_folded_[current_player_index_] && !player_all_in_[current_player_index_]) {
            break;
        }
    } while (true);
}

void GameState::reset_bets_for_new_street() {
     // Pot was collected in apply_action when round_over was detected, or just before calling advance_to_next_street
     // pot_size_ += std::accumulate(bets_this_round_.begin(), bets_this_round_.end(), 0); // Pot collected before calling this

     // Reset bets for the new round
     std::fill(bets_this_round_.begin(), bets_this_round_.end(), 0);
     // Reset last raise size for the new street
     last_raise_size_ = 0;
     // Reset aggressor and acted flags
     aggressor_this_round_ = -1;
     std::fill(player_acted_this_sequence_.begin(), player_acted_this_sequence_.end(), false);
     // actions_this_round_ = 0; // No longer needed
     // Note: player_folded_ and player_all_in_ persist across streets until the hand ends.
 }

// Updated function signature and logic to use indices
void GameState::post_antes_and_blinds(int sb_index, int bb_index, int sb_amount, int bb_amount) {
     // Post Antes first
     if (ante_size_ > 0) {
         for (int i = 0; i < num_players_; ++i) {
             int ante = std::min(ante_size_, player_stacks_[i]); // Cannot post more than stack
             if (ante > 0 && !player_folded_[i]) { // Only post if not already folded (unlikely at start, but safety)
                 player_stacks_[i] -= ante;
                 // Antes go directly into the pot before betting starts
                 pot_size_ += ante;
                 player_contributions_[i] += ante; // Add ante to contributions
                 if (player_stacks_[i] == 0) {
                     player_all_in_[i] = true; // Mark if ante makes player all-in
                 }
             }
         }
     }

     // Post Blinds using provided indices
     int final_sb_amount = 0;
     if (sb_index >= 0 && sb_index < num_players_) { // Check index validity
         if (!player_folded_[sb_index] && !player_all_in_[sb_index]) { // Check if SB can post
             final_sb_amount = std::min(sb_amount, player_stacks_[sb_index]);
             player_stacks_[sb_index] -= final_sb_amount;
             bets_this_round_[sb_index] = final_sb_amount;
             player_contributions_[sb_index] += final_sb_amount; // Add SB to contributions
             if (player_stacks_[sb_index] == 0) {
                 player_all_in_[sb_index] = true;
             }
         }
     } else {
         spdlog::warn("Invalid SB index ({}) in post_antes_and_blinds for {} players.", sb_index, num_players_);
     }


     int final_bb_amount = 0;
     // Ensure BB index is valid and different from SB index if num_players > 1
     if (bb_index >= 0 && bb_index < num_players_ && (num_players_ == 1 || bb_index != sb_index)) { // Allow same index if only 1 player (though constructor prevents this)
         if (!player_folded_[bb_index] && !player_all_in_[bb_index]) { // Check if BB can post
             final_bb_amount = std::min(bb_amount, player_stacks_[bb_index]);
             player_stacks_[bb_index] -= final_bb_amount;
             bets_this_round_[bb_index] = final_bb_amount;
             player_contributions_[bb_index] += final_bb_amount; // Add BB to contributions
             if (player_stacks_[bb_index] == 0) {
                 player_all_in_[bb_index] = true;
             }
         }
     } else if (num_players_ > 1) {
         // This case should ideally not happen with correct index calculation, but log if it does.
         spdlog::warn("Invalid BB index ({}) or same as SB index ({}) in post_antes_and_blinds for {} players.", bb_index, sb_index, num_players_);
     }


     // Set the initial "last raise size" based on the BB posting over the SB
     last_raise_size_ = final_bb_amount - final_sb_amount;
     if (last_raise_size_ < 0) last_raise_size_ = 0; // Ensure non-negative

     // BB is the initial aggressor preflop if they posted more than SB
     aggressor_this_round_ = (final_bb_amount > final_sb_amount) ? bb_index : -1;
     actions_this_round_ = 0; // Action count starts after blinds

}


} // namespace gto_solver
