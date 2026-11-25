# Integration: the player's view composed from the world, input, the
# controller, the camera and the game's tables, driven by scripted samples,
# with no window and no game data. One table-driven scenario.
add_executable(player_view_headless_tests player_view_headless_tests.cpp)
target_link_libraries(player_view_headless_tests PRIVATE bw_test_harness bw_game_alicia
  bw_scripted_device)
add_test(NAME integration.game.player_view_headless
  COMMAND player_view_headless_tests player_view_headless)
set_tests_properties(integration.game.player_view_headless PROPERTIES TIMEOUT 30)
