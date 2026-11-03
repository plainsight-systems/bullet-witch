# Unit tests of the RHI's public contracts: one executable per header, one
# CTest entry per scenario (unit.rhi.<header>.<scenario>), the scenario named
# as the first argument. Header-only headers link the header interface;
# the rest link the module. No test receives the module's source directory.
set(rhi_result_scenarios success_failure_and_detail)
set(rhi_access_scenarios masks_and_valid_access pipeline_bits)
set(rhi_format_scenarios block_table_and_byte_counts)
set(rhi_synchronization_scenarios staging_layout_pitch_and_bytes fence_wait_error_by_state)
set(rhi_queries_scenarios query_pool_validation_named)
set(rhi_device_scenarios device_description_validation_named)
set(rhi_lifetime_scenarios
  ref_count_and_release_enqueues
  nothing_destroyed_before_completion
  destruction_order_is_enqueue_order
  view_keeps_resource_alive
  frames_in_flight_wait_exactly_at_bound
  queue_destroys_remaining_at_destruction
  enqueue_during_destroy_at_or_below_frame_is_destroyed)
set(rhi_descriptor_allocator_scenarios
  allocate_free_reuse_lowest_first
  exhaustion_is_no_value
  pools_isolated_by_array)
set(rhi_memory_suballocator_scenarios
  first_fit_split_merge
  alignment_padding_belongs_to_allocation
  exhaustion_is_no_value)
set(rhi_resources_scenarios
  buffer_validation_named_in_order
  texture_validation_named_in_order
  view_validation_named
  subresource_arithmetic
  debug_name_copied_and_bounded
  view_returns_slot_on_destruction
  viewable_initial_tracked_access_and_kind
  array_for_by_dimension_and_kind
  initial_data_validation_named
  lock_validation_named)
set(rhi_transitions_scenarios
  unknown_before_resolves_from_tracked
  legal_and_illegal_pairs
  double_begin_rejected
  unsolicited_end_rejected
  pipelines_must_match
  end_writes_tracked_access
  use_requires_access_and_no_open_transition
  redundant_is_not_an_error)
set(rhi_binding_structure_scenarios
  engine_layout_is_160_bytes_in_order
  same_inputs_same_structure
  pipeline_binding_is_the_union)
set(rhi_pipeline_cache_scenarios
  key_stable_under_pointer_identity
  key_changes_on_shader_hash_or_state
  find_insert_no_replace
  precompile_set_from_enumeration)
set(rhi_pipeline_states_scenarios
  render_pass_validation_named
  render_target_layout_derived
  pipeline_validation_named
  graphics_pipeline_holds_states_and_drops_shaders
  compute_pipeline_copies_hash_and_group_size)
set(rhi_command_list_scenarios
  replay_in_order_with_copied_arguments
  reset_empties_and_pass_tracking
  markers_record_in_debug
  replay_stops_at_first_failure
  recording_adds_no_reference
  every_command_replays_with_copied_arguments)

foreach(rhi_header IN ITEMS result access format synchronization queries device)
  add_executable(rhi_${rhi_header}_tests ${rhi_header}_tests.cpp)
  target_link_libraries(rhi_${rhi_header}_tests PRIVATE bw_rhi_headers)
endforeach()
foreach(rhi_header IN ITEMS lifetime descriptor_allocator memory_suballocator resources transitions
                            binding_structure pipeline_cache pipeline_states command_list)
  add_executable(rhi_${rhi_header}_tests ${rhi_header}_tests.cpp)
  target_link_libraries(rhi_${rhi_header}_tests PRIVATE bw_rhi)
endforeach()
foreach(rhi_header IN ITEMS result access format synchronization queries device lifetime
                            descriptor_allocator
                            memory_suballocator resources transitions binding_structure
                            pipeline_cache pipeline_states command_list)
  foreach(scenario IN LISTS rhi_${rhi_header}_scenarios)
    add_test(NAME unit.rhi.${rhi_header}.${scenario} COMMAND rhi_${rhi_header}_tests ${scenario})
    set_tests_properties(unit.rhi.${rhi_header}.${scenario} PROPERTIES TIMEOUT 10)
  endforeach()
endforeach()

