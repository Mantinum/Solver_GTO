cmake_minimum_required(VERSION 3.15)
project(gto_solver)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON) # Ensure C++20 is used

# --- Fetch Spdlog ---
include(FetchContent)
FetchContent_Declare(
  spdlog # Corrected back to spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git # Corrected back to spdlog repo
  GIT_TAG        v1.14.1 # Corrected back to spdlog tag
)
FetchContent_MakeAvailable(spdlog)
# --- End Fetch Spdlog ---

# --- Fetch PokerHandEvaluator ---
include(FetchContent)
FetchContent_Declare(
  phevaluator # Name for FetchContent
  GIT_REPOSITORY https://github.com/HenryRLee/PokerHandEvaluator # Corrected username
  # GIT_TAG        1.4.1 # Removed tag, fetching default branch (main)
)
FetchContent_MakeAvailable(phevaluator) # Use MakeAvailable instead of Populate
# Explicitly add the subdirectory containing the library's CMakeLists.txt
# This might not be needed if MakeAvailable handles it, but keep for safety if pheval target isn't found otherwise.
add_subdirectory(${phevaluator_SOURCE_DIR}/cpp ${phevaluator_BINARY_DIR})
# --- End Fetch PokerHandEvaluator ---

# --- Fetch nlohmann/json (for JSON serialization) ---
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3 # Use a specific stable version
)
FetchContent_MakeAvailable(nlohmann_json)
# --- End Fetch nlohmann/json ---


include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(gto_solver
        src/main.cpp
        src/game_state.cpp
        src/info_set.cpp # Added info_set.cpp
        src/hand_generator.cpp
        src/hand_evaluator.cpp
        src/action_abstraction.cpp
        src/cfr_engine.cpp
        src/monte_carlo.cpp
)
# Link gto_solver against spdlog, phevaluator, and nlohmann_json
target_link_libraries(gto_solver PRIVATE spdlog::spdlog pheval nlohmann_json::nlohmann_json)
# Explicitly add include directories for dependencies
target_include_directories(gto_solver PRIVATE
    ${phevaluator_SOURCE_DIR}/cpp/include
    ${nlohmann_json_SOURCE_DIR}/include # Add include dir for nlohmann/json
)

install(TARGETS gto_solver DESTINATION bin)

enable_testing()

# --- Fetch GoogleTest ---
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0 # Corrected tag (v instead of release-)
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest) # Add this back - needed for gtest_discover_tests
# --- End Fetch GoogleTest ---


add_executable(hand_generator_test test/hand_generator_test.cpp src/hand_generator.cpp)
# Link with gtest & gtest_main (provides main function for tests)
target_link_libraries(hand_generator_test GTest::gtest GTest::gtest_main spdlog::spdlog) # Added spdlog link
# include(GoogleTest) # Already included above
gtest_discover_tests(hand_generator_test)


add_executable(hand_evaluator_test test/hand_evaluator_test.cpp src/hand_evaluator.cpp)
# Link hand_evaluator_test against gtest, spdlog and the main phevaluator target
target_link_libraries(hand_evaluator_test PRIVATE GTest::gtest GTest::gtest_main spdlog::spdlog pheval)
# Explicitly add phevaluator include directory
target_include_directories(hand_evaluator_test PRIVATE ${phevaluator_SOURCE_DIR}/cpp/include)
gtest_discover_tests(hand_evaluator_test)


add_executable(action_abstraction_test
        test/action_abstraction_test.cpp
        src/action_abstraction.cpp
        src/game_state.cpp # Added dependency
)
target_link_libraries(action_abstraction_test GTest::gtest GTest::gtest_main spdlog::spdlog) # Added spdlog link
gtest_discover_tests(action_abstraction_test)


add_executable(game_state_test test/game_state_test.cpp src/game_state.cpp)
target_link_libraries(game_state_test GTest::gtest GTest::gtest_main spdlog::spdlog) # Added spdlog link
gtest_discover_tests(game_state_test)


add_executable(cfr_engine_test
        test/cfr_engine_test.cpp
        src/cfr_engine.cpp
        src/game_state.cpp
        src/info_set.cpp
        src/action_abstraction.cpp
        src/hand_evaluator.cpp
        # monte_carlo not needed for this basic test
)
# Link cfr_engine_test against gtest, spdlog, phevaluator, and nlohmann_json
target_link_libraries(cfr_engine_test PRIVATE GTest::gtest GTest::gtest_main spdlog::spdlog pheval nlohmann_json::nlohmann_json)
# Explicitly add include directories
target_include_directories(cfr_engine_test PRIVATE
    ${phevaluator_SOURCE_DIR}/cpp/include
    ${nlohmann_json_SOURCE_DIR}/include # Add include dir for nlohmann/json
)
gtest_discover_tests(cfr_engine_test)


add_executable(action_abstraction_fix_test
        test/action_abstraction_fix_test.cpp
        src/action_abstraction.cpp
        src/game_state.cpp
)
target_link_libraries(action_abstraction_fix_test GTest::gtest GTest::gtest_main spdlog::spdlog)
gtest_discover_tests(action_abstraction_fix_test)
