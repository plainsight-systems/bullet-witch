cmake_minimum_required(VERSION 3.25)
project(BulletWitchRebuild LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(cmake/Quality.cmake)

add_library(bw_contracts INTERFACE)
target_include_directories(bw_contracts INTERFACE "${PROJECT_SOURCE_DIR}/src")
target_compile_options(bw_contracts INTERFACE
  -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion)

# Shared math consumers inherit one source Debug diagnostic configuration and
# the real constant/report/output definitions required by inline vector methods.
add_library(bw_math
  src/core/math/vector3.cpp
  src/core/diagnostics/math_diagnostic.cpp
  src/platform/macos/diagnostic_output.cpp)
target_link_libraries(bw_math PUBLIC bw_contracts)
target_compile_definitions(bw_math PUBLIC
  BW_ENABLE_MATH_DIAGNOSTICS=$<IF:$<CONFIG:Debug>,1,0>)

# Compile each public header independently, including during the header-first
# stage. Generated files are build products, not a runtime implementation.
set(BW_PUBLIC_HEADERS
  core/math/vector3.hpp
  core/diagnostics/math_diagnostic.hpp
  platform/math.hpp
  platform/diagnostic_output.hpp
  core/concurrency/word_mutex.hpp
  core/memory/allocator_checks.hpp
  core/memory/crash_allocator.hpp
  platform/task_dispatch.hpp
  core/tasks/task_queue.hpp
  core/tasks/task_storage.hpp
  core/tasks/named_thread_dispatch.hpp
  core/memory/global_allocation.hpp
  core/memory/allocator.hpp
  core/memory/binned_allocator.hpp
  core/memory/binned_bit_tree.hpp
  core/memory/binned_size_classes.hpp
  core/memory/binned_thread_cache.hpp
  platform/allocator_platform.hpp
  platform/allocator_sync.hpp
  platform/macos/allocator_sync.hpp
  core/memory/memory_failure.hpp
  platform/memory_failure.hpp
  platform/platform_time.hpp
  platform/macos/platform_time.hpp
  platform/platform_memory.hpp
  platform/macos/platform_memory.hpp
  engine/runtime/engine.hpp
  engine/runtime/engine_loop.hpp)
foreach(header IN LISTS BW_PUBLIC_HEADERS)
  string(REPLACE "/" "_" unit "${header}")
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/contracts/${unit}.cpp"
    CONTENT "#include \"${header}\"\n")
  list(APPEND BW_HEADER_UNITS "${CMAKE_CURRENT_BINARY_DIR}/contracts/${unit}.cpp")
endforeach()
add_library(header_contracts OBJECT ${BW_HEADER_UNITS})
target_link_libraries(header_contracts PRIVATE bw_contracts bw_math)

find_package(Threads REQUIRED)
include(CTest)
add_library(bw_platform_time src/platform/macos/platform_time.cpp)
target_link_libraries(bw_platform_time PUBLIC bw_contracts PRIVATE Threads::Threads)
add_library(bw_runtime
  src/engine/runtime/engine.cpp
  src/engine/runtime/engine_loop.cpp)
target_link_libraries(bw_runtime PUBLIC bw_contracts bw_platform_time PRIVATE Threads::Threads)
add_executable(engine_loop_tests tests/engine_loop_tests.cpp)
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
add_executable(engine_host src/hosts/macos/engine_host.cpp)
target_sources(engine_host PRIVATE src/core/memory/global_allocation.cpp)
target_link_libraries(engine_host PRIVATE bw_runtime bw_allocator)
add_executable(engine_loop_probe tools/engine_loop_probe.cpp)
target_link_libraries(engine_loop_probe PRIVATE bw_runtime)

# Accepted native VM/fatal foundation remains independent of the general allocator.
add_library(bw_memory
  src/core/memory/memory_failure.cpp
  src/platform/macos/memory_failure.cpp
  src/platform/platform_memory.cpp
  src/platform/macos/platform_memory.cpp)
target_link_libraries(bw_memory PUBLIC bw_contracts)
# Current target is headless: select the reference stdout output device alongside
# Mac native diagnostics. Future graphical hosts choose their output devices.
target_compile_definitions(bw_memory PRIVATE BW_HEADLESS_DIAGNOSTICS=1)
add_executable(memory_tests tests/memory_tests.cpp)
target_link_libraries(memory_tests PRIVATE bw_memory Threads::Threads)
add_test(NAME memory_native_and_failure_semantics COMMAND memory_tests)
set_tests_properties(memory_native_and_failure_semantics PROPERTIES TIMEOUT 60)

# Selected Mac Binned3, its synchronization, crash reserve and required named-
# thread dispatch dependency. Global operators are an executable-local choice.
add_library(bw_allocator
  src/core/concurrency/word_mutex.cpp
  src/core/memory/allocator.cpp
  src/core/memory/binned_allocator.cpp
  src/core/memory/binned_bit_tree.cpp
  src/core/memory/binned_size_classes.cpp
  src/core/memory/binned_thread_cache.cpp
  src/core/memory/crash_allocator.cpp
  src/core/tasks/task_queue.cpp
  src/core/tasks/task_storage.cpp
  src/core/tasks/named_thread_dispatch.cpp
  src/platform/macos/allocator_platform.cpp
  src/platform/macos/allocator_sync.cpp
  src/platform/macos/task_dispatch.cpp)
target_link_libraries(bw_allocator PUBLIC bw_memory PRIVATE bw_platform_time Threads::Threads)
foreach(allocator_case IN ITEMS allocator allocator_structure allocator_sync named_thread_dispatch crash_allocator)
  add_executable(${allocator_case}_tests tests/${allocator_case}_tests.cpp)
  target_link_libraries(${allocator_case}_tests PRIVATE bw_allocator Threads::Threads)
  add_test(NAME ${allocator_case}_contracts COMMAND ${allocator_case}_tests)
  set_tests_properties(${allocator_case}_contracts PROPERTIES TIMEOUT 60)
endforeach()

# Engine startup owns allocator cache setup and accepted named dispatch lifecycle.
target_link_libraries(bw_runtime PRIVATE bw_allocator)
# Link the same replacement unit directly into host and operator test, never
# transitively into all allocator consumers through the library.
add_executable(global_allocation_tests
  tests/global_allocation_tests.cpp src/core/memory/global_allocation.cpp)
target_link_libraries(global_allocation_tests PRIVATE bw_runtime)
add_executable(allocator_lifecycle_tests tests/allocator_lifecycle_tests.cpp)
target_link_libraries(allocator_lifecycle_tests PRIVATE bw_runtime bw_allocator)
foreach(integration_case IN ITEMS global_allocation allocator_lifecycle)
  add_test(NAME ${integration_case}_contracts COMMAND ${integration_case}_tests)
  set_tests_properties(${integration_case}_contracts PROPERTIES TIMEOUT 15)
endforeach()

# Native vector contracts include real child-process diagnostic output capture.
add_executable(math_vector_tests tests/math_vector_tests.cpp)
target_link_libraries(math_vector_tests PRIVATE bw_math)
add_test(NAME math_vector_contracts COMMAND math_vector_tests)
set_tests_properties(math_vector_contracts PROPERTIES TIMEOUT 15)
