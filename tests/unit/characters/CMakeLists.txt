# Unit: the player's controller (bw/characters/player_controller.hpp), one
# table-driven scenario.
add_executable(player_controller_tests player_controller_tests.cpp)
target_link_libraries(player_controller_tests PRIVATE bw_test_harness bw_characters)
add_test(NAME unit.characters.player_controller.table
  COMMAND player_controller_tests player_controller_table)
set_tests_properties(unit.characters.player_controller.table PROPERTIES TIMEOUT 10)
