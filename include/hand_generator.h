#ifndef GTO_SOLVER_HAND_GENERATOR_H
#define GTO_SOLVER_HAND_GENERATOR_H

#include <vector>
#include <string> // <-- Added missing include

namespace gto_solver {

class HandGenerator {
public:
    HandGenerator();
    std::vector<std::string> generate_hands();
};

} // namespace gto_solver

#endif // GTO_SOLVER_HAND_GENERATOR_H
