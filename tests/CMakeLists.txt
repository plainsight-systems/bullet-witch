# Tests mirror owners; private access is explicitly local to internal tests.
add_executable(engine_loop_tests engine/runtime/engine_loop_tests.cpp)
target_link_libraries(engine_loop_tests PRIVATE bw_runtime)
set(BW_SCENARIOS
  source_defaults
  lifecycle_order
  uncapped_first_delta
  first_frame_is_paced
  work_consumes_budget
  late_frame_rebases
  five_millisecond_threshold
  coarse_oversleep_is_measured
  rate_setting_changes_next_wait
  nonpositive_setting_disables_cap
  application_time_publication
  negative_time_uses_source_fallback
  exit_request_does_not_interrupt_frame
  requested_exit_before_startup
  requested_exit_during_start
  source_seconds_limit_accounting
  native_clock_and_sleep)
foreach(scenario IN LISTS BW_SCENARIOS)
  add_test(NAME ${scenario} COMMAND engine_loop_tests ${scenario})
  set_tests_properties(${scenario} PROPERTIES TIMEOUT 10)
endforeach()

add_executable(memory_tests engine/core/memory/memory_tests.cpp)
target_link_libraries(memory_tests PRIVATE bw_memory bw_macos_memory_headers Threads::Threads)
add_test(NAME memory_native_and_failure_semantics COMMAND memory_tests)
set_tests_properties(memory_native_and_failure_semantics PROPERTIES TIMEOUT 60)

foreach(allocator_case IN ITEMS allocator allocator_structure allocator_sync named_thread_dispatch crash_allocator)
  if(allocator_case STREQUAL "named_thread_dispatch")
    set(owner tasks)
  else()
    set(owner memory)
  endif()
  add_executable(${allocator_case}_tests engine/core/${owner}/${allocator_case}_tests.cpp)
  target_link_libraries(${allocator_case}_tests PRIVATE bw_allocator bw_concurrency_headers
    bw_tasks_headers bw_platform_memory_headers Threads::Threads)
  target_include_directories(${allocator_case}_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src/engine/core/memory"
    "${PROJECT_SOURCE_DIR}/src/engine/core/tasks")
  add_test(NAME ${allocator_case}_contracts COMMAND ${allocator_case}_tests)
  set_tests_properties(${allocator_case}_contracts PROPERTIES TIMEOUT 60)
endforeach()

add_executable(global_allocation_tests integration/runtime/global_allocation_tests.cpp
  ${PROJECT_SOURCE_DIR}/src/engine/core/memory/global_allocation.cpp)
target_link_libraries(global_allocation_tests PRIVATE bw_runtime bw_memory_headers)
add_executable(allocator_lifecycle_tests integration/runtime/allocator_lifecycle_tests.cpp)
target_link_libraries(allocator_lifecycle_tests PRIVATE bw_runtime bw_allocator
  bw_tasks_headers bw_concurrency_headers bw_platform_memory_headers)
target_include_directories(allocator_lifecycle_tests PRIVATE
  "${PROJECT_SOURCE_DIR}/src/engine/core/memory")
foreach(integration_case IN ITEMS global_allocation allocator_lifecycle)
  add_test(NAME ${integration_case}_contracts COMMAND ${integration_case}_tests)
  set_tests_properties(${integration_case}_contracts PROPERTIES TIMEOUT 15)
endforeach()

foreach(math_case IN ITEMS math_vector math_register math_register_composition)
  add_executable(${math_case}_tests engine/core/math/${math_case}_tests.cpp)
  target_link_libraries(${math_case}_tests PRIVATE bw_math)
  add_test(NAME ${math_case}_contracts COMMAND ${math_case}_tests)
  set_tests_properties(${math_case}_contracts PROPERTIES TIMEOUT 15)
endforeach()

# Source quaternion register algebra contracts.
add_executable(quaternion_register_tests
  engine/core/math/quaternion_register_tests.cpp)
target_link_libraries(quaternion_register_tests PRIVATE bw_math)
add_test(NAME quaternion_register_contracts COMMAND quaternion_register_tests)
set_tests_properties(quaternion_register_contracts PROPERTIES TIMEOUT 15)