# The validation layer's unit test (backend.hpp, wrap_with_validation). The
# layer and its declarations exist only when the header interface defines
# BW_RHI_DEBUG=1 (build.hpp), so the test is built only then; the definition
# is read from bw_rhi_headers rather than re-derived from the build type.
get_target_property(rhi_header_definitions bw_rhi_headers INTERFACE_COMPILE_DEFINITIONS)
if("BW_RHI_DEBUG=1" IN_LIST rhi_header_definitions)
  set(rhi_validation_scenarios
    undeclared_transition_rejected_by_name
    redundant_transition_reported_and_forwarded
    double_begin_and_unsolicited_end_named
    split_batch_on_single_queue_is_named
    use_without_access_is_named
    render_pass_nesting_named
    render_pass_description_invalid_named
    pipeline_targets_mismatch_and_no_pipeline_named
    draw_block_layout_mismatch_named
    marker_unbalanced_named
    query_outside_pass_and_index_named
    every_call_forwarded_once_with_same_arguments
    use_checks_cover_every_named_use
    use_during_open_transition_is_named
    null_resource_in_batch_is_named
    inside_only_and_outside_only_commands_named
    render_pass_open_at_submit_is_named
    query_kind_mismatch_is_named
    static_slot_out_of_range_is_named
    root_constants_out_of_range_is_named
    draw_block_not_set_is_named
    static_slot_not_set_is_named)
  add_executable(rhi_validation_tests validation_tests.cpp)
  target_link_libraries(rhi_validation_tests PRIVATE bw_rhi)
  foreach(scenario IN LISTS rhi_validation_scenarios)
    add_test(NAME unit.rhi.validation.${scenario} COMMAND rhi_validation_tests ${scenario})
    set_tests_properties(unit.rhi.validation.${scenario} PROPERTIES TIMEOUT 10)
  endforeach()
endif()

# Shipping absence (build.hpp, BW_RHI_DEBUG): configure-time compile probes under
# BW_RHI_DEBUG=0, modeled on bw_shader_compile_probes (tests/CMakeLists.txt).
# The usage requirements of bw_rhi_headers are walked transitively; the
# interface's own BW_RHI_DEBUG definition is dropped and BW_RHI_DEBUG=0
# passed instead, or the translation unit would see a redefinition. The
# control must compile; each probe must fail and its output must name the
# text given. Configure fails by name otherwise.
function(bw_rhi_compile_probes)
  set(pending bw_rhi_headers)
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
        message(FATAL_ERROR "RHI compile probes: ${current} ${property} holds a generator "
          "expression, which try_compile cannot evaluate at configure time: ${values}")
      endif()
      if(property STREQUAL "INTERFACE_INCLUDE_DIRECTORIES")
        list(APPEND probe_includes ${values})
      elseif(property STREQUAL "INTERFACE_COMPILE_OPTIONS")
        list(APPEND probe_options ${values})
      elseif(property STREQUAL "INTERFACE_COMPILE_DEFINITIONS")
        foreach(definition IN LISTS values)
          if(NOT definition MATCHES "^BW_RHI_DEBUG=")
            list(APPEND probe_definitions "-D${definition}")
          endif()
        endforeach()
      else()
        list(APPEND pending ${values})
      endif()
    endforeach()
  endwhile()
  list(APPEND probe_definitions "-DBW_RHI_DEBUG=0")
  # Compile only; a probe has no main.
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  set(probe_dir "${CMAKE_CURRENT_SOURCE_DIR}/compile_failures")
  # name | header declaration asserted | text the compiler output must contain
  set(probes
    "validation_absent|(build.hpp, BW_RHI_DEBUG) no validation factory with BW_RHI_DEBUG 0|'wrap_with_validation'"
    "validation_field_absent|device.hpp DeviceDescription: no validation field with BW_RHI_DEBUG 0|'validation'")
  try_compile(control_compiled SOURCES "${probe_dir}/control.cpp"
    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${probe_includes}"
    COMPILE_DEFINITIONS ${probe_options} ${probe_definitions}
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    NO_CACHE OUTPUT_VARIABLE control_output)
  if(NOT control_compiled)
    message(FATAL_ERROR "RHI compile probe control must compile under BW_RHI_DEBUG=0: "
      "${control_output}")
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
      message(FATAL_ERROR "RHI compile probe ${name} (${asserted}) compiled; it must not")
    endif()
    string(FIND "${probe_output}" "${expected}" found)
    if(found EQUAL -1)
      message(FATAL_ERROR "RHI compile probe ${name} (${asserted}) failed without naming "
        "${expected}: ${probe_output}")
    endif()
    message(STATUS "RHI compile probe ${name}: rejected (${asserted})")
  endforeach()
endfunction()
bw_rhi_compile_probes()
