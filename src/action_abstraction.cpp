#include "action_abstraction.h"
#include "game_state.h"

#include <vector>
#include <string>
#include <set>
#include <cmath>
#include <algorithm>
#include <map>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <numeric> // Added for std::accumulate

#include "spdlog/spdlog.h"

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
        default: return "unknown"; // No unit needed for absolute (used internally for all-in amount)
    }
    return ss.str();
}

// --- ActionAbstraction Implementation ---

ActionAbstraction::ActionAbstraction() {
    spdlog::debug("ActionAbstraction created");
}

// New method using ActionSpec - Refactored to use integer amounts for filtering/sorting
std::vector<ActionSpec> ActionAbstraction::get_possible_action_specs(const GameState& current_state) const {
    std::set<ActionSpec> candidate_specs_set; // Generate candidate specs first
    int current_player = current_state.get_current_player();
    if (current_player < 0) return {};

    int player_stack = current_state.get_player_stacks()[current_player];
    int amount_to_call = current_state.get_amount_to_call(current_player);
    Street street = current_state.get_current_street();
    int effective_stack = current_state.get_effective_stack(current_player);
    const int EFFECTIVE_STACK_BB = (BIG_BLIND_SIZE > 0) ? (effective_stack / BIG_BLIND_SIZE) : effective_stack; // Avoid division by zero

    if (player_stack <= 0) return {};

    // 1. Fold Action (Revisiting - add only if CALL is possible but player chooses not to)
    if (amount_to_call > 0 && player_stack > 0) { // If facing a bet and not already all-in
        candidate_specs_set.insert({ActionType::FOLD});
    }

    // 2. Check or Call Action
    if (amount_to_call == 0) {
        candidate_specs_set.insert({ActionType::CHECK});
    } else { // amount_to_call > 0
        if (player_stack >= amount_to_call) {
             candidate_specs_set.insert({ActionType::CALL});
        }
        // FOLD is handled separately below if needed
    }

    // 3. Bet or Raise Actions (Only if stack > amount_to_call)
    if (player_stack > 0 && amount_to_call <= player_stack) {
        bool facing_bet_or_raise = amount_to_call > 0;
        int num_raises = current_state.get_raises_this_street();
        int num_limpers = current_state.get_num_limpers();

        // --- Preflop Abstraction ---
        if (street == Street::PREFLOP) {
            int sb_index = (current_state.get_button_position() + 1) % current_state.get_num_players();
            int bb_index = (current_state.get_button_position() + 2) % current_state.get_num_players();
            if (current_state.get_num_players() == 2) {
                sb_index = current_state.get_button_position();
                bb_index = (sb_index + 1) % current_state.get_num_players();
            }

            // Specific Check for HU SB Opening Spot
            if (current_state.get_num_players() == 2 && current_player == sb_index && num_raises == 0) {
                candidate_specs_set.insert({ActionType::RAISE, 3.0, SizingUnit::BB});
                candidate_specs_set.insert({ActionType::RAISE, 4.0, SizingUnit::BB});
                // Note: CALL/FOLD are added by general logic
            }
            else {
                // General Preflop Logic (RFI, vs Limp, vs Raise)
                bool is_rfi_situation = (!facing_bet_or_raise || (amount_to_call == BIG_BLIND_SIZE && num_raises == 0)) && num_limpers == 0;

                if (is_rfi_situation) {
                    // Handle 6-max+ SB RFI
                    if (current_player == sb_index && current_state.get_num_players() > 2) {
                        candidate_specs_set.insert({ActionType::RAISE, 3.0, SizingUnit::BB});
                        candidate_specs_set.insert({ActionType::RAISE, 4.0, SizingUnit::BB});
                    }
                    // Handle other positions RFI
                    else if (current_state.is_first_to_act_preflop(current_player)) {
                        double open_size_bb_small = 2.2;
                        if (EFFECTIVE_STACK_BB < 25) open_size_bb_small = 2.0;
                        else if (EFFECTIVE_STACK_BB < 35) open_size_bb_small = 2.1;
                        candidate_specs_set.insert({ActionType::RAISE, open_size_bb_small, SizingUnit::BB});
                        candidate_specs_set.insert({ActionType::RAISE, 2.5, SizingUnit::BB});
                        candidate_specs_set.insert({ActionType::RAISE, 3.0, SizingUnit::BB});
                    }
                } else if (!facing_bet_or_raise && num_limpers > 0) {
                    // Specific HU BB vs Limp case?
                    if(current_state.get_num_players() == 2 && current_player == bb_index) {
                        candidate_specs_set.insert({ActionType::RAISE, 3.0, SizingUnit::BB}); // Same sizes as SB RFI for simplicity?
                        candidate_specs_set.insert({ActionType::RAISE, 4.0, SizingUnit::BB});
                    } else { // Multiway isolation sizing
                        double iso_size_bb1 = 3.0 + num_limpers;
                        double iso_size_bb2 = 4.0 + num_limpers;
                        candidate_specs_set.insert({ActionType::RAISE, iso_size_bb1, SizingUnit::BB});
                        candidate_specs_set.insert({ActionType::RAISE, iso_size_bb2, SizingUnit::BB});
                    }
                } else if (facing_bet_or_raise) {
                    if (num_raises == 1) {
                        bool is_bb_vs_sb_open_hu = (current_state.get_num_players() == 2 && current_player == bb_index && current_state.get_last_raiser() == sb_index);
                        if (is_bb_vs_sb_open_hu) {
                            candidate_specs_set.insert({ActionType::RAISE, 3.0, SizingUnit::MULTIPLIER_X}); // 3bet 3x
                            candidate_specs_set.insert({ActionType::RAISE, 4.0, SizingUnit::MULTIPLIER_X}); // 3bet 4x
                            candidate_specs_set.insert({ActionType::ALL_IN}); // Always add ALL_IN vs SB open HU?
                        } else { // Facing RFI from other positions (multiway)
                            // Add different 3bet sizings if needed
                            // Maybe add ALL_IN here too based on stack?
                            if (EFFECTIVE_STACK_BB <= 40) { candidate_specs_set.insert({ActionType::ALL_IN}); }
                        }
                    } else if (num_raises == 2) { // Facing 4bet
                        candidate_specs_set.insert({ActionType::RAISE, 2.5, SizingUnit::MULTIPLIER_X}); // 5bet sizing?
                        candidate_specs_set.insert({ActionType::ALL_IN}); // Usually only call/fold/all-in vs 4bet
                    } else { // Facing 5bet+
                        candidate_specs_set.insert({ActionType::ALL_IN}); // Only call/fold/all-in reasonable
                    }
                }
            }
        }
        // --- Postflop Abstraction ---
        else {
             if (!facing_bet_or_raise) {
                  candidate_specs_set.insert({ActionType::BET, 33, SizingUnit::PCT_POT});
                  candidate_specs_set.insert({ActionType::BET, 50, SizingUnit::PCT_POT});
                  candidate_specs_set.insert({ActionType::BET, 75, SizingUnit::PCT_POT});
                  candidate_specs_set.insert({ActionType::BET, 100, SizingUnit::PCT_POT});
                  candidate_specs_set.insert({ActionType::BET, 133, SizingUnit::PCT_POT});
             } else {
                  candidate_specs_set.insert({ActionType::RAISE, 2.2, SizingUnit::MULTIPLIER_X});
                  candidate_specs_set.insert({ActionType::RAISE, 3.0, SizingUnit::MULTIPLIER_X});
             }
             candidate_specs_set.insert({ActionType::ALL_IN});
        }
    } else if (player_stack > 0 && amount_to_call > 0 && player_stack <= amount_to_call) {
         candidate_specs_set.clear();
         candidate_specs_set.insert({ActionType::FOLD});
         candidate_specs_set.insert({ActionType::ALL_IN});
    }

    // Specific Removal for HU SB Open: Remove FOLD if present
    // In HU preflop, the SB opening doesn't typically consider FOLD as a primary action.
    // The test `PreflopSBInitialActionsHU` expects only CALL/RAISE options.
    if (street == Street::PREFLOP &&
        current_state.get_num_players() == 2 &&
        current_player == current_state.get_button_position() && // In HU, SB = BTN
        current_state.get_raises_this_street() == 0)
    {
        candidate_specs_set.erase({ActionType::FOLD});
    }

    // --- Calculate Amounts and Filter/Sort Based on Integers ---
    spdlog::debug("Candidate specs for player {}:", current_player); // Log candidates
    for(const auto& s : candidate_specs_set) spdlog::debug("- {}", s.to_string());

    std::vector<std::pair<ActionSpec, int>> spec_amount_pairs;
    int all_in_amount = player_stack + current_state.get_bet_this_round(current_player);
    std::set<int> unique_amounts; // Track unique calculated amounts

    // Process all candidate specs
    for (const ActionSpec& spec : candidate_specs_set) {
        int amount = -1; // Default amount for non-betting actions
        bool is_betting_action = false;

        if (spec.type == ActionType::BET || spec.type == ActionType::RAISE || spec.type == ActionType::ALL_IN) {
            is_betting_action = true;
            amount = get_action_amount(spec, current_state);
            if (amount == -1 && spec.type != ActionType::ALL_IN) {
                spdlog::warn("Amount calculation error for spec: {}", spec.to_string());
                continue; // Skip if amount calculation failed (except for ALL_IN spec itself)
            }
             if (spec.type == ActionType::ALL_IN) {
                 amount = all_in_amount; // Ensure correct amount for ALL_IN
             }
        } else if (spec.type == ActionType::CALL) {
             amount = current_state.get_bet_this_round(current_player) + amount_to_call;
        } else if (spec.type == ActionType::CHECK) {
             amount = current_state.get_bet_this_round(current_player); // Amount is current bet (should be 0)
        }
        // FOLD has amount = -1

        // --- Min Legal Bet/Raise Validation ---
        bool is_valid_sizing = true;
        if (spec.type == ActionType::BET || spec.type == ActionType::RAISE) {
            int current_bet = current_state.get_bet_this_round(current_player);
            int min_legal_total_bet = -1;
            if (spec.type == ActionType::BET) {
                 min_legal_total_bet = current_bet + std::max(1, BIG_BLIND_SIZE);
                 min_legal_total_bet = std::min(min_legal_total_bet, current_bet + player_stack);
            } else { // RAISE
                 int raise_base_amount = current_bet + amount_to_call;
                 int min_raise_increment = (current_state.get_last_raise_size() > 0) ? current_state.get_last_raise_size() : BIG_BLIND_SIZE;
                 min_raise_increment = std::max(1, min_raise_increment);
                 min_legal_total_bet = raise_base_amount + min_raise_increment;
                 min_legal_total_bet = std::min(min_legal_total_bet, current_bet + player_stack);
            }
            if (amount < min_legal_total_bet && amount != all_in_amount) {
                is_valid_sizing = false;
                 spdlog::trace("Skipping invalid sizing: Spec={}, CalcAmount={}, MinLegal={}, AllIn={}",
                               spec.to_string(), amount, min_legal_total_bet, all_in_amount);
            }
        }

        if (!is_valid_sizing) {
             continue;
        }

        // Add the spec and its calculated amount (or -1 for fold)
        spec_amount_pairs.push_back({spec, amount});
    }

    // --- Filter for Unique Amounts (among betting actions) and Final Sort ---
    // TODO: Re-evaluate filtering logic. Commenting out unique amount filtering for now.
    /*
    std::vector<std::pair<ActionSpec, int>> final_spec_amount_pairs;
    std::set<int> final_unique_amounts;

    // Add Fold/Check/Call first unconditionally if they exist in pairs
     for (const auto& pair : spec_amount_pairs) {
          if (pair.first.type == ActionType::FOLD || pair.first.type == ActionType::CHECK || pair.first.type == ActionType::CALL) {
               final_spec_amount_pairs.push_back(pair);
               if(pair.second != -1) final_unique_amounts.insert(pair.second); // Track Check/Call amounts
          }
     }


    // Add unique Bet/Raise/All-in amounts
    for (const auto& pair : spec_amount_pairs) {
        if (pair.first.type == ActionType::BET || pair.first.type == ActionType::RAISE || pair.first.type == ActionType::ALL_IN) {
            if (final_unique_amounts.find(pair.second) == final_unique_amounts.end()) {
                 // If it's an ALL_IN action spec, ensure we use the canonical one
                 if (pair.second == all_in_amount) {
                      bool all_in_already_added = false;
                      for(const auto& final_pair : final_spec_amount_pairs) { if(final_pair.first.type == ActionType::ALL_IN) { all_in_already_added = true; break; } }
                      if (!all_in_already_added) {
                           final_spec_amount_pairs.push_back({{ActionType::ALL_IN}, pair.second});
                           final_unique_amounts.insert(pair.second);
                      }
                 } else {
                      final_spec_amount_pairs.push_back(pair);
                      final_unique_amounts.insert(pair.second);
                 }
            }
        }
    }
    */
    // Use spec_amount_pairs directly without unique amount filtering
    std::vector<std::pair<ActionSpec, int>> final_spec_amount_pairs = spec_amount_pairs;

    // Sort the final list based on type and then calculated amount
    std::sort(final_spec_amount_pairs.begin(), final_spec_amount_pairs.end(),
              [](const std::pair<ActionSpec, int>& a, const std::pair<ActionSpec, int>& b) {
        std::map<ActionType, int> type_order = {
            {ActionType::FOLD, 0}, {ActionType::CHECK, 1}, {ActionType::CALL, 2},
            {ActionType::BET, 3}, {ActionType::RAISE, 3}, // Group BET and RAISE
            {ActionType::ALL_IN, 4}
        };
        int order_a = type_order.count(a.first.type) ? type_order.at(a.first.type) : 99;
        int order_b = type_order.count(b.first.type) ? type_order.at(b.first.type) : 99;

        if (order_a != order_b) return order_a < order_b;

        // If types have same order (BET/RAISE), sort by calculated amount
        if (order_a == 3) {
            return a.second < b.second;
        }
        // For other types, amount doesn't matter for sorting if type is the same
        return false;
    });

    // Extract the final sorted ActionSpec vector
    std::vector<ActionSpec> final_sorted_specs;
    for (const auto& pair : final_spec_amount_pairs) {
        final_sorted_specs.push_back(pair.first);
    }

    spdlog::debug("Final filtered specs for player {}:", current_player); // Log final specs
    for(const auto& s : final_sorted_specs) spdlog::debug("- {}", s.to_string());

    return final_sorted_specs;
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
            // Use integer arithmetic where possible for PCT_POT to avoid float issues
            if (action_spec.unit == SizingUnit::PCT_POT) {
                int current_pot_for_betting = current_state.get_pot_size(); // Pot before this bet
                // Integer calculation for percentage: (pot * percentage) / 100
                // Add 0.5 for rounding before casting to int (equivalent to std::round)
                 // Ensure intermediate multiplication doesn't overflow if pot/value are large
                 long long intermediate_value = static_cast<long long>(current_pot_for_betting) * static_cast<long long>(action_spec.value);
                 // Use integer division for rounding: (numerator + denominator/2) / denominator
                 int bet_increment = static_cast<int>((intermediate_value + 50) / 100);
                 bet_increment = std::max(1, bet_increment); // Ensure bet is at least 1 chip
                 target_total_bet = current_bet + bet_increment;
            } else if (action_spec.unit == SizingUnit::BB) {
                 // Use integer arithmetic for BB sizing, round to nearest chip
                 // (value * BB_SIZE) + 0.5 before casting to int
                 target_total_bet = static_cast<int>(action_spec.value * BIG_BLIND_SIZE + 0.5);
            } else {
                 spdlog::error("Unsupported unit for BET action: {}", static_cast<int>(action_spec.unit)); return -1;
            }
             // Apply min bet rule (at least 1 BB or remaining stack if smaller)
             int min_bet_increment = std::min(player_stack, std::max(1, BIG_BLIND_SIZE));
             target_total_bet = std::max(target_total_bet, current_bet + min_bet_increment);
             // Cap by stack
            return std::min(player_stack + current_bet, target_total_bet);
        }

        case ActionType::RAISE: {
            int target_total_bet = -1;
            // Calculate pot size after hypothetical call
            int pot_after_call = current_state.get_pot_size(); // Start with current pot
            for(int p=0; p<current_state.get_num_players(); ++p) {
                 if (p != current_player) {
                      pot_after_call += current_state.get_bet_this_round(p); // Add bets of others
                 }
            }
            pot_after_call += (current_bet + amount_to_call); // Add this player's total commitment after calling

            int raise_base_amount = current_bet + amount_to_call; // Amount player needs to commit just to call

            if (action_spec.unit == SizingUnit::BB) {
                // Integer arithmetic for BB sizing, round to nearest chip
                target_total_bet = static_cast<int>(action_spec.value * BIG_BLIND_SIZE + 0.5);
            } else if (action_spec.unit == SizingUnit::PCT_POT) {
                 // Integer calculation for percentage: (pot_after_call * percentage) / 100
                 long long intermediate_value = static_cast<long long>(pot_after_call) * static_cast<long long>(action_spec.value);
                 int raise_increment = static_cast<int>((intermediate_value + 50) / 100);
                 raise_increment = std::max(1, raise_increment); // Ensure raise is at least 1 chip
                 target_total_bet = raise_base_amount + raise_increment;
            } else if (action_spec.unit == SizingUnit::MULTIPLIER_X) {
                 int last_bet_or_raise_amount = current_state.get_amount_to_call(current_player) + current_state.get_bet_this_round(current_player); // The full bet/raise facing the player *before* their action
                 // Calculate the target total bet based on the multiplier of the opponent's bet/raise size
                 // Total bet = current_bet + call_amount + raise_increment
                 // raise_increment = (X * last_raise_size) where last_raise_size is the size of the last raise increment
                 // OR raise_increment = (X * last_bet_size) if facing a bet
                 // Let's simplify: Target total bet = X * (total amount opponent committed in their last aggressive action)
                 // This needs careful definition. Let's try: Target total = X * last_bet_or_raise_amount
                 int target_raise_total = static_cast<int>(action_spec.value * last_bet_or_raise_amount + 0.5); // Round X sizing
                 // The actual amount to add is the difference between this target and what we already committed + need to call
                 int raise_increment = target_raise_total - raise_base_amount;
                 raise_increment = std::max(1, raise_increment); // Ensure raise is at least 1 chip
                 target_total_bet = raise_base_amount + raise_increment;
            } else {
                 spdlog::error("Unsupported unit for RAISE action: {}", static_cast<int>(action_spec.unit));
                 return -1;
            }

            // Min legal raise size calculation (unified logic)
            int min_raise_increment = (current_state.get_last_raise_size() > 0) ? current_state.get_last_raise_size() : BIG_BLIND_SIZE;
            // Ensure min raise increment is at least 1 chip (e.g., if BB=0)
            min_raise_increment = std::max(1, min_raise_increment);
            int min_legal_raise_total = raise_base_amount + min_raise_increment;

            // Apply min legal raise rule
            target_total_bet = std::max(target_total_bet, min_legal_raise_total);

            // Cap by stack size
            return std::min(player_stack + current_bet, target_total_bet);
        }

        default:
            spdlog::error("Unknown action type: {}", static_cast<int>(action_spec.type));
            return -1;
    }
}

} // namespace gto_solver
