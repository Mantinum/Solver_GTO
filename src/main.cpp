#include <iostream>
#include <vector> // Include vector
#include <string> // Include string
#include <memory> // Include memory for std::make_shared

// Corrected includes (removed "include/")
#include "game_state.h"
#include "hand_generator.h"
#include "hand_evaluator.h"
#include "action_abstraction.h"
#include "cfr_engine.h"
#include "monte_carlo.h"
#include "info_set.h"

#include "spdlog/spdlog.h" // Include spdlog
#include "spdlog/sinks/stdout_color_sinks.h" // For console logging
#include <cstdlib> // For std::atoi, std::atof (or use C++ streams/std::stoi etc.)
#include <stdexcept> // For std::invalid_argument in parsing
#include <iomanip> // For std::setw, std::fixed, std::setprecision
#include <algorithm> // For std::sort, std::max_element
#include <iterator>  // For std::distance
#include <map>       // For storing strategies
#include <vector>    // Used extensively
#include <array>     // For grid structure
#include <sstream>   // For stringstream

// Assume Big Blind size is needed for calculations
// TODO: Make this configurable or get from GameState if possible
const int BIG_BLIND_SIZE = 2;


// Helper function to format hand vector to string like "AKs", "T9o", "77"
std::string format_hand_string(const std::vector<gto_solver::Card>& hand) {
    if (hand.size() != 2) return "??";

    std::string c1 = hand[0];
    std::string c2 = hand[1];

    // Ensure canonical order for display (e.g., high card first)
    // Note: InfoSet uses alphabetical sort internally, display order is different
    std::string ranks = "23456789TJQKA";
    if (ranks.find(c1[0]) < ranks.find(c2[0])) {
        std::swap(c1, c2);
    }

    char r1 = c1[0];
    char r2 = c2[0];
    char s1 = c1[1];
    char s2 = c2[1];

    if (r1 == r2) { // Pocket pair
        return std::string(1, r1) + std::string(1, r2);
    } else if (s1 == s2) { // Suited
        return std::string(1, r1) + std::string(1, r2) + "s";
    } else { // Offsuit
        return std::string(1, r1) + std::string(1, r2) + "o";
    }
}

