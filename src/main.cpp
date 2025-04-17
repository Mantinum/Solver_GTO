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

// Function to display the strategy grid
void display_strategy_grid(
    const std::map<std::string, std::vector<double>>& strategies,
    const std::vector<std::string>& legal_actions)
{
    spdlog::info("--- Preflop Strategy Grid (SB vs BB - Simplified View) ---");
    std::cout << "   A    K    Q    J    T    9    8    7    6    5    4    3    2" << std::endl;
    std::cout << "--------------------------------------------------------------------" << std::endl;

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
                if (!strategy.empty() && strategy.size() == legal_actions.size()) {
                    double max_prob = -1.0;
                    size_t max_idx = -1;
                    // Find the index of the action with the highest probability
                    for(size_t k = 0; k < strategy.size(); ++k) {
                        if (strategy[k] > max_prob) {
                            max_prob = strategy[k];
                            max_idx = k;
                        }
                    }

                    if (max_idx != static_cast<size_t>(-1)) { // Check if max_idx was found
                        const std::string& action = legal_actions[max_idx];
                        // Assign character based on the dominant action
                        if (action == "fold") display_char = 'F';
                        else if (action == "call") display_char = 'C';
                        else if (action == "all_in") display_char = 'A';
                        else if (action.find("raise") != std::string::npos) display_char = 'R';
                        else display_char = '?'; // Unknown dominant action
                    } else {
                         display_char = '-'; // Strategy likely empty or all zeros
                    }
                } else if (!strategy.empty()) {
                     display_char = 'E'; // Size mismatch error
                }
            }
            // Use setw for alignment, print the character
            std::cout << std::setw(4) << std::left << display_char << " "; // Added space for better separation
        }
        std::cout << std::endl;
    }
     std::cout << "--------------------------------------------------------------------" << std::endl;
     std::cout << "Legend: R=Raise(any), C=Call, F=Fold, A=All-in, .=NotFound, E=Error, -=No Action" << std::endl;
}


// Function to parse command line arguments (simple version)
// Added num_threads parameter
void parse_args(int argc, char* argv[], int& iterations, int& num_players, int& initial_stack, int& ante_size, int& num_threads) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-i" || arg == "--iterations") && i + 1 < argc) {
            try {
                iterations = std::stoi(argv[++i]);
            } catch (const std::exception& e) {
                spdlog::warn("Invalid value for iterations: {}. Using default {}.", argv[i], iterations);
            }
        } else if ((arg == "-n" || arg == "--num_players") && i + 1 < argc) {
             try {
                num_players = std::stoi(argv[++i]);
            } catch (const std::exception& e) {
                spdlog::warn("Invalid value for num_players: {}. Using default {}.", argv[i], num_players);
            }
        } else if ((arg == "-s" || arg == "--stack") && i + 1 < argc) {
             try {
                initial_stack = std::stoi(argv[++i]);
            } catch (const std::exception& e) {
                spdlog::warn("Invalid value for initial_stack: {}. Using default {}.", argv[i], initial_stack);
            }
        } else if ((arg == "-a" || arg == "--ante") && i + 1 < argc) {
             try {
                ante_size = std::stoi(argv[++i]);
            } catch (const std::exception& e) {
                spdlog::warn("Invalid value for ante_size: {}. Using default {}.", argv[i], ante_size);
            }
        } else if ((arg == "-t" || arg == "--threads") && i + 1 < argc) {
             try {
                num_threads = std::stoi(argv[++i]);
            } catch (const std::exception& e) {
                spdlog::warn("Invalid value for num_threads: {}. Using default (hardware concurrency).", argv[i]);
                num_threads = 0; // Use 0 to signify hardware concurrency default in engine
            }
        }
         else {
            spdlog::warn("Unknown or incomplete argument: {}", arg);
        }
    }
}


