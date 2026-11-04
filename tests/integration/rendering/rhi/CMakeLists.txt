# Integration scenarios of the RHI (docs/architecture/rhi.md). The validation
# reporter scenario needs no GPU and no shader toolchain; the Metal scenarios
# need this machine's Metal device and the fixture shader artifacts the build
# produced, so they exist only on Apple hosts with the shader toolchain. Every
# path is an argument; nothing is discovered from the environment.
# The validation layer's default reporter (public/bw/rendering/rhi/backend.hpp,
# report_validation_failure_and_abort) in a child process: no GPU and no
# toolchain, so outside the Apple and toolchain block below. Defined only when
# the header interface defines BW_RHI_DEBUG=1 (build.hpp), as the validation
# unit test is.
get_target_property(rhi_reporter_header_definitions bw_rhi_headers INTERFACE_COMPILE_DEFINITIONS)
if("BW_RHI_DEBUG=1" IN_LIST rhi_reporter_header_definitions)
  add_executable(rhi_validation_reporter_tests validation_reporter_tests.cpp)
  target_link_libraries(rhi_validation_reporter_tests PRIVATE bw_test_harness bw_rhi)
  add_test(NAME integration.rhi.validation.default_reporter_prints_and_aborts
    COMMAND rhi_validation_reporter_tests default_reporter_prints_and_aborts)
  set_tests_properties(integration.rhi.validation.default_reporter_prints_and_aborts
    PROPERTIES TIMEOUT 30)
endif()

# The Metal backend exists only on Apple hosts (public/bw/rendering/rhi/backend.hpp,
# create_backend), and the scenarios load the artifacts shader_artifacts builds.
if(APPLE AND BW_SHADER_TOOLCHAIN)
  set(rhi_metal_scenarios
    device_creation_and_required_capabilities
    device_create_rejects_bad_description
    clear_offscreen_and_read_back
    draw_fixture_triangle_and_read_back
    release_mid_frame_destroys_after_completion
    frames_in_flight_blocks_on_the_oldest
    fence_poll_false_then_true_and_wait_returns
    fence_wait_errors_named
    occlusion_query_counts_covered_samples
    query_pool_errors_named
    transient_budget_exhaustion_is_named
    lock_buffer_errors_named
    view_of_null_resource_is_named
    pipeline_miss_policy_error_returns_cache_miss
    precompile_stops_at_first_failure
    buffer_initial_data_is_uploaded
    vertex_layout_is_unsupported_on_metal
    pipeline_library_errors_named
    viewport_present_resize_present
    viewport_errors_named
    back_buffer_copies_to_staging
    decode_float3
    decode_short4n
    decode_half2
    decode_ubyte4
    decode_ubyte4n
    decode_from_second_stream
    decode_with_base_vertex
    cube_offscreen_matches_reference)
  # The validation probe exists only when the header interface defines
  # BW_RHI_DEBUG=1 (build.hpp), read from bw_rhi_headers as the validation
  # unit test does (tests/unit/rendering/rhi/CMakeLists.txt).
  get_target_property(rhi_metal_header_definitions bw_rhi_headers INTERFACE_COMPILE_DEFINITIONS)
  if("BW_RHI_DEBUG=1" IN_LIST rhi_metal_header_definitions)
    list(APPEND rhi_metal_scenarios validation_rejects_undeclared_transition_by_name)
  endif()

  # The viewport scenarios open a real window through the macOS adapter, so
  # they need a logged-in GUI session; the test process is the application.
  add_executable(rhi_metal_tests rhi_metal_tests.cpp)
  target_link_libraries(rhi_metal_tests PRIVATE bw_test_harness bw_rhi bw_shaders bw_window bw_geometry)
  add_dependencies(rhi_metal_tests shader_artifacts)
  foreach(scenario IN LISTS rhi_metal_scenarios)
    add_test(NAME integration.rhi.metal.${scenario}
      COMMAND rhi_metal_tests ${scenario} "${CMAKE_BINARY_DIR}/shaders/metal")
    set_tests_properties(integration.rhi.metal.${scenario} PROPERTIES TIMEOUT 120)
  endforeach()

  # The probes of the Metal viewport against its surface: the acquisition
  # failure through Device::execute and vsync applied to the layer.
  # Objective-C++, because the scenarios resize the window's CAMetalLayer
  # behind the viewport or read its display sync
  # (platform/window/window.hpp, NativeWindowHandle);
  # OBJCXX is enabled by cmake/ThirdParty.cmake on Apple platforms. It opens
  # a window, so it needs a logged-in GUI session like the viewport
  # scenarios above.
  add_executable(rhi_metal_layer_tests rhi_metal_layer_tests.mm)
  target_link_libraries(rhi_metal_layer_tests PRIVATE bw_test_harness bw_rhi bw_window "-framework QuartzCore")
  target_compile_options(rhi_metal_layer_tests PRIVATE -fobjc-arc)
  set_target_properties(rhi_metal_layer_tests PROPERTIES
    OBJCXX_CLANG_TIDY "${CMAKE_CXX_CLANG_TIDY}")
  foreach(scenario IN ITEMS stale_drawable_size_is_named failed_pass_holds_no_back_buffer
                            viewport_vsync_reported)
    add_test(NAME integration.rhi.metal.${scenario} COMMAND rhi_metal_layer_tests ${scenario})
    set_tests_properties(integration.rhi.metal.${scenario} PROPERTIES TIMEOUT 120)
  endforeach()
endif()
