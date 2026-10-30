bw_header_interface(bw_tasks_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_tasks_headers INTERFACE bw_platform_memory_headers)
# task_queue is compiler-required detail in NamedThreadDispatch's layout.
# task_storage is genuinely private; only implementation/internal checks see it.
