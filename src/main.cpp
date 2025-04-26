#include <iostream>
#include <vector> // Include vector
#include <string> // Include string
#include <memory> // Include memory for std::make_shared

// Corrected includes (removed "include/")
#include "game_state.h"
#include "hand_generator.h"
#include "hand_evaluator.h"
#include "action_abstraction.h"
#include "cfr_engine.h" // Includes StrategyInfo struct
#include "monte_carlo.h"
#include "info_set.h"
#include "node.h" // Include Node definition

#include "spdlog/spdlog.h" // Include spdlog
#include "spdlog/sinks/stdout_color_sinks.h" // For console logging
#include "spdlog/fmt/bundled/format.h" // Include fmt for logging vectors
#include <cstdlib> // For std::atoi, std::atof (or use C++ streams/std::stoi etc.)
#include <stdexcept> // For std::invalid_argument in parsing
#include <iomanip> // For std::setw, std::fixed, std::setprecision
#include <algorithm> // For std::sort, std::max_element
#include <iterator>  // For std::distance
#include <map>       // For storing strategies
#include <vector>    // Used extensively
#include <array>     // For grid structure
#include <sstream>   // For stringstream
#include <fstream>   // For std::ofstream (JSON export)

#include <nlohmann/json.hpp> // Include JSON library
using json = nlohmann::json;

// Assume Big Blind size is needed for calculations
const int BIG_BLIND_SIZE = 2; // TODO: Make configurable


// Helper function to format hand vector to string like "AKs", "T9o", "77"
std::string format_hand_string(const std::vector<gto_solver::Card>& hand) {
    if (hand.size() != 2) return "??";
    std::string c1 = hand[0];
    std::string c2 = hand[1];
    std::string ranks = "23456789TJQKA";
    if (ranks.find(c1[0]) < ranks.find(c2[0])) { std::swap(c1, c2); }
    char r1 = c1[0]; char r2 = c2[0]; char s1 = c1[1]; char s2 = c2[1];
    if (r1 == r2) { return std::string(1, r1) + std::string(1, r2); }
    else if (s1 == s2) { return std::string(1, r1) + std::string(1, r2) + "s"; }
    else { return std::string(1, r1) + std::string(1, r2) + "o"; }
}

// Helper function to create action string with amount (used by ActionAbstraction and main)
// This function might become obsolete if we fully switch to ActionSpec internally
std::string create_action_string_local(const std::string& base, double value, const std::string& unit) {
    std::string val_str;
    if (std::abs(value - std::round(value)) < 1e-5) { val_str = std::to_string(static_cast<int>(value)); }
    else { std::stringstream ss; ss << std::fixed << std::setprecision(1) << value; val_str = ss.str(); }
    return base + "_" + val_str + unit;
}


