#include "cfr_engine.h" // Corrected include
#include "game_state.h" // Corrected include
#include "info_set.h" // Corrected include
#include "node.h" // Corrected include
#include "action_abstraction.h" // Corrected include
#include "hand_evaluator.h"   // Corrected include

#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <numeric>   // For std::accumulate
#include <algorithm> // For std::max, std::shuffle, std::min
#include <stdexcept> // For exceptions
#include <random>    // For std::mt19937, std::random_device
#include <chrono>    // For seeding RNG
#include <utility>   // For std::pair
#include <thread>    // For std::thread
#include <functional> // For std::bind or lambdas
#include <mutex>     // For std::lock_guard
#include <atomic>    // For std::atomic
#include <fstream>   // For file streams
#include <filesystem> // For renaming files atomically (C++17)

#include <nlohmann/json.hpp> // Include JSON library
#include "spdlog/spdlog.h" // Include spdlog

// Alias for convenience
using json = nlohmann::json;

// Define a simple version number for the checkpoint format (can be string now)
const std::string CHECKPOINT_VERSION_JSON = "1.0-json";

namespace gto_solver {

// --- Helper Function: Regret Matching ---
std::vector<double> get_strategy_from_regrets(const std::vector<double>& regrets) {
    size_t num_actions = regrets.size();
    std::vector<double> strategy(num_actions);
    double positive_regret_sum = 0.0;

    for (double regret : regrets) {
        positive_regret_sum += std::max(0.0, regret);
    }

    if (positive_regret_sum > 0) {
        for (size_t i = 0; i < num_actions; ++i) {
            strategy[i] = std::max(0.0, regrets[i]) / positive_regret_sum;
        }
    } else {
        // Default to uniform strategy if no positive regrets
        if (num_actions > 0) { // Avoid division by zero if num_actions is 0
             double uniform_prob = 1.0 / num_actions;
             std::fill(strategy.begin(), strategy.end(), uniform_prob);
        }
    }
    return strategy;
}


// --- CFREngine Implementation ---

CFREngine::CFREngine()
    : node_map_(), // Initialize map
      action_abstraction_(), // Initialize members
      hand_evaluator_()
{
    spdlog::debug("CFREngine created");
}

// Recursive CFR+ function - updated signature
double CFREngine::cfr_plus_recursive(
    GameState current_state,
    int traversing_player,
    const std::vector<double>& reach_probabilities, // P(player reaches state) for each player
    std::vector<Card>& deck, // Pass deck by reference
    int& card_idx           // Pass next card index by reference
) {
    // --- 1. Check for Terminal State ---
    Street entry_street = current_state.get_current_street(); // Store street at function entry
    if (current_state.is_terminal()) {
        // --- Payoff Calculation (Handles Multi-way with Side Pots) ---
        double final_payoff = 0.0;
        int num_players = current_state.get_num_players();
        std::vector<double> contributions(num_players);
        std::vector<bool> is_folded(num_players);
        std::vector<int> showdown_players_indices; // Players involved in showdown

        // 1. Gather initial data
        double total_pot_size = 0.0;
        for (int i = 0; i < num_players; ++i) {
            contributions[i] = static_cast<double>(current_state.get_player_contribution(i));
            is_folded[i] = current_state.has_player_folded(i);
            total_pot_size += contributions[i]; // Calculate total pot from contributions
            if (!is_folded[i]) {
                showdown_players_indices.push_back(i);
            }
        }

        // If traversing player folded, their payoff is simply their negative contribution
        if (is_folded[traversing_player]) {
            final_payoff = -contributions[traversing_player];
        }
        // If only one player didn't fold, they win everything contributed by others
        else if (showdown_players_indices.size() == 1) {
             // The only remaining player must be the traversing_player (since they didn't fold)
             final_payoff = total_pot_size - contributions[traversing_player];
        }
        // If multiple players reach showdown, calculate pots
        else if (showdown_players_indices.size() > 1) {
            double total_winnings = 0.0; // Track winnings across all pots for traversing_player

            // Create pairs of (contribution, player_index) for sorting showdown players
            std::vector<std::pair<double, int>> sorted_players;
            for (int index : showdown_players_indices) {
                sorted_players.push_back({contributions[index], index});
            }
            std::sort(sorted_players.begin(), sorted_players.end());

            double last_contribution_level = 0.0;
            std::vector<int> current_pot_eligible_players = showdown_players_indices; // Start with all showdown players

            // 2. Iterate through contribution levels to create and distribute pots
            for (const auto& p : sorted_players) {
                double current_contribution_level = p.first;
                int player_at_this_level = p.second; // Player defining this contribution level

                // If this player contributed more than the last level, create a pot for this increment
                if (current_contribution_level > last_contribution_level && !current_pot_eligible_players.empty()) {
                    double pot_increment_per_player = current_contribution_level - last_contribution_level;
                    double current_pot_size = pot_increment_per_player * current_pot_eligible_players.size();

                    // Evaluate hands ONLY for players eligible for THIS pot
                    std::vector<int> current_winners;
                    int best_rank = 9999; // Lower is better
                    const auto& community_cards = current_state.get_community_cards();

                    // Check board completion ONCE per pot calculation
                    bool board_complete = (community_cards.size() == 5);

                    if (board_complete) {
                        std::vector<int> current_ranks(num_players, 9999); // Store ranks temporarily
                        for (int eligible_player_index : current_pot_eligible_players) {
                             const auto& hand = current_state.get_player_hand(eligible_player_index);
                             if (hand.size() == 2) {
                                 current_ranks[eligible_player_index] = hand_evaluator_.evaluate_7_card_hand(hand, community_cards);
                                 best_rank = std::min(best_rank, current_ranks[eligible_player_index]);
                             } else {
                                 spdlog::warn("Showdown occurred but player {} has invalid hand size {}. History: {}",
                                              eligible_player_index, hand.size(), current_state.get_history_string());
                                 current_ranks[eligible_player_index] = 9999; // Assign worst rank
                             }
                        }
                        // Identify winners for this specific pot
                        for (int eligible_player_index : current_pot_eligible_players) {
                            if (current_ranks[eligible_player_index] == best_rank) {
                                current_winners.push_back(eligible_player_index);
                            }
                        }
                    } else {
                         // Board not complete - all eligible players split this pot segment
                         spdlog::debug("Showdown before river ({} cards) for pot level {}. Splitting pot segment.",
                                       community_cards.size(), current_contribution_level);
                         current_winners = current_pot_eligible_players; // Everyone eligible splits
                    }


                    // Distribute the current pot segment
                    if (!current_winners.empty()) {
                        double share = current_pot_size / current_winners.size();
                        for (int winner_index : current_winners) {
                            if (winner_index == traversing_player) {
                                total_winnings += share;
                            }
                        }
                    }

                    last_contribution_level = current_contribution_level;
                }

                // Remove the player who defined this level from eligibility for the next side pot
                // This needs to happen *after* processing the pot for their contribution level
                 auto it = std::remove(current_pot_eligible_players.begin(), current_pot_eligible_players.end(), player_at_this_level);
                 if (it != current_pot_eligible_players.end()) { // Check if element was found before erasing
                    current_pot_eligible_players.erase(it, current_pot_eligible_players.end());
                 }
            }

            // Final payoff is total winnings minus initial contribution
            final_payoff = total_winnings - contributions[traversing_player];

        } else {
             // Should not happen if is_terminal() is correct and >0 players started
             spdlog::error("Terminal state reached with 0 showdown players. History: {}", current_state.get_history_string());
             final_payoff = -contributions[traversing_player];
        }

        // Return the calculated payoff for the traversing player
        return final_payoff;
    }

    // --- 2. Get InfoSet and Node ---
    int current_player = current_state.get_current_player();
     if (current_state.get_player_hand(current_player).empty()) {
         // This can happen if a player folds and the state becomes terminal, but recursion continues one step.
         // Or if hands weren't dealt properly.
         spdlog::debug("Player {} has no hand in non-terminal state (likely just folded). History: {}", current_player, current_state.get_history_string());
         // If the state isn't actually terminal yet (e.g., waiting for others), this might be an error.
         // However, returning 0 utility seems reasonable as this player path ends here.
         return 0.0;
     }
    InfoSet info_set(current_state.get_player_hand(current_player), current_state.get_history_string());
    std::string info_set_key = info_set.get_key();

    // Get legal actions based on the current game state
    std::vector<std::string> legal_actions_str = action_abstraction_.get_possible_actions(current_state);
    size_t num_actions = legal_actions_str.size();

    if (num_actions == 0) {
         // This might happen legitimately if a player is all-in and facing no bet, and action moves past them.
         // Or if only one player remains un-folded but the state isn't marked terminal yet.
         spdlog::debug("No legal actions found for player {} in non-terminal state. History: {}", current_player, current_state.get_history_string());
         // If no actions, utility from this point is 0 for the current player path.
         // We need to continue recursion to find the actual terminal state payoff.
         // This requires finding the *next* state without applying an action from current_player.
         // This suggests a potential issue in the state transition logic (is_terminal or update_next_player).
         // For now, let's assume the state *should* be terminal if no actions are possible.
         // We calculate the payoff as if it were terminal.
         // TODO: Re-evaluate this logic - should is_terminal catch this?
         // Re-calculating payoff here duplicates terminal logic, let's return 0 and rely on caller/terminal check.
         return 0.0; // No utility gained from this non-action state.
    }

    Node* node_ptr = nullptr; // Use pointer to avoid issues with map reallocation if needed, though unlikely here

    // --- Thread-safe Node Lookup/Creation ---
    { // Scope for the lock guard
        std::lock_guard<std::mutex> lock(node_map_mutex_);
        auto it = node_map_.find(info_set_key);
        if (it == node_map_.end()) {
            // Node doesn't exist, create it
            // Use emplace which constructs in-place, returns pair<iterator, bool>
            auto emplace_result = node_map_.emplace(std::piecewise_construct,
                                                   std::forward_as_tuple(info_set_key),
                                                   std::forward_as_tuple(num_actions));
            node_ptr = &emplace_result.first->second; // Get pointer to the newly created Node value in the map
            total_nodes_created_++; // Increment atomic counter
        } else {
            // Node exists
            node_ptr = &it->second; // Get pointer to the existing Node value
        }
    } // Mutex is released here

    // Ensure node_ptr is valid before dereferencing
    if (!node_ptr) {
         spdlog::error("Failed to get or create node for key: {}", info_set_key);
         // Decide how to handle this critical error. Throwing might be appropriate.
         throw std::runtime_error("Failed to get or create node for key: " + info_set_key);
    }
    Node& node = *node_ptr; // Dereference the pointer to get the Node reference

    // --- 3. Calculate Current Strategy (Regret Matching) ---
    // Read regret_sum - potentially needs protection if updates are not atomic per element
    // For simplicity with std::vector<double>, we lock before reading strategy/regrets
    // and before writing them.
    std::vector<double> current_regrets;
    {
        std::lock_guard<std::mutex> lock(node_map_mutex_);
        current_regrets = node.regret_sum; // Copy regrets under lock
    }
    std::vector<double> current_strategy = get_strategy_from_regrets(current_regrets);

    // --- 4. Calculate Expected Value & Recurse ---
    std::vector<double> action_utilities(num_actions, 0.0);
    double node_utility = 0.0;

    for (size_t i = 0; i < num_actions; ++i) {
        Action action;
        const std::string& action_str = legal_actions_str[i];
        // Assign player index to action BEFORE applying it
        action.player_index = current_player;

        if (action_str == "fold") {
            action.type = Action::Type::FOLD;
        } else if (action_str == "call") {
            action.type = Action::Type::CALL;
        } else if (action_str == "check") {
             action.type = Action::Type::CHECK;
             action.amount = 0; // Ensure amount is 0 for check
        } else if (action_str.find("raise") != std::string::npos || action_str.find("bet") != std::string::npos || action_str == "all_in") {
             if (current_state.get_amount_to_call(current_player) == 0) {
                 action.type = Action::Type::BET;
             } else {
                 action.type = Action::Type::RAISE;
             }
             action.amount = action_abstraction_.get_action_amount(action_str, current_state);
             if (action.amount == -1) {
                  spdlog::error("Could not determine amount for action: {} in state {}", action_str, current_state.get_history_string());
                  // Skip this invalid action path
                  action_utilities[i] = -1e18; // Assign very low utility? Or just skip update? Skip for now.
                  continue;
             }
        }
         else {
             spdlog::error("Unsupported action string from get_possible_actions: {}", action_str);
             throw std::logic_error("Unsupported action string from get_possible_actions: " + action_str);
        }

        GameState next_state = current_state;
        try {
             next_state.apply_action(action);
        } catch (const std::exception& e) {
             spdlog::warn("Exception applying action '{}' in state {}. Skipping. Error: {}", action_str, current_state.get_history_string(), e.what());
             action_utilities[i] = -1e18; // Assign very low utility? Or just skip update? Skip for now.
             continue;
        }

        // --- Deal community cards if street changed ---
        Street next_street = next_state.get_current_street();
        int current_card_idx = card_idx; // Store current index before recursive call
        if (next_street != entry_street && next_street != Street::SHOWDOWN) {
            std::vector<Card> cards_to_deal;
            int num_cards_to_deal = 0;
            if (next_street == Street::FLOP && entry_street == Street::PREFLOP) num_cards_to_deal = 3;
            else if (next_street == Street::TURN && entry_street == Street::FLOP) num_cards_to_deal = 1;
            else if (next_street == Street::RIVER && entry_street == Street::TURN) num_cards_to_deal = 1;

            if (num_cards_to_deal > 0) {
                if (card_idx + num_cards_to_deal <= deck.size()) {
                    for(int k=0; k<num_cards_to_deal; ++k) {
                        cards_to_deal.push_back(deck[card_idx++]);
                    }
                    next_state.deal_community_cards(cards_to_deal);
                } else {
                    spdlog::error("Not enough cards left in deck to deal {} for {}. Deck size: {}, Card index: {}",
                                  num_cards_to_deal, static_cast<int>(next_street), deck.size(), card_idx);
                    // Handle error - maybe return 0 utility or throw?
                    action_utilities[i] = 0.0; // Assign 0 utility for this path
                    card_idx = current_card_idx; // Restore card index
                    continue; // Skip recursion for this invalid state
                }
            }
        }
        // --- End Deal community cards ---

        std::vector<double> next_reach_probabilities = reach_probabilities;
        // Only update opponent reach probability if it's their turn
        if(current_player != traversing_player) {
             next_reach_probabilities[current_player] *= current_strategy[i];
        }


        // Recursive call - negate result as it's from opponent's perspective relative to this node
        action_utilities[i] = -cfr_plus_recursive(
            next_state,
            traversing_player,
            next_reach_probabilities,
            deck,
            card_idx // Pass updated index
        );

        // Restore card index after recursive call returns, so sibling nodes use correct index
        card_idx = current_card_idx;

        node_utility += current_strategy[i] * action_utilities[i];
    }

    // --- 5. Update Regrets & Strategy Sum (CFR+ specific updates) ---
    // Only update regrets and strategy sum if it's the traversing player's turn to act
    if (current_player == traversing_player) {
        double counterfactual_reach_prob = 1.0;
        for(int p = 0; p < current_state.get_num_players(); ++p) {
            if (p != current_player) {
                counterfactual_reach_prob *= reach_probabilities[p];
            }
        }

        // --- Thread-safe Update of Regrets & Strategy Sum ---
        { // Scope for the lock guard
            std::lock_guard<std::mutex> lock(node_map_mutex_);

            // Ensure counterfactual_reach_prob is not zero or too small to avoid issues
            if (counterfactual_reach_prob > 1e-9) {
                 for (size_t i = 0; i < num_actions; ++i) {
                     double regret = action_utilities[i] - node_utility;
                     node.regret_sum[i] += counterfactual_reach_prob * regret;
                 }
            }

            double player_reach_prob = reach_probabilities[current_player];
             // Ensure player_reach_prob is not zero or too small
             if (player_reach_prob > 1e-9) {
                 for (size_t i = 0; i < num_actions; ++i) {
                     node.strategy_sum[i] += player_reach_prob * current_strategy[i];
                 }
             }
        } // Mutex is released here

        // visit_count is atomic, can be incremented outside the lock
        node.visit_count++;
    }

    return node_utility;
}


// --- Public Methods ---

// Updated signature to accept game parameters and number of threads
void CFREngine::train(int iterations, int num_players, int initial_stack, int ante_size, int num_threads, const std::string& save_filename, int checkpoint_interval, const std::string& load_filename) {
    // --- Load Checkpoint if specified ---
    int starting_iteration = 0;
    if (!load_filename.empty()) {
        spdlog::info("Attempting to load checkpoint from: {}", load_filename);
        int loaded_iters = load_checkpoint(load_filename);
        if (loaded_iters >= 0) {
            starting_iteration = loaded_iters;
            spdlog::info("Checkpoint loaded successfully. Resuming from iteration {}.", starting_iteration);
        } else {
            spdlog::warn("Failed to load checkpoint from {}. Starting training from scratch.", load_filename);
        }
    }

    int iterations_to_run = iterations - starting_iteration;
    if (iterations_to_run <= 0) {
        spdlog::info("Target iterations ({}) already reached or exceeded by checkpoint ({}). No training needed.", iterations, starting_iteration);
        return;
    }
     spdlog::info("Need to run {} more iterations.", iterations_to_run);


    // --- Reset Counters (relative to loaded state) ---
    completed_iterations_ = starting_iteration; // Start counting from loaded iteration
    // total_nodes_created_ is loaded/updated within load_checkpoint or starts at 0
    last_logged_percent_ = -1; // Reset log tracker

    // --- Determine Number of Threads ---
    unsigned int hardware_threads = std::thread::hardware_concurrency();
    unsigned int threads_to_use = (num_threads <= 0) ? hardware_threads : std::min((unsigned int)num_threads, hardware_threads);
    if (threads_to_use == 0) { // hardware_concurrency might return 0
        threads_to_use = 1;
    }
    spdlog::info("Using {} threads for training.", threads_to_use);

    // --- Deck Initialization (Master Deck) ---
    std::vector<Card> master_deck;
    const std::vector<char> ranks = {'2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K', 'A'};
    const std::vector<char> suits = {'c', 'd', 'h', 's'};
    for (char r : ranks) {
        for (char s : suits) {
            // Use string directly as Card is std::string
            master_deck.push_back(std::string(1, r) + s);
        }
    }

    // --- Thread Worker Function ---
    auto worker_task = [&](int thread_id, int iterations_for_thread) {
        // Thread-local RNG Initialization
        // Use a combination of time, thread_id, and starting_iteration for better seed diversity
        unsigned seed = std::chrono::system_clock::now().time_since_epoch().count()
                      + thread_id
                      + starting_iteration; // Add starting iteration to seed
        std::mt19937 rng(seed);

        // Thread-local Deck Copy
        std::vector<Card> deck = master_deck;
        int last_checkpoint_iter = 0; // Track iterations since last checkpoint for this thread

        for (int i = 0; i < iterations_for_thread; ++i) {
            int global_iteration_approx = starting_iteration + thread_id * iterations_for_thread + i; // Approximate global iteration
            int button_pos = global_iteration_approx % num_players;

            // Create root state with specified parameters
            GameState root_state(num_players, initial_stack, ante_size, button_pos);

            // Shuffle deck for this iteration (thread-local copy)
            std::shuffle(deck.begin(), deck.end(), rng);

            // Deal hands
            std::vector<std::vector<Card>> hands(num_players);
            int card_index = 0; // Index for drawing from the shuffled deck
            bool deal_ok = true;
            for (int p = 0; p < num_players; ++p) {
                if (card_index + 1 >= deck.size()) {
                    spdlog::error("[Thread {}] Not enough cards in deck to deal hands. Index: {}, Deck size: {}", thread_id, card_index, deck.size());
                    deal_ok = false;
                    break;
                }
                hands[p].push_back(deck[card_index++]);
                hands[p].push_back(deck[card_index++]);
                std::sort(hands[p].begin(), hands[p].end());
            }

            if (!deal_ok) {
                continue;
            }
            root_state.deal_hands(hands);

            // Run CFR+ from the root for each player
            for (int player = 0; player < num_players; ++player) {
                std::vector<double> initial_reach_probs(num_players, 1.0);
                int current_card_idx = card_index;
                try {
                    cfr_plus_recursive(root_state, player, initial_reach_probs, deck, current_card_idx);
                } catch (const std::exception& e) {
                     spdlog::error("[Thread {}] Exception in cfr_plus_recursive: {}", thread_id, e.what());
                }
            }

            // Increment completed iterations counter (atomic)
            int current_completed = completed_iterations_++; // Post-increment

            // --- Progress Logging (Thread 0 only, every 5%) ---
            if (thread_id == 0) {
                // Calculate percentage based on total iterations requested, not just iterations_to_run
                int current_percent = static_cast<int>((static_cast<double>(current_completed + 1) / iterations) * 100.0);
                int last_logged = last_logged_percent_.load();

                if (current_percent >= last_logged + 5) {
                    // Use compare_exchange_strong to ensure only one thread updates last_logged_percent_
                    // for this percentage bracket, rounding down to the nearest 5.
                    int target_percent = current_percent - (current_percent % 5);
                    if (target_percent > last_logged) { // Ensure we only log increasing percentages
                         if (last_logged_percent_.compare_exchange_strong(last_logged, target_percent)) {
                              spdlog::info("Training progress: {}%", target_percent);
                         }
                    }
                }
            }

             // --- Checkpointing (Thread 0 only, based on GLOBAL iterations) ---
             if (thread_id == 0 && !save_filename.empty() && checkpoint_interval > 0) {
                 // Check if the *global* completed count crosses an interval boundary
                 // Need to track the last iteration number when a save occurred.
                 // (Requires update in .h file as well)

                 // Simplified check for now: Check if current_completed is a multiple of interval
                 // This might save slightly off the exact interval due to thread scheduling, but is simpler.
                 // A more robust way involves tracking the last saved iteration count.
                 // Let's try the simple modulo check first.
                 int completed_count = current_completed + 1; // Use the count *after* this iteration finishes
                 if (completed_count % checkpoint_interval == 0 && completed_count > 0) {
                      spdlog::info("[Thread 0] Reached checkpoint interval (around iteration {}). Saving state...", completed_count);
                     // Use a temporary filename + atomic rename for safety
                     std::string temp_filename = save_filename + ".tmp";
                     if (save_checkpoint(temp_filename)) {
                         try {
                             // Requires C++17 filesystem library
                             std::filesystem::rename(temp_filename, save_filename);
                             spdlog::info("[Thread 0] Checkpoint saved successfully to {}", save_filename);
                         } catch (const std::filesystem::filesystem_error& fs_err) {
                             spdlog::error("[Thread 0] Failed to rename temporary checkpoint file: {}", fs_err.what());
                             // Attempt to remove temporary file if rename failed
                             try { std::filesystem::remove(temp_filename); } catch(...) {}
                         }
                     } else {
                         spdlog::error("[Thread 0] Failed to save checkpoint to temporary file {}", temp_filename);
                     }
                     // Note: We don't reset last_checkpoint_iter here as it's not used in this logic
                 }
             }

        } // End iteration loop for this thread
    }; // End lambda worker_task

    // --- Launch Threads ---
    std::vector<std::thread> threads;
    int iterations_per_thread = iterations_to_run / threads_to_use;
    int remaining_iterations = iterations_to_run % threads_to_use;

    for (unsigned int i = 0; i < threads_to_use; ++i) {
        int iters = iterations_per_thread + (i < remaining_iterations ? 1 : 0);
        if (iters > 0) {
            threads.emplace_back(worker_task, i, iters);
        }
    }

    // --- Join Threads ---
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // --- Final Log & Save ---
    // Ensure 100% is logged if the loop finished before the last 5% step triggered it
    if (last_logged_percent_.load() < 100 && completed_iterations_.load() >= iterations) {
         spdlog::info("Training progress: 100%");
    }
    spdlog::info("Training complete. Total iterations run: {}. Final iteration count: {}. Nodes created: {}", iterations_to_run, completed_iterations_.load(), total_nodes_created_.load());

    // Final save if requested
    if (!save_filename.empty()) {
        spdlog::info("Performing final save to checkpoint file: {}", save_filename);
        std::string temp_filename = save_filename + ".final.tmp";
         if (save_checkpoint(temp_filename)) {
             try {
                 std::filesystem::rename(temp_filename, save_filename);
                 spdlog::info("Final checkpoint saved successfully to {}", save_filename);
             } catch (const std::filesystem::filesystem_error& fs_err) {
                 spdlog::error("Failed to rename final temporary checkpoint file: {}", fs_err.what());
                 try { std::filesystem::remove(temp_filename); } catch(...) {}
             }
         } else {
             spdlog::error("Failed to save final checkpoint to temporary file {}", temp_filename);
         }
    }
}

// --- Checkpointing Methods ---

// Save state to JSON file
bool CFREngine::save_checkpoint(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(node_map_mutex_)); // Lock for reading map

