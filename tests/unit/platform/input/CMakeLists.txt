# Unit: the scripted device's parse (bw/platform/input/scripted_device.hpp),
# one table-driven scenario. Links the standard-library-only parse library.
add_executable(scripted_device_tests scripted_device_tests.cpp)
target_link_libraries(scripted_device_tests PRIVATE bw_test_harness bw_scripted_device)
add_test(NAME unit.platform.input.scripted_device.parse_table
  COMMAND scripted_device_tests parse_table)
set_tests_properties(unit.platform.input.scripted_device.parse_table PROPERTIES TIMEOUT 10)