// Function to display the strategy grid for a specific position
// Takes the map of StrategyInfo objects for the specific position
void display_strategy_grid(
    const std::string& position_name,
    const std::map<std::string, gto_solver::StrategyInfo>& position_strategy_info)
{
    spdlog::info("--- Preflop Strategy Grid ({}) ---", position_name);
    // Note: We don't have a single "legal_actions" list anymore, as it can vary per node.
    // The display logic will use the actions stored within each StrategyInfo.

    std::cout << "   A    K    Q    J    T    9    8    7    6    5    4    3    2" << std::endl;
    std::cout << "----------------------------------------------------------------------" << std::endl;

    const std::array<char, 13> ranks = {'A', 'K', 'Q', 'J', 'T', '9', '8', '7', '6', '5', '4', '3', '2'};

    for (int i = 0; i < 13; ++i) {
        std::cout << ranks[i] << "| ";
        for (int j = 0; j < 13; ++j) {
            std::string hand_str_display;
            if (i == j) { hand_str_display = std::string(1, ranks[i]) + std::string(1, ranks[j]); }
            else if (i < j) { hand_str_display = std::string(1, ranks[i]) + std::string(1, ranks[j]) + "s"; }
            else { hand_str_display = std::string(1, ranks[j]) + std::string(1, ranks[i]) + "o"; }

            char display_char = '.'; // Default for not found

            auto it = position_strategy_info.find(hand_str_display);
            if (it != position_strategy_info.end()) {
                const auto& info = it->second;
                // Use info.actions (which are now strings converted from ActionSpec)
                if (info.found && !info.strategy.empty() && !info.actions.empty() && info.strategy.size() == info.actions.size()) {
                    const auto& strategy = info.strategy;
                    const auto& node_legal_actions_str = info.actions; // These are strings now

                    double max_prob = -1.0;
                    size_t max_idx = static_cast<size_t>(-1);
                    for(size_t k = 0; k < strategy.size(); ++k) {
                        // Find the action with highest probability, excluding fold if possible
                        if (node_legal_actions_str[k] != "fold" && strategy[k] > max_prob) {
                            max_prob = strategy[k];
                            max_idx = k;
                        }
                    }
                    // If only fold has probability > 0, or all probabilities are tiny/zero
                    if (max_idx == static_cast<size_t>(-1)) {
                         size_t fold_idx = static_cast<size_t>(-1);
                         for(size_t k=0; k<node_legal_actions_str.size(); ++k) { if(node_legal_actions_str[k] == "fold") { fold_idx = k; break; } }

                         if (fold_idx != static_cast<size_t>(-1) && strategy[fold_idx] > 0.5) { // If fold is clearly dominant
                              display_char = 'F';
                         } else {
                               // Fallback: Find the absolute max probability action, even if it's fold or tiny
                               max_prob = -1.0; // Reset max_prob
                               for(size_t k = 0; k < strategy.size(); ++k) { if (strategy[k] > max_prob) { max_prob = strategy[k]; max_idx = k; } }

                               if (max_idx != static_cast<size_t>(-1)) {
                                    const std::string& action = node_legal_actions_str[max_idx];
                                    if (action == "fold") display_char = 'F';
                                    else if (action == "call") display_char = 'C';
                                    else if (action == "check") display_char = 'K';
                                    else if (action == "all_in") display_char = 'A';
                                    else if (action.find("raise") != std::string::npos) display_char = 'R'; // Keep R for raise
                                    else if (action.find("bet") != std::string::npos) display_char = 'R'; // Marquer les bets comme R aussi
                                    else if (action.find("open") != std::string::npos) display_char = 'R'; // Marquer les open comme R aussi
                                    else display_char = '?'; // Unknown action format
                               } else { display_char = '-'; } // Strategy likely empty or all zeros
                         }
                    } else {
                        // We found a non-fold dominant action
                        const std::string& action = node_legal_actions_str[max_idx];
                        if (action == "call") display_char = 'C';
                        else if (action == "check") display_char = 'K';
                        else if (action == "all_in") display_char = 'A';
                        else if (action.find("raise") != std::string::npos) display_char = 'R'; // Keep R for raise
                        else if (action.find("bet") != std::string::npos) display_char = 'R'; // Marquer les bets comme R aussi
                        else if (action.find("open") != std::string::npos) display_char = 'R'; // Marquer les open comme R aussi
                        else display_char = '?'; // Unknown action format
                    }
                } else if (info.found && !info.strategy.empty()) {
                     display_char = 'E'; // Error
                     spdlog::warn("Strategy/Action size mismatch in node for hand {}", hand_str_display);
                }
            }
            std::cout << std::setw(4) << std::left << display_char << " ";
        }
        std::cout << std::endl;
    }
     std::cout << "----------------------------------------------------------------------" << std::endl;
     std::cout << "Legend: R=Raise/Bet, C=Call/Limp, F=Fold, K=Check, A=All-in, .=NotFound, E=SizeError, ?=UnknownAction, -=No Action" << std::endl << std::endl;
}

// Function to export strategies to JSON
void export_strategies_to_json(
    const std::string& filename,
    const std::map<std::string, std::map<std::string, gto_solver::StrategyInfo>>& position_strategy_infos)
{
    json output_json;

    spdlog::info("Exporting strategies to JSON file: {}", filename);

    for (const auto& pos_pair : position_strategy_infos) {
        const std::string& pos_name = pos_pair.first;
        const auto& strategy_map = pos_pair.second;
        json pos_json; // JSON object for this position

        for (const auto& hand_pair : strategy_map) {
            const std::string& canonical_hand = hand_pair.first;
            const auto& info = hand_pair.second;

            if (info.found && !info.strategy.empty() && !info.actions.empty()) {
                json hand_json; // JSON object for this hand
                hand_json["actions"] = info.actions; // Already strings from get_strategy_info
                json strat_array = json::array();
                for(double prob : info.strategy) {
                     strat_array.push_back(std::round(prob * 10000.0) / 10000.0);
                }
                hand_json["strategy"] = strat_array;
                pos_json[canonical_hand] = hand_json;
            }
        }
        output_json[pos_name] = pos_json;
    }

    try {
        std::ofstream ofs(filename);
        if (!ofs) {
            spdlog::error("Failed to open JSON file for writing: {}", filename);
            return;
        }
        ofs << std::setw(2) << output_json << std::endl;
        ofs.close();
        spdlog::info("Strategies successfully exported to {}", filename);
    } catch (const std::exception& e) {
        spdlog::error("Failed to write JSON file {}: {}", filename, e.what());
    }
}


