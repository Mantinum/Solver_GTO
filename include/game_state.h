#ifndef GTO_SOLVER_GAME_STATE_H
#define GTO_SOLVER_GAME_STATE_H

#include <vector>
#include <string>
#include <cstdint> // For uint8_t

namespace gto_solver {

// Represents a playing card (e.g., 'Ah', 'Td', '2c')
// For simplicity, using string for now. Could be optimized later.
using Card = std::string;

// Represents the different betting rounds
enum class Street : uint8_t {
    PREFLOP,
    FLOP,
    TURN,
    RIVER,
    SHOWDOWN
};

// Represents a player action
struct Action {
    enum class Type : uint8_t { FOLD, CALL, RAISE, CHECK, BET }; // Added CHECK, BET
    Type type;
    int amount = 0; // Amount for RAISE or BET
    int player_index = -1; // Index of the player who took the action
};

// Represents the state of the game at a specific point
class GameState {
public:
    // Constructor now includes ante size and button position
    // Button position defaults to 0 for HU, otherwise should be specified.
    GameState(int num_players = 2, int initial_stack = 100, int ante_size = 0, int button_position = 0);

    // --- Getters ---
    int get_num_players() const;
    int get_button_position() const; // Added getter for button position
    int get_current_player() const;
    int get_pot_size() const;
    const std::vector<int>& get_player_stacks() const;
    const std::vector<Card>& get_player_hand(int player_index) const; // Get specific player's hand
    const std::vector<Card>& get_community_cards() const;
    Street get_current_street() const;
    const std::vector<Action>& get_action_history() const;
    bool is_terminal() const; // Check if the game state is terminal (end of hand)
    int get_amount_to_call(int player_index) const; // Amount needed for the current player to call
    int get_bet_this_round(int player_index) const; // Get amount bet by player in current round
    const std::vector<int>& get_bets_this_round() const; // Get vector of all bets this round
    int get_last_raise_size() const; // Get the size of the last bet/raise increment
    bool has_player_folded(int player_index) const;
    bool is_player_all_in(int player_index) const;
    int get_player_contribution(int player_index) const; // Get total amount contributed by player to the pot

    // --- Modifiers ---
    void deal_hands(const std::vector<std::vector<Card>>& hands);
    void deal_community_cards(const std::vector<Card>& cards);
    void apply_action(const Action& action); // Apply an action and update the state
    void advance_to_next_street(); // Move to the next betting round

    // --- Utility ---
    std::string get_history_string() const; // Get a string representation of the action history
    int get_effective_stack(int player_index) const; // Smallest stack among active players including player_index
    int get_raises_this_street() const; // Count number of raises in the current street's history
    bool is_first_to_act_preflop(int player_index) const; // Check if player is first voluntary actor preflop
    int get_num_limpers() const; // Count number of limpers preflop before current player

private:
    int num_players_;
    int current_player_index_; // Index of the player whose turn it is
    int pot_size_;
    std::vector<int> player_stacks_;
    std::vector<int> bets_this_round_; // Track bets in the current round per player
    std::vector<std::vector<Card>> player_hands_; // Hands for each player
    std::vector<Card> community_cards_;
    Street current_street_;
    std::vector<Action> action_history_;
    bool is_game_over_; // Flag indicating if the hand has ended
    int last_raise_size_; // Size of the last bet/raise increment in the current street
    int aggressor_this_round_; // Index of last player to bet/raise this street (-1 if none)
    int actions_this_round_;   // Number of actions taken since last aggression or start of street
    std::vector<bool> player_folded_; // Tracks if a player has folded
    std::vector<bool> player_all_in_; // Tracks if a player is all-in
    std::vector<int> player_contributions_; // Total contributed by each player this hand
    int ante_size_; // Size of the ante
    int button_position_; // Index of the player on the button
    std::vector<bool> player_acted_this_sequence_; // Tracks if player acted since last aggression/start of street

    // Helper methods
    // Now takes SB/BB indices and amounts
    void post_antes_and_blinds(int sb_index, int bb_index, int sb_amount, int bb_amount);
    void update_next_player();
    void reset_bets_for_new_street();
};

} // namespace gto_solver

#endif // GTO_SOLVER_GAME_STATE_H
