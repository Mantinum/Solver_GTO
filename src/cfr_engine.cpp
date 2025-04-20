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
#include <algorithm> // For std::max, std::shuffle, std::min, std::min_element, std::max_element
#include <stdexcept> // For exceptions
#include <random>    // For std::mt19937, std::random_device, std::discrete_distribution
#include <chrono>    // For seeding RNG
#include <utility>   // For std::pair, std::move
#include <thread>    // For std::thread
#include <functional> // For std::bind or lambdas
#include <mutex>     // For std::lock_guard, std::scoped_lock
#include <atomic>    // For std::atomic
#include <fstream>   // For file streams
#include <filesystem> // For renaming files atomically (C++17)
#include <cmath>     // For std::isnan, std::isinf
#include <memory>    // For std::unique_ptr, std::make_unique

#include "spdlog/spdlog.h" // Include spdlog
#include "spdlog/fmt/bundled/format.h" // Include fmt for logging vectors

// Define a simple version number for the BINARY checkpoint format
const uint32_t CHECKPOINT_VERSION_BIN = 2;

namespace gto_solver {

// --- Helper Function: Regret Matching ---
// (remains the same)
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
        if (num_actions > 0) {
             double uniform_prob = 1.0 / num_actions;
             std::fill(strategy.begin(), strategy.end(), uniform_prob);
        }
    }
    // Normalize strategy
    double sum_strategy = std::accumulate(strategy.begin(), strategy.end(), 0.0);
    if (sum_strategy > 1e-9) {
        for (double& prob : strategy) { prob /= sum_strategy; }
    } else if (num_actions > 0) {
         double uniform_prob = 1.0 / num_actions;
         std::fill(strategy.begin(), strategy.end(), uniform_prob);
    }
    return strategy;
}


// --- CFREngine Implementation ---

CFREngine::CFREngine()
    : node_map_(),
      action_abstraction_(),
      hand_evaluator_()
{
    spdlog::debug("CFREngine created");
}

