#include "action_abstraction.h" // Corrected include
#include "game_state.h" // Corrected include

#include <iostream>
#include <vector>
#include <string>
#include <set>       // For std::set (used for unique actions)
#include <cmath>     // For std::round, std::max, std::abs
#include <algorithm> // For std::min, std::sort
#include <map>       // For mapping context to actions
#include <sstream>   // Include stringstream for formatting doubles
#include <iomanip>   // Include iomanip for setprecision

#include "spdlog/spdlog.h" // Include spdlog

namespace gto_solver {

// Assume Big Blind size is needed for calculations
const int BIG_BLIND_SIZE = 2; // TODO: Make configurable

// Helper function to create action string with amount
std::string create_action_string(const std::string& base, double value, const std::string& unit) {
    std::string val_str;
    if (std::abs(value - std::round(value)) < 1e-5) {
        val_str = std::to_string(static_cast<int>(value));
    } else {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << value;
        val_str = ss.str();
    }
    return base + "_" + val_str + unit;
}


ActionAbstraction::ActionAbstraction() {
    spdlog::debug("ActionAbstraction created");
}

std::vector<std::string> ActionAbstraction::get_possible_actions(const GameState& current_state) const {
    std::set<std::string> actions_set;
    int current_player = current_state.get_current_player();
    if (current_player < 0) return {}; // No player to act

    int player_stack = current_state.get_player_stacks()[current_player];
    int amount_to_call = current_state.get_amount_to_call(current_player);
    int current_bet = current_state.get_bet_this_round(current_player);
    Street street = current_state.get_current_street();
    int effective_stack = current_state.get_effective_stack(current_player);

    if (player_stack <= 0) {
        return {}; // No actions if player has no stack
    }

    // 1. Fold Action
    if (amount_to_call > 0) {
        actions_set.insert("fold");
    }

    // 2. Check or Call Action
    if (amount_to_call == 0) {
        actions_set.insert("check");
    } else {
        actions_set.insert("call");
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

            // Case 1: RFI (Raise First In) or SB action vs BB check
            if (!facing_bet_or_raise && num_limpers == 0) {
                 if (current_player == sb_index) { // SB action (RFI or Complete vs BB check)
                      actions_set.insert("call"); // Limp/Complete
                      actions_set.insert(create_action_string("raise", 3.0, "bb"));
                      actions_set.insert(create_action_string("raise", 4.0, "bb"));
                 } else if (current_state.is_first_to_act_preflop(current_player)) { // RFI from UTG, MP, CO, BTN
                      double open_size_bb_small = 2.2;
                      if (effective_stack < 25 * BIG_BLIND_SIZE) open_size_bb_small = 2.0;
                      else if (effective_stack < 35 * BIG_BLIND_SIZE) open_size_bb_small = 2.1;
                      actions_set.insert(create_action_string("raise", open_size_bb_small, "bb"));
                      actions_set.insert(create_action_string("raise", 2.5, "bb"));
                      actions_set.insert(create_action_string("raise", 3.0, "bb"));
                 }
                 // Note: BB check option already added if amount_to_call == 0
            }
            // Case 2: Facing Limper(s)
            else if (!facing_bet_or_raise && num_limpers > 0) {
                 actions_set.insert("call"); // Overlimp
                 double iso_size_bb1 = 3.0 + num_limpers;
                 double iso_size_bb2 = 4.0 + num_limpers;
                 actions_set.insert(create_action_string("raise", iso_size_bb1, "bb"));
                 actions_set.insert(create_action_string("raise", iso_size_bb2, "bb"));
            }
            // Case 3: Facing a Raise
            else if (facing_bet_or_raise) {
                 if (num_raises == 1) { // Facing RFI (a 2Bet) -> Options are Call, 3Bet, Fold
                      // TODO: Add IP vs OOP logic here
                      actions_set.insert(create_action_string("raise", 3.0, "x")); // 3Bet sizing 1
                      actions_set.insert(create_action_string("raise", 4.0, "x")); // 3Bet sizing 2
                 } else if (num_raises == 2) { // Facing a 3Bet -> Options are Call, 4Bet, Fold
                      actions_set.insert(create_action_string("raise", 2.5, "x")); // 4Bet sizing
                 }
                 // For num_raises >= 2 (facing 3bet or more), All-in is always an option
                 if (num_raises >= 2) {
                      actions_set.insert("all_in");
                 }
                 // Also consider All-in vs the initial RFI (num_raises == 1) if stacks are short?
                 // Let's add it always for now, filtering will remove if identical to another raise.
                 actions_set.insert("all_in");
            }
            // Add All-in as a general option if not already added by specific logic
            // This needs careful filtering later to avoid redundancy.
            // Let's rely on the final filtering step instead of adding it unconditionally here.
            // actions_set.insert("all_in"); // REMOVED from here
        }
        // --- Postflop Abstraction ---
        else {
             if (!facing_bet_or_raise) { // Option to Bet
                  actions_set.insert(create_action_string("bet", 33, "pct"));
                  actions_set.insert(create_action_string("bet", 50, "pct"));
                  actions_set.insert(create_action_string("bet", 75, "pct"));
                  actions_set.insert(create_action_string("bet", 100, "pct")); // Pot size bet
             } else { // Option to Raise
                  actions_set.insert(create_action_string("raise", 2.2, "x")); // ~Min-raise+
                  actions_set.insert(create_action_string("raise", 3.0, "x")); // Larger raise
                  // actions_set.insert("raise_pot"); // Pot size raise - often very large, maybe omit?
             }
             actions_set.insert("all_in");
        }
    }

