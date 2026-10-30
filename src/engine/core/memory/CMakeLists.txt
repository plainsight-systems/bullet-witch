bw_header_interface(bw_memory_headers "${CMAKE_CURRENT_SOURCE_DIR}")
set(platform_root "${PROJECT_SOURCE_DIR}/src/platform")
set(core_root "${PROJECT_SOURCE_DIR}/src/engine/core")

add_library(bw_memory
  memory_failure.cpp
  ${platform_root}/os/macos/memory/memory_failure.cpp
  ${platform_root}/memory/platform_memory.cpp
  ${platform_root}/os/macos/memory/platform_memory.cpp)
target_link_libraries(bw_memory PUBLIC bw_memory_headers PRIVATE bw_platform_memory_headers)
# Preserve the accepted headless output-device selection.
target_compile_definitions(bw_memory PRIVATE BW_HEADLESS_DIAGNOSTICS=1)

# Preserve the accepted allocator/concurrency/named-dispatch implementation
# cluster: these definitions call one another. Organizing them does not invent
# a new static-library cycle or export their internal headers to consumers.
add_library(bw_allocator
  allocator.cpp
  binned/binned_allocator.cpp
  binned/binned_bit_tree.cpp
  binned/binned_size_classes.cpp
  binned/binned_thread_cache.cpp
  crash_allocator.cpp
  ${core_root}/concurrency/word_mutex.cpp
  ${core_root}/tasks/task_queue.cpp
  ${core_root}/tasks/task_storage.cpp
  ${core_root}/tasks/named_thread_dispatch.cpp
  ${platform_root}/os/macos/memory/allocator_platform.cpp
  ${platform_root}/os/macos/memory/allocator_sync.cpp
  ${platform_root}/os/macos/tasks/task_dispatch.cpp)
target_include_directories(bw_allocator PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_allocator PUBLIC bw_memory
  PRIVATE bw_concurrency_headers bw_tasks_headers bw_platform_memory_headers
          bw_platform_tasks_headers bw_platform_time Threads::Threads)
