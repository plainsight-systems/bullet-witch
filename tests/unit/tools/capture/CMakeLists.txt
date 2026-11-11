# Unit: the capture tool's PNG writer and row packer (bw/capture/png.hpp),
# one CTest entry per scenario. Pure functions over bytes.
set(capture_png_scenarios
  encodes_known_image
  splits_stored_blocks
  packs_bgra_rows)
add_executable(capture_png_tests png_tests.cpp)
target_link_libraries(capture_png_tests PRIVATE bw_test_harness bw_capture)
foreach(scenario IN LISTS capture_png_scenarios)
  add_test(NAME unit.capture.png.${scenario} COMMAND capture_png_tests ${scenario})
  set_tests_properties(unit.capture.png.${scenario} PROPERTIES TIMEOUT 10)
endforeach()
