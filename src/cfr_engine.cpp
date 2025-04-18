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
#include <random>    // For std::mt19937, std::random_device
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

// #include <nlohmann/json.hpp> // No longer needed for binary format
#include "spdlog/spdlog.h" // Include spdlog

// Define a simple version number for the BINARY checkpoint format
const uint32_t CHECKPOINT_VERSION_BIN = 2; // Increment version for binary format

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

// Recursive CFR+ function - uses node_ptr and node_mutex
double CFREngine::cfr_plus_recursive(
    GameState current_state,
    int traversing_player,
    const std::vector<double>& reach_probabilities,
    std::vector<Card>& deck,
    int& card_idx
) {
    // (Terminal state logic remains the same)
     Street entry_street = current_state.get_current_street();
    if (current_state.is_terminal()) {
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
                                 spdlog::warn("Showdown occurred but player {} has invalid hand size {}. History: {}", eligible_player_index, hand.size(), current_state.get_history_string());
                                 current_ranks[eligible_player_index] = 9999;
                             }
                        }
                        for (int eligible_player_index : current_pot_eligible_players) {
                            if (current_ranks[eligible_player_index] == best_rank) current_winners.push_back(eligible_player_index);
                        }
                    } else {
                         spdlog::debug("Showdown before river ({} cards) for pot level {}. Splitting pot segment.", community_cards.size(), current_contribution_level);
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
        return final_payoff;
    }

    // --- 2. Get InfoSet and Node ---
    int current_player = current_state.get_current_player();
     if (current_state.get_player_hand(current_player).empty()) {
         spdlog::debug("Player {} has no hand in non-terminal state (likely just folded). History: {}", current_player, current_state.get_history_string());
         return 0.0;
     }
    InfoSet info_set(current_state.get_player_hand(current_player), current_state.get_history_string());
    std::string info_set_key = info_set.get_key();

    std::vector<std::string> legal_actions_str = action_abstraction_.get_possible_actions(current_state);
    size_t num_actions = legal_actions_str.size();

    if (num_actions == 0) {
         spdlog::debug("No legal actions found for player {} in non-terminal state. History: {}", current_player, current_state.get_history_string());
         return 0.0;
    }

    Node* node_ptr = nullptr;
    // --- Thread-safe Node Lookup/Creation ---
    {
        std::lock_guard<std::mutex> lock(node_map_mutex_);
        auto it = node_map_.find(info_set_key);
        if (it == node_map_.end()) {
            auto emplace_result = node_map_.emplace(info_set_key, std::make_unique<Node>(num_actions));
            node_ptr = emplace_result.first->second.get();
            total_nodes_created_++;
        } else {
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
        std::lock_guard<std::mutex> node_lock(node_ptr->node_mutex); // Lock specific node
        current_regrets = node_ptr->regret_sum; // Copy regrets under node lock
    }
    std::vector<double> current_strategy = get_strategy_from_regrets(current_regrets);

    // --- 4. Calculate Expected Value & Recurse ---
    std::vector<double> action_utilities(num_actions, 0.0);
    double node_utility = 0.0;

    for (size_t i = 0; i < num_actions; ++i) {
        Action action;
        const std::string& action_str = legal_actions_str[i];
        action.player_index = current_player;

        // (Action parsing logic remains the same)
        if (action_str == "fold") { action.type = Action::Type::FOLD; }
        else if (action_str == "call") { action.type = Action::Type::CALL; }
        else if (action_str == "check") { action.type = Action::Type::CHECK; action.amount = 0; }
        else if (action_str.find("raise") != std::string::npos || action_str.find("bet") != std::string::npos || action_str == "all_in") {
             action.type = (current_state.get_amount_to_call(current_player) == 0) ? Action::Type::BET : Action::Type::RAISE;
             action.amount = action_abstraction_.get_action_amount(action_str, current_state);
             if (action.amount == -1) { continue; }
        } else {
             spdlog::error("Unsupported action string: {}", action_str);
             throw std::logic_error("Unsupported action string: " + action_str);
        }

        GameState next_state = current_state;
        try {
             next_state.apply_action(action);
        } catch (const std::exception& e) {
             spdlog::warn("Exception applying action '{}' in state {}. Skipping. Error: {}", action_str, current_state.get_history_string(), e.what());
             continue;
        }

        // (Community card dealing logic remains the same)
        Street next_street = next_state.get_current_street();
        int current_card_idx = card_idx;
        if (next_street != entry_street && next_street != Street::SHOWDOWN) {
            std::vector<Card> cards_to_deal;
            int num_cards_to_deal = 0;
            if (next_street == Street::FLOP && entry_street == Street::PREFLOP) num_cards_to_deal = 3;
            else if (next_street == Street::TURN && entry_street == Street::FLOP) num_cards_to_deal = 1;
            else if (next_street == Street::RIVER && entry_street == Street::TURN) num_cards_to_deal = 1;

            if (num_cards_to_deal > 0) {
                if (card_idx + num_cards_to_deal <= deck.size()) {
                    for(int k=0; k<num_cards_to_deal; ++k) cards_to_deal.push_back(deck[card_idx++]);
                    next_state.deal_community_cards(cards_to_deal);
                } else {
                    spdlog::error("Not enough cards left in deck to deal {} for {}. Deck size: {}, Card index: {}",
                                  num_cards_to_deal, static_cast<int>(next_street), deck.size(), card_idx);
                    card_idx = current_card_idx; continue;
                }
            }
        }

        std::vector<double> next_reach_probabilities = reach_probabilities;
        if(current_player != traversing_player) {
             next_reach_probabilities[current_player] *= current_strategy[i];
        }

        action_utilities[i] = -cfr_plus_recursive(next_state, traversing_player, next_reach_probabilities, deck, card_idx);
        card_idx = current_card_idx;
        node_utility += current_strategy[i] * action_utilities[i];
    }

    // --- 5. Update Regrets & Strategy Sum (CFR+ specific updates) ---
    if (current_player == traversing_player) {
        double counterfactual_reach_prob = 1.0;
        for(int p = 0; p < current_state.get_num_players(); ++p) {
            if (p != current_player) {
                counterfactual_reach_prob *= reach_probabilities[p];
            }
        }

        // Lock the specific node's mutex to update its sums
        {
            std::lock_guard<std::mutex> node_lock(node_ptr->node_mutex); // Use node_ptr
            if (counterfactual_reach_prob > 1e-9) {
                 for (size_t i = 0; i < num_actions; ++i) {
                     double regret = action_utilities[i] - node_utility;
                     if (!std::isnan(regret) && !std::isinf(regret)) {
                        node_ptr->regret_sum[i] += counterfactual_reach_prob * regret; // Use simple +=
                     } else {
                        spdlog::warn("Invalid regret calculated for action {} in node {}: {}", i, info_set_key, regret);
                     }
                 }
            }
            double player_reach_prob = reach_probabilities[current_player];
             if (player_reach_prob > 1e-9) {
                 for (size_t i = 0; i < num_actions; ++i) {
                     if (!std::isnan(current_strategy[i]) && !std::isinf(current_strategy[i])) {
                        node_ptr->strategy_sum[i] += player_reach_prob * current_strategy[i]; // Use simple +=
                     } else {
                         spdlog::warn("Invalid strategy value calculated for action {} in node {}: {}", i, info_set_key, current_strategy[i]);
                     }
                 }
             }
        } // Node mutex released

        node_ptr->visit_count++; // Atomic increment
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
    completed_iterations_ = starting_iteration;
    if (starting_iteration == 0) total_nodes_created_ = 0;
    last_logged_percent_ = -1;
    unsigned int hardware_threads = std::thread::hardware_concurrency();
    unsigned int threads_to_use = (num_threads <= 0) ? hardware_threads : std::min((unsigned int)num_threads, hardware_threads);
    if (threads_to_use == 0) threads_to_use = 1;
    spdlog::info("Using {} threads for training.", threads_to_use);
    std::vector<Card> master_deck;
    const std::vector<char> ranks = {'2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K', 'A'};
    const std::vector<char> suits = {'c', 'd', 'h', 's'};
    for (char r : ranks) {
        for (char s : suits) {
            master_deck.push_back(std::string(1, r) + s);
        }
    }

    // --- Thread Worker Function (remains largely the same, calls updated cfr_plus_recursive) ---
    auto worker_task = [&](int thread_id, int iterations_for_thread) {
        unsigned seed = std::chrono::system_clock::now().time_since_epoch().count()
                      + thread_id + starting_iteration;
        std::mt19937 rng(seed);
        std::vector<Card> deck = master_deck;
        int last_checkpoint_iter_count = (checkpoint_interval > 0) ? starting_iteration / checkpoint_interval : 0; // Avoid division by zero

        for (int i = 0; i < iterations_for_thread; ++i) {
            int global_iteration_approx = starting_iteration + completed_iterations_.load();
            int button_pos = global_iteration_approx % num_players;
            GameState root_state(num_players, initial_stack, ante_size, button_pos);
            std::shuffle(deck.begin(), deck.end(), rng);
            std::vector<std::vector<Card>> hands(num_players);
            int card_index = 0;
            bool deal_ok = true;
            for (int p = 0; p < num_players; ++p) {
                if (card_index + 1 >= deck.size()) {
                    spdlog::error("[Thread {}] Not enough cards to deal hands. Index: {}, Deck size: {}", thread_id, card_index, deck.size());
                    deal_ok = false; break;
                }
                hands[p].push_back(deck[card_index++]);
                hands[p].push_back(deck[card_index++]);
                std::sort(hands[p].begin(), hands[p].end());
            }
            if (!deal_ok) continue;
            root_state.deal_hands(hands);
            for (int player = 0; player < num_players; ++player) {
                std::vector<double> initial_reach_probs(num_players, 1.0);
                int current_card_idx = card_index;
                try {
                    cfr_plus_recursive(root_state, player, initial_reach_probs, deck, current_card_idx);
                } catch (const std::exception& e) {
                     spdlog::error("[Thread {}] Exception in cfr_plus_recursive: {}", thread_id, e.what());
                }
            }
            int current_completed = completed_iterations_++;
            if (thread_id == 0) {
                int current_percent = static_cast<int>((static_cast<double>(current_completed + 1) / iterations) * 100.0);
                int last_logged = last_logged_percent_.load();
                if (current_percent >= last_logged + 5) {
                    int target_percent = current_percent - (current_percent % 5);
                    if (target_percent > last_logged) {
                         if (last_logged_percent_.compare_exchange_strong(last_logged, target_percent)) {
                              spdlog::info("Training progress: {}%", target_percent);
                         }
                    }
                }
            }
             if (thread_id == 0 && !save_filename.empty() && checkpoint_interval > 0) {
                 int completed_count = current_completed + 1;
                 if (completed_count / checkpoint_interval > last_checkpoint_iter_count) {
                     last_checkpoint_iter_count = completed_count / checkpoint_interval;
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
    }; // End lambda worker_task

    // (Thread launch and join logic remains the same)
    std::vector<std::thread> threads;
    int iterations_per_thread = iterations_to_run / threads_to_use;
    int remaining_iterations = iterations_to_run % threads_to_use;
    for (unsigned int i = 0; i < threads_to_use; ++i) {
        int iters = iterations_per_thread + (i < remaining_iterations ? 1 : 0);
        if (iters > 0) threads.emplace_back(worker_task, i, iters);
    }
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    // (Final log and save logic remains the same)
    if (last_logged_percent_.load() < 100 && completed_iterations_.load() >= iterations) {
         spdlog::info("Training progress: 100%");
    }
    spdlog::info("Training complete. Total iterations run: {}. Final iteration count: {}. Nodes created: {}", iterations_to_run, completed_iterations_.load(), total_nodes_created_.load());
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

// Save state to BINARY file (adapted for unique_ptr and mutex-per-node)
bool CFREngine::save_checkpoint(const std::string& filename) const {
    std::lock_guard<std::mutex> map_lock(const_cast<std::mutex&>(node_map_mutex_));

    std::ofstream ofs(filename, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        spdlog::error("Failed to open checkpoint file for writing: {}", filename);
        return false;
    }

    try {
        // 1. Version
        uint32_t version = CHECKPOINT_VERSION_BIN; // Use binary version
        ofs.write(reinterpret_cast<const char*>(&version), sizeof(version));
        if (!ofs) { spdlog::error("Error writing version"); return false; }


        // 2. Completed Iterations
        int completed = completed_iterations_.load();
        ofs.write(reinterpret_cast<const char*>(&completed), sizeof(completed));
         if (!ofs) { spdlog::error("Error writing completed iterations"); return false; }


        // 3. Node Map Size
        size_t map_size = node_map_.size();
        ofs.write(reinterpret_cast<const char*>(&map_size), sizeof(map_size));
         if (!ofs) { spdlog::error("Error writing map size"); return false; }


        // 4. Node Map Data
        for (const auto& pair : node_map_) {
            const std::string& key = pair.first;
            const std::unique_ptr<Node>& node_ptr = pair.second;

            if (!node_ptr) {
                 spdlog::warn("Null pointer found in map for key '{}'. Skipping save.", key);
                 continue;
            }

            // Lock the individual node's mutex before accessing its data
            std::lock_guard<std::mutex> node_lock(node_ptr->node_mutex);

            // Write key length and key
            size_t key_len = key.length();
            ofs.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
             if (!ofs) { spdlog::error("Error writing key length for key '{}'", key); return false; }
            ofs.write(key.c_str(), key_len);
             if (!ofs) { spdlog::error("Error writing key data for key '{}'", key); return false; }


            // Write vector sizes (assuming they are the same)
            size_t vec_size = node_ptr->regret_sum.size();
            if (vec_size != node_ptr->strategy_sum.size()) {
                 spdlog::error("Mismatch in vector sizes for node key '{}'. Cannot save checkpoint.", key);
                 return false;
            }
            ofs.write(reinterpret_cast<const char*>(&vec_size), sizeof(vec_size));
             if (!ofs) { spdlog::error("Error writing vector size for key '{}'", key); return false; }


            // Write regret_sum data
            ofs.write(reinterpret_cast<const char*>(node_ptr->regret_sum.data()), vec_size * sizeof(double));
             if (!ofs) { spdlog::error("Error writing regret_sum for key '{}'", key); return false; }


            // Write strategy_sum data
            ofs.write(reinterpret_cast<const char*>(node_ptr->strategy_sum.data()), vec_size * sizeof(double));
             if (!ofs) { spdlog::error("Error writing strategy_sum for key '{}'", key); return false; }


            // Write visit_count (load from atomic)
            int visits = node_ptr->visit_count.load();
            ofs.write(reinterpret_cast<const char*>(&visits), sizeof(visits));
             if (!ofs) { spdlog::error("Error writing visit_count for key '{}'", key); return false; }

        } // Node mutex released here

        // 5. Total Nodes Created (Optional but useful for consistency check)
        long long nodes_created = total_nodes_created_.load();
        ofs.write(reinterpret_cast<const char*>(&nodes_created), sizeof(nodes_created));
         if (!ofs) { spdlog::error("Error writing total nodes created"); return false; }


    } catch (const std::exception& e) {
         spdlog::error("Exception caught during checkpoint save: {}", e.what());
         ofs.close(); // Ensure file is closed on exception
         return false;
    }

    ofs.close();
    return ofs.good();
}


// Load state from BINARY file (adapted for unique_ptr and mutex-per-node)
int CFREngine::load_checkpoint(const std::string& filename) {

    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs) {
        spdlog::error("Failed to open checkpoint file for reading: {}", filename);
        return -1;
    }

    int loaded_iterations = -1;
    long long loaded_nodes_created = 0;
    NodeMap temp_node_map; // Load into a temporary map first

    try {
        // 1. Version Check
        uint32_t version;
        ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (!ifs || version != CHECKPOINT_VERSION_BIN) {
            spdlog::error("Checkpoint file version mismatch or read error. Expected: {}, Found: {}", CHECKPOINT_VERSION_BIN, version);
            ifs.close(); return -1;
        }

        // 2. Completed Iterations
        ifs.read(reinterpret_cast<char*>(&loaded_iterations), sizeof(loaded_iterations));
        if (!ifs || loaded_iterations < 0) {
             spdlog::error("Invalid or missing iteration count in checkpoint.");
             ifs.close(); return -1;
        }

        // 3. Node Map Size
        size_t map_size;
        ifs.read(reinterpret_cast<char*>(&map_size), sizeof(map_size));
         if (!ifs) {
             spdlog::error("Failed to read map size from checkpoint.");
             ifs.close(); return -1;
         }

        // 4. Node Map Data
        for (size_t i = 0; i < map_size; ++i) {
            // Read key length and key
            size_t key_len;
            ifs.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
            if (!ifs) { spdlog::error("Failed reading key length at entry {}", i); ifs.close(); return -1; }
            std::string key(key_len, '\0');
            ifs.read(&key[0], key_len);
             if (!ifs) { spdlog::error("Failed reading key data at entry {}", i); ifs.close(); return -1; }

            // Read vector size
            size_t vec_size;
            ifs.read(reinterpret_cast<char*>(&vec_size), sizeof(vec_size));
             if (!ifs) { spdlog::error("Failed reading vector size for key '{}'", key); ifs.close(); return -1; }

            // Create the node using make_unique
            auto node_ptr = std::make_unique<Node>(vec_size);

            // Read data directly into the node's vectors
            ifs.read(reinterpret_cast<char*>(node_ptr->regret_sum.data()), vec_size * sizeof(double));
             if (!ifs) { spdlog::error("Failed reading regret_sum for key '{}'", key); ifs.close(); return -1; }

            ifs.read(reinterpret_cast<char*>(node_ptr->strategy_sum.data()), vec_size * sizeof(double));
             if (!ifs) { spdlog::error("Failed reading strategy_sum for key '{}'", key); ifs.close(); return -1; }

            // Read visit_count
            int visits;
            ifs.read(reinterpret_cast<char*>(&visits), sizeof(visits));
             if (!ifs) { spdlog::error("Failed reading visit_count for key '{}'", key); ifs.close(); return -1; }
            node_ptr->visit_count.store(visits, std::memory_order_relaxed);

            // Use try_emplace with std::move into the temporary map
            temp_node_map.try_emplace(key, std::move(node_ptr));
        }

         // Try reading total_nodes_created (handle potential EOF for older versions)
         ifs.read(reinterpret_cast<char*>(&loaded_nodes_created), sizeof(loaded_nodes_created));
         if (!ifs) {
              spdlog::warn("Could not read total_nodes_created from checkpoint (might be older version or corrupted). Estimating based on loaded nodes.");
              loaded_nodes_created = temp_node_map.size(); // Estimate
         }

        // Check if we read the expected number of nodes (do this after trying to read nodes_created)
         if (temp_node_map.size() != map_size) {
             spdlog::error("Checkpoint file appears truncated or corrupted during node map load. Loaded {} of {} expected entries.", temp_node_map.size(), map_size);
             ifs.close(); return -1;
         }


    } catch (const std::exception& e) {
         spdlog::error("Standard exception during load: {}", e.what());
         if(ifs.is_open()) ifs.close(); return -1;
    }

    // --- Safely swap the loaded map with the member map ---
    {
        std::lock_guard<std::mutex> lock(node_map_mutex_);
        node_map_ = std::move(temp_node_map);
    }

    // Update atomic counters after successful load and swap
    completed_iterations_.store(loaded_iterations);
    total_nodes_created_.store(loaded_nodes_created);

    return loaded_iterations;

} // <--- Closing brace for load_checkpoint function


std::vector<double> CFREngine::get_strategy(const std::string& info_set_key) {
    // Lock the global map mutex first to find the node
    std::lock_guard<std::mutex> map_lock(node_map_mutex_);
    auto it = node_map_.find(info_set_key);
    if (it != node_map_.end()) {
        // Node found, use .get() to get the raw pointer from unique_ptr
        Node* node_ptr = it->second.get();
        if (node_ptr) {
             // Lock the node's specific mutex before accessing its data
             std::lock_guard<std::mutex> node_lock(node_ptr->node_mutex);
             // Return average strategy (calculated while node is locked)
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
