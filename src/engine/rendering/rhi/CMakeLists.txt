# RHI: the rendering hardware interface contract, its backend-neutral logic
# and its backends. See docs/architecture/rhi.md. This file declares the
# public header interface; the module library, the validation layer and the
# Metal backend are added by the implementation steps of the slice.
bw_header_interface(bw_rhi_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_rhi_headers INTERFACE bw_shaders_headers bw_memory_headers
  bw_platform_window_headers)

# The one configuration switch (public/bw/rendering/rhi/build.hpp): the debug
# surface is compiled in for Debug and RelWithDebInfo and out for Release and
# MinSizeRel. Single-configuration generators only, as the rest of the build.
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
  set(bw_rhi_debug 0)
else()
  set(bw_rhi_debug 1)
endif()
target_compile_definitions(bw_rhi_headers INTERFACE "BW_RHI_DEBUG=${bw_rhi_debug}")

# The backend-neutral module: lifetime, descriptor and memory bookkeeping,
# descriptor validation, state and pipeline objects, the description cache,
# command recording and replay, the backend dispatch and the device.
# transitions.hpp is header-only (every function constexpr) and has no source.
add_library(bw_rhi
  backend.cpp
  command_list.cpp
  descriptor_allocator.cpp
  device.cpp
  lifetime.cpp
  memory_suballocator.cpp
  pipeline_cache.cpp
  pipeline_states.cpp
  resources.cpp
  viewport.cpp)
target_link_libraries(bw_rhi PUBLIC bw_rhi_headers bw_shaders)

# The validation layer (public/bw/rendering/rhi/backend.hpp,
# wrap_with_validation): compiled into bw_rhi only when the debug surface is
# on. One library, so the device's call into the wrapper and the wrapper's
# calls into the module's validation functions form no static-library cycle;
# with bw_rhi_debug 0 the source is absent and so are its symbols.
if(bw_rhi_debug)
  target_sources(bw_rhi PRIVATE validation/validation_layer.cpp)
endif()

# The Metal backend (public/bw/rendering/rhi/backend.hpp, create_backend on
# Apple hosts): compiled into bw_rhi, one library as for the validation layer,
# so backend.cpp's call into the factory forms no static-library cycle.
# metal-cpp and the converter runtime shim are private to the module; linking
# them PRIVATE still carries their frameworks to every executable that links
# bw_rhi. BW_RHI_METAL selects backend.cpp's Metal branch, and the module's
# own directory is the include root of backend.cpp's private factory header.
if(APPLE)
  target_sources(bw_rhi PRIVATE
    backends/metal/metal_context.cpp
    backends/metal/metal_device.cpp
    backends/metal/metal_formats.cpp
    backends/metal/metal_implementation.cpp
    backends/metal/metal_memory.cpp
    backends/metal/metal_multi_draw.cpp
    backends/metal/metal_objects.cpp
    backends/metal/metal_viewport.cpp)
  target_link_libraries(bw_rhi PRIVATE bw_metal_cpp bw_metal_irconverter_runtime)
  target_compile_definitions(bw_rhi PRIVATE BW_RHI_METAL=1)
  target_include_directories(bw_rhi PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

  # The Metal backend's own kernel (docs/architecture/source-layout.md): the
  # counted multi-draw's encode kernel in the Metal Shading Language, built
  # by the Metal toolchain of the Xcode in use (a build prerequisite with the
  # SDK, rhi.md Dependencies) and embedded in bw_rhi as bytes. The air, the
  # metallib and the generated source are build products, never committed.
  foreach(bw_metal_tool IN ITEMS metal metallib)
    execute_process(COMMAND xcrun -sdk macosx -f ${bw_metal_tool}
      RESULT_VARIABLE bw_metal_tool_result OUTPUT_QUIET ERROR_QUIET)
    if(NOT bw_metal_tool_result EQUAL 0)
      message(FATAL_ERROR "The Metal backend needs the Metal toolchain: "
        "'xcrun -sdk macosx -f ${bw_metal_tool}' failed. Install it with "
        "'xcodebuild -downloadComponent MetalToolchain'.")
    endif()
  endforeach()
  set(bw_encode_source "${CMAKE_CURRENT_SOURCE_DIR}/backends/metal/multi_draw_encode.metal")
  set(bw_encode_air "${CMAKE_CURRENT_BINARY_DIR}/multi_draw_encode.air")
  set(bw_encode_library "${CMAKE_CURRENT_BINARY_DIR}/multi_draw_encode.metallib")
  set(bw_encode_embedded "${CMAKE_CURRENT_BINARY_DIR}/multi_draw_encode_library.cpp")
  add_custom_command(OUTPUT "${bw_encode_air}"
    COMMAND xcrun -sdk macosx metal -std=metal3.1 -c "${bw_encode_source}" -o "${bw_encode_air}"
    DEPENDS "${bw_encode_source}"
    COMMENT "Metal kernel multi_draw_encode" VERBATIM)
  add_custom_command(OUTPUT "${bw_encode_library}"
    COMMAND xcrun -sdk macosx metallib "${bw_encode_air}" -o "${bw_encode_library}"
    DEPENDS "${bw_encode_air}" VERBATIM)
  add_custom_command(OUTPUT "${bw_encode_embedded}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${bw_encode_library}" "-DOUTPUT=${bw_encode_embedded}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/backends/metal/embed_library.cmake"
    DEPENDS "${bw_encode_library}" "${CMAKE_CURRENT_SOURCE_DIR}/backends/metal/embed_library.cmake"
    VERBATIM)
  target_sources(bw_rhi PRIVATE "${bw_encode_embedded}")
endif()
