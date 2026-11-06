# Integration scenarios of the content module's file system paths. Every path
# comes from here: the loader scenarios' temporary trees live under this
# directory's binary directory, each in a subdirectory named for the scenario
# that the scenario removes before and after it runs; nothing is read from
# the environment.
#
# explicit_install_located reads the player's install at BW_GAME_DATA
# (tests/CMakeLists.txt). Without it the scenario is registered DISABLED, so
# ctest lists it as not run by name rather than passing it.
add_executable(content_loader_integration_tests loader_tests.cpp)
target_link_libraries(content_loader_integration_tests PRIVATE bw_test_harness bw_content)
foreach(scenario IN ITEMS override_wins_and_missing_named failures_named)
  add_test(NAME integration.content.loader.${scenario}
    COMMAND content_loader_integration_tests ${scenario} "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(integration.content.loader.${scenario} PROPERTIES TIMEOUT 30)
endforeach()

add_executable(content_install_integration_tests install_tests.cpp)
target_link_libraries(content_install_integration_tests PRIVATE bw_test_harness bw_content)
add_test(NAME integration.content.install.explicit_install_located
  COMMAND content_install_integration_tests explicit_install_located "${BW_GAME_DATA}")
set_tests_properties(integration.content.install.explicit_install_located PROPERTIES TIMEOUT 60)
if(BW_GAME_DATA STREQUAL "")
  set_tests_properties(integration.content.install.explicit_install_located
    PROPERTIES DISABLED TRUE)
endif()

# pw0_offscreen_not_clear loads a converted model and draws it with the
# fixture draw on this Mac's Metal device: it needs the Metal backend and the shader
# artifacts the build produced (Apple hosts with the shader toolchain), and
# reads the PW0 cache of the fixture pw0_cache
# (integration/tools/content_converter). Without BW_GAME_DATA it is
# registered DISABLED.
if(APPLE AND BW_SHADER_TOOLCHAIN)
  add_executable(content_textured_draw_integration_tests textured_draw_tests.cpp)
  target_link_libraries(content_textured_draw_integration_tests PRIVATE bw_test_harness
    bw_model bw_scene)
  add_dependencies(content_textured_draw_integration_tests shader_artifacts)
  add_test(NAME integration.content.textured_draw.pw0_offscreen_not_clear
    COMMAND content_textured_draw_integration_tests pw0_offscreen_not_clear
      "${BW_PW0_TEST_CACHE}" "${CMAKE_BINARY_DIR}/shaders/metal")
  set_tests_properties(integration.content.textured_draw.pw0_offscreen_not_clear
    PROPERTIES TIMEOUT 60 FIXTURES_REQUIRED pw0_cache)
  if(BW_GAME_DATA STREQUAL "")
    set_tests_properties(integration.content.textured_draw.pw0_offscreen_not_clear
      PROPERTIES DISABLED TRUE)
  endif()
endif()
