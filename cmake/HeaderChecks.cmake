# SF.11 self-contained header checks: every public and internal header compiles alone.
# Public API and compiler-required/private detail are intentionally separate.
set(BW_PUBLIC_HEADERS
  bw/memory/alignment.hpp
  bw/math/rotation/quaternion.hpp
  bw/math/rotation/quaternion_register.hpp
  bw/math/simd/register.hpp
  bw/platform/math/register_types.hpp
  bw/math/vector/vector3.hpp
  bw/math/matrix/matrix4.hpp
  bw/diagnostics/math_diagnostic.hpp
  bw/platform/math/math.hpp
  bw/platform/diagnostics/diagnostic_output.hpp
  bw/concurrency/word_mutex.hpp
  bw/platform/tasks/task_dispatch.hpp
  bw/tasks/named_thread_dispatch.hpp
  bw/memory/global_allocation.hpp
  bw/memory/allocator.hpp
  bw/platform/memory/allocator_platform.hpp
  bw/platform/memory/allocator_sync.hpp
  bw/memory/memory_failure.hpp
  bw/platform/memory/memory_failure.hpp
  bw/platform/time/platform_time.hpp
  bw/platform/os/macos/platform_time.hpp
  bw/platform/memory/platform_memory.hpp
  bw/platform/os/macos/platform_memory.hpp
  bw/runtime/engine.hpp
  bw/runtime/engine_loop.hpp
  bw/rendering/shaders/parameter_block.hpp
  bw/rendering/shaders/permutation.hpp
  bw/rendering/shaders/root_layout.hpp
  bw/rendering/shaders/artifact.hpp
  bw/rendering/shaders/compiler_options.hpp
  bw/rendering/rhi/build.hpp
  bw/rendering/rhi/result.hpp
  bw/rendering/rhi/access.hpp
  bw/rendering/rhi/format.hpp
  bw/rendering/rhi/lifetime.hpp
  bw/rendering/rhi/descriptor_allocator.hpp
  bw/rendering/rhi/memory_suballocator.hpp
  bw/rendering/rhi/resources.hpp
  bw/rendering/rhi/transitions.hpp
  bw/rendering/rhi/binding_structure.hpp
  bw/rendering/rhi/pipeline_states.hpp
  bw/rendering/rhi/pipeline_cache.hpp
  bw/rendering/rhi/synchronization.hpp
  bw/rendering/rhi/queries.hpp
  bw/rendering/rhi/command_list.hpp
  bw/rendering/rhi/viewport.hpp
  bw/rendering/rhi/backend.hpp
  bw/rendering/rhi/device.hpp
  bw/platform/window/window.hpp
  bw/platform/os/macos/window.hpp
  bw/rendering/geometry/vertex_streams.hpp
  bw/rendering/geometry/pulled_mesh_fixture.hpp
  bw/hosts/option_table.hpp
  bw/hosts/game_host_options.hpp
  bw/content/asset_id.hpp
  bw/content/cooked_asset.hpp
  bw/content/layers.hpp
  bw/content/loader.hpp
  bw/content/vdf.hpp
  bw/content/install.hpp
  bw/rendering/geometry/mesh_asset.hpp
  bw/rendering/geometry/morph_targets_asset.hpp
  bw/rendering/geometry/mesh_resources.hpp
  bw/rendering/shaders/artifact_file.hpp
  bw/rendering/scene/view.hpp
  bw/rendering/model_resources.hpp
  bw/rendering/pulled_cube_fixture.hpp
  bw/rendering/target_pass.hpp
  bw/rendering/texture_asset.hpp
  bw/rendering/materials/material_asset.hpp
  bw/rendering/materials/shading.hpp
  bw/rendering/materials/material.hpp
  bw/rendering/materials/lit_template.hpp
  bw/rendering/materials/material_buffer.hpp
  bw/rendering/scene/fixture_light.hpp
  bw/rendering/scene/forward_pass.hpp
  bw/rendering/model_draw.hpp
  bw/platform/process/executable_directory.hpp
  bw/animation/skeleton_asset.hpp
  bw/content_converter/cpr_container.hpp
  bw/content_converter/rat_index.hpp
  bw/content_converter/asset_data.hpp
  bw/content_converter/mesh_reader.hpp
  bw/content_converter/skeleton_reader.hpp
  bw/content_converter/texture_reader.hpp
  bw/content_converter/material_mapping.hpp
  bw/content_converter/conversion.hpp
)
# The shader_compiler tool's headers exist as a target only with the shader
# toolchain configured (tools/shader_compiler is added under it).
if(BW_SHADER_TOOLCHAIN)
  list(APPEND BW_PUBLIC_HEADERS
    bw/shader_compiler/logic.hpp
    bw/shader_compiler/enumeration.hpp
    bw/shader_compiler/toolchain.hpp)
