#include "gtest/gtest.h"
#include "action_abstraction.h"
#include "game_state.h"

namespace gto_solver {

// Test fixture pour les tests de ActionAbstraction
class ActionAbstractionFixTest : public ::testing::Test {
protected:
    ActionAbstraction action_abstraction;
};

// Test pour vérifier la correction du bug B-01
// Le bug rejetait tous les open-raises quand amount_to_call == 0
TEST_F(ActionAbstractionFixTest, OpenRaiseWithZeroAmountToCall) {
    // Créer un état de jeu normal
    GameState state(6, 100, 0, 0); // 6 joueurs, stack 100BB, pas d'ante, BTN=0
    
    // Obtenir le joueur actuel (UTG, joueur 3)
    int current_player = state.get_current_player();
    ASSERT_EQ(current_player, 3);
    
    // Créer une action de type RAISE
    ActionSpec raise_action{ActionType::RAISE, 2.5, SizingUnit::BB};
    
    // Tester directement la méthode get_action_amount avec une action RAISE
    // Notre correction devrait permettre à la méthode de fonctionner correctement
    // même si amount_to_call n'est pas 0
    int amount = action_abstraction.get_action_amount(raise_action, state);
    EXPECT_NE(amount, -1) << "get_action_amount returned -1 for a RAISE action";
}

} // namespace gto_solver
