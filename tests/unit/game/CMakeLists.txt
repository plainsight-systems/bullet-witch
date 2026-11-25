# Unit: the game's input tables (bw/game/input/actions.hpp), one
# table-driven scenario.
add_executable(game_input_tests game_input_tests.cpp)
target_link_libraries(game_input_tests PRIVATE bw_test_harness bw_game_input)
add_test(NAME unit.game.input.tables COMMAND game_input_tests input_tables)
set_tests_properties(unit.game.input.tables PROPERTIES TIMEOUT 10)
