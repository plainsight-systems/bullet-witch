# Unit tests of the rendering root's public contracts: texture_asset.hpp, one
# CTest entry per scenario named as the first argument. It links the texture
# asset library, which holds the decoder. Rendering's subsystems have their
# own test directories, added by tests/CMakeLists.txt.
set(rendering_texture_asset_scenarios
  mip_arithmetic
  rejections_named)
add_executable(rendering_texture_asset_tests texture_asset_tests.cpp)
target_link_libraries(rendering_texture_asset_tests PRIVATE bw_test_harness bw_texture_asset)
foreach(scenario IN LISTS rendering_texture_asset_scenarios)
  add_test(NAME unit.rendering.texture_asset.${scenario}
    COMMAND rendering_texture_asset_tests ${scenario})
  set_tests_properties(unit.rendering.texture_asset.${scenario} PROPERTIES TIMEOUT 10)
endforeach()

# Unit tests of the render graph's public contracts (graph/access.hpp,
# declaration.hpp, builder.hpp, compiled_frame.hpp, schedule_policy.hpp), one
# CTest entry per scenario. No device: imported resources are local RHI
# objects. It links the graph module.
set(rendering_graph_scenarios
  culls_unread_passes
  lifetimes_first_to_last
  transitions_merge_reads
  actions_from_use
  errors_named
  imported_enters_and_leaves
  metal_policy_merges_shared_attachments
  access.rhi_access_table)
add_executable(rendering_graph_tests graph/graph_tests.cpp)
target_link_libraries(rendering_graph_tests PRIVATE bw_test_harness bw_graph)
foreach(scenario IN LISTS rendering_graph_scenarios)
  add_test(NAME unit.rendering.graph.${scenario} COMMAND rendering_graph_tests ${scenario})
  set_tests_properties(unit.rendering.graph.${scenario} PROPERTIES TIMEOUT 10)
endforeach()

# Unit tests of the orbit camera (scene/orbit.hpp), one CTest entry per
# scenario. Pure functions over values; it links the scene module.
set(scene_orbit_scenarios
  reset_matches_frame_sphere
  turn_and_dolly_clamp)
add_executable(scene_orbit_tests scene/orbit_tests.cpp)
target_link_libraries(scene_orbit_tests PRIVATE bw_test_harness bw_scene)
foreach(scenario IN LISTS scene_orbit_scenarios)
  add_test(NAME unit.scene.orbit.${scenario} COMMAND scene_orbit_tests ${scenario})
  set_tests_properties(unit.scene.orbit.${scenario} PROPERTIES TIMEOUT 10)
endforeach()
