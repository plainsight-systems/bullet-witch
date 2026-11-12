# Integration scenarios of the character_viewer executable, run as a child
# process with every path passed from here; nothing is discovered from the
# environment. The child's stdout and stderr go to files in this directory's
# binary directory, one pair per scenario, beside the dumped file.
#
# dumps_first_frame and scripted_input_dumps open a real window and draw on
# this Mac's Metal device from the PW0 cache of the fixture pw0_cache
# (integration/tools/content_converter), converted from the player's
# install at BW_GAME_DATA; they need a logged-in GUI session, Apple hosts and
# the build's shader artifacts (tests/CMakeLists.txt adds this directory
# only with APPLE and BW_SHADER_TOOLCHAIN). Without BW_GAME_DATA they are
# registered DISABLED. The usage probe opens no window.
add_executable(character_viewer_tests character_viewer_tests.cpp)
# The orbit constants and the script's frame time give the camera the
# scripted run's dump lines are checked against.
target_link_libraries(character_viewer_tests PRIVATE bw_test_harness bw_contracts
  bw_scene_headers bw_character_viewer_headers)
add_dependencies(character_viewer_tests character_viewer shader_artifacts)
add_test(NAME integration.tools.character_viewer.dumps_first_frame
  COMMAND character_viewer_tests dumps_first_frame "$<TARGET_FILE:character_viewer>"
    "${CMAKE_BINARY_DIR}/shaders/metal" "${CMAKE_CURRENT_BINARY_DIR}" "${BW_PW0_TEST_CACHE}")
set_tests_properties(integration.tools.character_viewer.dumps_first_frame
  PROPERTIES TIMEOUT 60 FIXTURES_REQUIRED pw0_cache)
if(BW_GAME_DATA STREQUAL "")
  set_tests_properties(integration.tools.character_viewer.dumps_first_frame
    PROPERTIES DISABLED TRUE)
endif()
add_test(NAME integration.tools.character_viewer.scripted_input_dumps
  COMMAND character_viewer_tests scripted_input_dumps "$<TARGET_FILE:character_viewer>"
    "${CMAKE_BINARY_DIR}/shaders/metal" "${CMAKE_CURRENT_BINARY_DIR}" "${BW_PW0_TEST_CACHE}")
set_tests_properties(integration.tools.character_viewer.scripted_input_dumps
  PROPERTIES TIMEOUT 60 FIXTURES_REQUIRED pw0_cache)
if(BW_GAME_DATA STREQUAL "")
  set_tests_properties(integration.tools.character_viewer.scripted_input_dumps
    PROPERTIES DISABLED TRUE)
endif()
add_test(NAME integration.tools.character_viewer.dump_without_asset_exits_2
  COMMAND character_viewer_tests dump_without_asset_exits_2 "$<TARGET_FILE:character_viewer>"
    "${CMAKE_BINARY_DIR}/shaders/metal" "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(integration.tools.character_viewer.dump_without_asset_exits_2
  PROPERTIES TIMEOUT 30)
# image_unchanged compares the PW0 frame at 320 by 180 points with a
# reference frame this path dumped at an earlier run (model_scene.hpp,
# contract 2a, its last sentence); the reference's path is BW_SCENE_REFERENCE_FRAME (tests/CMakeLists.txt),
# given on the configure command line, and the scenario is registered
# DISABLED without it or without BW_GAME_DATA.
add_test(NAME integration.tools.character_viewer.image_unchanged
  COMMAND character_viewer_tests image_unchanged "$<TARGET_FILE:character_viewer>"
    "${CMAKE_BINARY_DIR}/shaders/metal" "${CMAKE_CURRENT_BINARY_DIR}" "${BW_PW0_TEST_CACHE}"
    "${BW_SCENE_REFERENCE_FRAME}")
set_tests_properties(integration.tools.character_viewer.image_unchanged
  PROPERTIES TIMEOUT 60 FIXTURES_REQUIRED pw0_cache)
if(BW_GAME_DATA STREQUAL "" OR BW_SCENE_REFERENCE_FRAME STREQUAL "")
  set_tests_properties(integration.tools.character_viewer.image_unchanged
    PROPERTIES DISABLED TRUE)
endif()
