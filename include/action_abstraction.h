#ifndef GTO_SOLVER_ACTION_ABSTRACTION_H
#define GTO_SOLVER_ACTION_ABSTRACTION_H

#include <vector>
#include <string>
#include <map> // Include map

// Forward declaration
namespace gto_solver { class GameState; }

namespace gto_solver {

// Define Action Types and Sizings more formally
enum class ActionType { FOLD, CHECK, CALL, BET, RAISE, ALL_IN };
enum class SizingUnit { BB, PCT_POT, MULTIPLIER_X, ABSOLUTE }; // ABSOLUTE for all-in amount

struct ActionSpec {
    ActionType type;
    double value = 0.0; // e.g., 3.0 for BB/X, 50 for PCT, or absolute amount for ALL_IN
    SizingUnit unit = SizingUnit::BB; // Default unit, relevant for BET/RAISE

    // Helper to convert to string (for compatibility or logging)
    std::string to_string() const;

    // Comparison operators needed for use in std::set
    bool operator==(const ActionSpec& other) const {
        return type == other.type &&
               unit == other.unit &&
               std::abs(value - other.value) < 1e-5; // Compare doubles with tolerance
    }

    bool operator<(const ActionSpec& other) const {
        if (type != other.type) return type < other.type;
        if (unit != other.unit) return unit < other.unit;
        return value < other.value; // Simple comparison for ordering
    }
};


class ActionAbstraction {
public:
    ActionAbstraction();

    // Returns a vector of possible action specifications
    std::vector<ActionSpec> get_possible_action_specs(const GameState& current_state) const;

    // Calculates the actual integer amount for a given ActionSpec and state
    int get_action_amount(const ActionSpec& action_spec, const GameState& current_state) const;

    // --- Deprecated string-based methods (keep for now for compatibility?) ---
    // std::vector<std::string> get_possible_actions(const GameState& current_state) const;
    // int get_action_amount(const std::string& action_str, const GameState& current_state) const;
    // ---

private:
    // Potential internal helper methods for specific sizings
    // void add_rfi_actions(std::set<ActionSpec>& actions_set, const GameState& state) const;
    // ... etc ...
};

} // namespace gto_solver

#endif // GTO_SOLVER_ACTION_ABSTRACTION_H
