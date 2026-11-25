# Unit: the world (bw/world/world.hpp, clocks.hpp), one scenario per
# invariant.
add_executable(world_tests world_tests.cpp)
target_link_libraries(world_tests PRIVATE bw_test_harness bw_world)
foreach(scenario IN ITEMS tick_order errors clocks_and_publication)
  add_test(NAME unit.world.${scenario} COMMAND world_tests ${scenario})
  set_tests_properties(unit.world.${scenario} PROPERTIES TIMEOUT 10)
endforeach()
