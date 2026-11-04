# Unit: game_host's command line (bw/hosts/game_host_options.hpp), one
# table-driven scenario. Links the parse library only, not the host.
set(game_host_options_scenarios
  parse_table)
add_executable(game_host_options_tests game_host_options_tests.cpp)
target_link_libraries(game_host_options_tests PRIVATE bw_test_harness bw_game_host_options)
foreach(scenario IN LISTS game_host_options_scenarios)
  add_test(NAME unit.hosts.game_host_options.${scenario}
    COMMAND game_host_options_tests ${scenario})
  set_tests_properties(unit.hosts.game_host_options.${scenario} PROPERTIES TIMEOUT 10)
endforeach()
