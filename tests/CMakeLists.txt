# Tests assert public contracts only. Unit tests need nothing beyond the code
# under test; integration tests may use the OS, threads or another module.
# No test target receives a private include directory.

# Unit: math.
foreach(math_case IN ITEMS math_vector math_register quaternion_register quaternion)
  add_executable(${math_case}_tests unit/math/${math_case}_tests.cpp)
  target_link_libraries(${math_case}_tests PRIVATE bw_math)
  add_test(NAME unit.${math_case} COMMAND ${math_case}_tests)
  set_tests_properties(unit.${math_case} PROPERTIES TIMEOUT 15)
endforeach()

# Unit: memory policy and alignment arithmetic (header only).
foreach(memory_case IN ITEMS alignment new_request)
  add_executable(${memory_case}_tests unit/memory/${memory_case}_tests.cpp)
  target_link_libraries(${memory_case}_tests PRIVATE bw_memory_headers)
  add_test(NAME unit.${memory_case} COMMAND ${memory_case}_tests)
  set_tests_properties(unit.${memory_case} PROPERTIES TIMEOUT 15)
endforeach()

# Unit: virtual memory block descriptor, no mapping made.
add_executable(virtual_memory_block_tests unit/platform/virtual_memory_block_tests.cpp)
target_link_libraries(virtual_memory_block_tests PRIVATE bw_memory bw_macos_memory_headers)
add_test(NAME unit.virtual_memory_block COMMAND virtual_memory_block_tests)
set_tests_properties(unit.virtual_memory_block PROPERTIES TIMEOUT 15)

# Unit: word mutex uncontended protocol.
add_executable(word_mutex_tests unit/concurrency/word_mutex_tests.cpp)
target_link_libraries(word_mutex_tests PRIVATE bw_allocator bw_concurrency_headers)
add_test(NAME unit.word_mutex COMMAND word_mutex_tests)
set_tests_properties(unit.word_mutex PROPERTIES TIMEOUT 15)

# Unit: engine loop on a recording clock, one scenario per entry.
add_executable(engine_loop_tests unit/runtime/engine_loop_tests.cpp)
target_link_libraries(engine_loop_tests PRIVATE bw_runtime)
foreach(scenario IN ITEMS
  defaults
  lifecycle_order
  capped_frame_is_paced
  late_frame_is_not_compensated
  oversleep_is_measured
  rate_setting_is_retained
  application_time_publication
  exit_request_does_not_interrupt_frame
  requested_exit_before_startup
  requested_exit_during_start
  seconds_limit_ends_the_run)
  add_test(NAME unit.engine_loop.${scenario} COMMAND engine_loop_tests ${scenario})
  set_tests_properties(unit.engine_loop.${scenario} PROPERTIES TIMEOUT 10)
endforeach()

# Unit: frame wait arithmetic.
add_executable(frame_wait_tests unit/runtime/frame_wait_tests.cpp)
target_link_libraries(frame_wait_tests PRIVATE bw_runtime)
add_test(NAME unit.frame_wait COMMAND frame_wait_tests)
set_tests_properties(unit.frame_wait PROPERTIES TIMEOUT 15)

# Integration: memory.
add_executable(memory_tests integration/memory/memory_tests.cpp)
target_link_libraries(memory_tests PRIVATE bw_memory bw_macos_memory_headers Threads::Threads)
add_test(NAME integration.memory COMMAND memory_tests)
set_tests_properties(integration.memory PROPERTIES TIMEOUT 60)

add_executable(allocator_tests integration/memory/allocator_tests.cpp)
target_link_libraries(allocator_tests PRIVATE bw_allocator bw_platform_memory_headers Threads::Threads)
add_test(NAME integration.allocator COMMAND allocator_tests)
set_tests_properties(integration.allocator PROPERTIES TIMEOUT 60)

add_executable(allocator_sync_tests integration/memory/allocator_sync_tests.cpp)
target_link_libraries(allocator_sync_tests PRIVATE bw_allocator bw_concurrency_headers
  bw_platform_memory_headers Threads::Threads)
add_test(NAME integration.allocator_sync COMMAND allocator_sync_tests)
set_tests_properties(integration.allocator_sync PROPERTIES TIMEOUT 60)

# Integration: runtime.
add_executable(global_allocation_tests integration/runtime/global_allocation_tests.cpp
  ${PROJECT_SOURCE_DIR}/src/engine/core/memory/global_allocation.cpp)
target_link_libraries(global_allocation_tests PRIVATE bw_runtime bw_memory_headers)
add_test(NAME integration.global_allocation COMMAND global_allocation_tests)
set_tests_properties(integration.global_allocation PROPERTIES TIMEOUT 15)

# Integration: tasks.
add_executable(named_thread_dispatch_tests integration/tasks/named_thread_dispatch_tests.cpp)
target_link_libraries(named_thread_dispatch_tests PRIVATE bw_allocator bw_tasks_headers Threads::Threads)
add_test(NAME integration.named_thread_dispatch COMMAND named_thread_dispatch_tests)
set_tests_properties(integration.named_thread_dispatch PROPERTIES TIMEOUT 60)
