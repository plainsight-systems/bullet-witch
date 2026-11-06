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