// Function to parse command line arguments (simple version)
// Added checkpoint parameters
void parse_args(int argc, char* argv[], int& iterations, int& num_players, int& initial_stack, int& ante_size, int& num_threads, std::string& save_file, int& checkpoint_interval, std::string& load_file, std::string& json_export_file) { // Added json export file
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-i" || arg == "--iterations") && i + 1 < argc) {
            try { iterations = std::stoi(argv[++i]); } catch (...) { spdlog::warn("Invalid iterations value."); }
        } else if ((arg == "-n" || arg == "--num_players") && i + 1 < argc) {
             try { num_players = std::stoi(argv[++i]); } catch (...) { spdlog::warn("Invalid num_players value."); }
        } else if ((arg == "-s" || arg == "--stack") && i + 1 < argc) {
             try { initial_stack = std::stoi(argv[++i]); } catch (...) { spdlog::warn("Invalid stack value."); }
        } else if ((arg == "-a" || arg == "--ante") && i + 1 < argc) {
             try { ante_size = std::stoi(argv[++i]); } catch (...) { spdlog::warn("Invalid ante value."); }
        } else if ((arg == "-t" || arg == "--threads") && i + 1 < argc) {
             try { num_threads = std::stoi(argv[++i]); } catch (...) { spdlog::warn("Invalid threads value."); num_threads = 0; }
        } else if ((arg == "--save") && i + 1 < argc) {
            save_file = argv[++i];
        } else if ((arg == "--interval") && i + 1 < argc) {
             try { checkpoint_interval = std::stoi(argv[++i]); if (checkpoint_interval < 0) checkpoint_interval = 0; } catch (...) { spdlog::warn("Invalid interval value."); checkpoint_interval = 0; }
        } else if ((arg == "--load") && i + 1 < argc) {
            load_file = argv[++i];
        } else if ((arg == "--json") && i + 1 < argc) { // Added JSON export argument
            json_export_file = argv[++i];
        }
         else {
            spdlog::warn("Unknown or incomplete argument: {}", arg);
        }
    }
}