// Helper function to create action string with amount (used by ActionAbstraction and main)
std::string create_action_string_local(const std::string& base, double value, const std::string& unit) {
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


// Function to display the strategy grid for a specific position
void display_strategy_grid(
    const std::string& position_name,
    const std::map<std::string, std::vector<double>>& strategies,
    const std::vector<std::string>& legal_actions) // Pass the CORRECT legal actions for this spot
{
    spdlog::info("--- Preflop Strategy Grid ({}) ---", position_name);
    // Display legal actions for context
    std::string actions_str = "";
    for(const auto& act : legal_actions) actions_str += act + " ";
    spdlog::info("Legal Actions: {}", actions_str);

    std::cout << "   A    K    Q    J    T    9    8    7    6    5    4    3    2" << std::endl;
    std::cout << "----------------------------------------------------------------------" << std::endl; // Adjusted width

    const std::array<char, 13> ranks = {'A', 'K', 'Q', 'J', 'T', '9', '8', '7', '6', '5', '4', '3', '2'};

    for (int i = 0; i < 13; ++i) {
        std::cout << ranks[i] << "| ";
        for (int j = 0; j < 13; ++j) {
            std::string hand_str;
            if (i == j) { // Pocket pair
                hand_str = std::string(1, ranks[i]) + std::string(1, ranks[j]);
            } else if (i < j) { // Suited connector/gapper (e.g., AKs)
                hand_str = std::string(1, ranks[i]) + std::string(1, ranks[j]) + "s";
            } else { // Offsuit connector/gapper (e.g., AKo)
                hand_str = std::string(1, ranks[j]) + std::string(1, ranks[i]) + "o";
            }

            char display_char = '.'; // Default for not found

            if (strategies.count(hand_str)) {
                const auto& strategy = strategies.at(hand_str);
                if (!strategy.empty()) {
                     // Check if strategy size matches the provided legal actions size
                     if (strategy.size() == legal_actions.size()) {
                        double max_prob = -1.0;
                        size_t max_idx = static_cast<size_t>(-1); // Use static_cast for clarity
                        for(size_t k = 0; k < strategy.size(); ++k) {
                            // Only consider non-fold actions for display character if possible
                            if (legal_actions[k] != "fold" && strategy[k] > max_prob) {
                                max_prob = strategy[k];
                                max_idx = k;
                            }
                        }
                        // If only fold has probability > 0, max_idx will remain -1
                        if (max_idx == static_cast<size_t>(-1)) {
                             // Check if fold is the dominant action
                             size_t fold_idx = static_cast<size_t>(-1);
                             for(size_t k=0; k<legal_actions.size(); ++k) { if(legal_actions[k] == "fold") { fold_idx = k; break; } }
                             if (fold_idx != static_cast<size_t>(-1) && strategy[fold_idx] > 0.5) { // Threshold for displaying fold
                                  display_char = 'F';
                             } else {
                                  // Find the overall max probability action if fold isn't dominant
                                   max_prob = -1.0; // Reset max_prob
                                   for(size_t k = 0; k < strategy.size(); ++k) {
                                       if (strategy[k] > max_prob) {
                                           max_prob = strategy[k];
                                           max_idx = k;
                                       }
                                   }
                                   if (max_idx != static_cast<size_t>(-1)) {
                                        // Fallback to showing the highest prob action even if it's fold
                                        const std::string& action = legal_actions[max_idx];
                                        if (action == "fold") display_char = 'F';
                                        else if (action == "call") display_char = 'C';
                                        else if (action == "check") display_char = 'K';
                                        else if (action == "all_in") display_char = 'A';
                                        else if (action.find("raise") != std::string::npos) display_char = 'R';
                                        else if (action.find("bet") != std::string::npos) display_char = 'B';
                                        else display_char = '?';
                                   } else {
                                        display_char = '-'; // Strategy likely empty or all zeros
                                   }
                             }
                        } else {
                            // We found a non-fold dominant action
                            const std::string& action = legal_actions[max_idx];
                            if (action == "call") display_char = 'C'; // Includes Limp for SB
                            else if (action == "check") display_char = 'K'; // Check for BB
                            else if (action == "all_in") display_char = 'A';
                            else if (action.find("raise") != std::string::npos) display_char = 'R';
                            else if (action.find("bet") != std::string::npos) display_char = 'B'; // Postflop
                            else display_char = '?';
                         }
                    } else {
                         // Commented out warning logs to reduce noise
                         // spdlog::warn("Strategy size ({}) mismatch for hand {} vs legal actions size ({}) for {}",
                         //              strategy.size(), hand_str, legal_actions.size(), position_name);
                         // spdlog::warn("Strategy size ({}) mismatch for hand {} vs expected legal actions size ({}) for {}. Displaying based on raw strategy.",
                         //              strategy.size(), hand_str, legal_actions.size(), position_name);
                         // Attempt to display dominant action based on raw strategy size if possible
                         double max_prob_raw = -1.0;
                         size_t max_idx_raw = static_cast<size_t>(-1);
                         for(size_t k = 0; k < strategy.size(); ++k) {
                             if (strategy[k] > max_prob_raw) {
                                 max_prob_raw = strategy[k];
                                 max_idx_raw = k;
                             }
                         }
                         // Cannot reliably map max_idx_raw to action name, use generic '?' or '-'
                         if (max_idx_raw != static_cast<size_t>(-1)) {
                              display_char = '?'; // Indicate mismatch but show dominant action exists
                         } else {
                              display_char = '-'; // No dominant action found in raw strategy
                         }
                    }
                } else {
                     // Strategy is empty, node likely not visited often enough
                     display_char = '.';
                }
            }
            // Use setw for alignment, print the character
            std::cout << std::setw(4) << std::left << display_char << " "; // Added space
        }
        std::cout << std::endl;
    }
     std::cout << "----------------------------------------------------------------------" << std::endl;
     std::cout << "Legend: R=Raise, C=Call/Limp, F=Fold, K=Check, A=All-in, .=NotFound, E=SizeError(DEPRECATED), X=IndexError, ?=MismatchDominant, -=No Action" << std::endl << std::endl;
}


// Function to parse command line arguments (simple version)
// Added checkpoint parameters
void parse_args(int argc, char* argv[], int& iterations, int& num_players, int& initial_stack, int& ante_size, int& num_threads, std::string& save_file, int& checkpoint_interval, std::string& load_file) {
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

    // --- Setup Logging ---
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::info);
        spdlog::logger logger("gto_solver_logger", {console_sink});
        logger.set_level(spdlog::level::info);
        logger.flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::make_shared<spdlog::logger>(logger));
        spdlog::info("Logging initialized.");

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        return 1;
    }

    spdlog::info("Starting GTO Solver");

    // --- Parse Arguments ---
    parse_args(argc, argv, num_iterations, num_players, initial_stack, ante_size, num_threads, save_file, checkpoint_interval, load_file);
    spdlog::info("Configuration - Iterations: {}, Players: {}, Stack: {}, Ante: {}, Threads: {}",
                 num_iterations, num_players, initial_stack, ante_size, (num_threads <= 0 ? "Auto" : std::to_string(num_threads)));
    if (!load_file.empty()) spdlog::info("Load Checkpoint: {}", load_file);
    if (!save_file.empty()) spdlog::info("Save Checkpoint: {}, Interval: {} iters (0=final only)", save_file, checkpoint_interval);


    try {
        // --- Initialization ---
        spdlog::info("Initializing modules...");
        gto_solver::ActionAbstraction action_abstraction; // Keep instance for get_action_amount
        gto_solver::HandGenerator hand_generator;
        gto_solver::CFREngine cfr_engine;
        spdlog::info("Modules initialized.");

        // --- Training ---
        spdlog::info("Starting training for target {} iterations...", num_iterations);
        cfr_engine.train(num_iterations, num_players, initial_stack, ante_size, num_threads, save_file, checkpoint_interval, load_file);

        // --- Strategy Extraction and Display ---
        spdlog::info("--- Strategy Extraction ---");

        // Define positions for 6-max (assuming BTN=0)
        std::map<std::string, int> position_map;
        if (num_players == 6) {
            position_map = {{"UTG", 3}, {"MP", 4}, {"CO", 5}, {"BTN", 0}, {"SB", 1}}; // BB=2 cannot RFI
        } else if (num_players == 2) {
             position_map = {{"SB", 0}}; // Only SB can RFI in HU
        } else {
             spdlog::warn("RFI extraction only implemented for 6-max and HU.");
             return 0; // Exit if not 6-max or HU
        }


        auto all_hands_str = hand_generator.generate_hands();

        // Store strategies per position
        std::map<std::string, std::map<std::string, std::vector<double>>> position_strategies;

        for (const auto& pos_pair : position_map) {
            const std::string& pos_name = pos_pair.first;
            int player_index = pos_pair.second;

            spdlog::info("Extracting RFI strategy for {} (Player {})", pos_name, player_index);

            // --- Determine Correct Legal Actions for RFI in this context ---
            // Manually construct the expected actions based on ActionAbstraction logic for RFI
            std::vector<std::string> rfi_legal_actions;
            rfi_legal_actions.push_back("fold"); // Always possible

            // Calculate the specific RFI raise size based on stack depth
            double open_size_bb = 2.3; // Default for >= 40bb
            if (initial_stack < 25 * BIG_BLIND_SIZE) open_size_bb = 2.0;
            else if (initial_stack < 35 * BIG_BLIND_SIZE) open_size_bb = 2.1;
            else if (initial_stack < 40 * BIG_BLIND_SIZE) open_size_bb = 2.2;

            // Special SB sizing & Limp option
            if (pos_name == "SB") {
                 open_size_bb = 3.0; // Use 3x for SB open (adjust if needed)
                 rfi_legal_actions.push_back("call"); // Represent Limp as Call
            }

            std::string raise_action_str = create_action_string_local("raise", open_size_bb, "bb");
            rfi_legal_actions.push_back(raise_action_str);

            // Check if All-in is a distinct and valid action
            // Create a temporary state just to calculate all-in amount and check validity
            // Note: This temp state doesn't perfectly reflect the RFI context, but is used for amount calculation
            gto_solver::GameState temp_state(num_players, initial_stack, ante_size, 0); // BTN=0 assumed
            int player_stack_val = temp_state.get_player_stacks()[player_index];
            int all_in_total_amount = player_stack_val; // All-in commits the whole stack
            int raise_total_amount = action_abstraction.get_action_amount(raise_action_str, temp_state);

            if (all_in_total_amount > raise_total_amount) {
                 int current_bet = temp_state.get_bet_this_round(player_index); // Should be 0 or blind
                 int amount_to_call = temp_state.get_amount_to_call(player_index); // Should be 0 or blind diff
                 int min_legal_total_bet = current_bet + amount_to_call + std::max(1, temp_state.get_last_raise_size());
                 if (all_in_total_amount >= min_legal_total_bet) {
                      rfi_legal_actions.push_back("all_in");
                 }
            }
            // --- End Determining Legal Actions ---


            std::map<std::string, std::vector<double>> current_pos_strategies;
            for (const std::string& hand_str_internal : all_hands_str) {
                 if (hand_str_internal.length() != 4) continue;
                std::vector<gto_solver::Card> hand_vec = {hand_str_internal.substr(0, 2), hand_str_internal.substr(2, 2)};
                std::vector<gto_solver::Card> sorted_hand_for_key = hand_vec;
                std::sort(sorted_hand_for_key.begin(), sorted_hand_for_key.end());

                std::string history = ""; // RFI history is empty
                gto_solver::InfoSet infoset(sorted_hand_for_key, history);
                std::string infoset_key = infoset.get_key(player_index); // Use player index in key

                std::vector<double> strategy = cfr_engine.get_strategy(infoset_key);
                std::string canonical_hand_str = format_hand_string(hand_vec);
                current_pos_strategies[canonical_hand_str] = strategy;
            }
            position_strategies[pos_name] = current_pos_strategies;

            // Display grid for this position using the CORRECT legal actions
            display_strategy_grid(pos_name, current_pos_strategies, rfi_legal_actions);
        }


    } catch (const std::exception& e) {
        spdlog::error("Exception caught during execution: {}", e.what());
        return 1;
    } catch (...) {
        spdlog::error("Unknown exception caught during execution.");
        return 1;
    }

    spdlog::info("GTO Solver finished successfully.");
    return 0;
}
