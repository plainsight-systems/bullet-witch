# Integration scenarios of the content converter's run. pw0_counts and the
# fixture read the player's install at BW_GAME_DATA (tests/CMakeLists.txt);
# without BW_GAME_DATA they are registered DISABLED, so ctest lists them as
# not run by name rather than passing them. The other scenarios convert a
# synthetic install they build and run on every machine. Nothing is written
# under the player's install.
#
# The fixture pw0_cache: the content_converter executable converts PW0 once
# into BW_PW0_TEST_CACHE, removed first so the conversion is a fresh one.
# The run exiting 0 is the executable's probe; every scenario that reads
# converted PW0 assets (here, integration/content, integration/hosts)
# requires the fixture instead of converting again.
add_test(NAME integration.content_converter.pw0_cache_removed
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${BW_PW0_TEST_CACHE}")
add_test(NAME integration.content_converter.pw0_cache
  COMMAND content_converter convert --install "${BW_GAME_DATA}" --out "${BW_PW0_TEST_CACHE}"
    --only model.pw0.)
set_tests_properties(integration.content_converter.pw0_cache_removed
  integration.content_converter.pw0_cache PROPERTIES FIXTURES_SETUP pw0_cache TIMEOUT 120)
set_tests_properties(integration.content_converter.pw0_cache
  PROPERTIES DEPENDS integration.content_converter.pw0_cache_removed)

# Each scenario converts into <scenario>.tmp under this directory's binary
# directory, removed before and after it runs.
add_executable(content_converter_convert_integration_tests convert_tests.cpp)
target_link_libraries(content_converter_convert_integration_tests PRIVATE bw_test_harness
  bw_conversion)
foreach(scenario IN ITEMS incremental_reconverts_dependents shared_texture_resolves
    collision_fails_second_container)
  add_test(NAME integration.content_converter.convert.${scenario}
    COMMAND content_converter_convert_integration_tests ${scenario}
      "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(integration.content_converter.convert.${scenario} PROPERTIES TIMEOUT 30)
endforeach()
add_test(NAME integration.content_converter.convert.pw0_counts
  COMMAND content_converter_convert_integration_tests pw0_counts
    "${CMAKE_CURRENT_BINARY_DIR}" "${BW_GAME_DATA}" "${BW_PW0_TEST_CACHE}")
set_tests_properties(integration.content_converter.convert.pw0_counts
  PROPERTIES FIXTURES_REQUIRED pw0_cache TIMEOUT 120)
if(BW_GAME_DATA STREQUAL "")
  set_tests_properties(integration.content_converter.convert.pw0_counts
    integration.content_converter.pw0_cache_removed integration.content_converter.pw0_cache
    PROPERTIES DISABLED TRUE)
endif()
