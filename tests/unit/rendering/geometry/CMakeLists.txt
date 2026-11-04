# Unit tests of the geometry module's public contracts: vertex_streams.hpp
# and pulled_mesh_fixture.hpp in one executable, one CTest entry per scenario
# named as the first argument. It links the module because the fixture
# scenario calls make_fixture_cube, and the shader module because the
# registry scenario reads the fixture entries through shader_registry, a
# public contract of that module (permutation.hpp).
set(geometry_vertex_streams_scenarios
  layout_errors_named_in_order
  vertex_range_overrun_named
  block_fill_and_absent_slots)
set(geometry_fixture_scenarios
  cube_as_stated
  registry_entries_name_fixture_blocks)
add_executable(geometry_vertex_streams_tests vertex_streams_tests.cpp)
target_link_libraries(geometry_vertex_streams_tests PRIVATE bw_test_harness bw_geometry bw_shaders)
foreach(scenario IN LISTS geometry_vertex_streams_scenarios)
  add_test(NAME unit.geometry.vertex_streams.${scenario}
    COMMAND geometry_vertex_streams_tests ${scenario})
  set_tests_properties(unit.geometry.vertex_streams.${scenario} PROPERTIES TIMEOUT 10)
endforeach()
foreach(scenario IN LISTS geometry_fixture_scenarios)
  add_test(NAME unit.geometry.fixture.${scenario}
    COMMAND geometry_vertex_streams_tests ${scenario})
  set_tests_properties(unit.geometry.fixture.${scenario} PROPERTIES TIMEOUT 10)
endforeach()
