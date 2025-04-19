#include "game_state.h" // Corrected include
#include "spdlog/spdlog.h" // Include spdlog
#include <numeric>   // For std::accumulate
#include <algorithm> // For std::min, std::max
#include <stdexcept> // For std::runtime_error
#include <limits>    // For std::numeric_limits

namespace gto_solver {

// Constants (assuming standard blinds for now)
const int SMALL_BLIND_AMOUNT = 1;
const int BIG_BLIND_AMOUNT = 2;

GameState::GameState(int num_players, int initial_stack, int ante_size, int button_position)
    : num_players_(num_players),
      current_player_index_(-1), // Will be set after blinds
      pot_size_(0),
      player_stacks_(num_players, initial_stack),
      bets_this_round_(num_players, 0),
      player_hands_(num_players),
      current_street_(Street::PREFLOP),
      is_game_over_(false),
      last_raise_size_(0),
      aggressor_this_round_(-1),
      actions_this_round_(0),
      player_folded_(num_players, false),
      player_all_in_(num_players, false),
      player_contributions_(num_players, 0),
      ante_size_(ante_size),
      button_position_(button_position),
      player_acted_this_sequence_(num_players, false)
{
    if (num_players < 2) {
        throw std::invalid_argument("GameState requires at least 2 players.");
    }
    if (button_position < 0 || button_position >= num_players) {
         throw std::invalid_argument("Invalid button position.");
    }

    // Determine SB and BB positions based on button
    int sb_index = (button_position_ + 1) % num_players_;
    int bb_index = (button_position_ + 2) % num_players_;
    // Handle Heads-Up case where BTN is SB
    if (num_players_ == 2) {
        sb_index = button_position_;
        bb_index = (button_position_ + 1) % num_players_;
    }

    post_antes_and_blinds(sb_index, bb_index, SMALL_BLIND_AMOUNT, BIG_BLIND_AMOUNT);

    // Determine the first player to act preflop
    if (num_players_ == 2) {
        current_player_index_ = sb_index; // SB acts first HU
    } else {
        current_player_index_ = (bb_index + 1) % num_players_; // UTG acts first 3+ players
    }
     // Skip players who might already be all-in from blinds/antes
     int initial_player = current_player_index_;
     while(player_all_in_[current_player_index_] || player_folded_[current_player_index_]) {
         current_player_index_ = (current_player_index_ + 1) % num_players_;
         if (current_player_index_ == initial_player) {
              // Everyone is all-in or folded - should likely be terminal
              spdlog::warn("All players all-in or folded after blinds/antes. Setting game over.");
              is_game_over_ = true;
              current_player_index_ = -1; // No one to act
              break;
         }
     }

    spdlog::debug("GameState initialized. Players: {}, Stack: {}, Ante: {}, BTN: {}, SB: {}, BB: {}, First Actor: {}",
                  num_players_, initial_stack, ante_size_, button_position_, sb_index, bb_index, current_player_index_);
}


void GameState::post_antes_and_blinds(int sb_index, int bb_index, int sb_amount, int bb_amount) {
    // Post Antes
    if (ante_size_ > 0) {
        for (int i = 0; i < num_players_; ++i) {
            int ante = std::min(ante_size_, player_stacks_[i]);
            player_stacks_[i] -= ante;
            player_contributions_[i] += ante;
            pot_size_ += ante;
            if (player_stacks_[i] == 0) {
                player_all_in_[i] = true;
            }
        }
    }

    // Post Small Blind
    int sb_post = std::min(sb_amount, player_stacks_[sb_index]);
    player_stacks_[sb_index] -= sb_post;
    bets_this_round_[sb_index] = sb_post;
    player_contributions_[sb_index] += sb_post;
    pot_size_ += sb_post;
    if (player_stacks_[sb_index] == 0) {
        player_all_in_[sb_index] = true;
    }

    // Post Big Blind
    int bb_post = std::min(bb_amount, player_stacks_[bb_index]);
    player_stacks_[bb_index] -= bb_post;
    bets_this_round_[bb_index] = bb_post;
    player_contributions_[bb_index] += bb_post;
    pot_size_ += bb_post;
    if (player_stacks_[bb_index] == 0) {
        player_all_in_[bb_index] = true;
    }

    // The initial "bet to match" is the Big Blind
    // The last raise size is effectively the difference between BB and SB (or BB if no SB)
    last_raise_size_ = bb_amount - sb_amount; // Simplified, assumes SB < BB
    if (num_players_ == 2) { // HU case, SB posts first, BB is the raise
         last_raise_size_ = bb_amount - sb_amount; // Still the diff
    } else if (sb_index == bb_index) { // Should not happen in standard poker
         last_raise_size_ = bb_amount;
    }
    // Ensure last_raise_size is at least the minimum bet (BB) if applicable preflop
    last_raise_size_ = std::max(last_raise_size_, bb_amount);


    aggressor_this_round_ = bb_index; // BB is the initial "aggressor" preflop
    actions_this_round_ = 0; // Reset action count for the start of preflop betting
    std::fill(player_acted_this_sequence_.begin(), player_acted_this_sequence_.end(), false);
}


// --- Getters ---
int GameState::get_num_players() const { return num_players_; }
int GameState::get_button_position() const { return button_position_; }
int GameState::get_current_player() const { return current_player_index_; }
int GameState::get_pot_size() const { return pot_size_; }
const std::vector<int>& GameState::get_player_stacks() const { return player_stacks_; }
const std::vector<Card>& GameState::get_player_hand(int player_index) const {
    if (player_index < 0 || player_index >= num_players_) {
        throw std::out_of_range("Invalid player index for get_player_hand");
    }
    return player_hands_[player_index];
}
const std::vector<Card>& GameState::get_community_cards() const { return community_cards_; }
Street GameState::get_current_street() const { return current_street_; }
const std::vector<Action>& GameState::get_action_history() const { return action_history_; }
int GameState::get_last_raise_size() const { return last_raise_size_; }
bool GameState::has_player_folded(int player_index) const {
     if (player_index < 0 || player_index >= num_players_) return true; // Treat invalid index as folded
     return player_folded_[player_index];
}
bool GameState::is_player_all_in(int player_index) const {
     if (player_index < 0 || player_index >= num_players_) return false;
     return player_all_in_[player_index];
}
int GameState::get_player_contribution(int player_index) const {
     if (player_index < 0 || player_index >= num_players_) return 0;
     return player_contributions_[player_index];
}
const std::vector<int>& GameState::get_bets_this_round() const { return bets_this_round_; }

int GameState::get_bet_this_round(int player_index) const {
    if (player_index < 0 || player_index >= num_players_) {
        return 0; // Or throw? Return 0 for safety.
    }
    return bets_this_round_[player_index];
}

int GameState::get_amount_to_call(int player_index) const {
    if (player_index < 0 || player_index >= num_players_ || player_folded_[player_index] || player_all_in_[player_index]) {
        return 0; // Cannot call if folded, all-in, or invalid index
    }
    int max_bet = 0;
    for (int bet : bets_this_round_) {
        max_bet = std::max(max_bet, bet);
    }
    int amount_needed = max_bet - bets_this_round_[player_index];
    // Cannot call more than stack allows
    return std::min(amount_needed, player_stacks_[player_index]);
}


bool GameState::is_terminal() const {
    if (is_game_over_) {
        return true;
    }

    int active_players = 0;
    for (int i = 0; i < num_players_; ++i) {
        if (!player_folded_[i]) {
            active_players++;
        }
    }

    // Terminal if only one player left
    if (active_players <= 1) {
        return true;
    }

    // Terminal if showdown is reached and betting round is over
    if (current_street_ == Street::SHOWDOWN) {
        return true; // Showdown is always terminal in this model
    }

     // Check if betting round is over (only relevant if not already SHOWDOWN)
     bool betting_over = false;
     if (actions_this_round_ > 0 && aggressor_this_round_ != -1) { // Check only if there was aggression
         int players_yet_to_act = 0;
         int potential_closer = aggressor_this_round_; // Player who needs to be called/folded to

         for (int i = 0; i < num_players_; ++i) {
             int current_idx = (potential_closer + 1 + i) % num_players_; // Start checking from player after aggressor

             // Skip folded or all-in players (unless they are the aggressor closing action)
             if (player_folded_[current_idx] || (player_all_in_[current_idx] && current_idx != aggressor_this_round_)) {
                 continue;
             }

             // If an active player hasn't matched the bet or hasn't acted in this sequence, betting isn't over
             if (bets_this_round_[current_idx] < bets_this_round_[potential_closer] || !player_acted_this_sequence_[current_idx]) {
                  players_yet_to_act++;
                  break; // Found someone who needs to act
             }
         }
         betting_over = (players_yet_to_act == 0);

     } else if (actions_this_round_ > 0 && aggressor_this_round_ == -1) {
          // Case: No aggression, but action occurred (e.g., checks around postflop, or BB checks preflop)
          // Betting is over if everyone active has acted in this sequence (checked)
          int players_yet_to_act = 0;
          for(int i=0; i<num_players_; ++i) {
               if (!player_folded_[i] && !player_all_in_[i] && !player_acted_this_sequence_[i]) {
                    players_yet_to_act++;
                    break;
               }
          }
          betting_over = (players_yet_to_act == 0);
     }
     // Preflop special case: If action gets back to BB and there was no raise, betting ends if BB checks.
     // This is handled implicitly by the logic above when BB acts.


    // If betting is over and it was the river, the game is terminal (go to SHOWDOWN state implicitly)
    if (betting_over && current_street_ == Street::RIVER) {
        return true;
    }

    return false; // Not terminal otherwise
}


// --- Modifiers ---
void GameState::deal_hands(const std::vector<std::vector<Card>>& hands) {
    if (hands.size() != num_players_) {
        throw std::invalid_argument("Number of hands does not match number of players.");
    }
    player_hands_ = hands;
}

void GameState::deal_community_cards(const std::vector<Card>& cards) {
    community_cards_.insert(community_cards_.end(), cards.begin(), cards.end());
    // Optionally advance street based on number of cards dealt, but advance_to_next_street is preferred
}

void GameState::apply_action(const Action& action) {
    if (is_game_over_) {
        spdlog::warn("Attempted action on a terminal game state.");
        return;
    }
    if (action.player_index != current_player_index_) {
        throw std::runtime_error("Action applied by wrong player.");
    }

    int player = action.player_index;
    action_history_.push_back(action);
    player_acted_this_sequence_[player] = true; // Mark player as having acted
    actions_this_round_++;

    switch (action.type) {
        case Action::Type::FOLD:
            player_folded_[player] = true;
            break;
        case Action::Type::CHECK:
            // No change in bet amount or pot size
            if (get_amount_to_call(player) > 0) {
                 throw std::runtime_error("Invalid action: Check when facing a bet.");
            }
            aggressor_this_round_ = -1; // Checking resets aggression sequence
            break;
        case Action::Type::CALL: {
            int call_amount = get_amount_to_call(player);
            if (call_amount == 0 && bets_this_round_[player] > 0) {
                 // This case handles calling an all-in shove when the caller is already all-in for less.
                 // Or calling when already matched the bet (shouldn't happen if logic is correct).
                 spdlog::debug("Player {} called with amount_to_call=0. Current bet: {}", player, bets_this_round_[player]);
            } else if (call_amount > 0) {
                 int amount_put_in = std::min(call_amount, player_stacks_[player]);
                 player_stacks_[player] -= amount_put_in;
                 bets_this_round_[player] += amount_put_in;
                 player_contributions_[player] += amount_put_in;
                 pot_size_ += amount_put_in;
                 if (player_stacks_[player] == 0) {
                     player_all_in_[player] = true;
                 }
            }
             // Calling does not make you the aggressor
             // aggressor_this_round_ remains unchanged unless it was -1 (check)
             if (aggressor_this_round_ == -1) {
                  // This implies someone checked, and then someone bet, and now we are calling.
                  // Find the actual aggressor.
                  int max_bet = 0;
                  int current_aggressor = -1;
                  for(int i=0; i<num_players_; ++i) {
                       if(bets_this_round_[i] > max_bet) {
                            max_bet = bets_this_round_[i];
                            current_aggressor = i;
                       }
                  }
                  aggressor_this_round_ = current_aggressor;
             }

            break;
        }
        case Action::Type::BET:
        case Action::Type::RAISE: {
            if (action.type == Action::Type::BET && get_amount_to_call(player) > 0) {
                 throw std::runtime_error("Invalid action: Bet when facing a bet (should be Raise).");
            }
             if (action.type == Action::Type::RAISE && get_amount_to_call(player) == 0) {
                  // Allow raise when amount_to_call is 0 only if it's preflop BB option? No, should be BET.
                  // Let's treat it as a BET if amount_to_call is 0.
                  // throw std::runtime_error("Invalid action: Raise when not facing a bet (should be Bet).");
                  spdlog::warn("Action type RAISE received when amount_to_call is 0. Treating as BET.");
             }

            int amount_committed_total = action.amount; // Total amount player wants committed this round
            int current_bet = bets_this_round_[player];
            int additional_amount = amount_committed_total - current_bet;

            if (additional_amount <= 0) {
                 throw std::runtime_error("Invalid bet/raise amount: Must be greater than current bet.");
            }
            if (additional_amount > player_stacks_[player]) {
                 // This indicates an attempt to bet/raise more than stack, treat as all-in
                 spdlog::debug("Bet/Raise amount {} exceeds stack {}. Treating as All-In.", additional_amount, player_stacks_[player]);
                 additional_amount = player_stacks_[player];
                 amount_committed_total = current_bet + additional_amount; // Adjust total commit
            }

            // Min-raise check (simplified: must raise by at least the last raise amount, or BB preflop)
            int call_amount = get_amount_to_call(player);
            int raise_increment = amount_committed_total - (current_bet + call_amount);
            int min_increment = (last_raise_size_ > 0) ? last_raise_size_ : BIG_BLIND_AMOUNT; // Use BB as min preflop/if no prior raise

            // Exception: If going all-in for less than a min-raise, it's allowed.
            bool is_all_in_commit = (additional_amount == player_stacks_[player]);
            if (!is_all_in_commit && raise_increment < min_increment) {
                 // If not all-in, must meet min-raise
                 throw std::runtime_error("Invalid raise amount: Raise increment " + std::to_string(raise_increment) + " is less than minimum " + std::to_string(min_increment));
            }


            player_stacks_[player] -= additional_amount;
            bets_this_round_[player] += additional_amount;
            player_contributions_[player] += additional_amount;
            pot_size_ += additional_amount;
            if (player_stacks_[player] == 0) {
                player_all_in_[player] = true;
            }

            // Update aggression state
            last_raise_size_ = raise_increment; // The amount *added* on top of the call
            aggressor_this_round_ = player;
            actions_this_round_ = 1; // Reset action count after aggression
            // Reset acted flags for others
            std::fill(player_acted_this_sequence_.begin(), player_acted_this_sequence_.end(), false);
            player_acted_this_sequence_[player] = true; // Aggressor has acted

            break;
        }
    }

    // Check if betting round is over or game is over
    if (is_terminal()) {
        is_game_over_ = true;
        current_player_index_ = -1; // No more actions
        // If betting ended on river, implicitly move to SHOWDOWN state
        if (current_street_ == Street::RIVER) {
             // Check if betting actually ended this action
             bool betting_over = false;
             int active_players = 0;
             int players_all_in_or_folded = 0;
             int max_bet = 0;
             for(int bet : bets_this_round_) max_bet = std::max(max_bet, bet);

             for(int i=0; i<num_players_; ++i) {
                  if (!player_folded_[i]) {
                       active_players++;
                       if (player_all_in_[i] || bets_this_round_[i] == max_bet) {
                            players_all_in_or_folded++;
                       }
                  } else {
                       players_all_in_or_folded++;
                  }
             }
             // Betting is over if all active players are all-in or have matched the max bet
             // Or if only one active player remains
             if (active_players <= 1 || active_players == players_all_in_or_folded) {
                  betting_over = true;
             }


             if (betting_over) {
                  current_street_ = Street::SHOWDOWN;
                  spdlog::debug("River betting concluded. Moving to SHOWDOWN. History: {}", get_history_string());
             }
        }
    } else {
        update_next_player();
        // Check if the next player starts a new betting round
        if (current_player_index_ == -1) { // Indicates betting round ended
             advance_to_next_street();
        }
    }
}


void GameState::advance_to_next_street() {
    if (current_street_ == Street::RIVER || current_street_ == Street::SHOWDOWN) {
        current_street_ = Street::SHOWDOWN;
        is_game_over_ = true;
        current_player_index_ = -1;
        return;
    }

    // Move pot contributions from bets_this_round_ conceptually (already added to pot_size_)
    reset_bets_for_new_street();

    // Determine next street
    if (current_street_ == Street::PREFLOP) current_street_ = Street::FLOP;
    else if (current_street_ == Street::FLOP) current_street_ = Street::TURN;
    else if (current_street_ == Street::TURN) current_street_ = Street::RIVER;

    // Determine first player to act postflop (first active player after button)
    current_player_index_ = (button_position_ + 1) % num_players_;
    while(player_folded_[current_player_index_] || player_all_in_[current_player_index_]) {
         current_player_index_ = (current_player_index_ + 1) % num_players_;
         // Check if we wrapped around - should only happen if all remaining are all-in
         if (current_player_index_ == (button_position_ + 1) % num_players_) {
              spdlog::debug("All remaining players are all-in. Advancing street to {}", static_cast<int>(current_street_));
              // If we reach river this way, game should end
              if (current_street_ == Street::RIVER) {
                   current_street_ = Street::SHOWDOWN;
                   is_game_over_ = true;
                   current_player_index_ = -1;
              }
              // Otherwise, just deal next cards, no one can act.
              // We might need to call advance_to_next_street again immediately.
              // This logic might need refinement for sequential all-ins across streets.
              break;
         }
    }
     spdlog::debug("Advanced to street {}. First player to act: {}", static_cast<int>(current_street_), current_player_index_);
}

void GameState::reset_bets_for_new_street() {
    std::fill(bets_this_round_.begin(), bets_this_round_.end(), 0);
    last_raise_size_ = 0;
    aggressor_this_round_ = -1;
    actions_this_round_ = 0;
    std::fill(player_acted_this_sequence_.begin(), player_acted_this_sequence_.end(), false);
}


void GameState::update_next_player() {
    if (is_game_over_) {
        current_player_index_ = -1;
        return;
    }

    int current_actor = current_player_index_;
    int potential_closer = aggressor_this_round_;
    int max_bet = 0;
    for(int bet : bets_this_round_) max_bet = std::max(max_bet, bet);

    int active_players_remaining = 0;
    int players_able_to_act = 0; // Count players not folded and not all-in

    for(int i=0; i<num_players_; ++i) {
        if (!player_folded_[i]) {
             active_players_remaining++;
             if (!player_all_in_[i]) {
                  players_able_to_act++;
             }
        }
    }

    if (active_players_remaining <= 1) {
         is_game_over_ = true;
         current_player_index_ = -1;
         // If only one player left, advance to showdown immediately if river, otherwise next street
         if (current_street_ == Street::RIVER) current_street_ = Street::SHOWDOWN;
         // else advance_to_next_street(); // Let the main loop handle street advance
         return;
    }


    // Loop to find the next player who needs to act
    int next_player = (current_actor + 1) % num_players_;
    int checked_players = 0;
    while(checked_players < num_players_) {
         // Skip folded players
         if (player_folded_[next_player]) {
              next_player = (next_player + 1) % num_players_;
              checked_players++;
              continue;
         }
         // Skip players who are all-in (they cannot act)
         if (player_all_in_[next_player]) {
              next_player = (next_player + 1) % num_players_;
              checked_players++;
              continue;
         }

         // Check if this player needs to act
         // Condition 1: There was aggression, and this player hasn't acted since OR hasn't matched the bet
         // Condition 2: There was no aggression, and this player hasn't acted yet (checked)
         bool needs_to_act = false;
         if (aggressor_this_round_ != -1) { // Aggression occurred
              if (!player_acted_this_sequence_[next_player] || bets_this_round_[next_player] < max_bet) {
                   needs_to_act = true;
              }
         } else { // No aggression (checks or initial preflop state before UTG)
              if (!player_acted_this_sequence_[next_player]) {
                   needs_to_act = true;
              }
         }


         if (needs_to_act) {
              current_player_index_ = next_player;
              return; // Found next player
         }

         // If this player doesn't need to act, move to the next
         next_player = (next_player + 1) % num_players_;
         checked_players++;
    }

    // If we loop through everyone and no one needs to act, the betting round is over.
    current_player_index_ = -1; // Signal end of round
    spdlog::debug("Betting round concluded on street {}. History: {}", static_cast<int>(current_street_), get_history_string());

}


// --- Utility ---
std::string GameState::get_history_string() const {
    std::string history = "";
     // Include street transitions? Maybe later.
     // For now, just concatenate actions.
     // Use simple delimiters for now.
     Street last_street = Street::PREFLOP;
     for (const auto& action : action_history_) {
          if (action.type == Action::Type::FOLD) history += "f";
          else if (action.type == Action::Type::CHECK) history += "k";
          else if (action.type == Action::Type::CALL) history += "c";
          else if (action.type == Action::Type::BET) history += "b" + std::to_string(action.amount);
          else if (action.type == Action::Type::RAISE) history += "r" + std::to_string(action.amount);
          // Add player index? Maybe too verbose.
          history += "_"; // Separator
     }
     // Remove trailing separator if exists
     if (!history.empty()) {
          history.pop_back();
     }
    return history;
}

// --- New Helper Functions ---

int GameState::get_effective_stack(int player_index) const {
    if (player_index < 0 || player_index >= num_players_) return 0;

    // Stack at the beginning of the current betting round for the player
    // This requires tracking stack sizes at the start of the street, which we don't explicitly do.
    // Approximation: Current stack + amount bet this round.
    int current_player_potential = player_stacks_[player_index] + bets_this_round_[player_index];

    int min_opponent_potential = std::numeric_limits<int>::max();
    bool opponent_found = false;

    for (int i = 0; i < num_players_; ++i) {
        // Consider only opponents who are not folded
        if (i != player_index && !player_folded_[i]) {
             // Their potential amount is their current stack + what they've bet this round
             min_opponent_potential = std::min(min_opponent_potential, player_stacks_[i] + bets_this_round_[i]);
             opponent_found = true;
        }
    }

    if (!opponent_found) {
        return player_stacks_[player_index]; // Should not happen if game not over, but return current stack
    }

    // Effective stack is the minimum of player's potential and smallest opponent's potential
    return std::min(current_player_potential, min_opponent_potential);
}

int GameState::get_raises_this_street() const {
    int raise_count = 0;
    // Find the start of the current street in the history
    size_t start_of_street_idx = 0;
    // TODO: Need a robust way to find the start of the current street in action_history_
    // For now, just count all raises in the entire history (inaccurate postflop)
    for(const auto& action : action_history_) {
         if (action.type == Action::Type::RAISE || action.type == Action::Type::BET) {
              raise_count++;
         }
    }

     // Preflop: BB is the first "raise level" if no voluntary action yet
     if (current_street_ == Street::PREFLOP && raise_count == 0) {
          bool voluntary_action_taken = false;
          for(const auto& action : action_history_) {
               if (action.type == Action::Type::CALL || action.type == Action::Type::RAISE) {
                    voluntary_action_taken = true;
                    break;
               }
          }
          if (!voluntary_action_taken) {
               raise_count = 1; // Count the BB post as the first raise level
          }
     }
    return raise_count;
}

bool GameState::is_first_to_act_preflop(int player_index) const {
     if (current_street_ != Street::PREFLOP) return false;
     // Check if any voluntary action (call or raise) happened before this player
     for(const auto& action : action_history_) {
          // BET is only possible postflop, so check CALL or RAISE preflop
          if (action.type == Action::Type::CALL || action.type == Action::Type::RAISE) {
               return false; // Someone already acted voluntarily
          }
     }
     // If no voluntary actions yet, the current player *is* the first to act.
     return player_index == current_player_index_;
}

int GameState::get_num_limpers() const {
     if (current_street_ != Street::PREFLOP) return 0;
     int limpers = 0;
     int bb_index = (num_players_ == 2) ? (button_position_ + 1) % num_players_ : (button_position_ + 2) % num_players_;
     bool raise_occurred = false;
     for(const auto& action : action_history_) {
          if (action.type == Action::Type::RAISE) {
               raise_occurred = true;
               break;
          }
          // Count calls that exactly match the BB amount as limps (excluding BB itself)
          // This needs refinement based on exact blind structure and amounts
          int sb_index = (num_players_ == 2) ? button_position_ : (button_position_ + 1) % num_players_;
          if (action.type == Action::Type::CALL && action.player_index != bb_index) {
               // Check if the amount called makes the total bet equal to BB
               // This requires knowing the amount put in by the player in this action, not just the action type.
               // We need to look at the state *before* this action to know the bet they were facing.
               // Simplified: Assume any call before a raise (not by BB) is a limp. This is inaccurate.
               // TODO: Refine limp counting logic based on actual amounts called relative to BB.
               limpers++;
          }
     }
     return raise_occurred ? 0 : limpers;
}

} // namespace gto_solver
