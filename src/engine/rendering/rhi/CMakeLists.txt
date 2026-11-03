# RHI: the rendering hardware interface contract, its backend-neutral logic
# and its backends. See docs/architecture/rhi.md. This file declares the
# public header interface; the module library, the validation layer and the
# Metal backend are added by the implementation steps of the slice.
bw_header_interface(bw_rhi_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_rhi_headers INTERFACE bw_shaders_headers bw_memory_headers)

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
  resources.cpp)
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
    backends/metal/metal_objects.cpp)
  target_link_libraries(bw_rhi PRIVATE bw_metal_cpp bw_metal_irconverter_runtime)
  target_compile_definitions(bw_rhi PRIVATE BW_RHI_METAL=1)
  target_include_directories(bw_rhi PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
