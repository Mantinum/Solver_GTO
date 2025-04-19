#include "action_abstraction.h" // Corrected include
#include "game_state.h" // Corrected include

#include <iostream>
#include <vector>
#include <string>
#include <set>       // For std::set (used for unique actions)
#include <cmath>     // For std::round, std::max
#include <algorithm> // For std::min
#include <map>       // For mapping context to actions
#include <sstream>   // Include stringstream for formatting doubles
#include <iomanip>   // Include iomanip for setprecision

#include "spdlog/spdlog.h" // Include spdlog

namespace gto_solver {

// Assume Big Blind size is needed for calculations
// TODO: Make this configurable or get from GameState if possible
const int BIG_BLIND_SIZE = 2;

// Helper function to create action string with amount
std::string create_action_string(const std::string& base, double value, const std::string& unit) {
    // Format to one decimal place if needed, otherwise integer
    std::string val_str;
    if (std::abs(value - std::round(value)) < 1e-5) { // Check if it's effectively an integer
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
    std::set<std::string> actions_set; // Use set to avoid duplicates initially
    int current_player = current_state.get_current_player();
    int player_stack = current_state.get_player_stacks()[current_player];
    int amount_to_call = current_state.get_amount_to_call(current_player);
    int current_bet = current_state.get_bet_this_round(current_player);
    Street street = current_state.get_current_street();
    int effective_stack = current_state.get_effective_stack(current_player); // Use helper

    if (player_stack <= 0) {
        return {}; // No actions if player has no stack
    }

    // 1. Fold Action (Always possible if facing a bet)
    if (amount_to_call > 0) {
        actions_set.insert("fold");
    }

    // 2. Check or Call Action
    if (amount_to_call == 0) {
        actions_set.insert("check");
    } else {
        // Can always attempt to call (even if it results in all-in for less)
        actions_set.insert("call");
    }

    // 3. Bet or Raise Actions (Only if stack > amount_to_call)
    if (player_stack > amount_to_call) {
        bool facing_bet_or_raise = amount_to_call > 0;
        int num_raises = current_state.get_raises_this_street(); // Use helper
        int num_limpers = current_state.get_num_limpers(); // Use helper

        // --- Preflop Abstraction ---
        if (street == Street::PREFLOP) {
            int sb_index = (current_state.get_button_position() + 1) % current_state.get_num_players();
             if (current_state.get_num_players() == 2) sb_index = current_state.get_button_position(); // HU case

            if (!facing_bet_or_raise && num_limpers == 0) { // RFI or SB complete/raise vs BB check
                 if (current_player == sb_index && current_state.get_num_players() == 2) { // SB first action HU
                      actions_set.insert("raise_3x"); // Standard SB open size
                      // Optionally add Limp for SB HU/BvB
                      // actions_set.insert("call"); // Represent Limp as Call
                 } else if (current_state.is_first_to_act_preflop(current_player)) { // RFI from other positions
                      // Depth-dependent RFI sizing
                      double open_size_bb = 2.3; // Default for >= 40bb
                      if (effective_stack < 25 * BIG_BLIND_SIZE) open_size_bb = 2.0;
                      else if (effective_stack < 35 * BIG_BLIND_SIZE) open_size_bb = 2.1;
                      else if (effective_stack < 40 * BIG_BLIND_SIZE) open_size_bb = 2.2;
                      actions_set.insert(create_action_string("raise", open_size_bb, "bb"));
                 }
                 // BB check option is handled by adding "check" earlier
            }
            else if (!facing_bet_or_raise && num_limpers > 0) { // Facing limper(s) -> Iso-raise or Overlimp
                 actions_set.insert("call"); // Option to overlimp
                 // Iso-raise sizing (e.g., 3bb + 1bb per limper)
                 double iso_size_bb = 3.0 + num_limpers;
                 actions_set.insert(create_action_string("raise", iso_size_bb, "bb"));
            }
            else if (facing_bet_or_raise) { // Facing a raise (2bet, 3bet, 4bet...)
                 if (num_raises == 1) { // Facing an open raise -> 3Bet or Call
                      // Add standard 3bet sizings (IP vs OOP)
                      // TODO: Need position info relative to raiser
                      // Simplified: Use one size for now
                      double three_bet_size_x = 3.5; // Average size
                      actions_set.insert(create_action_string("raise", three_bet_size_x, "x"));
                 } else if (num_raises == 2) { // Facing a 3bet -> 4Bet or Call
                      // Simplified: Only offer All-in as 4bet/5bet+
                      // actions_set.insert(create_action_string("raise", 2.2, "x")); // Optional smaller 4bet
                      actions_set.insert("all_in");
                 } else { // Facing 4bet or higher
                      // Only offer All-in or Fold/Call
                       actions_set.insert("all_in");
                 }
            }
             // Always add All-in preflop if not already covered and affordable
             actions_set.insert("all_in");

        }
        // --- Postflop Abstraction ---
        else {
             if (!facing_bet_or_raise) { // Option to Bet
                  actions_set.insert(create_action_string("bet", 33, "pct")); // Use unified helper
                  actions_set.insert(create_action_string("bet", 75, "pct")); // Use unified helper
             } else { // Option to Raise
                  actions_set.insert("raise_pot"); // Pot size raise
                  actions_set.insert(create_action_string("raise", 2.5, "x")); // Use unified helper
             }
             // Always add All-in postflop if affordable
             actions_set.insert("all_in");
        }
    }

    // --- Final Filtering ---
    std::vector<std::string> final_actions;
    std::set<int> unique_amounts; // Track amounts to avoid redundant actions like raise_3x == all_in
    int all_in_amount = -1; // Calculate only if needed

    for (const std::string& action_str : actions_set) {
        int amount = get_action_amount(action_str, current_state);

        if (amount == -1) { // Fold, Check, Call
            final_actions.push_back(action_str);
        } else {
            // Check if amount is valid (greater than current bet + call amount, unless it's all-in)
            int min_legal_total_bet = current_bet + amount_to_call + std::max(1, current_state.get_last_raise_size()); // Simplified min raise check
             if (amount == current_bet + player_stack) { // Is this action effectively all-in?
                 if (all_in_amount == -1) all_in_amount = amount; // Calculate all_in amount once
                 if (unique_amounts.find(all_in_amount) == unique_amounts.end()) {
                      final_actions.push_back("all_in"); // Prefer "all_in" string
                      unique_amounts.insert(all_in_amount);
                 }
             } else if (amount >= min_legal_total_bet) { // Is it a valid raise/bet size?
                  if (unique_amounts.find(amount) == unique_amounts.end()) {
                       final_actions.push_back(action_str);
                       unique_amounts.insert(amount);
                  }
             } else {
                  // spdlog::trace("Filtering out invalid/sub-minimal action: {} (amount {})", action_str, amount);
             }
        }
    }

    // Ensure "all_in" is present if it's the only valid raise/bet option left after filtering
    if (player_stack > amount_to_call && unique_amounts.empty() && actions_set.count("all_in")) {
         if (all_in_amount == -1) all_in_amount = get_action_amount("all_in", current_state);
         if (all_in_amount > current_bet + amount_to_call) { // Check if all-in is actually a valid raise
              // Check if "all_in" wasn't already added
              bool already_added = false;
              for(const auto& fa : final_actions) { if (fa == "all_in") { already_added = true; break; } }
              if (!already_added) final_actions.push_back("all_in");
         }
    }


    // Sort actions for consistency (optional, but good for debugging/comparison)
    // std::sort(final_actions.begin(), final_actions.end());

    return final_actions;
}


// Helper to calculate the bet/raise amount for a given action string and state
int ActionAbstraction::get_action_amount(const std::string& action_str, const GameState& current_state) const {
    int current_player = current_state.get_current_player();
    int player_stack = current_state.get_player_stacks()[current_player];
    int amount_to_call = current_state.get_amount_to_call(current_player);
    int current_pot = current_state.get_pot_size();
    int current_bet = current_state.get_bet_this_round(current_player);

    // Calculate effective pot size (including current round bets) for pot-relative sizing
    int effective_pot = current_pot; // Pot size *before* current action
    // Correct effective pot: pot + all bets currently on the table
    effective_pot = current_state.get_pot_size();


    if (action_str == "fold" || action_str == "call" || action_str == "check") {
        return -1; // No amount associated
    } else if (action_str == "all_in") {
        return player_stack + current_bet; // Total amount committed
    } else if (action_str.find("raise_") != std::string::npos || action_str.find("bet_") != std::string::npos) {
        int target_total_bet = -1;

        // --- Preflop BB Raise (e.g., raise_2.3bb) ---
        if (action_str.find("bb") != std::string::npos) {
             size_t start = action_str.find('_') + 1;
             size_t end = action_str.find("bb");
             if (start != std::string::npos && end != std::string::npos && end > start) {
                 try {
                     double bbs = std::stod(action_str.substr(start, end - start));
                     target_total_bet = static_cast<int>(std::round(bbs * BIG_BLIND_SIZE));
                 } catch (...) { /* Handle exceptions */ spdlog::error("Invalid BB amount in action: {}", action_str); }
             }
        }
        // --- Percentage Bet (e.g., bet_33pct) ---
        else if (action_str.find("bet_") != std::string::npos && action_str.find("pct") != std::string::npos) {
             size_t start = action_str.find('_') + 1;
             size_t end = action_str.find("pct");
             if (start != std::string::npos && end != std::string::npos && end > start) {
                 try {
                     double fraction = std::stod(action_str.substr(start, end - start)) / 100.0;
                     int bet_increment = static_cast<int>(std::round(effective_pot * fraction));
                     target_total_bet = current_bet + bet_increment; // Bet amount is total commit
                 } catch (...) { /* Handle exceptions */ spdlog::error("Invalid PCT amount in action: {}", action_str); }
              }
          }
        // --- Percentage Raise (e.g., raise_75pct) ---
         else if (action_str.find("raise_") != std::string::npos && action_str.find("pct") != std::string::npos) {
             size_t start = action_str.find('_') + 1;
             size_t end = action_str.find("pct");
             if (start != std::string::npos && end != std::string::npos && end > start) {
                 try {
                     double fraction = std::stod(action_str.substr(start, end - start)) / 100.0;
                     int pot_after_call = effective_pot + amount_to_call; // Pot size *after* calling
                     int raise_increment = static_cast<int>(std::round(pot_after_call * fraction));
                     target_total_bet = current_bet + amount_to_call + raise_increment;
                 } catch (...) { /* Handle exceptions */ spdlog::error("Invalid PCT amount in action: {}", action_str); }
              }
          }
          // --- Multiplier Raise (e.g., raise_3.5x) ---
          else if (action_str.find("raise_") != std::string::npos && action_str.find("x") == action_str.length() - 1) {
                size_t start = action_str.find('_') + 1;
                size_t end = action_str.find("x");
                if (start != std::string::npos && end != std::string::npos && end > start) {
                    try {
                        double multiplier = std::stod(action_str.substr(start, end - start));
                        int last_bet_or_raise_amount = current_state.get_last_raise_size(); // Use the increment size
                        if (last_bet_or_raise_amount <= 0) { // Facing limp or BB preflop, or check postflop
                             // Base raise on the pot size instead? Or BB? Use BB for now preflop.
                             last_bet_or_raise_amount = BIG_BLIND_SIZE;
                             spdlog::warn("Multiplier raise '{}' used when last raise size <=0. Basing multiplier on BB.", action_str);
                        }
                        // Raise BY multiplier * previous raise increment
                        int raise_increment = static_cast<int>(std::round(static_cast<double>(last_bet_or_raise_amount) * multiplier));
                        target_total_bet = current_bet + amount_to_call + raise_increment;

                    } catch (...) { /* Handle exceptions */ spdlog::error("Invalid multiplier amount in action: {}", action_str); }
                }
          }
          // --- Pot Size Raise ---
          else if (action_str == "raise_pot") {
             int pot_after_call = effective_pot + amount_to_call;
             int pot_raise_increment = pot_after_call;
             target_total_bet = current_bet + amount_to_call + pot_raise_increment;
         }

        if (target_total_bet != -1) {
            // --- Apply Min-Raise Rules ---
            int last_raise_size = current_state.get_last_raise_size();
            int min_raise_increment = (last_raise_size > 0) ? last_raise_size : BIG_BLIND_SIZE;
            int min_legal_total_bet = current_bet + amount_to_call + min_raise_increment;

            // Ensure the calculated total bet meets the minimum legal raise
            int final_total_bet = std::max(target_total_bet, min_legal_total_bet);

            // Cannot bet more than stack
            return std::min(player_stack + current_bet, final_total_bet);
        }
    }

    // Fallback or error
    spdlog::warn("Could not determine amount for action string: {}", action_str);
    return -1;
}

} // namespace gto_solver
