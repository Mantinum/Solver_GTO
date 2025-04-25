#include "action_abstraction.h" // Corrected include
#include "game_state.h" // Corrected include

#include <vector>
#include <string>
#include <set>       // For std::set (used for unique actions)
#include <cmath>     // For std::round, std::max, std::abs
#include <algorithm> // For std::min, std::sort
#include <map>       // For mapping context to actions
#include <sstream>   // Include stringstream for formatting doubles
#include <iomanip>   // Include iomanip for setprecision
#include <stdexcept> // For invalid_argument

#include "spdlog/spdlog.h" // Include spdlog

namespace gto_solver {

// Assume Big Blind size is needed for calculations
const int BIG_BLIND_SIZE = 2; // TODO: Make configurable

// --- ActionSpec Implementation ---
std::string ActionSpec::to_string() const {
    std::stringstream ss;
    switch (type) {
        case ActionType::FOLD:   return "fold";
        case ActionType::CHECK:  return "check";
        case ActionType::CALL:   return "call";
        case ActionType::ALL_IN: return "all_in";
        case ActionType::BET:    ss << "bet"; break;
        case ActionType::RAISE:  ss << "raise"; break;
        default: return "unknown";
    }
    // Add sizing info for BET/RAISE
    ss << "_";
    if (std::abs(value - std::round(value)) < 1e-5) {
        ss << static_cast<int>(value);
    } else {
        ss << std::fixed << std::setprecision(1) << value;
    }
    switch (unit) {
        case SizingUnit::BB:       ss << "bb"; break;
        case SizingUnit::PCT_POT:  ss << "pct"; break;
        case SizingUnit::MULTIPLIER_X: ss << "x"; break;
        case SizingUnit::ABSOLUTE: break; // No unit needed for absolute (used internally for all-in amount)
    }
    return ss.str();
}

// --- ActionAbstraction Implementation ---

ActionAbstraction::ActionAbstraction() {
    spdlog::debug("ActionAbstraction created");
}

// New method using ActionSpec
std::vector<ActionSpec> ActionAbstraction::get_possible_action_specs(const GameState& current_state) const {
    std::set<ActionSpec> actions_set; // Use set to handle potential duplicates based on calculated amount later
    int current_player = current_state.get_current_player();
    if (current_player < 0) return {};

    int player_stack = current_state.get_player_stacks()[current_player];
    int amount_to_call = current_state.get_amount_to_call(current_player);
    Street street = current_state.get_current_street();
    int effective_stack = current_state.get_effective_stack(current_player);
    const int EFFECTIVE_STACK_BB = (BIG_BLIND_SIZE > 0) ? (effective_stack / BIG_BLIND_SIZE) : effective_stack; // Avoid division by zero

    if (player_stack <= 0) return {};

    // 1. Fold Action
    if (amount_to_call > 0) {
        actions_set.insert({ActionType::FOLD});
    }

    // 2. Check or Call Action
    if (amount_to_call == 0) {
        actions_set.insert({ActionType::CHECK});
    } else {
        if (player_stack >= amount_to_call) {
             actions_set.insert({ActionType::CALL});
        }
    }

    // 3. Bet or Raise Actions (Only if stack > amount_to_call)
    if (player_stack > amount_to_call) {
        bool facing_bet_or_raise = amount_to_call > 0;
        int num_raises = current_state.get_raises_this_street();
        int num_limpers = current_state.get_num_limpers();

        // --- Preflop Abstraction ---
        if (street == Street::PREFLOP) {
            int sb_index = (current_state.get_button_position() + 1) % current_state.get_num_players();
            if (current_state.get_num_players() == 2) sb_index = current_state.get_button_position();

            if (!facing_bet_or_raise && num_limpers == 0) { // RFI or SB complete/raise vs BB check
                 if (current_player == sb_index) { // SB action
                      // Call already added if possible
                      actions_set.insert({ActionType::RAISE, 3.0, SizingUnit::BB});
                      actions_set.insert({ActionType::RAISE, 4.0, SizingUnit::BB});
                 } else if (current_state.is_first_to_act_preflop(current_player)) { // RFI
                      double open_size_bb_small = 2.2;
                      if (EFFECTIVE_STACK_BB < 25) open_size_bb_small = 2.0;
                      else if (EFFECTIVE_STACK_BB < 35) open_size_bb_small = 2.1;
                      actions_set.insert({ActionType::RAISE, open_size_bb_small, SizingUnit::BB});
                      actions_set.insert({ActionType::RAISE, 2.5, SizingUnit::BB});
                      actions_set.insert({ActionType::RAISE, 3.0, SizingUnit::BB});
                 }
            }
            else if (!facing_bet_or_raise && num_limpers > 0) { // Facing limper(s)
                 // Call already added
                 double iso_size_bb1 = 3.0 + num_limpers;
                 double iso_size_bb2 = 4.0 + num_limpers;
                 actions_set.insert({ActionType::RAISE, iso_size_bb1, SizingUnit::BB});
                 actions_set.insert({ActionType::RAISE, iso_size_bb2, SizingUnit::BB});
            }
            else if (facing_bet_or_raise) { // Facing a raise
                 if (num_raises == 1) { // Facing RFI (2Bet)
                      actions_set.insert({ActionType::RAISE, 3.0, SizingUnit::MULTIPLIER_X});
                      actions_set.insert({ActionType::RAISE, 4.0, SizingUnit::MULTIPLIER_X});
                 } else if (num_raises == 2) { // Facing 3Bet
                      actions_set.insert({ActionType::RAISE, 2.5, SizingUnit::MULTIPLIER_X});
                 }
                 // Add All-in option only if facing a 3Bet or more, OR if stack is short vs RFI
                 if (num_raises >= 2 || (num_raises == 1 && EFFECTIVE_STACK_BB <= 40)) {
                      actions_set.insert({ActionType::ALL_IN});
                 }
            }
            // Add All-in as an option in RFI/vs Limp spots only if stack is short
            else if (!facing_bet_or_raise && EFFECTIVE_STACK_BB <= 20) {
                 actions_set.insert({ActionType::ALL_IN});
            }
        }
        // --- Postflop Abstraction ---
        else {
             if (!facing_bet_or_raise) { // Option to Bet
                  actions_set.insert({ActionType::BET, 33, SizingUnit::PCT_POT});
                  actions_set.insert({ActionType::BET, 50, SizingUnit::PCT_POT});
                  actions_set.insert({ActionType::BET, 75, SizingUnit::PCT_POT});
                  actions_set.insert({ActionType::BET, 100, SizingUnit::PCT_POT});
                  actions_set.insert({ActionType::BET, 133, SizingUnit::PCT_POT});
             } else { // Option to Raise
                  actions_set.insert({ActionType::RAISE, 2.2, SizingUnit::MULTIPLIER_X});
                  actions_set.insert({ActionType::RAISE, 3.0, SizingUnit::MULTIPLIER_X});
             }
             actions_set.insert({ActionType::ALL_IN});
        }
    } else if (player_stack > 0 && amount_to_call > 0 && player_stack <= amount_to_call) {
         // If player cannot cover the call, only options are fold or all-in
         actions_set.clear();
         actions_set.insert({ActionType::FOLD});
         actions_set.insert({ActionType::ALL_IN});
    }


    // --- Final Filtering (Based on calculated amounts) ---
    std::vector<ActionSpec> final_actions;
    std::set<int> unique_amounts; // Track unique *calculated* amounts
    int all_in_amount = player_stack + current_state.get_bet_this_round(current_player); // Use getter

    // Ensure Fold/Check/Call are present if legal
     if (amount_to_call > 0) final_actions.push_back({ActionType::FOLD});
     if (amount_to_call == 0) final_actions.push_back({ActionType::CHECK});
     else if (player_stack >= amount_to_call) final_actions.push_back({ActionType::CALL});

    // Process Bets/Raises/All-ins from the set
    for (const ActionSpec& spec : actions_set) {
        if (spec.type == ActionType::BET || spec.type == ActionType::RAISE || spec.type == ActionType::ALL_IN) {
            int amount = get_action_amount(spec, current_state); // Calculate amount based on spec

            if (amount == -1) continue; // Error during calculation

            // Calculate min legal raise amount (total bet)
            int current_bet = current_state.get_bet_this_round(current_player); // Get current bet again
            int min_legal_total_bet = current_bet + amount_to_call;
            if (amount > min_legal_total_bet) { // If it's intended as a raise/bet
                 int min_raise_increment = std::max(1, current_state.get_last_raise_size());
                 if (street == Street::PREFLOP && current_state.get_raises_this_street() == 0) {
                      min_raise_increment = BIG_BLIND_SIZE;
                 }
                 min_legal_total_bet += min_raise_increment;
            }

            // Ensure the calculated amount is valid
            if (amount < min_legal_total_bet && amount != all_in_amount) {
                 continue; // Skip invalid bet/raise size (unless it's exactly all-in)
            }

            // Ensure amount doesn't exceed stack (already handled by get_action_amount capping)
            // amount = std::min(amount, all_in_amount); // Recalculate capped amount if needed

            // Add if the calculated amount is unique
            if (unique_amounts.find(amount) == unique_amounts.end()) {
                 // If the calculated amount IS all-in, use the ALL_IN type
                 if (amount == all_in_amount) {
                      // Avoid adding duplicate ALL_IN spec if already present
                      bool all_in_present = false;
                      for(const auto& fa : final_actions) { if(fa.type == ActionType::ALL_IN) { all_in_present = true; break; } }
                      if (!all_in_present) {
                           final_actions.push_back({ActionType::ALL_IN, (double)all_in_amount, SizingUnit::ABSOLUTE});
                           unique_amounts.insert(all_in_amount);
                      }
                 } else {
                      final_actions.push_back(spec); // Add the original spec
                      unique_amounts.insert(amount);
                 }
            }
        }
    }

    // Sort actions? Maybe by type then amount? For now, return as is.
    return final_actions;
}


// New method using ActionSpec
int ActionAbstraction::get_action_amount(const ActionSpec& action_spec, const GameState& current_state) const {
    int current_player = current_state.get_current_player();
    if (current_player < 0) return -1;

    int player_stack = current_state.get_player_stacks()[current_player];
    int amount_to_call = current_state.get_amount_to_call(current_player);
    int current_pot = current_state.get_pot_size();
    int current_bet = current_state.get_bet_this_round(current_player); // Declaration added here

    switch (action_spec.type) {
        case ActionType::FOLD:
        case ActionType::CHECK:
        case ActionType::CALL:
            return -1; // No amount associated

        case ActionType::ALL_IN:
            return player_stack + current_bet; // Total commitment

        case ActionType::BET: {
            if (amount_to_call != 0) {
                 spdlog::error("BET specified but facing a bet (amount_to_call > 0)");
                 return -1; // Invalid BET
            }
            int target_total_bet = -1;
            if (action_spec.unit == SizingUnit::PCT_POT) {
                int current_pot_for_betting = current_state.get_pot_size(); // Pot before this bet
                int bet_increment = static_cast<int>(std::round(current_pot_for_betting * (action_spec.value / 100.0)));
                target_total_bet = current_bet + bet_increment;
            } else {
                 spdlog::error("Unsupported unit for BET action"); return -1;
            }
             // Apply min bet rule (usually 1 BB, but can be less if stack is smaller)
             target_total_bet = std::max(target_total_bet, current_bet + std::min(player_stack, BIG_BLIND_SIZE));
             // Cap by stack
             return std::min(player_stack + current_bet, target_total_bet);
        }

        case ActionType::RAISE: {
             if (amount_to_call == 0) {
                  // Check if it's the BB option preflop
                  bool is_bb = (current_player == (current_state.get_button_position() + 2) % current_state.get_num_players()) || (current_state.get_num_players() == 2 && current_player == (current_state.get_button_position() + 1) % current_state.get_num_players());
                  int max_other_bet = 0;
                  for(int i=0; i<current_state.get_num_players(); ++i) { if(i != current_player) max_other_bet = std::max(max_other_bet, current_state.get_bet_this_round(i)); }
                  if (!(current_state.get_current_street() == Street::PREFLOP && is_bb && max_other_bet <= BIG_BLIND_SIZE)) {
                       spdlog::error("RAISE specified but check is possible (and not BB option)"); return -1;
                  }
             }

            int target_total_bet = -1;
            int pot_after_call = current_state.get_pot_size();
            for(int bet : current_state.get_current_bets()) { pot_after_call += bet; }
            pot_after_call += amount_to_call;
            int raise_base_amount = current_bet + amount_to_call;

            if (action_spec.unit == SizingUnit::BB) {
                target_total_bet = static_cast<int>(std::round(action_spec.value * BIG_BLIND_SIZE));
            } else if (action_spec.unit == SizingUnit::PCT_POT) {
                int raise_increment = static_cast<int>(std::round(pot_after_call * (action_spec.value / 100.0)));
                target_total_bet = raise_base_amount + raise_increment;
            } else if (action_spec.unit == SizingUnit::MULTIPLIER_X) {
                int last_raise_size = current_state.get_last_raise_size();
                if (last_raise_size <= 0) {
                     last_raise_size = (current_state.get_current_street() == Street::PREFLOP) ? BIG_BLIND_SIZE : std::max(BIG_BLIND_SIZE, 1);
                }
                int raise_increment = static_cast<int>(std::round(static_cast<double>(last_raise_size) * action_spec.value));
                target_total_bet = raise_base_amount + raise_increment;
            } else {
                 spdlog::error("Unsupported unit for RAISE action"); return -1;
            }

            // Apply Min-Raise Rules
            int last_raise_size = current_state.get_last_raise_size();
            int min_raise_increment = (last_raise_size > 0) ? last_raise_size : BIG_BLIND_SIZE;
             if (current_state.get_current_street() == Street::PREFLOP && current_state.get_raises_this_street() == 0) {
                  min_raise_increment = BIG_BLIND_SIZE;
             }
            int min_legal_total_bet = current_bet + amount_to_call + min_raise_increment;

            int final_total_bet = std::max(target_total_bet, min_legal_total_bet);

            // Cap by stack
            return std::min(player_stack + current_bet, final_total_bet);
        }
        default:
             spdlog::error("Unknown ActionType in get_action_amount");
             return -1;
    }
}


// --- Deprecated string-based methods ---
/*
std::vector<std::string> ActionAbstraction::get_possible_actions(const GameState& current_state) const {
    // ... implementation ...
}

int ActionAbstraction::get_action_amount(const std::string& action_str, const GameState& current_state) const {
    // ... implementation ...
}
*/

} // namespace gto_solver