// Recursive MCCFR function (External Sampling) - Takes RNG reference and depth
double CFREngine::cfr_plus_recursive(
    GameState current_state,
    int traversing_player,
    const std::vector<double>& reach_probabilities,
    std::vector<Card>& deck,
    int& card_idx,
    std::mt19937& rng,
    int depth // Added depth parameter
) {
    // --- Update Max Depth Reached ---
    int current_max_depth = max_depth_reached_.load(std::memory_order_relaxed);
    if (depth > current_max_depth) {
        max_depth_reached_.compare_exchange_strong(current_max_depth, depth, std::memory_order_relaxed);
    }

    // --- 1. Check for Terminal State ---
     Street entry_street = current_state.get_current_street();
    if (current_state.is_terminal()) {
        // spdlog::trace("Depth {}: Terminal state reached.", depth); // DEBUG LOG COMMENTED
        // ... (payoff calculation logic is unchanged) ...
        double final_payoff = 0.0;
        int num_players = current_state.get_num_players();
        std::vector<double> contributions(num_players);
        std::vector<bool> is_folded(num_players);
        std::vector<int> showdown_players_indices;
        double total_pot_size = 0.0;
        for (int i = 0; i < num_players; ++i) {
            contributions[i] = static_cast<double>(current_state.get_player_contribution(i));
            is_folded[i] = current_state.has_player_folded(i);
            total_pot_size += contributions[i];
            if (!is_folded[i]) showdown_players_indices.push_back(i);
        }
        if (is_folded[traversing_player]) final_payoff = -contributions[traversing_player];
        else if (showdown_players_indices.size() == 1) final_payoff = total_pot_size - contributions[traversing_player];
        else if (showdown_players_indices.size() > 1) {
            double total_winnings = 0.0;
            std::vector<std::pair<double, int>> sorted_players;
            for (int index : showdown_players_indices) sorted_players.push_back({contributions[index], index});
            std::sort(sorted_players.begin(), sorted_players.end());
            double last_contribution_level = 0.0;
            std::vector<int> current_pot_eligible_players = showdown_players_indices;
            for (const auto& p : sorted_players) {
                double current_contribution_level = p.first;
                int player_at_this_level = p.second;
                if (current_contribution_level > last_contribution_level && !current_pot_eligible_players.empty()) {
                    double pot_increment_per_player = current_contribution_level - last_contribution_level;
                    double current_pot_size = pot_increment_per_player * current_pot_eligible_players.size();
                    std::vector<int> current_winners;
                    int best_rank = 9999;
                    const auto& community_cards = current_state.get_community_cards();
                    bool board_complete = (community_cards.size() == 5);
                    if (board_complete) {
                        std::vector<int> current_ranks(num_players, 9999);
                        for (int eligible_player_index : current_pot_eligible_players) {
                             const auto& hand = current_state.get_player_hand(eligible_player_index);
                             if (hand.size() == 2) {
                                 current_ranks[eligible_player_index] = hand_evaluator_.evaluate_7_card_hand(hand, community_cards);
                                 best_rank = std::min(best_rank, current_ranks[eligible_player_index]);
                             } else {
                                 // spdlog::warn("Showdown occurred but player {} has invalid hand size {}. History: {}", eligible_player_index, hand.size(), current_state.get_history_string());
                                 current_ranks[eligible_player_index] = 9999;
                             }
                        }
                        for (int eligible_player_index : current_pot_eligible_players) {
                            if (current_ranks[eligible_player_index] == best_rank) current_winners.push_back(eligible_player_index);
                        }
                    } else {
                         // spdlog::debug("Showdown before river ({} cards) for pot level {}. Splitting pot segment.", community_cards.size(), current_contribution_level);
                         current_winners = current_pot_eligible_players;
                    }
                    if (!current_winners.empty()) {
                        double share = current_pot_size / current_winners.size();
                        for (int winner_index : current_winners) {
                            if (winner_index == traversing_player) total_winnings += share;
                        }
                    }
                    last_contribution_level = current_contribution_level;
                }
                 auto it = std::remove(current_pot_eligible_players.begin(), current_pot_eligible_players.end(), player_at_this_level);
                 if (it != current_pot_eligible_players.end()) current_pot_eligible_players.erase(it, current_pot_eligible_players.end());
            }
            final_payoff = total_winnings - contributions[traversing_player];
        } else {
             spdlog::error("Terminal state reached with 0 showdown players. History: {}", current_state.get_history_string());
             final_payoff = -contributions[traversing_player];
        }
        // spdlog::trace("Depth {}: Terminal Payoff for player {}: {:.2f}", depth, traversing_player, final_payoff); // DEBUG LOG COMMENTED
        return final_payoff;
    }

    // --- 2. Get InfoSet and Node ---
    int current_player = current_state.get_current_player();
     if (current_state.get_player_hand(current_player).empty()) {
         // spdlog::debug("Player {} has no hand in non-terminal state (likely just folded). History: {}", current_player, current_state.get_history_string());
         return 0.0;
     }
    InfoSet info_set(current_state.get_player_hand(current_player), current_state.get_history_string());
    std::string info_set_key = info_set.get_key(current_player);

    std::vector<std::string> legal_actions_str = action_abstraction_.get_possible_actions(current_state);
    size_t num_actions = legal_actions_str.size();

    if (num_actions == 0) {
         // spdlog::debug("No legal actions found for player {} in non-terminal state. History: {}", current_player, current_state.get_history_string());
         return 0.0;
    }

    Node* node_ptr = nullptr;
    // --- Thread-safe Node Lookup/Creation ---
    {
        std::lock_guard<std::mutex> lock(node_map_mutex_);
        auto it = node_map_.find(info_set_key);
        if (it == node_map_.end()) {
            // spdlog::trace("Depth {}: Creating node: {}", depth, info_set_key); // DEBUG LOG COMMENTED
            auto emplace_result = node_map_.emplace(info_set_key, std::make_unique<Node>(num_actions));
            node_ptr = emplace_result.first->second.get();
            total_nodes_created_++;
        } else {
            // spdlog::trace("Depth {}: Found node: {}", depth, info_set_key); // DEBUG LOG COMMENTED
            node_ptr = it->second.get();
        }
    }

    if (!node_ptr) {
         spdlog::error("Failed to get or create node pointer for key: {}", info_set_key);
         throw std::runtime_error("Failed to get or create node pointer for key: " + info_set_key);
    }

    // --- 3. Calculate Current Strategy (Regret Matching) ---
    std::vector<double> current_regrets;
    {
        std::lock_guard<std::mutex> node_lock(node_ptr->node_mutex);
        current_regrets = node_ptr->regret_sum;
    }
    std::vector<double> current_strategy = get_strategy_from_regrets(current_regrets);

    // --- 4. MCCFR Logic: Sample Opponent Actions, Explore Own Actions ---
    double node_utility = 0.0;
    std::vector<double> action_utilities(num_actions, 0.0);

    if (current_player != traversing_player) {
        // --- Opponent's Turn: Sample one action ---
        size_t sampled_action_idx = 0;
        if (!current_strategy.empty()) {
             bool all_zero = true;
             for(double p : current_strategy) { if (p > 1e-9) { all_zero = false; break; } }
             if (all_zero) {
                  std::uniform_int_distribution<size_t> uniform_dist(0, current_strategy.size() - 1);
                  sampled_action_idx = uniform_dist(rng);
                  // spdlog::trace("Depth {}: Opponent {} strategy all zero/negative, sampling uniformly: action {}", depth, current_player, sampled_action_idx); // DEBUG LOG COMMENTED
             } else {
                  try {
                       std::discrete_distribution<size_t> dist(current_strategy.begin(), current_strategy.end());
                       sampled_action_idx = dist(rng);
                       // spdlog::trace("Depth {}: Opponent {} sampled action index: {} ({}) from strategy [{}]", depth, current_player, sampled_action_idx, legal_actions_str[sampled_action_idx], fmt::join(current_strategy, ", ")); // DEBUG LOG COMMENTED
                  } catch (const std::exception& e) {
                       spdlog::error("Error creating discrete_distribution for node {}: {}. Strategy: [{}]", info_set_key, e.what(), fmt::join(current_strategy, ", "));
                       std::uniform_int_distribution<size_t> uniform_dist(0, current_strategy.size() - 1);
                       sampled_action_idx = uniform_dist(rng);
                       // spdlog::trace("Depth {}: Opponent {} distribution error, sampling uniformly: action {}", depth, current_player, sampled_action_idx); // DEBUG LOG COMMENTED
                  }
             }
        } else {
             spdlog::error("Empty strategy for opponent node: {}", info_set_key);
             return 0.0;
        }

        const std::string& action_str = legal_actions_str[sampled_action_idx];
        Action action;
        action.player_index = current_player;
        // (Action parsing logic...)
        if (action_str == "fold") { action.type = Action::Type::FOLD; }
        else if (action_str == "call") { action.type = Action::Type::CALL; }
        else if (action_str == "check") { action.type = Action::Type::CHECK; action.amount = 0; }
        else if (action_str.find("raise") != std::string::npos || action_str.find("bet") != std::string::npos || action_str == "all_in") {
             action.type = (current_state.get_amount_to_call(current_player) == 0) ? Action::Type::BET : Action::Type::RAISE;
             action.amount = action_abstraction_.get_action_amount(action_str, current_state);
             if (action.amount == -1) return 0.0;
        } else { throw std::logic_error("Unsupported action string: " + action_str); }

        GameState next_state = current_state;
        try { next_state.apply_action(action); } catch (...) { return 0.0; }

        // (Community card dealing logic...)
        Street next_street = next_state.get_current_street();
        int current_card_idx = card_idx;
        if (next_street != entry_street && next_street != Street::SHOWDOWN) {
             // spdlog::trace("Depth {}: Dealing cards for {}", depth, static_cast<int>(next_street)); // DEBUG LOG COMMENTED
             std::vector<Card> cards_to_deal;
            int num_cards_to_deal = 0;
            if (next_street == Street::FLOP && entry_street == Street::PREFLOP) num_cards_to_deal = 3;
            else if (next_street == Street::TURN && entry_street == Street::FLOP) num_cards_to_deal = 1;
            else if (next_street == Street::RIVER && entry_street == Street::TURN) num_cards_to_deal = 1;
            if (num_cards_to_deal > 0) {
                if (card_idx + num_cards_to_deal <= deck.size()) {
                    for(int k=0; k<num_cards_to_deal; ++k) cards_to_deal.push_back(deck[card_idx++]);
                    next_state.deal_community_cards(cards_to_deal);
                } else { card_idx = current_card_idx; return 0.0; }
            }
        }

        std::vector<double> next_reach_probabilities = reach_probabilities;
        if (sampled_action_idx < current_strategy.size() && current_strategy[sampled_action_idx] > 1e-9) {
             next_reach_probabilities[current_player] *= current_strategy[sampled_action_idx];
        } else {
             return 0.0;
        }

        // Recursive call for the sampled action, passing RNG and incremented depth
        node_utility = -cfr_plus_recursive(next_state, traversing_player, next_reach_probabilities, deck, card_idx, rng, depth + 1);
        card_idx = current_card_idx;

    } else {
        // --- Traversing Player's Turn: Explore all actions ---
        for (size_t i = 0; i < num_actions; ++i) {
            Action action;
            const std::string& action_str = legal_actions_str[i];
            action.player_index = current_player;
            // (Action parsing logic...)
             if (action_str == "fold") { action.type = Action::Type::FOLD; }
             else if (action_str == "call") { action.type = Action::Type::CALL; }
             else if (action_str == "check") { action.type = Action::Type::CHECK; action.amount = 0; }
             else if (action_str.find("raise") != std::string::npos || action_str.find("bet") != std::string::npos || action_str == "all_in") {
                  action.type = (current_state.get_amount_to_call(current_player) == 0) ? Action::Type::BET : Action::Type::RAISE;
                  action.amount = action_abstraction_.get_action_amount(action_str, current_state);
                  if (action.amount == -1) { continue; }
             } else { throw std::logic_error("Unsupported action string: " + action_str); }

            GameState next_state = current_state;
             try { next_state.apply_action(action); } catch (...) { continue; }

            // (Community card dealing logic...)
             Street next_street = next_state.get_current_street();
             int current_card_idx = card_idx;
             if (next_street != entry_street && next_street != Street::SHOWDOWN) {
                 // spdlog::trace("Depth {}: Dealing cards for {}", depth, static_cast<int>(next_street)); // DEBUG LOG COMMENTED
                 std::vector<Card> cards_to_deal;
                 int num_cards_to_deal = 0;
                 if (next_street == Street::FLOP && entry_street == Street::PREFLOP) num_cards_to_deal = 3;
                 else if (next_street == Street::TURN && entry_street == Street::FLOP) num_cards_to_deal = 1;
                 else if (next_street == Street::RIVER && entry_street == Street::TURN) num_cards_to_deal = 1;
                 if (num_cards_to_deal > 0) {
                     if (card_idx + num_cards_to_deal <= deck.size()) {
                         for(int k=0; k<num_cards_to_deal; ++k) cards_to_deal.push_back(deck[card_idx++]);
                         next_state.deal_community_cards(cards_to_deal);
                     } else { card_idx = current_card_idx; continue; }
                 }
             }

            // Pass RNG down, increment depth
            action_utilities[i] = -cfr_plus_recursive(next_state, traversing_player, reach_probabilities, deck, card_idx, rng, depth + 1);
            card_idx = current_card_idx;
            node_utility += current_strategy[i] * action_utilities[i];
        }

        // --- 5. Update Regrets & Strategy Sum (Traversing Player Only) ---
        double counterfactual_reach_prob = 1.0;
        for(int p = 0; p < current_state.get_num_players(); ++p) {
            if (p != current_player) {
                counterfactual_reach_prob *= reach_probabilities[p];
            }
        }

        // Lock the specific node's mutex to update its sums
        {
            std::lock_guard<std::mutex> node_lock(node_ptr->node_mutex);
            if (counterfactual_reach_prob > 1e-9) {
                 for (size_t i = 0; i < num_actions; ++i) {
                     double regret = action_utilities[i] - node_utility;
                     if (!std::isnan(regret) && !std::isinf(regret)) {
                        node_ptr->regret_sum[i] += counterfactual_reach_prob * regret;
                     } else { /* Log warning */ }
                 }
            }
            double player_reach_prob = reach_probabilities[current_player];
             if (player_reach_prob > 1e-9) {
                 for (size_t i = 0; i < num_actions; ++i) {
                     if (!std::isnan(current_strategy[i]) && !std::isinf(current_strategy[i])) {
                        node_ptr->strategy_sum[i] += player_reach_prob * current_strategy[i];
                     } else { /* Log warning */ }
                 }
             }
        } // Node mutex released

        node_ptr->visit_count++; // Atomic increment
    }

    // spdlog::trace("Depth {}: Returning utility {:.2f} for player {}", depth, node_utility, traversing_player); // DEBUG LOG COMMENTED
    return node_utility;
}


