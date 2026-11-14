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

# Unit tests of the scene's pure parts, one executable per public header
# (instance_set.hpp, frame_views.hpp, detail_level.hpp, cpu_order.hpp,
# scene_geometry.hpp, draw_records.hpp), one CTest entry per scenario. No
# device; each links the scene module.
set(scene_instance_set_scenarios ids_and_previous_transform)
set(scene_frame_views_scenarios planes_and_previous)
set(scene_detail_level_scenarios choice_and_dither)
set(scene_cpu_order_scenarios key_and_frustum)
set(scene_scene_geometry_scenarios rebases_tables)
set(scene_draw_records_scenarios layout_and_table)
foreach(header IN ITEMS instance_set frame_views detail_level cpu_order scene_geometry
    draw_records)
  add_executable(scene_${header}_tests scene/${header}_tests.cpp)
  target_link_libraries(scene_${header}_tests PRIVATE bw_test_harness bw_scene)
  foreach(scenario IN LISTS scene_${header}_scenarios)
    add_test(NAME unit.scene.${header}.${scenario} COMMAND scene_${header}_tests ${scenario})
    set_tests_properties(unit.scene.${header}.${scenario} PROPERTIES TIMEOUT 10)
  endforeach()
endforeach()

# Unit tests of post-processing's pure parts, one executable per public
# header with pure functions (postprocess/exposure.hpp; postprocess/
# grade_bake.hpp, whose executable also holds the grade's validation and the
# vignette of tone_mapping.hpp), one CTest entry per scenario. No device;
# each links the postprocess module.
set(postprocess_exposure_scenarios camera_relation meter_buckets windowed_average adaptation)
set(postprocess_grade_bake_scenarios shaper white_balance fixed_steps correction curve
  encode_and_whole grade_rules)
add_executable(postprocess_exposure_tests postprocess/exposure_tests.cpp)
add_executable(postprocess_grade_bake_tests postprocess/grade_bake_tests.cpp)
foreach(header IN ITEMS exposure grade_bake)
  target_link_libraries(postprocess_${header}_tests PRIVATE bw_test_harness bw_postprocess)
  foreach(scenario IN LISTS postprocess_${header}_scenarios)
    add_test(NAME unit.rendering.postprocess.${header}.${scenario}
      COMMAND postprocess_${header}_tests ${scenario})
    set_tests_properties(unit.rendering.postprocess.${header}.${scenario} PROPERTIES TIMEOUT 10)
  endforeach()
endforeach()
add_test(NAME unit.rendering.postprocess.tone_mapping.vignette
  COMMAND postprocess_grade_bake_tests vignette)
set_tests_properties(unit.rendering.postprocess.tone_mapping.vignette PROPERTIES TIMEOUT 10)
