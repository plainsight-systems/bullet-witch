# Unit: key state and the player's input (bw/input), one scenario per
# invariant.
add_executable(input_tests input_tests.cpp)
target_link_libraries(input_tests PRIVATE bw_test_harness bw_input)
foreach(scenario IN ITEMS samples_to_actions load_errors)
  add_test(NAME unit.input.${scenario} COMMAND input_tests ${scenario})
  set_tests_properties(unit.input.${scenario} PROPERTIES TIMEOUT 10)
endforeach()
