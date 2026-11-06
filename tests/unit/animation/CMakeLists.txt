# Unit tests of the animation module's public contracts: skeleton_asset.hpp,
# one CTest entry per scenario named as the first argument. It links the
# module library, which holds the decoder.
set(animation_skeleton_asset_scenarios
  rejections_named)
add_executable(animation_skeleton_asset_tests skeleton_asset_tests.cpp)
target_link_libraries(animation_skeleton_asset_tests PRIVATE bw_test_harness bw_animation)
foreach(scenario IN LISTS animation_skeleton_asset_scenarios)
  add_test(NAME unit.animation.skeleton_asset.${scenario}
    COMMAND animation_skeleton_asset_tests ${scenario})
  set_tests_properties(unit.animation.skeleton_asset.${scenario} PROPERTIES TIMEOUT 10)
endforeach()