// --- Public Methods ---

// Train function needs to pass down RNG for opponent sampling
void CFREngine::train(int iterations, int num_players, int initial_stack, int ante_size, int num_threads, const std::string& save_filename, int checkpoint_interval, const std::string& load_filename) {
    // spdlog::info("Entering train function..."); // DEBUG LOG COMMENTED

    // --- Load Checkpoint if specified ---
    int starting_iteration = 0;
    if (!load_filename.empty()) { /* ... load logic ... */
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
    if (iterations_to_run <= 0) { /* ... return if done ... */
        spdlog::info("Target iterations ({}) already reached or exceeded by checkpoint ({}). No training needed.", iterations, starting_iteration);
        return;
    }
     spdlog::info("Need to run {} more iterations.", iterations_to_run);
    completed_iterations_ = starting_iteration;
    if (starting_iteration == 0) total_nodes_created_ = 0;
    last_logged_percent_ = -1;
    max_depth_reached_ = 0; // Reset max depth tracker
    unsigned int hardware_threads = std::thread::hardware_concurrency();
    unsigned int threads_to_use = (num_threads <= 0) ? hardware_threads : std::min((unsigned int)num_threads, hardware_threads);
    if (threads_to_use == 0) threads_to_use = 1;
    spdlog::info("Using {} threads for training.", threads_to_use);
    std::vector<Card> master_deck;
    // (Deck initialization remains the same)
    const std::vector<char> ranks = {'2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K', 'A'};
    const std::vector<char> suits = {'c', 'd', 'h', 's'};
    for (char r : ranks) { for (char s : suits) { master_deck.push_back(std::string(1, r) + s); } }
    // spdlog::info("Master deck initialized."); // DEBUG LOG COMMENTED

    // --- Thread Worker Function (Needs RNG) ---
    auto worker_task = [&](int thread_id, int iterations_for_thread) {
        // spdlog::info("[Thread {}] Starting worker task for {} iterations.", thread_id, iterations_for_thread); // DEBUG LOG COMMENTED
        unsigned seed = std::chrono::system_clock::now().time_since_epoch().count() + thread_id + starting_iteration;
        std::mt19937 rng(seed); // Thread-local RNG
        std::vector<Card> deck = master_deck;
        int last_checkpoint_iter_count = (checkpoint_interval > 0 && checkpoint_interval != 0) ? starting_iteration / checkpoint_interval : 0; // Avoid division by zero

        for (int i = 0; i < iterations_for_thread; ++i) {
            // spdlog::trace("[Thread {}] Starting iteration {}", thread_id, i); // DEBUG LOG (very verbose)
            int global_iteration_approx = starting_iteration + completed_iterations_.load(std::memory_order_relaxed); // Use relaxed for approx count
            int button_pos = global_iteration_approx % num_players;
            GameState root_state(num_players, initial_stack, ante_size, button_pos);
            std::shuffle(deck.begin(), deck.end(), rng);
            std::vector<std::vector<Card>> hands(num_players);
            int card_index = 0;
            bool deal_ok = true;
            for (int p = 0; p < num_players; ++p) { /* ... deal hands ... */
                 if (card_index + 1 >= deck.size()) { deal_ok = false; break; }
                 hands[p].push_back(deck[card_index++]);
                 hands[p].push_back(deck[card_index++]);
                 std::sort(hands[p].begin(), hands[p].end());
            }
            if (!deal_ok) { spdlog::error("[Thread {}] Deal error.", thread_id); continue; }
            root_state.deal_hands(hands);

            // Run MCCFR from the root for each player
            for (int player = 0; player < num_players; ++player) {
                std::vector<double> initial_reach_probs(num_players, 1.0);
                int current_card_idx = card_index;
                try {
                    // Pass the thread-local RNG and initial depth 0
                    cfr_plus_recursive(root_state, player, initial_reach_probs, deck, current_card_idx, rng, 0);
                } catch (const std::exception& e) {
                     spdlog::error("[Thread {}] Exception in cfr_plus_recursive: {}", thread_id, e.what());
                }
            }
            // (Progress logging and checkpointing remain the same)
            int current_completed = completed_iterations_++;
            if (thread_id == 0) { /* ... log progress ... */
                 int current_percent = static_cast<int>((static_cast<double>(current_completed + 1) / iterations) * 100.0);
                 int last_logged = last_logged_percent_.load(std::memory_order_relaxed);

                 if (current_percent >= last_logged + 5) {
                     int target_percent = current_percent - (current_percent % 5);
                     if (target_percent > last_logged) {
                          if (last_logged_percent_.compare_exchange_strong(last_logged, target_percent)) {
                               spdlog::info("Training progress: {}%", target_percent);
                          }
                     }
                 }
            }
             if (thread_id == 0 && !save_filename.empty() && checkpoint_interval > 0) { /* ... save checkpoint ... */
                  int completed_count = current_completed + 1; // Use the value *after* increment
                  if (completed_count / checkpoint_interval > last_checkpoint_iter_count) {
                      last_checkpoint_iter_count = completed_count / checkpoint_interval; // Update count
                      spdlog::info("[Thread 0] Reached checkpoint interval (around iteration {}). Saving state...", completed_count);
                      std::string temp_filename = save_filename + ".tmp";
                      if (save_checkpoint(temp_filename)) {
                          try {
                              std::filesystem::rename(temp_filename, save_filename);
                              spdlog::info("[Thread 0] Checkpoint saved successfully to {}", save_filename);
                          } catch (const std::filesystem::filesystem_error& fs_err) {
                              spdlog::error("[Thread 0] Failed to rename temporary checkpoint file: {}", fs_err.what());
                              try { std::filesystem::remove(temp_filename); } catch(...) {}
                          }
                      } else {
                          spdlog::error("[Thread 0] Failed to save checkpoint to temporary file {}", temp_filename);
                      }
                  }
             }
        } // End iteration loop
        // spdlog::info("[Thread {}] Worker task finished.", thread_id); // DEBUG LOG COMMENTED
    }; // End lambda worker_task

    // --- Launch Threads ---
    // spdlog::info("Launching threads..."); // DEBUG LOG COMMENTED
    std::vector<std::thread> threads;
    int iterations_per_thread = iterations_to_run / threads_to_use;
    int remaining_iterations = iterations_to_run % threads_to_use;

    for (unsigned int i = 0; i < threads_to_use; ++i) {
        int iters = iterations_per_thread + (i < remaining_iterations ? 1 : 0);
        if (iters > 0) {
            // spdlog::info("Launching thread {} for {} iterations.", i, iters); // DEBUG LOG COMMENTED
            threads.emplace_back(worker_task, i, iters);
        }
    }
    // spdlog::info("All threads launched. Joining..."); // DEBUG LOG COMMENTED

    // --- Join Threads ---
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    // spdlog::info("All threads joined."); // DEBUG LOG COMMENTED

    // --- Final Log & Save ---
    // Ensure 100% is logged if the loop finished before the last 5% step triggered it
    if (last_logged_percent_.load() < 100 && completed_iterations_.load() >= iterations) {
         spdlog::info("Training progress: 100%");
    }
    spdlog::info("Training complete. Total iterations run: {}. Final iteration count: {}. Nodes created: {}. Max depth reached: {}",
                 iterations_to_run, completed_iterations_.load(), total_nodes_created_.load(), max_depth_reached_.load()); // Log max depth
    if (!save_filename.empty()) { /* ... final save ... */
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
// (save_checkpoint and load_checkpoint remain the same binary versions)
bool CFREngine::save_checkpoint(const std::string& filename) const {
    std::lock_guard<std::mutex> map_lock(const_cast<std::mutex&>(node_map_mutex_));
    std::ofstream ofs(filename, std::ios::binary | std::ios::trunc);
    if (!ofs) { spdlog::error("Failed to open checkpoint file for writing: {}", filename); return false; }
    try {
        uint32_t version = CHECKPOINT_VERSION_BIN;
        ofs.write(reinterpret_cast<const char*>(&version), sizeof(version)); if (!ofs) return false;
        int completed = completed_iterations_.load();
        ofs.write(reinterpret_cast<const char*>(&completed), sizeof(completed)); if (!ofs) return false;
        size_t map_size = node_map_.size();
        ofs.write(reinterpret_cast<const char*>(&map_size), sizeof(map_size)); if (!ofs) return false;
        for (const auto& pair : node_map_) {
            const std::string& key = pair.first;
            const std::unique_ptr<Node>& node_ptr = pair.second;
            if (!node_ptr) continue;
            std::lock_guard<std::mutex> node_lock(node_ptr->node_mutex);
            size_t key_len = key.length();
            ofs.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len)); if (!ofs) return false;
            ofs.write(key.c_str(), key_len); if (!ofs) return false;
            size_t vec_size = node_ptr->regret_sum.size();
            if (vec_size != node_ptr->strategy_sum.size()) { spdlog::error("Vector size mismatch for key '{}'", key); return false; }
            ofs.write(reinterpret_cast<const char*>(&vec_size), sizeof(vec_size)); if (!ofs) return false;
            ofs.write(reinterpret_cast<const char*>(node_ptr->regret_sum.data()), vec_size * sizeof(double)); if (!ofs) return false;
            ofs.write(reinterpret_cast<const char*>(node_ptr->strategy_sum.data()), vec_size * sizeof(double)); if (!ofs) return false;
            int visits = node_ptr->visit_count.load();
            ofs.write(reinterpret_cast<const char*>(&visits), sizeof(visits)); if (!ofs) return false;
        }
        long long nodes_created = total_nodes_created_.load();
        ofs.write(reinterpret_cast<const char*>(&nodes_created), sizeof(nodes_created)); if (!ofs) return false;
    } catch (const std::exception& e) { spdlog::error("Exception during checkpoint save: {}", e.what()); ofs.close(); return false; }
    ofs.close(); return ofs.good();
}

int CFREngine::load_checkpoint(const std::string& filename) {
    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs) { spdlog::error("Failed to open checkpoint file for reading: {}", filename); return -1; }
    int loaded_iterations = -1;
    long long loaded_nodes_created = 0;
    NodeMap temp_node_map;
    try {
        uint32_t version;
        ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (!ifs || version != CHECKPOINT_VERSION_BIN) { spdlog::error("Checkpoint version mismatch. Expected: {}, Found: {}", CHECKPOINT_VERSION_BIN, version); ifs.close(); return -1; }
        ifs.read(reinterpret_cast<char*>(&loaded_iterations), sizeof(loaded_iterations));
        if (!ifs || loaded_iterations < 0) { spdlog::error("Invalid iteration count in checkpoint."); ifs.close(); return -1; }
        size_t map_size;
        ifs.read(reinterpret_cast<char*>(&map_size), sizeof(map_size)); if (!ifs) { spdlog::error("Failed to read map size."); ifs.close(); return -1; }
        for (size_t i = 0; i < map_size; ++i) {
            size_t key_len;
            ifs.read(reinterpret_cast<char*>(&key_len), sizeof(key_len)); if (!ifs) { spdlog::error("Failed reading key length at entry {}", i); ifs.close(); return -1; }
            std::string key(key_len, '\0');
            ifs.read(&key[0], key_len); if (!ifs) { spdlog::error("Failed reading key data at entry {}", i); ifs.close(); return -1; }
            size_t vec_size;
            ifs.read(reinterpret_cast<char*>(&vec_size), sizeof(vec_size)); if (!ifs) { spdlog::error("Failed reading vector size for key '{}'", key); ifs.close(); return -1; }
            auto node_ptr = std::make_unique<Node>(vec_size);
            ifs.read(reinterpret_cast<char*>(node_ptr->regret_sum.data()), vec_size * sizeof(double)); if (!ifs) { spdlog::error("Failed reading regret_sum for key '{}'", key); ifs.close(); return -1; }
            ifs.read(reinterpret_cast<char*>(node_ptr->strategy_sum.data()), vec_size * sizeof(double)); if (!ifs) { spdlog::error("Failed reading strategy_sum for key '{}'", key); ifs.close(); return -1; }
            int visits;
            ifs.read(reinterpret_cast<char*>(&visits), sizeof(visits)); if (!ifs) { spdlog::error("Failed reading visit_count for key '{}'", key); ifs.close(); return -1; }
            node_ptr->visit_count.store(visits, std::memory_order_relaxed);
            temp_node_map.try_emplace(key, std::move(node_ptr));
        }
        ifs.read(reinterpret_cast<char*>(&loaded_nodes_created), sizeof(loaded_nodes_created));
        if (!ifs) { spdlog::warn("Could not read total_nodes_created from checkpoint."); loaded_nodes_created = temp_node_map.size(); }
        if (temp_node_map.size() != map_size) { spdlog::error("Checkpoint truncated. Loaded {} of {} entries.", temp_node_map.size(), map_size); ifs.close(); return -1; }
    } catch (const std::exception& e) { spdlog::error("Exception during load: {}", e.what()); if(ifs.is_open()) ifs.close(); return -1; }
    { std::lock_guard<std::mutex> lock(node_map_mutex_); node_map_ = std::move(temp_node_map); }
    completed_iterations_.store(loaded_iterations);
    total_nodes_created_.store(loaded_nodes_created);
    return loaded_iterations;
}


std::vector<double> CFREngine::get_strategy(const std::string& info_set_key) {
    std::lock_guard<std::mutex> map_lock(node_map_mutex_);
    auto it = node_map_.find(info_set_key);
    if (it != node_map_.end()) {
        Node* node_ptr = it->second.get();
        if (node_ptr) {
             std::lock_guard<std::mutex> node_lock(node_ptr->node_mutex);
             return node_ptr->get_average_strategy();
        } else {
             spdlog::error("Null pointer found in NodeMap for key: {}", info_set_key);
             return {};
        }
    } else {
        return {};
    }
}

} // namespace gto_solver
