# Integration scenarios of the renderer's material and model draw paths on
# this Mac's Metal device (Apple hosts with the shader toolchain, added by
# tests/CMakeLists.txt). material_buffer_tests needs the device only;
# model_draw_tests also reads the build's forward lit artifacts and writes
# its synthetic converted layers under this directory's binary directory.
# pw0_draws_with_materials reads the PW0 cache of the fixture pw0_cache
# (integration/tools/content_converter); without BW_GAME_DATA it is
# registered DISABLED.
add_executable(rendering_material_buffer_tests material_buffer_tests.cpp)
target_link_libraries(rendering_material_buffer_tests PRIVATE bw_test_harness bw_material_buffer)
add_test(NAME integration.rendering.materials.material_buffer_ranges
  COMMAND rendering_material_buffer_tests material_buffer_ranges)
set_tests_properties(integration.rendering.materials.material_buffer_ranges PROPERTIES TIMEOUT 10)

add_executable(rendering_model_draw_tests model_draw_tests.cpp)
target_link_libraries(rendering_model_draw_tests PRIVATE bw_test_harness bw_model
  bw_content_converter bw_graph)
add_dependencies(rendering_model_draw_tests shader_artifacts)
set(model_draw_shaders "${CMAKE_BINARY_DIR}/shaders/metal")
foreach(scenario IN ITEMS forward_lit_pixels failures_named)
  add_test(NAME integration.rendering.model_draw.${scenario}
    COMMAND rendering_model_draw_tests ${scenario} "${model_draw_shaders}"
      "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(integration.rendering.model_draw.${scenario} PROPERTIES TIMEOUT 10)
endforeach()
add_test(NAME integration.rendering.model_draw.pw0_draws_with_materials
  COMMAND rendering_model_draw_tests pw0_draws_with_materials "${model_draw_shaders}"
    "${CMAKE_CURRENT_BINARY_DIR}" "${BW_PW0_TEST_CACHE}")
set_tests_properties(integration.rendering.model_draw.pw0_draws_with_materials
  PROPERTIES TIMEOUT 60 FIXTURES_REQUIRED pw0_cache)
if(BW_GAME_DATA STREQUAL "")
  set_tests_properties(integration.rendering.model_draw.pw0_draws_with_materials
    PROPERTIES DISABLED TRUE)
endif()

# The scene's culling dispatch and depth hierarchy on the Metal device with
# the build's compute artifacts (bw/rendering/scene/culling.hpp,
# depth_hierarchy.hpp): synthetic instances and depth targets, nothing read
# from game data.
add_executable(rendering_scene_culling_tests scene/culling_tests.cpp)
target_link_libraries(rendering_scene_culling_tests PRIVATE bw_test_harness bw_scene)
add_dependencies(rendering_scene_culling_tests shader_artifacts)
foreach(scenario IN ITEMS records_match_cpu occlusion_two_phase)
  add_test(NAME integration.scene.culling.${scenario}
    COMMAND rendering_scene_culling_tests ${scenario} "${CMAKE_BINARY_DIR}/shaders/metal")
  set_tests_properties(integration.scene.culling.${scenario} PROPERTIES TIMEOUT 10)
endforeach()
add_executable(rendering_scene_depth_hierarchy_tests scene/depth_hierarchy_tests.cpp)
target_link_libraries(rendering_scene_depth_hierarchy_tests PRIVATE bw_test_harness bw_scene)
add_dependencies(rendering_scene_depth_hierarchy_tests shader_artifacts)
add_test(NAME integration.scene.depth_hierarchy.furthest_depth
  COMMAND rendering_scene_depth_hierarchy_tests furthest_depth "${CMAKE_BINARY_DIR}/shaders/metal")
set_tests_properties(integration.scene.depth_hierarchy.furthest_depth PROPERTIES TIMEOUT 10)
