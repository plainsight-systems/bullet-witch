bw_header_interface(bw_diagnostics_headers "${CMAKE_CURRENT_SOURCE_DIR}")

# Private diagnostics implementation; accepted WordMutex remains in its current
# allocator dependency cluster. No private include root is exported to consumers.
add_library(bw_diagnostics error_history.cpp)
target_link_libraries(bw_diagnostics PRIVATE bw_contracts bw_allocator bw_concurrency_headers)