endif()
set(BW_INTERNAL_HEADERS
  src/platform/cpu/arm64/math/simd/register_types.hpp
  src/engine/core/memory/allocator_checks.hpp
  src/engine/core/memory/crash_allocator.hpp
  src/engine/core/tasks/public/bw/tasks/detail/task_queue.hpp
  src/engine/core/tasks/task_storage.hpp
  src/engine/core/memory/binned/binned_allocator.hpp
  src/engine/core/memory/binned/binned_bit_tree.hpp
  src/engine/core/memory/binned/binned_size_classes.hpp
  src/engine/core/memory/binned/binned_thread_cache.hpp
  src/platform/os/macos/memory/public/bw/platform/os/macos/allocator_sync.hpp
  src/engine/content/file_bytes.hpp
)
# Each unit sits at its header's own path with the extension changed, so an
# editor that borrows a compile command by path similarity (clangd, for a
# header or an .inl that has none of its own) picks the unit that sees every
# public include root.
foreach(header IN LISTS BW_PUBLIC_HEADERS)
  string(REGEX REPLACE "\\.hpp$" "" unit "${header}")
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/contracts/public/${unit}.cpp"
    CONTENT "#include \"${header}\"\n")
  list(APPEND BW_PUBLIC_HEADER_UNITS "${CMAKE_BINARY_DIR}/contracts/public/${unit}.cpp")
endforeach()
foreach(header IN LISTS BW_INTERNAL_HEADERS)
  string(REPLACE "/" "_" unit "${header}")
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/contracts/internal/${unit}.cpp"
    CONTENT "#include \"${PROJECT_SOURCE_DIR}/${header}\"\n")
  list(APPEND BW_INTERNAL_HEADER_UNITS "${CMAKE_BINARY_DIR}/contracts/internal/${unit}.cpp")
endforeach()
add_library(public_header_contracts OBJECT ${BW_PUBLIC_HEADER_UNITS})
add_library(internal_header_contracts OBJECT ${BW_INTERNAL_HEADER_UNITS})
foreach(check IN ITEMS public_header_contracts internal_header_contracts)
  target_link_libraries(${check} PRIVATE bw_math bw_runtime_headers bw_memory_headers
    bw_tasks_headers bw_concurrency_headers bw_platform_memory_headers
    bw_platform_time_headers bw_macos_time_headers bw_platform_tasks_headers
    bw_platform_diagnostics_headers bw_platform_process_headers bw_shaders_headers bw_rhi_headers
    bw_platform_window_headers bw_macos_window_headers bw_geometry_headers
    bw_game_host_options_headers bw_content_headers bw_animation_headers
    bw_texture_asset_headers bw_materials_headers bw_content_converter_headers
    bw_scene_headers
    $<$<BOOL:${BW_SHADER_TOOLCHAIN}>:bw_shader_compiler_headers>)
endforeach()
target_include_directories(internal_header_contracts PRIVATE
  "${PROJECT_SOURCE_DIR}/src/engine/core/memory"
  "${PROJECT_SOURCE_DIR}/src/engine/core/tasks")
add_custom_target(header_contracts ALL DEPENDS public_header_contracts internal_header_contracts)
