#ifndef GTO_SOLVER_CFR_ENGINE_H
#define GTO_SOLVER_CFR_ENGINE_H

#include "node.h" // Corrected include
#include "action_abstraction.h" // Corrected include
#include "hand_evaluator.h" // Corrected include
#include <string>
#include <vector>
#include <map> // For NodeMap
#include <thread> // For std::thread
#include <mutex>  // For std::mutex
#include <atomic> // For std::atomic

namespace gto_solver {

class GameState; // Forward declaration

class CFREngine {
public:
    CFREngine();
    // Modified train signature to accept game parameters and number of threads
    void train(int iterations, int num_players, int initial_stack, int ante_size = 0, int num_threads = 1);
    std::vector<double> get_strategy(const std::string& info_set_key);

private:
    NodeMap node_map_; // Stores regrets and strategies for each infoset
    std::mutex node_map_mutex_; // Mutex to protect access to node_map_ and Node data
    std::atomic<long long> total_nodes_created_{0}; // Track node creation thread-safely
    std::atomic<int> completed_iterations_{0}; // Track completed iterations thread-safely
    std::atomic<int> last_logged_percent_{-1}; // Track last logged percentage to avoid spam

    ActionAbstraction action_abstraction_; // To get legal actions
    HandEvaluator hand_evaluator_;       // To evaluate terminal states

    // Recursive CFR+ function - now a private member
    double cfr_plus_recursive(
        GameState current_state,
        int traversing_player,
        const std::vector<double>& reach_probabilities,
        std::vector<Card>& deck, // Pass deck by reference
        int& card_idx           // Pass next card index by reference
    );
};

} // namespace gto_solver

#endif // GTO_SOLVER_CFR_ENGINE_H
