# Unit: the window event queue (bw/platform/window/window.hpp,
# WindowEventQueue), one CTest entry per invariant. Links the platform-neutral
# queue library only, not the OS adapter.
set(window_event_queue_scenarios
  pop_in_push_order
  consecutive_resizes_coalesce
  close_is_sticky
  full_queue_drops_and_reports)
add_executable(window_event_queue_tests window_event_queue_tests.cpp)
target_link_libraries(window_event_queue_tests PRIVATE bw_test_harness bw_window_events)
foreach(scenario IN LISTS window_event_queue_scenarios)
  add_test(NAME unit.window.event_queue.${scenario}
    COMMAND window_event_queue_tests ${scenario})
  set_tests_properties(unit.window.event_queue.${scenario} PROPERTIES TIMEOUT 10)
endforeach()

# Unit: the pure window decisions (window.hpp, validate_window_description,
# decide_mode_request, mode_request_error), one table-driven scenario per
# CTest entry. The functions are in the header, so the target links the
# header library only.
set(window_decisions_scenarios
  description_validation_order
  mode_request_decision)
add_executable(window_decisions_tests window_decisions_tests.cpp)
target_link_libraries(window_decisions_tests PRIVATE bw_test_harness bw_platform_window_headers)
foreach(scenario IN LISTS window_decisions_scenarios)
  add_test(NAME unit.window.decisions.${scenario}
    COMMAND window_decisions_tests ${scenario})
  set_tests_properties(unit.window.decisions.${scenario} PROPERTIES TIMEOUT 10)
endforeach()
