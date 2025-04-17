#ifndef GTO_SOLVER_ACTION_ABSTRACTION_H
#define GTO_SOLVER_ACTION_ABSTRACTION_H

#include <vector>
#include <string>
#include <vector> // Ensure vector is included

namespace gto_solver {

class GameState; // Forward declaration

class ActionAbstraction {
public:
    // TODO: Allow configuration of bet sizes (e.g., fractions of pot, fixed amounts)
    ActionAbstraction();

    // Returns specific action strings like "fold", "call", "check", "raise_3bb", "raise_half_pot", "all_in"
    std::vector<std::string> get_possible_actions(const GameState& current_state) const;

    // Helper to calculate the bet/raise amount for a given action string and state
    // Returns -1 if the action string doesn't imply an amount (fold, call, check)
    int get_action_amount(const std::string& action_str, const GameState& current_state) const;

private:
    // Define abstract bet/raise sizes as fractions of the pot
    const double BET_FRACTION_SMALL = 0.33;
    const double BET_FRACTION_MEDIUM = 0.50; // Keep existing one
    const double BET_FRACTION_LARGE = 0.75;
    // const double BET_FRACTION_OVERBET = 1.25; // Example for later

    // Define preflop open raise sizes in big blinds
    // We'll generate strings like "raise_2.2bb", "raise_2.5bb", "raise_3bb"
    // No specific constants needed here if handled in .cpp
    // TODO: Add constants or logic for 3-bet, 4-bet sizing etc.
};

} // namespace gto_solver

#endif // GTO_SOLVER_ACTION_ABSTRACTION_H
