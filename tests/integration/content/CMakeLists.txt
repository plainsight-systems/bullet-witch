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