    // --- Final Filtering (Remove duplicates, ensure all-in is handled correctly) ---
    std::vector<std::string> final_actions;
    std::set<int> unique_amounts;
    int all_in_amount = player_stack + current_bet; // Calculate potential all-in amount

    for (const std::string& action_str : actions_set) {
        int amount = get_action_amount(action_str, current_state);

        if (amount == -1) { // Fold, Check, Call
            // Ensure Check is only added if amount_to_call is 0
            if (action_str == "check" && amount_to_call == 0) {
                 final_actions.push_back(action_str);
            } else if (action_str == "call" && amount_to_call > 0) {
                 final_actions.push_back(action_str);
            } else if (action_str == "fold") {
                 final_actions.push_back(action_str);
            }
        } else {
            // Filter invalid amounts (less than call or less than min-raise if not all-in)
            int min_legal_total_bet = current_bet + amount_to_call; // Minimum is call
            if (amount > min_legal_total_bet) { // If it's a raise/bet
                 int min_raise_increment = std::max(1, current_state.get_last_raise_size());
                 // Preflop BB is special case for min-raise size
                 if (street == Street::PREFLOP && current_state.get_raises_this_street() == 0) {
                      min_raise_increment = BIG_BLIND_SIZE;
                 }
                 min_legal_total_bet += min_raise_increment;
            }

            if (amount == all_in_amount) { // Handle All-in specifically
                 // Add "all_in" only if it's a valid raise OR if it's the only option besides fold/call
                 if (amount >= min_legal_total_bet || player_stack <= amount_to_call) {
                      if (unique_amounts.find(all_in_amount) == unique_amounts.end()) {
                           final_actions.push_back("all_in");
                           unique_amounts.insert(all_in_amount);
                      }
                 }
            } else if (amount >= min_legal_total_bet) { // Handle regular Bet/Raise
                 if (unique_amounts.find(amount) == unique_amounts.end()) {
                      final_actions.push_back(action_str);
                      unique_amounts.insert(amount);
                 }
            }
             // Implicitly filter out amounts < min_legal_total_bet (unless it was all-in)
        }
    }

    // Sort actions for consistency (optional)
    // std::sort(final_actions.begin(), final_actions.end());

    return final_actions;
}


