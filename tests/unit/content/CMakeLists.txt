# Unit tests of the content module's pure contracts: one executable per public
# header, one CTest entry per scenario named as the first argument. Each links
# the module library, which holds the out-of-line definitions.
set(content_asset_id_scenarios
  validate_table
  derivation_table
  relative_path)
set(content_cooked_asset_scenarios
  header_round_trip
  rejections_named)
set(content_layers_scenarios
  cache_record_round_trip_and_errors)
set(content_vdf_scenarios
  grammar_table
  errors_named_with_line
  find_child_ignores_case)
set(content_install_scenarios
  root_candidates_per_platform
  steam_files_parsed
  signature_table)
foreach(header IN ITEMS asset_id cooked_asset layers vdf install)
  add_executable(content_${header}_tests ${header}_tests.cpp)
  target_link_libraries(content_${header}_tests PRIVATE bw_test_harness bw_content)
  foreach(scenario IN LISTS content_${header}_scenarios)
    add_test(NAME unit.content.${header}.${scenario}
      COMMAND content_${header}_tests ${scenario})
    set_tests_properties(unit.content.${header}.${scenario} PROPERTIES TIMEOUT 10)
  endforeach()
endforeach()
