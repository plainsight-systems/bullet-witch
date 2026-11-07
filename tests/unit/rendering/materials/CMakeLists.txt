# Unit tests of the materials module's public contracts, one executable per
# header (material_asset.hpp, shading.hpp, lit_template.hpp, material.hpp),
# one CTest entry per scenario named as the first argument. The material
# payloads both material_asset_tests and material_tests build come from the
# harness's bw/testing/material_payload.hpp.
set(materials_material_asset_scenarios rejections_named)
set(materials_shading_scenarios attribute_table)
set(materials_lit_template_scenarios vertex_attributes)
set(materials_material_scenarios created_from_asset mismatches_named instance_overrides)
foreach(header IN ITEMS material_asset shading lit_template material)
  add_executable(materials_${header}_tests ${header}_tests.cpp)
  target_link_libraries(materials_${header}_tests PRIVATE bw_test_harness bw_materials)
  foreach(scenario IN LISTS materials_${header}_scenarios)
    add_test(NAME unit.materials.${header}.${scenario}
      COMMAND materials_${header}_tests ${scenario})
    set_tests_properties(unit.materials.${header}.${scenario} PROPERTIES TIMEOUT 10)
  endforeach()
endforeach()
