# Unit tests of the content converter's pure library: one executable per
# public header, one CTest entry per scenario named as the first argument.
# Containers are built in memory from the headers' stated framing
# (cpr_fixture.hpp beside the tests); no file, no install. Each links the
# converter library; the texture test also links the vendored block
# decoders, against which it checks the encoder.
set(content_converter_asset_data_scenarios
  round_trips)
set(content_converter_cpr_container_scenarios
  synthetic_container
  errors_named
  qualified_name_lookup)
set(content_converter_rat_index_scenarios
  synthetic_catalog
  errors_named)
set(content_converter_mesh_reader_scenarios
  synthetic_model
  errors_named)
set(content_converter_skeleton_reader_scenarios
  synthetic_pair_and_errors)
set(content_converter_texture_reader_scenarios
  descriptor_fields
  plan_table
  half_texels
  mip_chain
  blocks_decode_back
  png_decoded_and_errors)
set(content_converter_material_mapping_scenarios
  table_from_call
  slot_resolution
  mapping_rows)
foreach(header IN ITEMS asset_data cpr_container rat_index mesh_reader skeleton_reader
    texture_reader material_mapping)
  add_executable(content_converter_${header}_tests ${header}_tests.cpp)
  target_link_libraries(content_converter_${header}_tests PRIVATE bw_test_harness
    bw_content_converter)
  foreach(scenario IN LISTS content_converter_${header}_scenarios)
    add_test(NAME unit.content_converter.${header}.${scenario}
      COMMAND content_converter_${header}_tests ${scenario})
    set_tests_properties(unit.content_converter.${header}.${scenario} PROPERTIES TIMEOUT 30)
  endforeach()
endforeach()
target_link_libraries(content_converter_texture_reader_tests PRIVATE bw_bc7enc)
# The conversion driver's pure parts: the command line, the plan, the report
# text and the input hash (conversion.hpp); nothing touches a file.
set(content_converter_conversion_scenarios
  parse_arguments
  plan_kinds_and_order
  report_text
  input_hash_order)
add_executable(content_converter_conversion_tests conversion_tests.cpp)
target_link_libraries(content_converter_conversion_tests PRIVATE bw_test_harness bw_conversion)
foreach(scenario IN LISTS content_converter_conversion_scenarios)
  add_test(NAME unit.content_converter.conversion.${scenario}
    COMMAND content_converter_conversion_tests ${scenario})
  set_tests_properties(unit.content_converter.conversion.${scenario} PROPERTIES TIMEOUT 30)
endforeach()
