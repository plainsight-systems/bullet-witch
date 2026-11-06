# Unit tests of the materials module's public contracts: material_asset.hpp,
# one CTest entry per scenario named as the first argument. It links the
# module library, which holds the decoder.
set(materials_material_asset_scenarios
  rejections_named)
add_executable(materials_material_asset_tests material_asset_tests.cpp)
target_link_libraries(materials_material_asset_tests PRIVATE bw_test_harness bw_materials)
foreach(scenario IN LISTS materials_material_asset_scenarios)
  add_test(NAME unit.materials.material_asset.${scenario}
    COMMAND materials_material_asset_tests ${scenario})
  set_tests_properties(unit.materials.material_asset.${scenario} PROPERTIES TIMEOUT 10)
endforeach()
