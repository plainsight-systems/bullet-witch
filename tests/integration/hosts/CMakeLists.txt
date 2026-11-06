# Integration scenarios of the host executables, each run as a child process
# with every path passed from here; nothing is discovered from the
# environment. The child's stdout and stderr go to files in this directory's
# binary directory, one pair per scenario.
#
# game_host opens a real window and draws on this Mac's Metal device, so the
# run and the missing-artifact probe need a logged-in GUI session, Apple
# hosts and the shader artifacts the build produced (tests/CMakeLists.txt
# adds this directory only with APPLE and BW_SHADER_TOOLCHAIN). The
# missing-artifact probe is given a directory under the build tree that the
# build never creates.
#
# draws_asset_one_second reads the PW0 cache of the fixture pw0_cache
# (integration/tools/content_converter), converted from the player's install
# at BW_GAME_DATA (tests/CMakeLists.txt); without BW_GAME_DATA it is
# registered DISABLED, so ctest lists it as not run by name rather than
# passing it.
add_executable(game_host_tests game_host_tests.cpp)
target_link_libraries(game_host_tests PRIVATE bw_test_harness bw_contracts)
add_dependencies(game_host_tests game_host shader_artifacts)
add_test(NAME integration.hosts.game_host_runs_one_second
  COMMAND game_host_tests game_host_runs_one_second "$<TARGET_FILE:game_host>"
    "${CMAKE_BINARY_DIR}/shaders/metal" "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(integration.hosts.game_host_runs_one_second PROPERTIES TIMEOUT 60)
add_test(NAME integration.hosts.game_host_unknown_option_exits_2
  COMMAND game_host_tests game_host_unknown_option_exits_2 "$<TARGET_FILE:game_host>"
    "${CMAKE_BINARY_DIR}/shaders/metal" "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(integration.hosts.game_host_unknown_option_exits_2 PROPERTIES TIMEOUT 30)
add_test(NAME integration.hosts.game_host_missing_shaders_exits_1
  COMMAND game_host_tests game_host_missing_shaders_exits_1 "$<TARGET_FILE:game_host>"
    "${CMAKE_BINARY_DIR}/shaders/no-such-artifact-directory" "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(integration.hosts.game_host_missing_shaders_exits_1 PROPERTIES TIMEOUT 60)
add_test(NAME integration.hosts.draws_asset_one_second
  COMMAND game_host_tests draws_asset_one_second "$<TARGET_FILE:game_host>"
    "${CMAKE_BINARY_DIR}/shaders/metal" "${CMAKE_CURRENT_BINARY_DIR}" "${BW_PW0_TEST_CACHE}")
set_tests_properties(integration.hosts.draws_asset_one_second
  PROPERTIES TIMEOUT 60 FIXTURES_REQUIRED pw0_cache)
if(BW_GAME_DATA STREQUAL "")
  set_tests_properties(integration.hosts.draws_asset_one_second PROPERTIES DISABLED TRUE)
endif()
