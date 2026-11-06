# Tests assert public contracts only. Unit tests need nothing beyond the code
# under test; integration tests may use the OS, threads or another module.
# No test target receives a private include directory.

# The one test harness (support/bw/testing/harness.hpp): require, the
# scenario table and run_scenario with the exit-code contract. Every test
# executable links it; no test defines its own main or require.
add_library(bw_test_harness INTERFACE)
target_include_directories(bw_test_harness INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/support")

# Unit: math.
foreach(math_case IN ITEMS math_vector math_register quaternion_register quaternion)
  add_executable(${math_case}_tests unit/math/${math_case}_tests.cpp)
  target_link_libraries(${math_case}_tests PRIVATE bw_test_harness bw_math)
  add_test(NAME unit.${math_case} COMMAND ${math_case}_tests ${math_case})
  set_tests_properties(unit.${math_case} PROPERTIES TIMEOUT 15)
endforeach()

# Unit: memory policy and alignment arithmetic (header only).
foreach(memory_case IN ITEMS alignment new_request)
  add_executable(${memory_case}_tests unit/memory/${memory_case}_tests.cpp)
  target_link_libraries(${memory_case}_tests PRIVATE bw_test_harness bw_memory_headers)
  add_test(NAME unit.${memory_case} COMMAND ${memory_case}_tests)
  set_tests_properties(unit.${memory_case} PROPERTIES TIMEOUT 15)
endforeach()

# Unit: virtual memory block descriptor, no mapping made.
add_executable(virtual_memory_block_tests unit/platform/virtual_memory_block_tests.cpp)
target_link_libraries(virtual_memory_block_tests PRIVATE bw_test_harness bw_memory bw_macos_memory_headers)
add_test(NAME unit.virtual_memory_block COMMAND virtual_memory_block_tests virtual_memory_block)
set_tests_properties(unit.virtual_memory_block PROPERTIES TIMEOUT 15)

# Unit: word mutex uncontended protocol.
add_executable(word_mutex_tests unit/concurrency/word_mutex_tests.cpp)
target_link_libraries(word_mutex_tests PRIVATE bw_test_harness bw_allocator bw_concurrency_headers)
add_test(NAME unit.word_mutex COMMAND word_mutex_tests word_mutex)
set_tests_properties(unit.word_mutex PROPERTIES TIMEOUT 15)

# Unit: engine loop on a recording clock, one scenario per entry.
add_executable(engine_loop_tests unit/runtime/engine_loop_tests.cpp)
target_link_libraries(engine_loop_tests PRIVATE bw_test_harness bw_runtime)
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
target_link_libraries(frame_wait_tests PRIVATE bw_test_harness bw_runtime)
add_test(NAME unit.frame_wait COMMAND frame_wait_tests frame_wait)
set_tests_properties(unit.frame_wait PROPERTIES TIMEOUT 15)

# Unit: shader system public contracts, one executable per header plus the
# shader_compiler option parser, one CTest entry per scenario.
set(shader_parameter_block_scenarios
  dense_layout_offsets_equal_offsetof
  hlsl_placement_matches_cpp_offsets
  hash_stable_under_member_reordering
  hash_changes_on_any_member_change
  generated_struct_text_is_exact
  hlsl_errors_named
  binding_line_text)
set(shader_permutation_scenarios
  id_round_trip_and_count
  defines_one_per_dimension_in_order
  enumeration_ascending_excluded_absent
  registry_names_unique)
set(shader_root_layout_scenarios
  hash_changes_when_slot_moves
  find_static_slot
  root_signature_text_is_exact
  placement_table)
set(shader_artifact_scenarios
  write_then_load_round_trip
  identical_bytes_for_equal_inputs
  named_failures
  write_errors_named
  unknown_trailer_key_ignored
  binding_use_same_for_same_reflection
  file_name_convention)
set(shader_compiler_options_scenarios
  parse_enumerate_and_compile
  parse_rejections
  arguments_fixed_order_no_paths
  enumerate_line_format)
# Compile probes: the compile-time preconditions of parameter_block.hpp and
# permutation.hpp, proven at configure time. Each source under
# unit/rendering/shaders/compile_failures/ must fail to compile, and its
# compiler output must name the precondition it breaks, with the same usage
# requirements as bw_shaders_headers; the control must compile with them, so a
# probe cannot pass on a broken setup. Configure fails by name otherwise.
# try_compile cannot link a non-imported interface target, so its usage
# requirements are read from the targets themselves, transitively, and a
# generator expression among them fails configure by name rather than being
# passed through unevaluated.
function(bw_shader_compile_probes)
  set(pending bw_shaders_headers)
  set(visited)
  set(probe_includes)
  set(probe_options)
  set(probe_definitions)
  while(pending)
    list(POP_FRONT pending current)
    if(current IN_LIST visited OR NOT TARGET ${current})
      continue()
    endif()
    list(APPEND visited ${current})
    foreach(property IN ITEMS INTERFACE_INCLUDE_DIRECTORIES INTERFACE_COMPILE_OPTIONS
                              INTERFACE_COMPILE_DEFINITIONS INTERFACE_LINK_LIBRARIES)
      get_target_property(values ${current} ${property})
      if(NOT values)
        continue()
      endif()
      if(values MATCHES [[\$<]])
        message(FATAL_ERROR "Shader compile probes: ${current} ${property} holds a generator "
          "expression, which try_compile cannot evaluate at configure time: ${values}")
      endif()
      if(property STREQUAL "INTERFACE_INCLUDE_DIRECTORIES")
        list(APPEND probe_includes ${values})
      elseif(property STREQUAL "INTERFACE_COMPILE_OPTIONS")
        list(APPEND probe_options ${values})
      elseif(property STREQUAL "INTERFACE_COMPILE_DEFINITIONS")
        foreach(definition IN LISTS values)
          list(APPEND probe_definitions "-D${definition}")
        endforeach()
      else()
        list(APPEND pending ${values})
      endif()
    endforeach()
  endwhile()
  # Compile only; a probe has no main.
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  set(probe_dir "${CMAKE_CURRENT_SOURCE_DIR}/unit/rendering/shaders/compile_failures")
  # name | header declaration asserted | text the compiler output must contain
  set(probes
    "unlisted_member|(parameter_block.hpp, BW_PARAMETER_BLOCK_LAYOUT) every member listed|parameter block member is not dense"
    "wrong_alignment|(parameter_block.hpp, ParameterTraits) a type outside the value set|ParameterTraits<PlainFloat3>"
    "size_not_multiple_of_16|(parameter_block.hpp, BW_PARAMETER_BLOCK_LAYOUT) alignof 16, sizeof a multiple of 16|a parameter block is a multiple of 16 bytes"
    "nested_before_layout|(parameter_block.hpp, BW_PARAMETER_BLOCK_LAYOUT) nested layouts first|BlockLayoutOf<ProbeInner>"
    "domain_over_max_permutations|(permutation.hpp, kMaxPermutations) domain within kMaxPermutations|permutation domain exceeds kMaxPermutations")
  try_compile(control_compiled SOURCES "${probe_dir}/control.cpp"
    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${probe_includes}"
    COMPILE_DEFINITIONS ${probe_options} ${probe_definitions}
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    NO_CACHE OUTPUT_VARIABLE control_output)
  if(NOT control_compiled)
    message(FATAL_ERROR "Shader compile probe control must compile: ${control_output}")
  endif()
  foreach(probe IN LISTS probes)
    string(REPLACE "|" ";" fields "${probe}")
    list(GET fields 0 name)
    list(GET fields 1 asserted)
    list(GET fields 2 expected)
    try_compile(probe_compiled SOURCES "${probe_dir}/${name}.cpp"
      CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${probe_includes}"
      COMPILE_DEFINITIONS ${probe_options} ${probe_definitions}
      CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
      NO_CACHE OUTPUT_VARIABLE probe_output)
    if(probe_compiled)
      message(FATAL_ERROR "Shader compile probe ${name} (${asserted}) compiled; it must not")
    endif()
    string(FIND "${probe_output}" "${expected}" found)
    if(found EQUAL -1)
      message(FATAL_ERROR "Shader compile probe ${name} (${asserted}) failed without naming "
        "'${expected}': ${probe_output}")
    endif()
    message(STATUS "Shader compile probe ${name}: rejected (${asserted})")
  endforeach()
endfunction()
bw_shader_compile_probes()

foreach(shader_case IN ITEMS parameter_block permutation root_layout artifact compiler_options)
  add_executable(${shader_case}_tests unit/rendering/shaders/${shader_case}_tests.cpp)
  target_link_libraries(${shader_case}_tests PRIVATE bw_test_harness bw_shaders)
  foreach(scenario IN LISTS shader_${shader_case}_scenarios)
    add_test(NAME unit.${shader_case}.${scenario} COMMAND ${shader_case}_tests ${scenario})
    set_tests_properties(unit.${shader_case}.${scenario} PROPERTIES TIMEOUT 10)
  endforeach()
endforeach()
add_subdirectory(unit/rendering/geometry)
add_subdirectory(unit/rendering)
add_subdirectory(unit/rendering/materials)
add_subdirectory(unit/animation)
add_subdirectory(unit/rendering/rhi)
add_subdirectory(unit/platform/window)
# Content: the module's pure contracts (unit) and its file system paths over
# temporary trees and the player's install (integration). BW_GAME_DATA is the
# install root, given on the configure command line and never written into a
# tracked file; empty registers the scenarios that need it DISABLED.
set(BW_GAME_DATA "" CACHE PATH
  "Root of the player's Bullet Witch install for the integration scenarios that read it")
# The PW0 cache the fixture pw0_cache converts once
# (integration/tools/content_converter); the scenarios that read converted
# PW0 assets require the fixture and are given this path.
set(BW_PW0_TEST_CACHE "${CMAKE_BINARY_DIR}/tests/pw0_cache")
add_subdirectory(unit/content)
add_subdirectory(unit/tools/content_converter)
add_subdirectory(integration/content)
add_subdirectory(integration/tools/content_converter)
# game_host's command line, the pure parse the Apple host links.
if(APPLE)
  add_subdirectory(unit/hosts)
endif()

# Shader compiler tool: pure tool logic (unit) and the toolchain scenarios
# (integration), each in its own directory and CMake file, built only with
# the shader toolchain configured.
if(BW_SHADER_TOOLCHAIN)
  add_subdirectory(unit/tools/shader_compiler)
  add_subdirectory(integration/rendering/shaders)
endif()
add_subdirectory(integration/rendering/rhi)
add_subdirectory(integration/platform/window)
# The game host draws on the Metal device with the build's shader artifacts.
if(APPLE AND BW_SHADER_TOOLCHAIN)
  add_subdirectory(integration/hosts)
endif()

# Integration: memory.
add_executable(memory_tests integration/memory/memory_tests.cpp)
target_link_libraries(memory_tests PRIVATE bw_test_harness bw_memory bw_macos_memory_headers Threads::Threads)
add_test(NAME integration.memory COMMAND memory_tests memory)
set_tests_properties(integration.memory PROPERTIES TIMEOUT 60)

add_executable(allocator_tests integration/memory/allocator_tests.cpp)
target_link_libraries(allocator_tests PRIVATE bw_test_harness bw_allocator bw_platform_memory_headers Threads::Threads)
add_test(NAME integration.allocator COMMAND allocator_tests allocator)
set_tests_properties(integration.allocator PROPERTIES TIMEOUT 60)

add_executable(allocator_sync_tests integration/memory/allocator_sync_tests.cpp)
target_link_libraries(allocator_sync_tests PRIVATE bw_test_harness bw_allocator bw_concurrency_headers
  bw_platform_memory_headers Threads::Threads)
add_test(NAME integration.allocator_sync COMMAND allocator_sync_tests allocator_sync)
set_tests_properties(integration.allocator_sync PROPERTIES TIMEOUT 60)

# Integration: runtime.
add_executable(global_allocation_tests integration/runtime/global_allocation_tests.cpp
  ${PROJECT_SOURCE_DIR}/src/engine/core/memory/global_allocation.cpp)
target_link_libraries(global_allocation_tests PRIVATE bw_test_harness bw_runtime bw_memory_headers)
add_test(NAME integration.global_allocation COMMAND global_allocation_tests global_allocation)
set_tests_properties(integration.global_allocation PROPERTIES TIMEOUT 15)

# Integration: tasks.
add_executable(named_thread_dispatch_tests integration/tasks/named_thread_dispatch_tests.cpp)
target_link_libraries(named_thread_dispatch_tests PRIVATE bw_test_harness bw_allocator bw_tasks_headers Threads::Threads)
add_test(NAME integration.named_thread_dispatch COMMAND named_thread_dispatch_tests named_thread_dispatch)
set_tests_properties(integration.named_thread_dispatch PROPERTIES TIMEOUT 60)
