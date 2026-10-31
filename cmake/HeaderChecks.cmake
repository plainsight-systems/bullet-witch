# SF.11 self-contained header checks, covering 36 current public and internal/detail units.
# Public API and compiler-required/private detail are intentionally separate.
set(BW_PUBLIC_HEADERS
  bw/platform/diagnostics/debugger.hpp
  bw/platform/compiler/assume.hpp
  bw/math/rotation/quaternion.hpp
  bw/math/rotation/quaternion_register.hpp
  bw/math/simd/register.hpp
  bw/platform/math/register_types.hpp
  bw/math/vector/vector3.hpp
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
)
set(BW_INTERNAL_HEADERS
  src/engine/core/diagnostics/error_history.hpp
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
)
foreach(header IN LISTS BW_PUBLIC_HEADERS)
  string(REPLACE "/" "_" unit "${header}")
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
    bw_platform_diagnostics_headers bw_platform_compiler_headers)
endforeach()
target_include_directories(internal_header_contracts PRIVATE
  "${PROJECT_SOURCE_DIR}/src/engine/core/memory"
  "${PROJECT_SOURCE_DIR}/src/engine/core/tasks")
add_custom_target(header_contracts ALL DEPENDS public_header_contracts internal_header_contracts)
