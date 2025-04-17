#include "hand_generator.h" // Corrected include

#include <iostream> // Remove if not needed elsewhere
#include <vector>
#include <string>
#include "spdlog/spdlog.h" // Include spdlog
#include <algorithm>

namespace gto_solver {

HandGenerator::HandGenerator() {
    // std::cout << "HandGenerator created" << std::endl; // Replaced
    spdlog::debug("HandGenerator created");
}

char getRankChar(int rank) {
    if (rank < 10) {
        return '2' + rank - 2;
    } else {
        switch (rank) {
            case 10: return 'T';
            case 11: return 'J';
            case 12: return 'Q';
            case 13: return 'K';
            case 14: return 'A';
            default: return '?';
        }
    }
}

char getSuitChar(int suit) {
    switch (suit) {
        case 0: return 'c';
        case 1: return 'd';
        case 2: return 'h';
        case 3: return 's';
        default: return '?';
    }
}

std::vector<std::string> HandGenerator::generate_hands() {
    std::vector<std::string> hands;
    for (int rank1 = 2; rank1 <= 14; ++rank1) {
        for (int suit1 = 0; suit1 < 4; ++suit1) {
            for (int rank2 = rank1; rank2 <= 14; ++rank2) {
                for (int suit2 = 0; suit2 < 4; ++suit2) {
                    if (rank1 == rank2 && suit1 == suit2) continue; // Avoid duplicate cards
                    std::string hand;
                    hand += getRankChar(rank1);
                    hand += getSuitChar(suit1);
                    hand += getRankChar(rank2);
                    hand += getSuitChar(suit2);
                    // Create canonical representation: Higher rank first, then suit order if ranks equal
                    std::string card1_str = {getRankChar(rank1), getSuitChar(suit1)};
                    std::string card2_str = {getRankChar(rank2), getSuitChar(suit2)};
                    if (rank1 < rank2 || (rank1 == rank2 && suit1 < suit2)) {
                         hands.push_back(card2_str + card1_str);
                    } else {
                         hands.push_back(card1_str + card2_str);
                    }
                }
            }
        }
    }
    // Sort and remove duplicates
    std::sort(hands.begin(), hands.end());
    hands.erase(std::unique(hands.begin(), hands.end()), hands.end());
    spdlog::debug("Generated {} unique hands.", hands.size());
    return hands;
}

} // namespace gto_solver
