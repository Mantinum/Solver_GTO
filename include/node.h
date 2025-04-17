#ifndef GTO_SOLVER_NODE_H
#define GTO_SOLVER_NODE_H

#include "info_set.h" // Corrected include
#include <vector>
#include <map>
#include <string> // Include string for map key
#include <memory> // For std::shared_ptr or std::unique_ptr if needed
#include <atomic> // Include atomic for thread-safe counters

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

    // Constructor to initialize vectors based on the number of actions
    // visit_count is default-initialized to 0 by std::atomic
    Node(size_t num_actions) : regret_sum(num_actions, 0.0), strategy_sum(num_actions, 0.0) {}

    // Get the current average strategy based on the accumulated strategy sum.
    std::vector<double> get_average_strategy() const {
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
            // Default to uniform strategy if sum is zero (e.g., at the beginning)
            double uniform_prob = 1.0 / strategy_sum.size();
            std::fill(avg_strategy.begin(), avg_strategy.end(), uniform_prob);
        }
        return avg_strategy;
    }
};

// Using a map to store nodes, keyed by the InfoSet string representation.
// Consider std::unordered_map for potentially better performance, using InfoSetHash.
using NodeMap = std::map<std::string, Node>;
// using NodeMap = std::unordered_map<InfoSet, Node, InfoSetHash>; // Alternative

} // namespace gto_solver

#endif // GTO_SOLVER_NODE_H
