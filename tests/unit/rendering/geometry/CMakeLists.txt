# Unit tests of the geometry module's vertex stream contracts
# (vertex_streams.hpp) in one executable, one CTest entry per scenario named
# as the first argument.
set(geometry_vertex_streams_scenarios
  layout_errors_named_in_order
  vertex_range_overrun_named
  block_fill_and_absent_slots)
add_executable(geometry_vertex_streams_tests vertex_streams_tests.cpp)
target_link_libraries(geometry_vertex_streams_tests PRIVATE bw_test_harness bw_geometry)
foreach(scenario IN LISTS geometry_vertex_streams_scenarios)
  add_test(NAME unit.geometry.vertex_streams.${scenario}
    COMMAND geometry_vertex_streams_tests ${scenario})
  set_tests_properties(unit.geometry.vertex_streams.${scenario} PROPERTIES TIMEOUT 10)
endforeach()
# The mesh and morph target payload contracts (mesh_asset.hpp,
# morph_targets_asset.hpp) in one executable; it links the module, which
# holds the decoders.
set(geometry_mesh_asset_scenarios
  rejections_named
  cluster_tables
  morph_targets_rejections_named)
add_executable(geometry_mesh_asset_tests mesh_asset_tests.cpp)
target_link_libraries(geometry_mesh_asset_tests PRIVATE bw_test_harness bw_geometry)
foreach(scenario IN LISTS geometry_mesh_asset_scenarios)
  add_test(NAME unit.geometry.mesh_asset.${scenario}
    COMMAND geometry_mesh_asset_tests ${scenario})
  set_tests_properties(unit.geometry.mesh_asset.${scenario} PROPERTIES TIMEOUT 10)
endforeach()
