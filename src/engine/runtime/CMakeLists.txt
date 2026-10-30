bw_header_interface(bw_runtime_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_runtime_headers INTERFACE bw_platform_time_headers)
add_library(bw_runtime engine.cpp engine_loop.cpp)
target_link_libraries(bw_runtime PUBLIC bw_runtime_headers bw_platform_time
  PRIVATE bw_allocator bw_tasks_headers Threads::Threads)
