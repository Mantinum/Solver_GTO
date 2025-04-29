#include "game_state.h" // Corrected include
#include <numeric>   // For std::accumulate
#include <algorithm> // For std::max, std::min, std::find
#include <stdexcept> // For std::runtime_error
#include <sstream>   // For history string

#include "spdlog/spdlog.h" // Include spdlog
#include "spdlog/fmt/bundled/format.h" // Include fmt for logging vectors


namespace gto_solver {

// Assume Big Blind size is needed for calculations
const int BIG_BLIND_SIZE_GS = 2; // TODO: Make configurable, avoid conflict with AA.cpp

// --- Constructor ---
GameState::GameState(int num_players, int initial_stack, int ante_size, int button_position)
    : num_players_(num_players),
      current_player_index_(-1), // Will be set after blinds
      pot_size_(0),
      player_stacks_(num_players, initial_stack),
      bets_this_round_(num_players, 0),
      player_hands_(num_players), // Initialize outer vector
      community_cards_(),
      current_street_(Street::PREFLOP),
      action_history_(),
      is_game_over_(false),
      last_raise_size_(0), // Initialized properly after blinds
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

    // Post Antes
    if (ante_size_ > 0) {
        for (int i = 0; i < num_players_; ++i) {
            int post_amount = std::min(ante_size_, player_stacks_[i]);
            player_stacks_[i] -= post_amount;
            player_contributions_[i] += post_amount;
            pot_size_ += post_amount;
            if (player_stacks_[i] == 0) {
                 player_all_in_[i] = true;
            }
        }
         spdlog::trace("Ante posted. Pot: {}", pot_size_);
    }


    // Determine SB and BB positions and amounts
    int sb_index = (button_position_ + 1) % num_players_;
    int bb_index = (button_position_ + 2) % num_players_;
    int sb_amount = BIG_BLIND_SIZE_GS / 2;
    int bb_amount = BIG_BLIND_SIZE_GS;

    // Handle Heads-Up case
    if (num_players_ == 2) {
        sb_index = button_position_; // Button is SB in HU
        bb_index = (button_position_ + 1) % num_players_;
    }

    // Post Blinds
    post_antes_and_blinds(sb_index, bb_index, sb_amount, bb_amount);

    // Set initial player to act (UTG or SB in HU)
    current_player_index_ = (button_position_ + 3) % num_players_;
    if (num_players_ == 2) {
        current_player_index_ = sb_index; // SB acts first HU preflop
    }
     // Skip players who are already all-in from antes/blinds
     int initial_actor = current_player_index_;
     while(player_all_in_[current_player_index_] && std::accumulate(player_all_in_.begin(), player_all_in_.end(), 0) < num_players_) {
          current_player_index_ = (current_player_index_ + 1) % num_players_;
          if (current_player_index_ == initial_actor) break; // Avoid infinite loop if everyone is all-in
     }


    spdlog::trace("GameState Initialized. Players: {}, Stack: {}, Ante: {}, BTN: {}, SB: {}, BB: {}, First Actor: {}",
                  num_players_, initial_stack, ante_size_, button_position_, sb_index, bb_index, current_player_index_);
}