int main(int argc, char* argv[]) { // Modified main signature
    // --- Default Parameters ---
    int num_iterations = 10000;
    int num_players = 2; // Default to HU
    int initial_stack = 100; // Default to 100BB
    int ante_size = 0; // Default to no ante
    int num_threads = 0; // Default to 0 (engine will use hardware_concurrency)

    // --- Setup Logging ---
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::info); // Log info and above
        // Example pattern: [%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v
        // console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

        spdlog::logger logger("gto_solver_logger", {console_sink});
        logger.set_level(spdlog::level::info); // Set logger level
        // Flush automatically on info level logs and above
        logger.flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::make_shared<spdlog::logger>(logger));
        spdlog::info("Logging initialized."); // This should now definitely flush

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        return 1;
    }

    spdlog::info("Starting GTO Solver");

    // --- Parse Arguments ---
    parse_args(argc, argv, num_iterations, num_players, initial_stack, ante_size, num_threads);
    spdlog::info("Configuration - Iterations: {}, Players: {}, Stack: {}, Ante: {}, Threads: {}",
                 num_iterations, num_players, initial_stack, ante_size, (num_threads <= 0 ? "Auto" : std::to_string(num_threads)));


    try {
        // --- Initialization ---
        spdlog::info("Initializing modules...");
        // Create instances needed outside the engine (if any)
        gto_solver::ActionAbstraction action_abstraction;
        gto_solver::HandGenerator hand_generator; // Instantiate HandGenerator
        // Create the main engine instance
        gto_solver::CFREngine cfr_engine;

        spdlog::info("Modules initialized.");

        // --- Training ---
        spdlog::info("Starting training for {} iterations...", num_iterations);
        // Call train with parsed parameters, including num_threads
        cfr_engine.train(num_iterations, num_players, initial_stack, ante_size, num_threads);
        spdlog::info("Training finished.");

        // --- Strategy Extraction and Display ---
        spdlog::info("--- Strategy Extraction ---");

        if (num_players == 2) { // Focus on HU SB (Player 0) for now
            int target_player = 0;
            int button_pos = 0; // Assuming SB is P0, BB is P1, BTN is P0
            spdlog::info("Extracting strategy for Player {} (SB) Preflop", target_player);

            // Create the initial state to get legal actions for this specific spot
            gto_solver::GameState initial_state(num_players, initial_stack, ante_size, button_pos);
            // Ensure the state reflects the SB's turn to act initially
            if (initial_state.get_current_player() != target_player) {
                 spdlog::error("Initial state logic error: Expected player {} to act first. Actual: {}", target_player, initial_state.get_current_player());
                 // Depending on GameState logic, this might need adjustment or indicate an error
                 // For now, we proceed assuming the actions are correct for the initial SB decision.
            }

            std::vector<std::string> legal_actions = action_abstraction.get_possible_actions(initial_state);
            spdlog::info("Legal actions for SB: {}", fmt::join(legal_actions, ", "));

            // Generate all starting hands (1326 combos initially, will be grouped later)
            auto all_hands_str = hand_generator.generate_hands(); // Returns vector<string> like "AsKc"

            spdlog::info("Strategy Profile (Hand: Action=Probability%):");
            // Use std::cout for potentially long output, format percentages
            std::cout << std::fixed << std::setprecision(1); // 1 decimal place for percentage

            // Use a map to store strategies for the 169 canonical hand types
            std::map<std::string, std::vector<double>> canonical_strategies;
            std::map<std::string, int> canonical_counts; // To average strategies if needed (though CFR should converge)

            for (const std::string& hand_str_internal : all_hands_str) { // e.g., "AsKc"
                 if (hand_str_internal.length() != 4) {
                     spdlog::warn("Skipping invalid hand string from generator: {}", hand_str_internal);
                     continue;
                 }
                // Convert "AsKc" to {"As", "Kc"} vector for InfoSet and formatting
                std::vector<gto_solver::Card> hand_vec = {hand_str_internal.substr(0, 2), hand_str_internal.substr(2, 2)};

                // Ensure hand is sorted alphabetically for canonical InfoSet key
                std::vector<gto_solver::Card> sorted_hand_for_key = hand_vec;
                std::sort(sorted_hand_for_key.begin(), sorted_hand_for_key.end());

                std::string history = ""; // Initial preflop node
                gto_solver::InfoSet infoset(sorted_hand_for_key, history);
                std::string infoset_key = infoset.get_key(); // e.g., "AcKs|"

                std::vector<double> strategy = cfr_engine.get_strategy(infoset_key);

                // Format hand string for display (e.g., "AKs", "T9o", "77")
                std::string canonical_hand_str = format_hand_string(hand_vec); // Use the helper

                // Store the strategy for the canonical hand type
                // If multiple specific hands map to the same canonical type (e.g. AcKc, AdKd -> AKs),
                // the last one encountered will overwrite previous ones. This is generally fine
                // as the strategy should converge to be the same for equivalent hands.
                // If averaging is desired, uncomment the count logic.
                canonical_strategies[canonical_hand_str] = strategy;
                // canonical_counts[canonical_hand_str]++; // Optional: for averaging later if needed
            }

            // Display the strategy grid
            display_strategy_grid(canonical_strategies, legal_actions);

        } else {
            spdlog::warn("Strategy grid display currently only implemented for Heads-Up (num_players=2).");
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