int main(int argc, char* argv[]) { // Modified main signature
    // --- Default Parameters ---
    int num_iterations = 10000;
    int num_players = 6; // Default to 6-max now
    int initial_stack = 100; // Default to 100BB
    int ante_size = 0; // Default to no ante
    int num_threads = 0; // Default to 0 (engine will use hardware_concurrency)
    std::string save_file = ""; // Default: no saving
    int checkpoint_interval = 0; // Default: no periodic saving (only final if save_file specified)
    std::string load_file = ""; // Default: no loading
    std::string json_export_file = ""; // Default: no JSON export

    // --- Setup Logging ---
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::info); // Set level to info
        // Set level to debug to see debug logs:
        // console_sink->set_level(spdlog::level::debug);
        spdlog::logger logger("gto_solver_logger", {console_sink});
        logger.set_level(spdlog::level::info); // Set logger level to info
        // logger.set_level(spdlog::level::debug); // Set logger level to debug
        logger.flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::make_shared<spdlog::logger>(logger));
        spdlog::info("Logging initialized.");

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        return 1;
    }

    spdlog::info("Starting GTO Solver");

    // --- Parse Arguments ---
    parse_args(argc, argv, num_iterations, num_players, initial_stack, ante_size, num_threads, save_file, checkpoint_interval, load_file, json_export_file); // Pass json_export_file
    spdlog::info("Configuration - Iterations: {}, Players: {}, Stack: {}, Ante: {}, Threads: {}",
                 num_iterations, num_players, initial_stack, ante_size, (num_threads <= 0 ? "Auto" : std::to_string(num_threads)));
    if (!load_file.empty()) spdlog::info("Load Checkpoint: {}", load_file);
    if (!save_file.empty()) spdlog::info("Save Checkpoint: {}, Interval: {} iters (0=final only)", save_file, checkpoint_interval);
    if (!json_export_file.empty()) spdlog::info("JSON Export File: {}", json_export_file); // Log JSON export file


    try { // START MAIN TRY BLOCK
        // --- Initialization ---
        spdlog::info("Initializing modules...");
        gto_solver::HandGenerator hand_generator;
        gto_solver::CFREngine cfr_engine;
        // ActionAbstraction is now only needed inside CFREngine
        spdlog::info("Modules initialized.");

        // --- Training ---
        spdlog::info("Starting training for target {} iterations...", num_iterations);
        cfr_engine.train(num_iterations, num_players, initial_stack, ante_size, num_threads, save_file, checkpoint_interval, load_file);

        // --- Strategy Extraction and Display ---
        spdlog::info("--- Strategy Extraction ---");

        // Define positions based on num_players
        std::map<std::string, int> position_map;
        if (num_players == 6) {
            // Corrected 6-max positions relative to BTN=0: SB=1, BB=2, UTG=3, MP=4, CO=5
            position_map = {{"UTG", 3}, {"MP", 4}, {"CO", 5}, {"BTN", 0}, {"SB", 1}};
        } else if (num_players == 2) {
             position_map = {{"SB", 0}}; // BTN=SB=0, BB=1
        } else {
             spdlog::warn("RFI extraction only implemented for 6-max and HU.");
             // Continue without extraction if not 6-max or HU
        }

        if (!position_map.empty()) { // Proceed only if positions are defined
            auto all_hands_str = hand_generator.generate_hands();

            // Store strategies per position using StrategyInfo
            std::map<std::string, std::map<std::string, gto_solver::StrategyInfo>> position_strategy_infos;

            // Create a base state for context (street, board) - BTN=0 is arbitrary
            // This state is ONLY used to provide street/board context to the InfoSet constructor
            gto_solver::GameState context_state(num_players, initial_stack, ante_size, 0);

            for (const auto& pos_pair : position_map) {
                const std::string& pos_name = pos_pair.first;
                int player_index = pos_pair.second;

                spdlog::info("Extracting RFI strategy for {} (Player {})", pos_name, player_index);

                // --- Manually construct the expected RFI history string ---
                std::string rfi_history = "s/b/"; // Commencer par les blindes postées
                int button_pos_for_sim = 0; // Assume BTN=0 for consistency
                int first_actor = (num_players == 2) ? 0 : 3; // SB in HU, UTG in 6max (assuming BTN=0)
                int players_before = 0;
                int current_p = first_actor;
                while(current_p != player_index) {
                     if (players_before >= num_players) { spdlog::error("Infinite loop detected..."); break; }
                     players_before++;
                     current_p = (current_p + 1) % num_players;
                }
                for (int i = 0; i < players_before; ++i) { rfi_history += "f/"; }
                // --- END RFI History Construction ---

                // --- DEBUG LOGGING for RFI History ---
                spdlog::info("  Generated RFI History for {}: '{}'", pos_name, rfi_history);


                std::map<std::string, gto_solver::StrategyInfo> current_pos_strategy_info;
                for (const std::string& hand_str_internal : all_hands_str) {
                     if (hand_str_internal.length() != 4) continue;
                    std::vector<gto_solver::Card> hand_vec = {hand_str_internal.substr(0, 2), hand_str_internal.substr(2, 2)};
                    std::vector<gto_solver::Card> sorted_hand_for_key = hand_vec;
                    std::sort(sorted_hand_for_key.begin(), sorted_hand_for_key.end());

                    // Create the InfoSet using the constructor that takes the specific components:
                    gto_solver::InfoSet infoset(sorted_hand_for_key, rfi_history, context_state, player_index); // Use manually constructed history
                    const std::string& infoset_key = infoset.get_key();

                    // Use the new function to get strategy and actions
                    gto_solver::StrategyInfo strat_info = cfr_engine.get_strategy_info(infoset_key);

                    std::string canonical_hand_str = format_hand_string(hand_vec);
                    current_pos_strategy_info[canonical_hand_str] = strat_info;

                    // --- DEBUG LOGGING for specific hands ---
                    if (pos_name == "UTG" && (canonical_hand_str == "AA" || canonical_hand_str == "72o" || canonical_hand_str == "KQs")) { // Added KQs
                         // Log the key directly from the object
                         spdlog::info("  Debug {}: Hand={}, Key={}", pos_name, canonical_hand_str, infoset.get_key()); // Use info level for visibility
                         if (strat_info.found) {
                              std::stringstream ss;
                              if (strat_info.actions.size() == strat_info.strategy.size()) {
                                   for(size_t i=0; i<strat_info.actions.size(); ++i) {
                                        ss << strat_info.actions[i] << "=" << std::fixed << std::setprecision(4) << strat_info.strategy[i] << " ";
                                   }
                              } else {
                                   ss << "ACTION/STRATEGY SIZE MISMATCH!";
                              }
                              spdlog::info("    Strategy: {}", ss.str());
                         } else {
                              spdlog::info("    Strategy: Not Found");
                         }
                    }
                    // --- END DEBUG LOGGING ---
                }
                position_strategy_infos[pos_name] = current_pos_strategy_info;

                // Display grid for this position using the collected StrategyInfo map
                display_strategy_grid(pos_name, current_pos_strategy_info);
            }

            // --- Export to JSON if filename provided ---
            if (!json_export_file.empty()) {
                export_strategies_to_json(json_export_file, position_strategy_infos);
            }
        } // End if (!position_map.empty())

    } catch (const std::exception& e) { // Catch block for main try
        spdlog::error("Exception caught during execution: {}", e.what());
        return 1;
    } catch (...) { // Catch-all block
        spdlog::error("Unknown exception caught during execution.");
        return 1;
    } // Added missing closing brace for main try block

    spdlog::info("GTO Solver finished successfully.");
    return 0;
}
