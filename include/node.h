#ifndef GTO_SOLVER_NODE_H
#define GTO_SOLVER_NODE_H

#include "info_set.h" // Corrected include
#include <vector>
#include <map>
#include <string> // Include string for map key
#include <memory> // For std::shared_ptr or std::unique_ptr if needed
#include <atomic> // Include atomic for thread-safe counters

#include <nlohmann/json.hpp> // Include the json library header

// Alias for convenience
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

    // Constructor to initialize vectors based on the number of actions
    // visit_count is default-initialized to 0 by std::atomic
    Node(size_t num_actions) : regret_sum(num_actions, 0.0), strategy_sum(num_actions, 0.0) {}

    // --- Rule of 5/6 since we have atomic member (non-copyable) ---
    // Delete copy constructor and copy assignment operator
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    // Manually implement move constructor
    Node(Node&& other) noexcept // Add noexcept for performance/correctness
        : regret_sum(std::move(other.regret_sum)),      // Move vectors
          strategy_sum(std::move(other.strategy_sum)),
          visit_count(other.visit_count.load())         // Load value from source atomic
    {}

    // Manually implement move assignment operator
    Node& operator=(Node&& other) noexcept {
        if (this != &other) { // Protect against self-assignment
            regret_sum = std::move(other.regret_sum);
            strategy_sum = std::move(other.strategy_sum);
            visit_count.store(other.visit_count.load()); // Load from source, store to destination
        }
        return *this;
    }
    // --- End Rule of 5/6 ---


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

// --- JSON Serialization for Node ---
// Allow nlohmann/json to serialize/deserialize Node objects
inline void to_json(json& j, const Node& node) {
    j = json{
        {"regret_sum", node.regret_sum},
        {"strategy_sum", node.strategy_sum},
        {"visit_count", node.visit_count.load()} // Load atomic value for serialization
    };
}

inline void from_json(const json& j, Node& node) {
    // Need to handle potential missing keys or incorrect types if the JSON is malformed
    // Also, need to construct Node correctly if default constructor isn't suitable (it is here)
    j.at("regret_sum").get_to(node.regret_sum);
    j.at("strategy_sum").get_to(node.strategy_sum);
    // Ensure vector sizes match after loading (optional check)
    if (node.regret_sum.size() != node.strategy_sum.size()) {
        throw std::runtime_error("Mismatch in regret_sum and strategy_sum sizes during JSON deserialization");
    }
    // Load visit_count into atomic
    int visits = 0;
    j.at("visit_count").get_to(visits);
    node.visit_count.store(visits);
}
// --- End JSON Serialization ---


} // namespace gto_solver

#endif // GTO_SOLVER_NODE_H