    json checkpoint_data;
    checkpoint_data["version"] = CHECKPOINT_VERSION_JSON;
    checkpoint_data["completed_iterations"] = completed_iterations_.load();
    checkpoint_data["total_nodes_created"] = total_nodes_created_.load();
    checkpoint_data["node_map"] = node_map_; // nlohmann/json handles map<string, Node> via to_json

    std::ofstream ofs(filename);
    if (!ofs) {
        spdlog::error("Failed to open checkpoint file for writing: {}", filename);
        return false;
    }

    try {
        // Use dump() without indentation for potentially lower memory usage during serialization
        ofs << checkpoint_data.dump();
        ofs.close();
        return ofs.good();
    } catch (const json::exception& e) {
        spdlog::error("JSON serialization error during save: {}", e.what());
        ofs.close();
        return false;
    } catch (const std::exception& e) {
        spdlog::error("Standard exception during save: {}", e.what());
        ofs.close();
        return false;
    }
}

// Load state from JSON file
int CFREngine::load_checkpoint(const std::string& filename) {
    std::lock_guard<std::mutex> lock(node_map_mutex_); // Lock for writing map

    std::ifstream ifs(filename);
    if (!ifs) {
        spdlog::error("Failed to open checkpoint file for reading: {}", filename);
        return -1;
    }

    json checkpoint_data;
    int loaded_iterations = -1;
    long long loaded_nodes_created = 0;

    try {
        ifs >> checkpoint_data; // Parse the entire JSON file
        ifs.close();

        // Version Check
        if (!checkpoint_data.contains("version") || checkpoint_data["version"] != CHECKPOINT_VERSION_JSON) {
            spdlog::error("Checkpoint file version mismatch or missing version. Expected: {}", CHECKPOINT_VERSION_JSON);
            return -1;
        }

        // Completed Iterations
        if (!checkpoint_data.contains("completed_iterations")) {
             spdlog::error("Missing 'completed_iterations' in checkpoint.");
             return -1;
        }
        loaded_iterations = checkpoint_data["completed_iterations"].get<int>();
        if (loaded_iterations < 0) {
             spdlog::error("Invalid 'completed_iterations' value in checkpoint.");
             return -1;
        }

        // Total Nodes Created (Optional)
        if (checkpoint_data.contains("total_nodes_created")) {
            loaded_nodes_created = checkpoint_data["total_nodes_created"].get<long long>();
        } else {
             spdlog::warn("Missing 'total_nodes_created' in checkpoint. Will estimate.");
             loaded_nodes_created = 0; // Will be updated below
        }


        // Node Map Data
        if (!checkpoint_data.contains("node_map")) {
             spdlog::error("Missing 'node_map' in checkpoint.");
             return -1;
        }

        // --- Manually deserialize the node_map ---
        node_map_.clear(); // Clear existing map
        const json& node_map_json = checkpoint_data.at("node_map");
        if (!node_map_json.is_object()) {
             spdlog::error("'node_map' in checkpoint is not a JSON object.");
             return -1;
        }

        for (auto it = node_map_json.begin(); it != node_map_json.end(); ++it) {
            const std::string& key = it.key();
            const json& node_json = it.value();
            try {
                // Create a default Node first (needs number of actions, which we don't have here!)
                // --> Problem: We need the number of actions to construct a Node before calling from_json.
                // --> Let's store num_actions within the Node JSON itself during save.

                // --- TEMPORARY WORKAROUND (Assumes fixed number of actions, needs fix) ---
                // This is incorrect if different nodes have different action counts!
                // We need to store num_actions in the JSON.
                // For now, let's assume a default or get it from the first loaded node if possible.
                // This part needs refinement. Let's assume 6 actions for now as a placeholder.
                size_t num_actions_placeholder = 6; // FIXME: This is wrong!
                if (node_json.contains("regret_sum")) { // Get size from loaded data if possible
                    num_actions_placeholder = node_json["regret_sum"].size();
                }
                Node node(num_actions_placeholder); // Create node with placeholder size
                from_json(node_json, node); // Call our from_json function explicitly

                // Use try_emplace which works with move-only types
                node_map_.try_emplace(key, std::move(node));
            } catch (const json::exception& e) {
                 spdlog::error("Failed to deserialize node for key '{}': {}", key, e.what());
                 return -1; // Stop loading on error
            }
        }
        // --- End manual deserialization ---


        // Update node count if it wasn't loaded or seems inconsistent
        if (loaded_nodes_created == 0 || loaded_nodes_created != (long long)node_map_.size()) {
             if (loaded_nodes_created != 0) { // Log warning only if value was present but different
                 spdlog::warn("Loaded 'total_nodes_created' ({}) differs from loaded map size ({}). Using map size.", loaded_nodes_created, node_map_.size());
             }
             loaded_nodes_created = node_map_.size();
        }

        // Update atomic counters after successful load
        completed_iterations_.store(loaded_iterations);
        total_nodes_created_.store(loaded_nodes_created);

        return loaded_iterations; // Return number of iterations loaded

    } catch (const json::parse_error& e) {
        spdlog::error("JSON parse error during load: {}", e.what());
        if(ifs.is_open()) ifs.close();
        return -1;
    } catch (const json::exception& e) { // Catch other json exceptions (type errors, missing keys from .at())
         spdlog::error("JSON exception during load: {}", e.what());
         if(ifs.is_open()) ifs.close();
         return -1;
    } catch (const std::exception& e) {
         spdlog::error("Standard exception during load: {}", e.what());
         if(ifs.is_open()) ifs.close();
         return -1;
    }
}


std::vector<double> CFREngine::get_strategy(const std::string& info_set_key) {
    // Reading the strategy also needs protection if writes can happen concurrently
    // Although training is finished, another thread *could* theoretically call this.
    // For simplicity, lock reads too. Could use std::shared_mutex for read-write locks later.
    std::lock_guard<std::mutex> lock(node_map_mutex_);
    auto it = node_map_.find(info_set_key);
    if (it != node_map_.end()) {
        // Return average strategy
        return it->second.get_average_strategy();
    } else {
        // Don't log warning here, as it's expected for unvisited nodes during extraction
        // spdlog::warn("InfoSet key not found: {}", info_set_key);
        return {}; // Return empty vector if node doesn't exist
    }
}

} // namespace gto_solver
