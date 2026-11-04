# Integration: the macOS window adapter (bw/platform/os/macos/window.hpp).
# Each scenario opens a real window, so it needs a logged-in GUI session on
# the Mac running the tests; the test process itself is the application and
# owns NSApplication on its main thread.
if(APPLE)
  set(window_macos_scenarios
    create_pump_and_pixel_size
    pumped_events_well_formed
    set_mode_to_current_mode_produces_nothing
    off_main_thread_calls_refused)
  add_executable(window_macos_tests window_macos_tests.cpp)
  target_link_libraries(window_macos_tests PRIVATE bw_test_harness bw_window)
  foreach(scenario IN LISTS window_macos_scenarios)
    add_test(NAME integration.window.macos.${scenario}
      COMMAND window_macos_tests ${scenario})
    set_tests_properties(integration.window.macos.${scenario} PROPERTIES TIMEOUT 30)
  endforeach()
endif()
