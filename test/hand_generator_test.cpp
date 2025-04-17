#include "gtest/gtest.h"
#include "hand_generator.h" // Corrected include

#include <vector>
#include <string>
#include <algorithm>

namespace gto_solver {

TEST(HandGeneratorTest, GenerateAllHands) {
    HandGenerator hand_generator;
    std::vector<std::string> hands = hand_generator.generate_hands();
    ASSERT_EQ(hands.size(), 1326);

    // Check for duplicates (very inefficient, but good enough for a test)
    for (size_t i = 0; i < hands.size(); ++i) {
        for (size_t j = i + 1; j < hands.size(); ++j) {
            ASSERT_NE(hands[i], hands[j]);
        }
    }
}

} // namespace gto_solver