// Helper to calculate the bet/raise amount for a given action string and state
int ActionAbstraction::get_action_amount(const std::string& action_str, const GameState& current_state) const {
    int current_player = current_state.get_current_player();
    if (current_player < 0) return -1; // No player

    int player_stack = current_state.get_player_stacks()[current_player];
    int amount_to_call = current_state.get_amount_to_call(current_player);
    int current_pot = current_state.get_pot_size();
    int current_bet = current_state.get_bet_this_round(current_player);

    // Effective pot for sizing: Current pot + all bets on table (including caller's potential call)
    int effective_pot_for_betting = current_pot;
    for(int i=0; i < current_state.get_num_players(); ++i) {
        effective_pot_for_betting += current_state.get_bet_this_round(i);
    }


    if (action_str == "fold" || action_str == "call" || action_str == "check") {
        return -1;
    } else if (action_str == "all_in") {
        return player_stack + current_bet; // Total amount committed if going all-in
    } else if (action_str.find("raise_") != std::string::npos || action_str.find("bet_") != std::string::npos) {
        int target_total_bet = -1; // The total amount player needs to have in front after the action

        try {
            size_t split_pos = action_str.find('_');
            if (split_pos == std::string::npos) throw std::invalid_argument("Invalid action format");
            std::string base = action_str.substr(0, split_pos);
            std::string value_unit = action_str.substr(split_pos + 1);
            double value = 0;
            std::string unit = "";

            // Extract value and unit
            size_t unit_pos = std::string::npos;
            if ((unit_pos = value_unit.find("bb")) != std::string::npos) {
                unit = "bb";
                value = std::stod(value_unit.substr(0, unit_pos));
            } else if ((unit_pos = value_unit.find("pct")) != std::string::npos) {
                unit = "pct";
                value = std::stod(value_unit.substr(0, unit_pos));
            } else if ((unit_pos = value_unit.find("x")) != std::string::npos) {
                unit = "x";
                value = std::stod(value_unit.substr(0, unit_pos));
            } else if (action_str == "raise_pot") { // Handle raise_pot separately
                 unit = "pot";
                 value = 1.0; // Represent as 1.0 * pot
                 base = "raise"; // Ensure base is raise
            }
             else {
                 throw std::invalid_argument("Unknown unit in action string");
            }

            // Calculate target_total_bet based on unit
            if (base == "bet") {
                if (unit == "pct") {
                    int bet_increment = static_cast<int>(std::round(effective_pot_for_betting * (value / 100.0)));
                    target_total_bet = current_bet + bet_increment;
                } else {
                     spdlog::error("Unsupported unit '{}' for base 'bet'", unit);
                }
            } else if (base == "raise") {
                int pot_after_call = effective_pot_for_betting + amount_to_call; // Pot if we call
                int raise_base_amount = current_bet + amount_to_call; // Amount needed to call

                if (unit == "bb") {
                    target_total_bet = static_cast<int>(std::round(value * BIG_BLIND_SIZE));
                } else if (unit == "pct") {
                    int raise_increment = static_cast<int>(std::round(pot_after_call * (value / 100.0)));
                    target_total_bet = raise_base_amount + raise_increment;
                } else if (unit == "x") {
                    int last_raise_or_bet_increment = current_state.get_last_raise_size();
                    if (last_raise_or_bet_increment <= 0) { // Facing limp/BB or check
                         last_raise_or_bet_increment = (current_state.get_current_street() == Street::PREFLOP) ? BIG_BLIND_SIZE : std::max(BIG_BLIND_SIZE, 1); // Use BB preflop, min bet postflop
                    }
                    int raise_increment = static_cast<int>(std::round(static_cast<double>(last_raise_or_bet_increment) * value));
                    target_total_bet = raise_base_amount + raise_increment;
                } else if (unit == "pot") {
                     int raise_increment = pot_after_call; // Pot size raise increment
                     target_total_bet = raise_base_amount + raise_increment;
                }
                 else {
                     spdlog::error("Unsupported unit '{}' for base 'raise'", unit);
                 }
            }

        } catch (const std::exception& e) {
            spdlog::error("Error parsing action string '{}': {}", action_str, e.what());
            return -1; // Indicate error
        }

        if (target_total_bet != -1) {
            // --- Apply Min-Raise Rules ---
            int last_raise_size = current_state.get_last_raise_size();
            int min_raise_increment = (last_raise_size > 0) ? last_raise_size : BIG_BLIND_SIZE;
             // Preflop BB is special case for min-raise size if no prior raise
             if (current_state.get_current_street() == Street::PREFLOP && current_state.get_raises_this_street() == 0) {
                  min_raise_increment = BIG_BLIND_SIZE;
             }
            int min_legal_total_bet = current_bet + amount_to_call + min_raise_increment;

            // Ensure the calculated total bet meets the minimum legal raise
            int final_total_bet = std::max(target_total_bet, min_legal_total_bet);

            // Cannot bet more than stack allows
            return std::min(player_stack + current_bet, final_total_bet);
        }
    }

    // Fallback or error
    spdlog::warn("Could not determine amount for action string: {}", action_str);
    return -1;
}

} // namespace gto_solver
