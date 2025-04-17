#include "action_abstraction.h" // Corrected include
#include "game_state.h" // Corrected include

#include <iostream> // Keep for std::cerr fallback? Or remove.
#include <vector>
#include <string>
#include "spdlog/spdlog.h" // Include spdlog
#include <algorithm> // For std::min, std::max
#include <cmath>     // For std::round
#include <set>       // For std::set (used for unique actions)

namespace gto_solver {

// Assume Big Blind size is needed for calculations (could be passed or stored)
const int BIG_BLIND_SIZE = 2; // Example BB size

// Helper function to create action string from fraction
std::string create_bet_action_string(double fraction) {
    return "bet_" + std::to_string(static_cast<int>(fraction * 100)) + "pct";
}

// Helper function to create action string from fraction (for raises)
std::string create_raise_action_string(double fraction) {
    return "raise_" + std::to_string(static_cast<int>(fraction * 100)) + "pct";
}


ActionAbstraction::ActionAbstraction() {
    spdlog::debug("ActionAbstraction created");
}

std::vector<std::string> ActionAbstraction::get_possible_actions(const GameState& current_state) const {
    std::vector<std::string> actions;
    int current_player = current_state.get_current_player();
    int player_stack = current_state.get_player_stacks()[current_player];
    int amount_to_call = current_state.get_amount_to_call(current_player);

    if (player_stack <= 0) {
        return {}; // No actions if player has no stack (folded or all-in previously)
    }

    // 1. Fold is always possible
    actions.push_back("fold");

    // 2. Check or Call
    if (amount_to_call == 0) {
        actions.push_back("check");
    } else {
        // Can always attempt to call (even if it results in all-in for less)
        actions.push_back("call");
    }

    // 3. Bet or Raise
    // Can only bet/raise if stack > amount_to_call (must have chips left after calling)
    if (player_stack > amount_to_call) {
        bool is_facing_bet_or_raise = amount_to_call > 0;

        // --- Preflop Sizing ---
        if (current_state.get_current_street() == Street::PREFLOP) {
            if (!is_facing_bet_or_raise) { // Open Raise Sizings
                 // Add multiple open raise sizes
                 actions.push_back("raise_2.2bb");
                 actions.push_back("raise_2.5bb");
                 actions.push_back("raise_3bb");
            } else { // Re-raise (3bet, 4bet...)
                 // Add multiplier-based re-raise sizing
                 actions.push_back("raise_2.2x"); // Added 2.2x re-raise
                 actions.push_back("raise_2.5x"); // Added 2.5x re-raise
                 actions.push_back("raise_3x");   // Kept 3x re-raise
                 // TODO: Add more sophisticated re-raise sizing logic if needed (e.g., pot size)
            }
        }
        // --- Postflop Sizing ---
        else {
             if (!is_facing_bet_or_raise) { // Bet sizing
                 actions.push_back(create_bet_action_string(BET_FRACTION_SMALL));  // e.g., "bet_33pct"
                 actions.push_back(create_bet_action_string(BET_FRACTION_MEDIUM)); // e.g., "bet_50pct"
                 actions.push_back(create_bet_action_string(BET_FRACTION_LARGE));  // e.g., "bet_75pct"
                 // actions.push_back(create_bet_action_string(BET_FRACTION_OVERBET)); // If needed later
             } else { // Raise sizing
                 // Add more sophisticated raise sizing logic (e.g., 2x, 2.5x raise)
                 actions.push_back("raise_pot");
                 actions.push_back("raise_2.0x"); // Added 2.0x raise
                 actions.push_back("raise_2.5x"); // Added 2.5x raise
                 // actions.push_back(create_raise_action_string(BET_FRACTION_LARGE)); // Example: raise 75% pot
             }
        }

        // Always allow going all-in if possible (if not already covered by other sizes)
        // TODO: Check if any generated bet/raise size is already effectively all-in
        actions.push_back("all_in");
    }

    // TODO: Remove duplicate actions if generated (e.g., if raise_pot results in all-in)
    // Example: Sort and unique, but requires careful handling if amounts differ slightly
    // Use a set to store unique valid actions
    std::set<std::string> unique_actions;
    for(const auto& action : actions) {
        unique_actions.insert(action); // Add fold, check/call
    }

    // Calculate potential bet/raise actions and add unique ones
    if (player_stack > amount_to_call) {
        std::vector<std::string> potential_bet_raise_strs;
        bool is_facing_bet_or_raise = amount_to_call > 0;

        // --- Generate potential action strings ---
        if (current_state.get_current_street() == Street::PREFLOP) {
            if (!is_facing_bet_or_raise) { // Open Raise Sizings
                 potential_bet_raise_strs.push_back("raise_2.2bb");
                 potential_bet_raise_strs.push_back("raise_2.5bb");
                 potential_bet_raise_strs.push_back("raise_3bb");
            } else { // Re-raise (3bet, 4bet...)
                 potential_bet_raise_strs.push_back("raise_2.2x"); // Added 2.2x re-raise
                 potential_bet_raise_strs.push_back("raise_2.5x"); // Added 2.5x re-raise
                 potential_bet_raise_strs.push_back("raise_3x");   // Kept 3x re-raise
                 // potential_bet_raise_strs.push_back("raise_pot"); // Add if needed
            }
        } else { // Postflop Sizing
             if (!is_facing_bet_or_raise) { // Bet sizing
                 potential_bet_raise_strs.push_back(create_bet_action_string(BET_FRACTION_SMALL));
                 potential_bet_raise_strs.push_back(create_bet_action_string(BET_FRACTION_MEDIUM));
                  potential_bet_raise_strs.push_back(create_bet_action_string(BET_FRACTION_LARGE));
              } else { // Raise sizing
                  potential_bet_raise_strs.push_back("raise_pot");
                  potential_bet_raise_strs.push_back("raise_2.0x"); // Added 2.0x raise
                  potential_bet_raise_strs.push_back("raise_2.5x"); // Added 2.5x raise
                  // potential_bet_raise_strs.push_back(create_raise_action_string(BET_FRACTION_LARGE));
              }
         }
        potential_bet_raise_strs.push_back("all_in"); // Always consider all-in

        // --- Calculate amounts and filter unique actions ---
        int all_in_amount = get_action_amount("all_in", current_state);
        std::set<int> unique_amounts; // Track amounts to detect duplicates

        for (const std::string& action_str : potential_bet_raise_strs) {
            int amount = get_action_amount(action_str, current_state);
            if (amount != -1) { // Check if amount calculation was valid
                 // Ensure the action is legal (amount > amount_to_call or it's an all-in)
                 int current_bet = current_state.get_bet_this_round(current_player);
                 if (amount > current_bet + amount_to_call || amount == player_stack + current_bet) {
                     // Check if this amount leads to the same result as all-in
                     if (amount == all_in_amount) {
                         unique_actions.insert("all_in"); // Prefer "all_in" string
                         unique_amounts.insert(amount);
                     } else if (unique_amounts.find(amount) == unique_amounts.end()) {
                         // Only add if the resulting amount is unique
                         unique_actions.insert(action_str);
                         unique_amounts.insert(amount);
                     }
                 }
            }
        }
    }

    // Convert set back to vector
    return std::vector<std::string>(unique_actions.begin(), unique_actions.end());
}


// Helper to calculate the bet/raise amount for a given action string and state
int ActionAbstraction::get_action_amount(const std::string& action_str, const GameState& current_state) const {
    int current_player = current_state.get_current_player();
    int player_stack = current_state.get_player_stacks()[current_player];
    int amount_to_call = current_state.get_amount_to_call(current_player);
    int current_pot = current_state.get_pot_size();
    int current_bet = current_state.get_bet_this_round(current_player); // Amount already invested this round

    // Calculate effective pot size (including current round bets) for pot-relative sizing
    int effective_pot = current_pot;
    for(int bet : current_state.get_bets_this_round()) {
         effective_pot += bet;
    }

    if (action_str == "fold" || action_str == "call" || action_str == "check") {
        return -1; // No amount associated
    } else if (action_str == "all_in") {
        // Total amount committed after going all-in is the player's starting stack for the round
        return player_stack + current_bet;
    } else if (action_str.find("raise_") != std::string::npos || action_str.find("bet_") != std::string::npos) {
        int target_total_bet = -1; // The total amount the player wants to have bet this round

        // --- Preflop BB Raise (Handles 2.2bb, 2.5bb, 3bb etc.) ---
        if (action_str.find("bb") != std::string::npos) {
             size_t start = action_str.find('_') + 1;
             size_t end = action_str.find("bb");
             if (start != std::string::npos && end != std::string::npos && end > start) {
                 try {
                     // Use stod to handle decimals like 2.2, 2.5
                     double bbs = std::stod(action_str.substr(start, end - start));
                     target_total_bet = static_cast<int>(std::round(bbs * BIG_BLIND_SIZE));
                 } catch (const std::invalid_argument& e) {
                     spdlog::error("Invalid BB amount (stod) in action string: {}", action_str);
                 } catch (const std::out_of_range& e) {
                     spdlog::error("BB amount out of range in action string: {}", action_str);
                 }
             }
        }
        // --- Percentage Bet/Raise (Pot Relative) ---
        else if (action_str.find("pct") != std::string::npos) {
             size_t start = action_str.find('_') + 1;
             size_t end = action_str.find("pct");
             if (start != std::string::npos && end != std::string::npos && end > start) {
                 try {
                     double fraction = std::stod(action_str.substr(start, end - start)) / 100.0;
                     int bet_or_raise_increment = static_cast<int>(std::round(effective_pot * fraction));
                     // Amount is added ON TOP of the call amount if raising
                     target_total_bet = current_bet + amount_to_call + bet_or_raise_increment;
                 } catch (const std::invalid_argument& e) {
                     spdlog::error("Invalid percentage amount in action string: {}", action_str);
                 } catch (const std::out_of_range& e) {
                     spdlog::error("Percentage amount out of range in action string: {}", action_str);
                 }
              }
          }
          // --- Multiplier Raise (Handles 2.0x, 2.5x etc.) ---
          else if (action_str.find("raise_") != std::string::npos && action_str.find("x") == action_str.length() - 1) {
                size_t start = action_str.find('_') + 1;
                size_t end = action_str.find("x");
                if (start != std::string::npos && end != std::string::npos && end > start) {
                    try {
                        double multiplier = std::stod(action_str.substr(start, end - start));
                        int last_bet_or_raise = 0;
                        // Find the amount of the last bet/raise this street
                        int max_bet_this_round = 0;
                        for(int bet : current_state.get_bets_this_round()) max_bet_this_round = std::max(max_bet_this_round, bet);
                        // The amount the current player needs to call represents the size of the bet/raise they are facing
                        last_bet_or_raise = amount_to_call; // This is the *additional* amount needed to call

                        // If amount_to_call is 0, it means we are the first to bet this street (postflop),
                        // which shouldn't happen for a "raise_Nx" action. Log a warning.
                        // Or, if preflop, it might be an open raise scenario, but these use "bb" sizing.
                        // Let's assume this sizing is primarily for *re-raises*.
                        if (last_bet_or_raise <= 0) {
                             spdlog::warn("Multiplier raise '{}' used when not facing a bet/raise (amount_to_call={}). Using BB as fallback increment.", action_str, amount_to_call);
                             last_bet_or_raise = BIG_BLIND_SIZE; // Fallback, might need refinement
                        }

                        int raise_increment = static_cast<int>(std::round(static_cast<double>(last_bet_or_raise) * multiplier));
                        target_total_bet = current_bet + amount_to_call + raise_increment;

                    } catch (const std::invalid_argument& e) {
                        spdlog::error("Invalid multiplier amount (stod) in action string: {}", action_str);
                    } catch (const std::out_of_range& e) {
                        spdlog::error("Multiplier amount out of range in action string: {}", action_str);
                    }
                }
          }
          // --- Preflop 3x Raise ---
          else if (action_str == "raise_3x") {
              int last_raise = current_state.get_last_raise_size();
              if (last_raise <= 0) { // Should not happen if facing a bet/raise (unless it's the first raise vs BB)
                  // If facing only the BB post, the 'last_raise' might be BB-SB.
                  // A more robust way is to consider the amount needed to call.
                  int effective_last_raise = (amount_to_call > 0) ? amount_to_call : BIG_BLIND_SIZE;
                  spdlog::warn("raise_3x called but last_raise_size is not positive ({}). Using effective raise: {}", last_raise, effective_last_raise);
                  last_raise = effective_last_raise;
              }
              // A 3x raise usually means raising *by* 3 times the previous bet/raise amount,
              // or raising *to* 3 times the previous bet/raise amount. Let's assume raising *to* 3x the previous total bet size.
              // Example: Blinds 1/2. BTN raises to 6 (BB). SB wants to 3bet 3x. Raise TO 18.
              // Example: Blinds 1/2. BTN raises to 6. SB calls 6. BB wants to squeeze 3x. Raise TO 18? Or 3x the raise size (6-2=4)? Let's use 3x the raise size increment.
              // Raise increment = 3 * last_raise_size.
              int raise_increment = last_raise * 3;
              target_total_bet = current_bet + amount_to_call + raise_increment;
          }
          // --- Pot Size Raise (Postflop for now) ---
          else if (action_str == "raise_pot") { // Applies to postflop raise for now
             // Pot size raise amount = size of the pot *after* the player calls
             int pot_after_call = effective_pot + amount_to_call;
             int pot_raise_increment = pot_after_call;
             target_total_bet = current_bet + amount_to_call + pot_raise_increment;
         }

        if (target_total_bet != -1) {
            // --- Apply Min-Raise Rules ---
            int last_raise_size = current_state.get_last_raise_size();
            // If no raise has occurred yet this street, the minimum raise is typically the size of the big blind (or the initial bet size postflop).
            // For simplicity, we'll use the Big Blind size as the default minimum increment if last_raise_size is 0.
            // A more robust implementation might track the initial bet size postflop.
            int min_raise_increment = (last_raise_size > 0) ? last_raise_size : BIG_BLIND_SIZE;

            // Calculate the minimum legal total bet amount for this player
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