// --- Getters ---
int GameState::get_num_players() const { return num_players_; }
int GameState::get_button_position() const { return button_position_; }
int GameState::get_current_player() const { return current_player_index_; }
int GameState::get_pot_size() const { return pot_size_; }
const std::vector<int>& GameState::get_player_stacks() const { return player_stacks_; }
const std::vector<Card>& GameState::get_player_hand(int player_index) const {
    if (player_index < 0 || player_index >= num_players_) {
        static const std::vector<Card> empty_hand;
        return empty_hand;
    }
    return player_hands_[player_index];
}
const std::vector<Card>& GameState::get_community_cards() const { return community_cards_; }
Street GameState::get_current_street() const { return current_street_; }
const std::vector<Action>& GameState::get_action_history() const { return action_history_; }
int GameState::get_amount_to_call(int player_index) const {
    if (player_index < 0 || player_index >= num_players_) return 0;
    int max_bet = 0;
    for (int bet : bets_this_round_) {
        max_bet = std::max(max_bet, bet);
    }
    return max_bet - bets_this_round_[player_index];
}
int GameState::get_bet_this_round(int player_index) const {
     if (player_index < 0 || player_index >= num_players_) return 0;
     return bets_this_round_[player_index];
}
const std::vector<int>& GameState::get_bets_this_round() const {
     return bets_this_round_;
}
// Added getter implementation
const std::vector<int>& GameState::get_current_bets() const {
     return bets_this_round_;
}
int GameState::get_last_raise_size() const { return last_raise_size_; }
bool GameState::has_player_folded(int player_index) const {
     if (player_index < 0 || player_index >= num_players_) return true;
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
int GameState::get_last_raiser() const {
     return aggressor_this_round_;
}
int GameState::get_num_active_players() const {
     // Correction: Count players who haven't folded
     int count = 0;
      for(int i=0; i<num_players_; ++i) {
          if (!player_folded_[i]) {
               count++;
          }
     }
     return count;
}
int GameState::get_num_limpers() const {
     if (current_street_ != Street::PREFLOP || get_raises_this_street() > 0) {
          return 0;
     }
     int limper_count = 0;
     int bb_index = (button_position_ + 2) % num_players_;
     if (num_players_ == 2) bb_index = (button_position_ + 1) % num_players_;

     for(int i=0; i < num_players_; ++i) {
          if (!player_folded_[i] && !player_all_in_[i] && bets_this_round_[i] == BIG_BLIND_SIZE_GS && i != bb_index) {
               limper_count++;
          }
          else if (!player_folded_[i] && !player_all_in_[i] && bets_this_round_[i] == BIG_BLIND_SIZE_GS / 2 && i == (button_position_ + 1) % num_players_ && num_players_ > 2) {
               limper_count++;
          }
     }
     return limper_count;
}

bool GameState::is_first_to_act_preflop(int player_index) const {
     if (current_street_ != Street::PREFLOP) return false;
     int utg_index = (button_position_ + 3) % num_players_;
      if (num_players_ == 2) utg_index = button_position_;
      return player_index == utg_index;
}


// --- Modifiers ---
void GameState::deal_hands(const std::vector<std::vector<Card>>& hands) {
    if (hands.size() != num_players_) {
        throw std::runtime_error("Number of hands provided does not match number of players.");
    }
    player_hands_ = hands;
     spdlog::trace("Hands dealt.");
}

void GameState::deal_community_cards(const std::vector<Card>& cards) {
    community_cards_.insert(community_cards_.end(), cards.begin(), cards.end());
     spdlog::trace("Community cards dealt. Board: {}", fmt::join(community_cards_, " "));
}

void GameState::apply_action(const Action& action) {
    if (is_game_over_) {
        spdlog::warn("Attempted action on a terminal game state.");
        return;
    }
    if (action.player_index != current_player_index_) {
        throw std::runtime_error("Action applied by wrong player.");
    }
    if (player_folded_[current_player_index_] || player_all_in_[current_player_index_]) {
         spdlog::warn("Player {} cannot act (folded or all-in). Skipping action.", current_player_index_);
         update_next_player();
         return;
    }

    int player_stack = player_stacks_[current_player_index_];
    int call_amount = get_amount_to_call(current_player_index_);
    int current_bet = bets_this_round_[current_player_index_];
    int amount_committed = 0;

    action_history_.push_back(action);
    player_acted_this_sequence_[current_player_index_] = true;

    switch (action.type) {
        case Action::Type::FOLD:
            player_folded_[current_player_index_] = true;
            spdlog::trace("Player {} folds.", current_player_index_);
            break;

        case Action::Type::CHECK:
            if (call_amount != 0) {
                throw std::runtime_error("Invalid action: Check not allowed when facing a bet.");
            }
            spdlog::trace("Player {} checks.", current_player_index_);
            actions_this_round_++;
            break;

        case Action::Type::CALL:
            if (call_amount == 0) {
                 if (current_street_ == Street::PREFLOP && current_player_index_ == (button_position_ + 1) % num_players_ && bets_this_round_[current_player_index_] < BIG_BLIND_SIZE_GS && num_players_ > 2) {
                      amount_committed = BIG_BLIND_SIZE_GS / 2 - bets_this_round_[current_player_index_];
                 } else {
                      spdlog::warn("Player {} attempted to call 0 when check was possible or invalid context.", current_player_index_);
                      actions_this_round_++;
                      break;
                 }
            } else {
                 amount_committed = std::min(player_stack, call_amount);
            }

            spdlog::trace("Player {} calls {}.", current_player_index_, amount_committed);
            player_stacks_[current_player_index_] -= amount_committed;
            bets_this_round_[current_player_index_] += amount_committed;
            player_contributions_[current_player_index_] += amount_committed;
            pot_size_ += amount_committed;
            if (player_stacks_[current_player_index_] == 0) {
                player_all_in_[current_player_index_] = true;
            }
            actions_this_round_++;
            break;

        case Action::Type::BET:
        case Action::Type::RAISE:
            if (action.type == Action::Type::BET && call_amount != 0) {
                throw std::runtime_error("Invalid action: Bet not allowed when facing a bet/raise.");
            }
            if (action.type == Action::Type::RAISE && call_amount == 0) {
                 bool is_bb = (current_player_index_ == (button_position_ + 2) % num_players_) || (num_players_ == 2 && current_player_index_ == (button_position_ + 1) % num_players_);
                 int max_other_bet = 0;
                 for(int i=0; i<num_players_; ++i) { if(i != current_player_index_) max_other_bet = std::max(max_other_bet, bets_this_round_[i]); }
                 if (!(current_street_ == Street::PREFLOP && is_bb && max_other_bet <= BIG_BLIND_SIZE_GS)) {
                      throw std::runtime_error("Invalid action: Raise not allowed when check is possible (except BB option).");
                 }
            }

            int total_bet_amount = action.amount;
            int bet_increment = total_bet_amount - current_bet;

            if (bet_increment <= 0) {
                 throw std::runtime_error("Invalid bet/raise amount: Increment must be positive.");
            }
            if (bet_increment > player_stack) {
                 spdlog::warn("Player {} bet/raise amount {} capped by stack {}. Going all-in.", current_player_index_, bet_increment, player_stack);
                 bet_increment = player_stack;
                 total_bet_amount = current_bet + player_stack;
                 player_all_in_[current_player_index_] = true;
            }

            // Check min-raise rule
            int num_raises_this_street = get_raises_this_street(); // Calculate raises on this street
            int min_raise_increment = (last_raise_size_ > 0) ? last_raise_size_ : BIG_BLIND_SIZE_GS;
             if (current_street_ == Street::PREFLOP && num_raises_this_street == 0) {
                  min_raise_increment = BIG_BLIND_SIZE_GS;
             }
            int actual_raise_increment = total_bet_amount - (current_bet + call_amount);

            if (action.type == Action::Type::RAISE && actual_raise_increment < min_raise_increment && !player_all_in_[current_player_index_]) {
                 int min_legal_total_bet = current_bet + call_amount + min_raise_increment;
                 if (player_stack + current_bet >= min_legal_total_bet) {
                      spdlog::warn("Player {} raise amount {} (increment {}) too small (min inc {}), forcing min-raise to {}.",
                                   current_player_index_, total_bet_amount, actual_raise_increment, min_raise_increment, min_legal_total_bet);
                      total_bet_amount = min_legal_total_bet;
                      bet_increment = total_bet_amount - current_bet;
                 } else {
                      throw std::runtime_error("Invalid raise amount: Too small and cannot make min-raise.");
                 }
            }

            spdlog::trace("Player {} {}s to {}.", current_player_index_, (action.type == Action::Type::BET ? "bet" : "raise"), total_bet_amount);
            player_stacks_[current_player_index_] -= bet_increment;
            bets_this_round_[current_player_index_] += bet_increment;
            player_contributions_[current_player_index_] += bet_increment;
            pot_size_ += bet_increment;
            if (player_stacks_[current_player_index_] == 0 && !player_all_in_[current_player_index_]) {
                player_all_in_[current_player_index_] = true;
            }

            last_raise_size_ = actual_raise_increment;
            aggressor_this_round_ = current_player_index_;
            actions_this_round_ = 1;
            std::fill(player_acted_this_sequence_.begin(), player_acted_this_sequence_.end(), false);
            player_acted_this_sequence_[current_player_index_] = true;

            break;
    }

    // --- Check for End of Round / Street / Hand ---
    int active_players_remaining = 0;
    int players_can_still_act = 0;
    for(int i=0; i<num_players_; ++i) {
        if (!player_folded_[i]) {
            active_players_remaining++;
            if (!player_all_in_[i]) {
                 players_can_still_act++;
            }
        }
    }

    if (active_players_remaining <= 1) {
        is_game_over_ = true;
        spdlog::trace("Hand over - only one player remaining.");
    } else {
        bool betting_round_over = true;
        int max_bet = 0;
        for(int bet : bets_this_round_) max_bet = std::max(max_bet, bet);

        for(int i=0; i<num_players_; ++i) {
            if (!player_folded_[i] && !player_all_in_[i]) {
                if (bets_this_round_[i] < max_bet || !player_acted_this_sequence_[i]) {
                    betting_round_over = false;
                    break;
                }
            }
        }
         int bb_index = (button_position_ + 2) % num_players_;
         if (num_players_ == 2) bb_index = (button_position_ + 1) % num_players_;
         if (current_street_ == Street::PREFLOP && aggressor_this_round_ == -1 && !player_acted_this_sequence_[bb_index] && !player_all_in_[bb_index] && !player_folded_[bb_index]) {
              betting_round_over = false;
         }


        if (betting_round_over) {
            spdlog::trace("Betting round over for street {}.", static_cast<int>(current_street_));
            if (current_street_ == Street::RIVER || players_can_still_act <= 1) {
                is_game_over_ = true;
                current_street_ = Street::SHOWDOWN;
                 spdlog::trace("Hand over - River betting complete or <=1 player can act.");
            } else {
                advance_to_next_street();
            }
        } else {
            update_next_player();
        }
    }
}

void GameState::advance_to_next_street() {
    if (current_street_ == Street::PREFLOP) current_street_ = Street::FLOP;
    else if (current_street_ == Street::FLOP) current_street_ = Street::TURN;
    else if (current_street_ == Street::TURN) current_street_ = Street::RIVER;
    else {
        is_game_over_ = true;
        current_street_ = Street::SHOWDOWN;
        return;
    }
    spdlog::trace("Advancing to street {}.", static_cast<int>(current_street_));
    reset_bets_for_new_street();

    // Determine the first player to act on the new street
    if (num_players_ == 2) {
        // In Heads-Up, the button (SB) acts first postflop
        current_player_index_ = button_position_;
    } else {
        // In multiway, player left of the button acts first
        current_player_index_ = (button_position_ + 1) % num_players_;
    }

    int initial_actor = current_player_index_;
    // Skip players who are folded or all-in
    while(player_folded_[current_player_index_] || player_all_in_[current_player_index_]) {
         current_player_index_ = (current_player_index_ + 1) % num_players_;
         if (current_player_index_ == initial_actor) {
              spdlog::warn("No player can act postflop, forcing showdown/end.");
              is_game_over_ = true;
              current_street_ = Street::SHOWDOWN;
              current_player_index_ = -1;
              break;
         }
    }
     if (!is_game_over_) {
          spdlog::trace("First player to act on new street: {}", current_player_index_);
     }
}

// --- Utility ---
std::string GameState::get_history_string() const {
    std::stringstream ss;
    // TODO: Improve history string to be more robust / less ambiguous
    for (const auto& action : action_history_) {
        switch (action.type) {
            case Action::Type::FOLD:  ss << "f"; break;
            case Action::Type::CHECK: ss << "k"; break;
            case Action::Type::CALL:  ss << "c"; break;
            case Action::Type::BET:   ss << "b" << action.amount; break;
            case Action::Type::RAISE: ss << "r" << action.amount; break;
        }
        ss << "/";
    }
    return ss.str();
}

int GameState::get_effective_stack(int player_index) const {
     if (player_index < 0 || player_index >= num_players_) return 0;
     int min_stack = player_stacks_[player_index];
     for(int i=0; i<num_players_; ++i) {
          if (i != player_index && !player_folded_[i]) {
               min_stack = std::min(min_stack, player_stacks_[i]);
          }
     }
     // Effective stack should not include current player's bet this round
     return min_stack;
}

int GameState::get_raises_this_street() const {
     int raise_count = 0;
     // TODO: Implement proper tracking of raises per street using action_history_ or dedicated counter
     // This simplified version is inaccurate for multi-street scenarios.
     for(const auto& action : action_history_) {
          if (action.type == Action::Type::RAISE || action.type == Action::Type::BET) {
               // Need to differentiate bet from raise based on context (amount_to_call at that point)
               // For simplicity now, count both as potential raises/bets
               raise_count++;
          }
     }
     return raise_count;
}


// --- Private Helpers ---
void GameState::post_antes_and_blinds(int sb_index, int bb_index, int sb_amount, int bb_amount) {
     if (sb_index >= 0 && sb_index < num_players_) {
          int post_amount = std::min(sb_amount, player_stacks_[sb_index]);
          player_stacks_[sb_index] -= post_amount;
          bets_this_round_[sb_index] = post_amount;
          player_contributions_[sb_index] += post_amount;
          pot_size_ += post_amount;
          if (player_stacks_[sb_index] == 0) player_all_in_[sb_index] = true;
          spdlog::trace("Player {} posts SB {}", sb_index, post_amount);
     }
     if (bb_index >= 0 && bb_index < num_players_) {
          int post_amount = std::min(bb_amount, player_stacks_[bb_index]);
          player_stacks_[bb_index] -= post_amount;
          bets_this_round_[bb_index] = post_amount;
          player_contributions_[bb_index] += post_amount;
          pot_size_ += post_amount;
          if (player_stacks_[bb_index] == 0) player_all_in_[bb_index] = true;
          spdlog::trace("Player {} posts BB {}", bb_index, post_amount);
     }
     last_raise_size_ = bb_amount; // Initial "raise" size is the BB
     aggressor_this_round_ = bb_index;
     actions_this_round_ = 0;
     std::fill(player_acted_this_sequence_.begin(), player_acted_this_sequence_.end(), false);
}

void GameState::update_next_player() {
    if (is_game_over_) {
        current_player_index_ = -1;
        return;
    }
    int start_index = current_player_index_;
    do {
        current_player_index_ = (current_player_index_ + 1) % num_players_;
        if (current_player_index_ == start_index) break;
        if (!player_folded_[current_player_index_] && !player_all_in_[current_player_index_]) break;
    } while (true);

     if (current_player_index_ == start_index && (player_folded_[current_player_index_] || player_all_in_[current_player_index_])) {
          bool can_anyone_act = false;
          for(int i=0; i<num_players_; ++i) {
               if (!player_folded_[i] && !player_all_in_[i]) {
                    can_anyone_act = true;
                    break;
               }
          }
          if (!can_anyone_act) {
               spdlog::trace("No player can act, advancing street/ending hand.");
               if (current_street_ == Street::RIVER) {
                    is_game_over_ = true;
                    current_street_ = Street::SHOWDOWN;
                    current_player_index_ = -1;
               } else {
                    // This case should ideally be caught by betting_round_over logic in apply_action
                    // If betting round is over because only all-in/folded players remain,
                    // apply_action should call advance_to_next_street.
                    spdlog::debug("update_next_player found no one can act, but not River. State should likely advance.");
                    // To prevent potential infinite loops if apply_action logic fails, force advance here? Risky.
                    // Let's assume apply_action handles the betting_round_over condition correctly.
                    current_player_index_ = -1; // Indicate no one can act now
                    is_game_over_ = true; // Force end if stuck
                    current_street_ = Street::SHOWDOWN;
               }
          } else {
               spdlog::error("update_next_player loop finished but active player found?");
          }
     }
     spdlog::trace("Next player to act: {}", current_player_index_);
}

void GameState::reset_bets_for_new_street() {
    std::fill(bets_this_round_.begin(), bets_this_round_.end(), 0);
    last_raise_size_ = 0;
    aggressor_this_round_ = -1;
    actions_this_round_ = 0;
    std::fill(player_acted_this_sequence_.begin(), player_acted_this_sequence_.end(), false);
}

bool GameState::is_terminal() const {
     if (is_game_over_) return true;

     int active_count = 0;
     for(int i=0; i<num_players_; ++i) {
          if (!player_folded_[i]) {
               active_count++;
          }
     }
     if (active_count <= 1) {
          return true;
     }

     if (current_street_ == Street::SHOWDOWN) {
          return true;
     }

     int players_can_bet = 0;
      for(int i=0; i<num_players_; ++i) {
          if (!player_folded_[i] && !player_all_in_[i]) {
               players_can_bet++;
          }
     }
      if (players_can_bet <= 1) {
           bool betting_closed = true;
           int max_bet = 0;
           for(int bet : bets_this_round_) max_bet = std::max(max_bet, bet);
           for(int i=0; i<num_players_; ++i) {
                if (!player_folded_[i] && !player_all_in_[i]) {
                     if (bets_this_round_[i] < max_bet || !player_acted_this_sequence_[i]) {
                          betting_closed = false;
                          break;
                     }
                }
           }
            int bb_index = (button_position_ + 2) % num_players_;
            if (num_players_ == 2) bb_index = (button_position_ + 1) % num_players_;
            if (current_street_ == Street::PREFLOP && aggressor_this_round_ == -1 && !player_acted_this_sequence_[bb_index] && !player_all_in_[bb_index] && !player_folded_[bb_index]) {
                 betting_closed = false;
            }

           if (betting_closed && current_street_ == Street::RIVER) {
                return true;
           }
      }

     return false;
}

} // namespace gto_solver
