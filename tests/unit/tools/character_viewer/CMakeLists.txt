# Unit: character_viewer's command line (bw/character_viewer/viewer_options.hpp),
# one table scenario over the rows this table adds to the option grammar.
add_executable(viewer_options_tests viewer_options_tests.cpp)
target_link_libraries(viewer_options_tests PRIVATE bw_test_harness bw_viewer_options)
add_test(NAME unit.character_viewer.options.rows COMMAND viewer_options_tests rows)
set_tests_properties(unit.character_viewer.options.rows PROPERTIES TIMEOUT 10)

# Unit: character_viewer's input script (bw/character_viewer/script.hpp),
# one table scenario over every command form and each error name.
add_executable(viewer_script_tests script_tests.cpp)
target_link_libraries(viewer_script_tests PRIVATE bw_test_harness bw_viewer_script)
add_test(NAME unit.character_viewer.script.parse_table COMMAND viewer_script_tests parse_table)
set_tests_properties(unit.character_viewer.script.parse_table PROPERTIES TIMEOUT 10)
