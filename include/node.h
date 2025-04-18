#ifndef GTO_SOLVER_NODE_H
#define GTO_SOLVER_NODE_H

#include "info_set.h" // Corrected include
#include <vector>
#include <map>
#include <string>
#include <memory> // For std::unique_ptr
#include <atomic>
#include <mutex>

// Forward declare json class to avoid including the full header here (optional, but good practice)
// #include <nlohmann/json_fwd.hpp>
// using json = nlohmann::json;
// --> Actually, keep the full include for simplicity for now, as from_json/to_json might be useful later
#include <nlohmann/json.hpp>
using json = nlohmann::json;


namespace gto_solver {

// Represents a node in the game tree or the storage for CFR data per InfoSet.
// This structure will hold the regrets and the average strategy.
struct Node {
    // Regrets for not taking action 'a' at this infoset. Size = number of possible actions.
    std::vector<double> regret_sum;

    // Accumulated strategy profile. Size = number of possible actions.
    std::vector<double> strategy_sum;

    // Number of times this node/infoset has been visited (thread-safe)
    std::atomic<int> visit_count{0}; // Use atomic int, initialize to 0

    // Mutex to protect access to this specific node's data (regret_sum, strategy_sum)
    // Mutable allows locking even in const methods if needed (like get_average_strategy)
    mutable std::mutex node_mutex;

    // Constructor to initialize vectors based on the number of actions
    Node(size_t num_actions) : regret_sum(num_actions, 0.0), strategy_sum(num_actions, 0.0) {}

    // Node is non-copyable and non-movable due to the mutex member.

    // Get the current average strategy based on the accumulated strategy sum.
    // IMPORTANT: Caller must ensure node_mutex is locked before calling this in a multithreaded context.
    std::vector<double> get_average_strategy() const {
        // Assumes node_mutex is already locked by caller (e.g., in CFREngine::get_strategy)
        std::vector<double> avg_strategy(strategy_sum.size(), 0.0);
        double total_strategy_sum = 0.0;
        for (double sum : strategy_sum) {
            total_strategy_sum += sum;
        }

        if (total_strategy_sum > 0) {
            for (size_t i = 0; i < strategy_sum.size(); ++i) {
                avg_strategy[i] = strategy_sum[i] / total_strategy_sum;
            }
        } else {
            if (!strategy_sum.empty()) { // Avoid division by zero if size is 0
                double uniform_prob = 1.0 / strategy_sum.size();
                std::fill(avg_strategy.begin(), avg_strategy.end(), uniform_prob);
            }
        }
        return avg_strategy;
    }
};

// Using a map to store pointers to nodes, keyed by the InfoSet string representation.
using NodeMap = std::map<std::string, std::unique_ptr<Node>>;
// Consider: using NodeMap = std::unordered_map<std::string, std::unique_ptr<Node>>;

// Note: Global to_json/from_json for Node are removed as serialization
// will be handled manually in CFREngine due to pointer ownership and locking.

} // namespace gto_solver

#endif // GTO_SOLVER_NODE_H
