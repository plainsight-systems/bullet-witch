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

# Compile each public header independently, including during the header-first
# stage. Generated files are build products, not a runtime implementation.
set(BW_PUBLIC_HEADERS
  platform/platform_time.hpp
  platform/macos/platform_time.hpp
  engine/runtime/engine.hpp
  engine/runtime/engine_loop.hpp)
foreach(header IN LISTS BW_PUBLIC_HEADERS)
  string(REPLACE "/" "_" unit "${header}")
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/contracts/${unit}.cpp"
    CONTENT "#include \"${header}\"\n")
  list(APPEND BW_HEADER_UNITS "${CMAKE_CURRENT_BINARY_DIR}/contracts/${unit}.cpp")
endforeach()
add_library(header_contracts OBJECT ${BW_HEADER_UNITS})
target_link_libraries(header_contracts PRIVATE bw_contracts)

find_package(Threads REQUIRED)
include(CTest)
add_library(bw_runtime
  src/platform/macos/platform_time.cpp
  src/engine/runtime/engine.cpp
  src/engine/runtime/engine_loop.cpp)
target_link_libraries(bw_runtime PUBLIC bw_contracts PRIVATE Threads::Threads)
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
target_link_libraries(engine_host PRIVATE bw_runtime)
add_executable(engine_loop_probe tools/engine_loop_probe.cpp)
target_link_libraries(engine_loop_probe PRIVATE bw_runtime)
